# Forwarded create starvation — the create half of the non-blocking-session chain

**Artifact type:** runtime measurement (instrumented QEMU/base-sel4 boots)
**Scenario:** `run/sponge-desktop-disk-uefi-usb` (product UEFI USB media)
**Fork commits:** `74c84f9f0a` → `e0c2706225` (non-blocking create) → `205b84fcad` (quota-erosion recovery, coherent expiry)
**Ledger row:** #31 (`docs/11-environment.md` §4)

## 1. The question

`docs/evidence/forwarded-close-starvation.md` closed the **close** half of
the non-blocking-session chain (ledger row #28): a container forwards a
child's session *close* to its own parent without parking the container's
entrypoint, and completes it when the parent answers.

The **create** half was still the old, blocking one. A container that
forwarded a child's session *create* called the blocking four-argument
`Env::try_session()`, which parks in `Component_env::_block_for_session()`
**while holding the component's mutex**, on the container's single shared
RPC endpoint.

That is the container's one thread that serves *every* child's session
plumbing **and** the containment alarm. A container that parks there takes
its whole subtree down — silently and forever, because the alarm that
would have noticed is parked on the same thread.

## 2. The field timeline (promoted from the capture)

`/tmp/opencode/field-full-300s.log` — the 300 s full-speed QEMU boot of the
product media that reproduced the field freeze:

| line | event |
|---|---|
| 238 | `[init -> system -> layouter] Warning: [init] Warning: system: attempt to request cap for unavailable session: service=Timer … state=CREATING` — the layouter's **Timer** session is still in flight at the container |
| 243, 248 | the same signature for `service=Gui` (`system -> wm -> decorator`, `system -> wm -> `) — the window stack's **Gui** sessions never arrive |
| end of log | the container's heartbeat gives up: `decorator`/`layouter` "no heartbeat response after 120000 ms — the startup grace (120000 ms) is exhausted, treating the child as wedged" |

The two `state=CREATING` streams are the fingerprint of the defect: the
container polls the parent for a session capability that the provider has
not created yet, and the poll happens on the container's entrypoint — the
thread that has to dispatch the children's heartbeat answers. The children
stop answering, the container declares them wedged, the windows stay black,
the field freezes.

## 3. The settled mechanism

The create becomes **non-blocking**, and the completion is driven by the
container's **existing** 50 ms `Pending_op_timer` tick, which polls the
parent's `session_cap` for the forwarded session:

1. `Try_parent_service::initiate_request()` (CREATE path) forwards the
   request with the **direct parent RPC** `_env.parent().session(...)`
   instead of the blocking `Env::try_session()`. No mutex, no parking.
   * a valid capability → `phase = AVAILABLE` (as before),
   * an invalid capability (the parent took the request, the provider has
     not acted on it yet) → `phase` stays `CREATE_REQUESTED`,
   * an error → today's `session_phase_from_error()` / `result_from_error()`
     mapping, so `OUT_OF_RAM`/`OUT_OF_CAPS` stay retryable through
     `Parent_service`'s resource-request loop.
2. `Child::session()` registers the in-flight create with the container's
   own registry (`Op::Create`) whenever the service reports the operation
   as `Pending` at the parent.
3. The container's 50 ms tick resolves it: `Service::complete_create()`
   asks the parent for the session's capability, and on a valid capability
   sets `session.cap`, `phase = AVAILABLE` and fires the client's
   `ready_callback` — the client is released.
4. If the provider never answers, the create runs into the container's
   existing 5 s deadline: the session is denied (`SERVICE_DENIED`), the
   client is released with the denial, and the parent's ID is **not**
   released (the parent may still create the session — releasing the ID
   would let a new session reuse an ID the parent still owns).

The **notification-channel design was analysed and discarded**: one signal
registration per component cannot serve both a parked leaf and the
container's entrypoint thread, so the completion had to ride on the clock
the container already owns.

## 4. The adaptations the non-blocking create forced

A non-blocking create breaks every place that assumed "the create is
synchronous". The commit therefore adapts four of them — each one is a
place where the old code would have **re-requested** the session or
**mis-classified** it:

| place | old assumption | adaptation |
|---|---|---|
| `Local_connection_base` ctor (`base/include/base/local_connection.h`) | "the session is established iff `alive()`" — `alive()` is **false** for a `CREATE_REQUESTED` session, so the 10-attempt quota loop re-requested the session 10 times, each time destroying the previous session state (the parent's ID was released and immediately re-used → *"conflicting session ID"* at the parent) | a session whose creation is in flight is **established**: the loop breaks |
| `Local_connection_base::close()` | a session that is still being created was never closed at the provider | the close is issued for a `CREATE_REQUESTED` session as well |
| `Child::_close()` (`base/src/lib/base/child.cc`) | `alive()` is false for `CREATE_REQUESTED`, so a pending session was never closed at the parent | same |
| `Child::Env_connection::Env_service` (`base/include/base/child.h`) | the environment sessions (log, binary) are created through the `Local_connection`, i.e. **not** through `Child::session()` — they never entered the pending-op registry, so nothing would ever complete them | the env service registers the pending create itself and delegates `probe_at_parent()` / `complete_create()` to the routed service |

A fifth adaptation is a consequence of the fourth: the environment
sessions now arrive **asynchronously**, so the child becomes *active*
after `try_start()` has run. The container's child state therefore has to
follow the child's activity (`Sandbox::Child::_refresh_state()`), or the
child would never be sent a heartbeat (the heartbeat is expected only from
an *alive* child) and every child with a `<heartbeat>` node would be
reported as "starting" forever.

## 4a. The quota erosion — the real reason the create never completed

The first non-blocking create (`e0c2706225`) removed the wedge at full
speed, but the packages of the `pkg_runtime` never started: their ROM
sessions (`config`, `libc.lib.so`) were **refused by the provider**, and
the refusal was thrown away.

The mechanism is the **quota erosion** of a forwarded session. Every hop of
a chain of providers keeps a part of the client's donation for its own
session meta data (810 bytes per hop), so the donation shrinks on the way
down:

```
6144 (the client's request)  ->  5334  ->  4524  ->  3714
```

The provider at the end of the chain (`cached_fs_rom`) refuses a session
whose donation is below its 4 KiB floor — the request arrives with 3714
bytes and is denied with `INSUFFICIENT_RAM`.

Pre-S1 the denial travelled **synchronously** through the chain: the
client learned about it, bumped its quota by 4096 bytes
(`Component_env::session()`, `base/src/lib/base/component.cc`) and
requested the session again. That recovery was lost twice by the
non-blocking create:

1. `Try_parent_service::complete_create()` threw the error away: its
   error branch was **empty**. The create stayed pending until its
   deadline, the client was released with a plain denial, and the
   client-side recovery (bump the quota, ask again) never happened.
2. The environment sessions (log, binary, linker) are created through
   the `Local_connection`, i.e. **not** through `Child::session()`. Their
   quota-bump loop runs in the constructor of the connection — and with an
   asynchronous create the refusal arrives long after the constructor has
   returned, so the loop was gone by then.

The fix has two parts:

* `Try_parent_service::complete_create()` **resolves** the refused create:
  the parent's error is mapped to the session's phase
  (`INSUFFICIENT_RAM_QUOTA` / `INSUFFICIENT_CAP_QUOTA` / `SERVICE_DENIED`),
  the parent-side ID is released (the parent destroyed its side), the
  client is released via its `ready_callback`, and the operation is
  marked as **completed** — never left pending.
* The environment sessions re-issue their request when the refusal
  arrives: `Local_connection_base::reissue()` (the request parameters are
  kept for exactly that purpose) and the `Env_service`'s ready callback.

With the recovery in place the refusal is answered the way it always was:
the client asks again with 4096 bytes more, and the session is created.

**Speed was never relevant**: the erosion is a function of the chain
length, not of the storage speed. The 300 s deadline experiment (a create
that is never refused but queued behind the DE's library loads) showed
that a generous deadline does not fix anything — the creates were
*refused*, not slow.

## 5. The measurements

`run/sponge-desktop-disk-uefi-usb`, product UEFI USB media, QEMU full
speed, 360 s manual run (`timeout 360 bash -c "<spawn line>"`):

| marker | before (`field-full-300s.log`) | after |
|---|---|---|
| `treating the child as wedged` | 2 (decorator, layouter) | **0** |
| `attempt to request cap for unavailable session … state=CREATING` | 7+ (Gui, Timer, ROM) | **0** |
| `create of … failed` (the create deadline) | — | **0** |
| `stop because parent denied ROM-session` | — | **0** |
| `destroying session with a live parent-side id` / `dangling session in parent-side ID space` | — | **0** |
| `requested conflicting session ID` | — | **0** |
| `hello from package: running` (the `pkg_runtime` child's ROM chain completes) | absent | **present** |
| `alpha-probe: PASS` (the panel is composited on the framebuffer) | never reached | **yes** |
| `sponge_supervisor: nominal — subtree healthy` | — | **34** (0 s … 340 s) |

The `state=CREATING` stream is gone because a capability request for a
session that is still being created is a **normal poll**, not a mistake:
`Child::session_cap()` no longer warns for a `CREATE_REQUESTED` session.

The `create of … failed` / `stop because parent denied ROM-session`
signatures are gone because a create that the provider **refuses** is
resolved with the provider's reason instead of being left pending: the
client asks again with a bigger quota.

## 6. The coverage gap that hid the defect

The scenario's gate closed at the DE's first diagnostic line
(`sponge-de diag gates:`) — long before the compositor was healthy. The
container's entrypoint could starve the window stack's heartbeat dispatch
*after* the gate had said PASS. The gate now keeps the serial capture
running until the compositor proves itself (`alpha-probe: PASS` or
`sponge_debug: FIRSTPAINT ms=`) and **fails** on a wedge line or on a
`state=CREATING` starvation line.

## 7. What is not fixed here

* The **leaf** path (`Component_env::try_session()`, the leaf's own
  `Blockade`) is unchanged: a leaf parks in its own address space, which
  is not a container.
* The container's **Timer acquisition** (`Pending_op_timer::arm()`, the
  heartbeat's `Timer::Connection`) still uses the blocking session
  request. It happens once, during the initial configuration, before any
  child exists — the boot storm of the field came from the *create* path.
* The `Service` vtable grew by one slot (`complete_create()`), appended at
  the end of the class. Everything in the tree is rebuilt; no `Env`
  virtual was added.
* **Scenario-side calibration (out of scope here):** the alpha-probe's
  install step waits 12 s for the package manager's acknowledgement
  (`INSTALL_WAIT_ITERS`, `repos/sponge/src/test/alpha_probe/main.cc`).
  On a medium limited to 1000 KB/s the install of seven packages takes
  longer than that, so the probe reports `sponge_pkgd did not answer
  install hello` even though the desktop itself comes up. The probe's
  budget — not the session machinery — is what limits the slow-media
  bring-up.
* At 400 KB/s the honest bring-up of the Qt6 desktop exceeds the 120 s
  startup grace of the heartbeat (`startup_grace_ms`): the container
  declares the desktop environment wedged and tears it down, which
  surfaces as `dynamic linker is an invalid dataspace`. The grace is a
  product-policy value (ledger row #26), not a property of the session
  machinery.
