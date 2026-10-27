# Forwarded create starvation — the create half of the non-blocking-session chain

**Artifact type:** runtime measurement (instrumented QEMU/base-sel4 boots)
**Scenario:** `run/sponge-desktop-disk-uefi-usb` (product UEFI USB media)
**Fork commits:** `74c84f9f0a` → `e0c2706225` (non-blocking create) → `205b84fcad` (quota-erosion recovery, coherent expiry) → `82f0143d1f` (the re-issued session must not outlive its pending operation)
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
* **OPEN:** `run/sponge-pkg-driver` still fails at its DHCP gate. The
  driver package comes up (`e1000 00:02.0 eth0: Intel(R) PRO/1000 Network
  Connection`, `NIC Link is Up 1000 Mbps`, `create uplink for net device
  eth0`), and the package manager's install is acknowledged
  (`driver-probe: PASS`) — but the **uplink session**, which crosses the
  `system` sandbox boundary (the driver runtime -> the system sandbox ->
  the top-level init -> the `nic_router`), never completes: the
  `nic_router` never sees a NIC session and the DHCP offer never arrives.
  The pre-change run of the same scenario (with the same scenario-side
  build) reaches `dhcp offer from 10.0.2.2` — so this is a REGRESSION of
  the create chain, not a scenario-side artefact. Its cause is not yet
  isolated: no create expires, no session is denied, and no pending
  operation is left behind (the driver package's own sessions complete).

* At 400 KB/s the honest bring-up of the Qt6 desktop exceeds the 120 s
  startup grace of the heartbeat (`startup_grace_ms`): the container
  declares the desktop environment wedged and tears it down, which
  surfaces as `dynamic linker is an invalid dataspace`. The grace is a
  product-policy value (ledger row #26), not a property of the session
  machinery.

## 8. 2026-10-28 capture (P0.2) — the open pkg-driver uplink regression at pin `82f0143d1f`

**Date:** 2026-10-28 · **Task:** P0.2 (`docs/plans/phase16-runtime-overhaul.md`)
· **Scope:** bounded, reproducible capture before any create-path
refactoring. No fix, no code change, no bisect, no commit. This section
records what is and is not attributed; it **amends two mechanism claims
inside the §7 OPEN note** with raw-log evidence (§8.3) — the OPEN item
itself **stands** (the DHCP gate still fails at the current pin).

### 8.1 Exact serial commands

Serial only — one `make` in `genode/build/x86_64` at a time
(`docs/08-development.md`, serialized regression sweep; `build.conf`'s
`MAKE += -j80` applies only to the run tool's inner target build):

```sh
make -j1 -C genode/build/x86_64 run/sponge-pkg-driver KERNEL=sel4 BOARD=pc
```

Run conditions (defaults, nothing overridden): `KERNEL=sel4 BOARD=pc`,
build dir `genode/build/x86_64`, no `SPONGE_*` knobs set. QEMU as spawned
by the run tool (verbatim from the capture's spawn line):

```
-accel kvm -machine q35 -cpu Skylake-Client -nographic -m 2G
-serial mon:stdio -cdrom var/run/sponge-pkg-driver.iso
-machine q35 -net nic,model=e1000,netdev=net0 -netdev user,id=net0
```

Pin under test: submodule `genode` = `82f0143d1f` ("the re-issued
environment session must not outlive its pending operation"); the fork
chain is the one in this file's header (`74c84f9f0a` → `e0c2706225` →
`205b84fcad` → `82f0143d1f`, ledger row #31).

**Gate model (needed to read the results):** the seven two-argument
`run_genode_until` gates in `run/sponge-pkg-driver.run` **power-cycle QEMU
per gate** (`tool/run/run: run_genode_until` calls `run_power_cycle`), so
each gate observes a **fresh boot** and the install flow runs once per
boot: 7 boots on a green run, 6 boots + a timeout on a red one.

### 8.2 Current-pin gate results (2026-10-28, pin `82f0143d1f`)

`make` exit 2 (run tool `Error: Test execution timed out`, make error 254)
— the scenario failed at gate 6 exactly as §7's OPEN note describes.

| # | gate marker | result | observed evidence |
|---|---|---|---|
| 1 | `sponge_pkgd: ready` | PASS | boot 1 |
| 2 | `driver resource admission armed` | PASS | boot 2 |
| 3 | `driver-probe: install answered status=ok` | PASS | boot 3 — pkgd **install ack** |
| 4 | `driver-probe: driver_runtime lists child 'pc_nic'` | PASS | boot 4 — driver **created** in driver_runtime's own state report |
| 5 | `Intel(R) PRO/1000 Network Driver` | PASS | boot 5 — **e1000 bind / driver start** |
| 6 | `dhcp offer from 10.0.2.2, offering 10.0.2.15` | **FAIL (60 s timeout)** | boot 6 — fingerprint in §8.3 |
| 7 | `driver-probe: PASS` | NOT REACHED | run aborted at gate 6 |

Gate-6 boot (the failing window), verbatim and in order
(`[...]` = boot noise elided; `report_rom`'s `driver_runtime -> state`
reports elided):

```
[init -> system -> driver_probe] driver-probe: install answered status=ok
[init -> system -> driver_probe] driver-probe: driver_runtime lists child 'pc_nic'
[init -> system -> driver_probe] driver-probe: PASS
[init -> system] child "driver_probe" exited with exit value 0
[...]
[init -> system -> driver_runtime -> pc_nic] netdev: Intel(R) PRO/1000 Network Driver
[init -> system -> driver_runtime -> pc_nic] e1000_main: Intel(R) PRO/1000 Network Driver
[init -> system -> driver_runtime -> pc_nic] e1000 00:02.0 eth0: (PCI:33MHz:32-bit) 52:54:00:12:34:56
[init -> system -> driver_runtime -> pc_nic] e1000 00:02.0 eth0: Intel(R) PRO/1000 Network Connection
[init -> system -> driver_runtime -> pc_nic] e1000_main: eth0 NIC Link is Up 1000 Mbps Full Duplex, Flow Control: RX
[init -> system -> driver_runtime -> pc_nic] create uplink for net device eth0
[init -> nic_router] [uplink] NIC sessions: 1
[...]
[init -> system] Warning: driver_probe: close of Report (session 8) failed - the provider did not answer within 5000 ms
[init -> system] Warning: driver_probe: close of ROM (session 10) failed - the provider did not answer within 5000 ms
[init -> system] Warning: driver_probe: close of ROM (session 9) failed - the provider did not answer within 5000 ms
[init -> system] Warning: driver_probe: close of ROM (session 11) failed - the provider did not answer within 5000 ms
Error: Test execution timed out
```

Marker census (this capture vs the comparison set; counts per capture):

| marker | 2026-10-28 `82f0143d1f` (this capture) | 2026-10-24 10:33–10:50 `205b84fcad` ×4 (`/tmp/opencode/reg-pkg-driver*.log`) | 2026-10-24 11:09–11:10 `82f0143d1f` ×2 (`/tmp/opencode/pkgd-verify*.log`) | pre-change refs (§8.4) |
|---|---|---|---|---|
| QEMU boots (`spawn qemu`) | 6 | 6 | 7 | 7 |
| `create uplink for net device eth0` | 1 | 1 | 1 | 1 |
| `[uplink] NIC sessions: 1` | 1 | 1 | 1 | 1 |
| `dhcp offer from 10.0.2.2` | **0** | **0** | 1 | 1 |
| `close of ... failed` | **4** | **4** | 0 | 0 |
| `driver-probe: PASS` | 2 | 2 | 3 | 3 |
| `Test execution timed out` | 1 | 1 | 0 | 0 |
| `state=CREATING` / `create of ... failed` / `requested conflicting session ID` | 0 | 0 | 0 | 0 |
| `stop because parent denied ROM-session` | 12 | 12 | 14 | 14 |

`stop because parent denied ROM-session` is the fb-probe exclusivity
harness (`expect_status: exclusive-conflict`, docs/12 §4.6) — it occurs
at the same volume in passing captures and is **not** a regression
marker.

### 8.3 Two amendments to the §7 OPEN note's mechanism claims

The OPEN item stands (the DHCP gate still fails at the current pin).
Two mechanism claims inside it are contradicted by every raw failing
capture, this one included:

1. *"the `nic_router` never sees a NIC session"* — **not what the logs
   show.** `[uplink] NIC sessions: 1` is printed by
   `Domain::attach_interface` (`genode/repos/os/src/server/nic_router/domain.cc`),
   i.e. the router **accepted and attached** the Uplink session — in the
   failing runs as well. What never happens is the DHCP exchange over the
   attached uplink (no offer from `10.0.2.2` inside the 60 s gate). The
   `route` form of the hypothesis (the session never crosses
   driver_runtime → system → top init → nic_router) is therefore refuted
   for these captures; the failure is *after* session attach.
2. *"no pending operation is left behind (the driver package's own
   sessions complete)"* — **not what the logs show.** The probe exits 0,
   and every failing capture then carries four
   `driver_probe: close of ... failed - the provider did not answer
   within 5000 ms` warnings (its teardown: Report session 8, ROM
   sessions 9/10/11) — pending operations left behind on the
   row #28/#30 close machinery. No passing capture shows them. Caveat:
   passing captures are killed at their dhcp gate, so a warning printing
   later in those boots would be truncated; the passing boot-6 window
   does span the ≥5 s close deadline by line count, but wall-clock was
   not instrumented.

### 8.4 Last known-good pre-change reference

| capture | pin | date | result |
|---|---|---|---|
| `docs/evidence/churn-regression-sponge-pkg-driver.log` (committed, durable) | `f44bc47ee5` | committed 2026-10-19 (`6072140a4a`) | full green incl. `dhcp offer from 10.0.2.2, offering 10.0.2.15` |
| `/tmp/opencode/regress-sponge-pkg-driver.log` | `57075c4d26` | 2026-10-20 01:05 | full green |
| `/tmp/opencode/acc-sponge-pkg-driver.log` | `74c84f9f0a` | 2026-10-23 08:44 | full green — **the last known good before the create chain, on the current scenario-side build** |

The scenario side is identical across the whole window:
`run/sponge-pkg-driver.run`, `run/driver_runtime.inc`,
`repos/sponge/src/test/driver_probe` and `repos/sponge/src/sponge_pkgd`
last changed 2026-10-18..10-20 (`git log --since=2026-10-17 -- <those
paths>`); the only run/-side change inside the failing window is
`run/sponge-desktop-disk-uefi-usb.run` (`69a79d288b`) — a different
scenario. "Pre-change" here therefore means a **genode-fork pin change**,
not a scenario-side change.

### 8.5 Attribution statement (disciplined)

**Attributed (window only).** The regression window in the genode fork is
bounded by the last known-good pin `74c84f9f0a` (2026-10-23, full green,
same scenario build) and the first failing captures at `205b84fcad`
(2026-10-24, 4/4 gate-6 failures). The only fork commits inside that
window are the create chain `e0c2706225` (non-blocking create) and
`205b84fcad` (refused-create resolution + environment re-issue); the
failure also reproduces at the current pin `82f0143d1f` (this capture).
Scenario-side causation is excluded (§8.4).

**Not attributed.**

- **Which commit** is necessary or sufficient — no per-commit run and no
  bisect was performed (out of scope for this capture; P4.2 owns
  root-causing). The create chain is the *suspect family by construction*
  (it is the only base change in the window), not a proven cause.
- **Determinism** — the same pin `82f0143d1f` produced two full green
  runs (2026-10-24 11:09/11:10; their patch-ledger pre-flight verifies
  row #31 `82f0143d1f` as ancestor of the checked-out fork HEAD, which
  pins those runs to this commit) and one failing run (this capture,
  2026-10-28): observed 1 fail / 2 pass at `82f0143d1f`, 4/4 fail at
  `205b84fcad`. Intermittent or timing-sensitive; **not** established as
  deterministic in either direction. (The `82f0143d1f` commit message
  itself describes a use-after-free that made `pc_nic` "sometimes never
  come up".)
- **Root cause** — none claimed. Hypothesis status at capture time:

| hypothesis | distinguishing evidence | status at capture |
|---|---|---|
| create-in-flight (a forwarded CREATE never completes) | deadline denial (`create of ... failed`), `state=CREATING` polls | **not observed** (0 occurrences across the five failing-window captures); the Uplink create visibly completes (§8.3.1) |
| refusal (quota erosion; provider denies below the 4 KiB floor) | refusal / re-issue lines, `INSUFFICIENT_*` | **not observed** in the uplink window; `stop because parent denied ROM-session` occurs equally in passing captures |
| route (Uplink never reaches nic_router) | absence of `[uplink] NIC sessions: 1` | **refuted** for these captures (§8.3.1) |
| scenario-side (probe budget / gate calibration / config) | same build green at earlier pins | **refuted as a code-change artefact** (§8.4); the 60 s gate budget vs slirp DHCP timing is not separately instrumented and is **not** excluded |
| close-path co-symptom (row #28/#30 machinery) | `close of ... failed` warnings | **open** — 4/4 in failing captures, 0 in passing (truncation caveat §8.3.2); correlated, not proven causal |

### 8.6 Re-run recipe (for P4.2 / any implementer)

1. `git -C genode rev-parse --short HEAD` must print the pin under test
   (`82f0143d1f` for this capture).
2. Nothing else may run `make` in `genode/build/x86_64` meanwhile.
3. `make -j1 -C genode/build/x86_64 run/sponge-pkg-driver KERNEL=sel4 BOARD=pc`
4. Read the seven gates (§8.2). Green = `sponge-pkg-driver: PASS (pc_nic
   installed as a package into driver_runtime, bound the e1000, leased
   10.0.2.15; a second fb driver was REFUSED as exclusive-conflict)` +
   `Run script execution successful.` Red = `Error: Test execution timed
   out` at the failing gate, with the §8.3 fingerprint at gate 6.
5. Because the failure is intermittent (§8.5), one run is a data point,
   not a verdict: count gate-6 outcomes over several runs before calling
   a pin green.

Raw full-session make logs stay in session scratch and are deliberately
not promoted (AGENTS.md §5.6); every fact needed for regression
comparison is curated above.
