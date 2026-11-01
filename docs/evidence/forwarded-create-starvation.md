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
* **RESOLVED (2026-11-02, P4.2, §9):** the `run/sponge-pkg-driver` DHCP
  regression is **fixed in place** at pin `48a946ba15`. The regression was
  never "the uplink session never completes": §8.3 amended that claim (the
  Uplink create completes and attaches in every capture, failing ones
  included — `[uplink] NIC sessions: 1`), and §9 closes the item with the
  named mechanism: a session-lifecycle state corruption (stale RPC-object-
  key alias + pending-operation lifetime over replaced/freed `Session_state`
  — ledger rows #32 `489f977822` and #33 `48a946ba15`) that strikes the
  gate-6 churn window and silently stalls the just-attached uplink's packet
  path while losing the probe's teardown-close completions. Six serial
  current-pin runs reach `dhcp offer from 10.0.2.2, offering 10.0.2.15`
  (6/6) with zero close co-symptoms and no e1000 regression; the failing
  window is 0/5. The pre-fix runs of the same scenario reached the offer —
  this was a REGRESSION of the create chain's *exposure* (the non-blocking
  machinery moved completion into the pending-op/churn path where the two
  lifecycle defects could corrupt live session dispatch), not of the create
  logic itself. D16R.17's sequencing (capture → classify → disposition) is
  complete; pruning remains P4.3's alone and nothing here authorizes it.

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

## 9. 2026-11-02 (P4.2) — disposition: fixed in place (rows #32/#33)

**Date:** 2026-11-02 · **Task:** P4.2 (`docs/plans/phase16-runtime-overhaul.md`)
· **Decision:** **fixed in place (keep).** The regression is the
session-lifecycle corruption class fixed by ledger rows #32 (`489f977822`,
RPC object-key uniqueness) and #33 (`48a946ba15`, pending-session
incarnation/destruction lifetimes) — both already pinned at the current
fork HEAD. Six serial current-pin runs reach the DHCP gate (6/6) with zero
close co-symptoms and no e1000 regression. **No revert, no replacement, and
no code change** accompanies this disposition (0 fork commits, 0 pin
bumps). D16R.17's sequencing rule and the e1000 trap are preserved
verbatim: the disposition *existing* is what unblocks P4.3's pruning
decisions, and P4.3 is a separate task — nothing here removes any row,
patch, or code path.

### 9.1 Exact serial commands and run conditions

Serial only — one `make` in `genode/build/x86_64` at a time, no concurrent
runners (§8.1). Pin under test: submodule `genode` = `48a946ba15` ("preserve
pending sessions across reissue and destruction"); the fork chain under
test is `74c84f9f0a` → `e0c2706225` → `205b84fcad` → `82f0143d1f` →
`489f977822` → `48a946ba15` (ledger rows #30–#33).

```sh
# documented recipe (host io_uring clamp, docs/08 + P3.3 §8):
timeout 1200 env LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/sponge-pkg-driver KERNEL=sel4 BOARD=pc

# apples-to-apples with the §8.1 failing-window conditions — every capture
# in the failing window predates the clamp (it appeared 2026-10-29), so
# runs R4/R5 deliberately run WITHOUT it to exclude it as the differentiator:
timeout 1200 env -u LD_PRELOAD \
  make -j1 -C genode/build/x86_64 run/sponge-pkg-driver KERNEL=sel4 BOARD=pc
```

QEMU spawn (verbatim): `-accel kvm -machine q35 -cpu Skylake-Client
-nographic -m 2G -serial mon:stdio -cdrom var/run/sponge-pkg-driver.iso
-machine q35 -net nic,model=e1000,netdev=net0 -netdev user,id=net0`.
Gate model unchanged (§8.1): the seven two-argument `run_genode_until`
gates power-cycle QEMU per gate — 7 boots on a green run.

### 9.2 Current-pin run census — 6 of 6 reach the DHCP gate

| run | log (session scratch, not promoted — AGENTS.md §5.6) | conditions | result |
|---|---|---|---|
| R0 | `/tmp/opencode/gate-pkg-driver-remeasure.log` (the P3.3 §8.4 input, 2026-11-01) | clamp | **green** end to end |
| R1 | `/tmp/opencode/p42-pkg-driver-run1.log` | clamp | **green** |
| R2 | `/tmp/opencode/p42-pkg-driver-run2.log` | clamp | **green** |
| R3 | `/tmp/opencode/p42-pkg-driver-run3.log` | clamp | **green** |
| R4 | `/tmp/opencode/p42-pkg-driver-run4-noclamp.log` | **no clamp** (§8.1 conditions) | **green** |
| R5 | `/tmp/opencode/p42-pkg-driver-run5-noclamp.log` | **no clamp** (§8.1 conditions) | **green** |

Every run closes with `sponge-pkg-driver: PASS (pc_nic installed as a
package into driver_runtime, bound the e1000, leased 10.0.2.15; …)` and
`Run script execution successful.` Per-run marker census (identical in all
six runs except the noted fb-harness count):

| marker | each of R0–R5 |
|---|---|
| QEMU boots (`spawn qemu`) | 7 |
| `driver-probe: install answered status=ok` | 5 |
| `driver-probe: driver_runtime lists child 'pc_nic'` | 4 |
| `Intel(R) PRO/1000 Network Driver` (e1000 bind) | 3 |
| `create uplink for net device eth0` | 1 |
| `[uplink] NIC sessions: 1` | 1 |
| `dhcp offer from 10.0.2.2` | **1** |
| `driver-probe: PASS` | 3 |
| `close of ... failed` (row #28/#30 close machinery) | **0** |
| `Warning: invalid signal-context capability` | 2 (baseline — §9.3) |
| `state=CREATING` / `create of ... failed` / `requested conflicting session ID` | 0 / 0 / 0 |
| `treating the child as wedged` | 0 |
| `Error: Test execution timed out` | 0 |
| `stop because parent denied ROM-session` | 13–14 (fb-probe harness, see scope note) |

**Scope note (binding, unchanged from P3.3 §8.4).** This disposition claims
the install ack, the `driver_runtime` child listing, the e1000 bind, the
uplink attach, the DHCP lease, and `driver-probe: PASS` ONLY. The
scenario's final PASS string is unconditional, and its fb exclusivity
probes never assert in this topology (pre-existing report_rom policy gap:
each fb probe ends in `stop because parent denied ROM-session`, the same
volume as in every earlier capture). **No fb/exclusivity result is claimed
here** — exclusivity evidence is `run/sponge-pkg-exclusive`
(`docs/evidence/phase16-runtime-p33.md` §2). The e1000 constraint stands:
no variant that breaks the e1000 path may be shipped
(`docs/evidence/sponge-upgrade-wedge.md`; the net-zero pair
`500cb2cc8a`+`3df26cf22b` stays as the negative-result guard).

### 9.3 The marker census against the failing window

| capture set | pin | dhcp offer | `close of ... failed` | `invalid signal-context capability` | boots | outcome |
|---|---|---|---|---|---|---|
| `/tmp/opencode/reg-pkg-driver{,2,3,4}.log` (2026-10-24) | `205b84fcad` | 0 (×4) | 4 each | 6 each | 6 | timeout at gate 6 |
| P0.2 capture, this file §8.2 (2026-10-28) | `82f0143d1f` | 0 | 4 | (same fingerprint, §8.2) | 6 | timeout at gate 6 |
| `/tmp/opencode/pkgd-verify{,2}.log` (2026-10-24) | `82f0143d1f` | 1 each | 0 | 2 each | 7 | green |
| R0–R5, §9.2 | `48a946ba15` | 1 each (6/6) | **0** | 2 each | 7 | green |

Two census facts carry the mechanism:

1. **The close co-symptom is perfectly correlated with the DHCP failure.**
   5/5 failing captures carry exactly four `driver_probe: close of ...
   failed - the provider did not answer within 5000 ms` warnings (its
   teardown: Report session 8, ROM sessions 9/10/11, closed through the
   `system` container on the row #28/#30 machinery); 0/8 passing captures
   carry any (including the two pre-fix passes at `82f0143d1f`).
2. **Every lost close is paired 1:1 with a capability-dispatch failure.**
   `Warning: invalid signal-context capability`
   (`genode/repos/base/src/core/include/signal_delivery_proxy.h` — core
   routed a signal to an invalid signal-context capability) occurs twice in
   every capture (baseline) and six times in each failing capture: the
   baseline plus one immediately after each of the four close failures.
   The failing window is not "a provider was slow"; four session
   completions were dispatched to dead/stale capabilities and dropped.

### 9.4 The named mechanism

The gate-6 boot is the heaviest session-churn window of the scenario:
`driver_probe` exits and closes its four sessions (Report + 3 ROM, against
`report_rom` through the `system` container) *while* `pc_nic`'s Uplink
create crosses `driver_runtime` → `system` → top-level init → `nic_router`
and the DHCP exchange begins on the freshly attached interface.

**What the logs rule out.**

- "The uplink session chain never completes" (the original §7 claim):
  refuted already in §8.3 — `[uplink] NIC sessions: 1`
  (`Domain::attach_interface`) prints in **every** capture, failing ones
  included. The Uplink create completes provider-side.
- "Slirp DHCP timing vs the 60 s gate budget" (the §8.5 residual): now
  excluded by measurement. In healthy runs the offer arrives on the **very
  next line** after the attach — `create uplink for net device eth0` →
  `[uplink] NIC sessions: 1` → `dhcp offer from 10.0.2.2, offering
  10.0.2.15` are consecutive lines (e.g. R5 lines 2160–2162): slirp
  answers within the same boot second. Failing runs are attach-then-silence
  for the full 60 s. The budget is irrelevant; the path is corrupted, not
  slow.
- Quota erosion / refusal (`INSUFFICIENT_*`, re-issue lines): still 0 in
  the uplink window. Scenario-side wiring: excluded by §8.4 (same scenario
  build green at earlier pins).

**What the logs show.** In the same window, the probe's four forwarded
close completions are lost (each ending in a core dispatch to an invalid
signal-context capability, §9.3) and the just-attached uplink's exchange
never runs. Both are the defect class the two current-pin fixes restore
invariants for:

- Row #32 (`489f977822`) — a recycled `Rpc_obj_key` could alias a new
  server incarnation to a stale client `Data`: `Capability_space::lookup`
  hits the stale mapping and `reset_sel` returns the old handle while
  deleting the fresh received capability
  (`docs/evidence/phase16-runtime-capability-key-reuse.md` §2). A session
  completed *during churn* can therefore be created correctly on the
  provider while its client-side dispatch (the received capability, the
  signal context, the packet-stream acks) lands on a dead handle.
- Row #33 (`48a946ba15`) — a pending operation could be left pointing at a
  replaced or already-freed `Session_state`, so a late completion could
  never resolve the op
  (`docs/evidence/phase16-runtime-pending-session-lifetime.md` §2–3). That
  is precisely the shape of `close of ... failed — the provider did not
  answer within 5000 ms`: the answer cannot resolve the requester's pending
  op, and the row #28 per-op deadline fires.

**The mechanism, stated.** The uplink regression is a **session-lifecycle
state-corruption race**: during the gate-6 churn window, stale RPC-object-
key aliasing (row #32) and/or the pending-operation lifetime hole (row #33)
corrupt live session dispatch. The Uplink session is created and attached
provider-side (`NIC sessions: 1`), but its client-side packet path (and the
teardown closes' completions) is dispatched to stale/dead capabilities and
silently dropped — the DHCP discover/offer exchange never completes and the
gate sees attach-then-silence, while the probe's teardown closes hit their
5 s deadlines (the deterministic co-symptom, with its 1:1
`invalid signal-context capability` pairs). When the interleaving misses
the race window, everything completes — which is why `82f0143d1f` passed
2/3 (§8.5): `82f0143d1f` had already closed the reissue use-after-free
layer of the hole, and the intermittent residue was exactly the alias +
lifetime corruption the rows #32/#33 pair closes.

**Why the create chain is exposure, not defect.** The non-blocking create
chain (rows #30/#31) completes every create in every capture (passing and
failing alike); its role in the regression is that it moved session
completion onto the container's pending-op registry and the churn-dense
timer path, where the two lifecycle defects could reach live sessions. The
key-alias defect itself predates the chain (the row-#32 bisect found it at
`74c84f9f0a`, `e0c2706225` and `205b84fcad` alike — "scenario recovery is
not defect absence"), and the pending-op lifetime hole was introduced by
the chain's registry machinery. With both invariants restored
(`489f977822` + `48a946ba15`), the chain's own semantics are intact and the
gate is green 6/6.

### 9.5 Classification (the P4.2 decision)

| question | answer |
|---|---|
| Where does the regression live? | Base session-lifecycle machinery (the rows #32/#33 defect class) that the create chain exposed — **not** scenario-side wiring, **not** the non-blocking create logic (creates complete in every capture). Closes plan Open Question 2. |
| Disposition | **fixed in place (keep)** — fixed by the already-pinned rows #32/#33 at `48a946ba15`. |
| Revert? | No. Reverting the create chain would re-park container entrypoints (rows #28/#30/#31's reason to exist) and is not what the evidence supports. |
| Replace? | No. No new fix is needed and none was written; no new base async semantics were introduced (D16R.9 clean). |
| Code change in this task? | None (0 fork commits, 0 pin bumps). The fix predates P4.2 and is verified here against the pkg-driver gate. |
| P4.1 table effect | Class census unchanged (keep 32 · remove 0 · replace 1). The create-chain rows keep class **keep**; rows #32/#33 are now additionally the *verified fix* of the D16R.17 regression. |
| D16R.17 | Preserved. Its sequencing (capture P0.2 → classify P4.1 → dispose P4.2) is complete with this section; pruning stays P4.3's alone and this disposition does not authorize it. |
| e1000 constraint | Preserved. No variant shipped; e1000 binds in 6/6 runs; the rejected upgrade-wedge variant remains a documented negative result (`docs/evidence/sponge-upgrade-wedge.md`). |

### 9.6 What this disposition does not say (honest limits)

- **Not a determinism claim in the green direction either.** The pre-fix
  failure was timing-sensitive (`82f0143d1f` observed 2 pass / 1 fail); six
  consecutive greens cannot absolutely exclude an arbitrarily rare residual
  race. What is closed: the *named corruption class* (rows #32/#33 carry
  their own unpatched-toggle reds in their evidence files) and its
  deterministic co-symptom (0/6 close failures, 0/6 excess
  `invalid signal-context` warnings).
- **No red-toggle rebuild of the old pin was run for this disposition.**
  The pre-fix red evidence is the historical failing-window captures
  (`205b84fcad` 4/4, `82f0143d1f` 1/3 — §8.2, §8.5); rebuilding `82f0143d1f`
  to re-hit a ~1/3 race would be a sampling exercise, not a mechanism test.
- **Clamp exclusion is measured, not assumed:** runs R4/R5 reproduce the
  green result under the failing window's exact (clamp-free) host
  conditions, so the host io_uring clamp is not the differentiator.
- **QEMU only.** All runs are QEMU q35 / Skylake-Client / KVM / base-sel4
  (D16R.12 labelling); no real-hardware claim.
- **fb/exclusivity untouched** — see the §9.2 scope note; the report_rom
  policy gap of the fb probes remains open and is tracked where it was.
