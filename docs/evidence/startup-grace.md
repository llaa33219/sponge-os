# Startup grace: a child that is slow to START is not a wedged child

**Date:** 2026-10-19/20
**Scenario:** `run/sponge-startup-grace.run` (new) +
`run/sponge-desktop-disk-uefi-usb.run` (product media, new
`SPONGE_SLOW_STORAGE_KBPS` knob)
**Components:** `init` (fork patch `dda40f3a87`, ledger row #26),
`sponge_supervisor` (repos/sponge), `test/ep_wedge` (`wedge_ms` mode
reused as "a construct that takes N ms")
**Field report:** LG gram 17ZD90N, FBCON diagnostic image — boot log
shows `display released`, **`published system kill=25`**, sandbox
resource warnings. The supervisor had killed and rebuilt the whole
`system` subtree 25 times; the desktop never stabilised.

Design authority: `docs/17-runtime-component-topology.md` §5
("Broker fault containment — system level"), AGENTS.md §5.2.

---

## 1. The defect, stated exactly

In the classic (non-pthread) Genode — the only threading model in this
vendored tree — **`Component::construct()` runs on the ENTRYPOINT
thread**: `Entrypoint`'s constructor invokes the user constructor as an
RPC on the entrypoint (`genode/repos/base/src/lib/base/entrypoint.cc`,
`invoke_constructor_at_entrypoint`), so the entrypoint is *inside*
`construct()` for the whole of it and cannot process the heartbeat
signal meanwhile. (`repos/sponge/src/test/ep_wedge/main.cc` documents
the same property from the other side: "Component::construct runs on
the ep's dispatch thread, so sleep_forever wedges every RPC and signal
processing in this component".)

The heartbeat is a signal the child answers from that ep
(`init_heartbeat_monitoring()` is called immediately **before**
`Component::construct()`, so monitoring starts the moment the child's
constructor is entered). Therefore:

> **A child whose construct outlives the heartbeat budget cannot answer
> a single heartbeat — and the parent reads that as a WEDGE.**

What upstream keeps is `skipped_heartbeats`, which is **0** both for
"no heartbeat was sent yet" and for "the heartbeat was answered". A
child on its *very first* construct and a child that has been wedged
for a week are indistinguishable to the parent. The signal that
separates them does not exist yet: it is the sticky fact *this child
has never answered anything*.

The product media's budget, made exact: `<heartbeat rate_ms="2000"/>`
with `restart_after_skipped="3"`. The skip count lags one heartbeat
behind the number of unanswered ones (`skipped = outstanding - 1`), and
the kill fires when `skipped > 3`, i.e. on the **5th** unanswered
heartbeat: **10 s**, not the "~6 s" the older docs state (corrected in
§5 below).

The field loop, end to end:

```
slow storage chain → a child's construct exceeds 10 s
  → skipped_heartbeats climbs to 4 → the sandbox abandons and respawns it
  → the fresh incarnation loads just as slowly → killed again
  → and above it, sponge_supervisor reads the same rising skip count
    and tears down the WHOLE system subtree after its 4 s escalation
    window → the whole desktop restarts from zero → kill=2, 3, … 25
```

Both halves are the same misclassification seen from two levels.

## 2. The reproduction (QEMU), and the QEMU/field gap

### 2.1 The deterministic repro — `run/sponge-startup-grace.run`

A `system` sandbox supervised by `sponge_supervisor`, containing

* `slow` — `ep_wedge` with `wedge_ms="20000"`: a child whose CONSTRUCT
  takes 20 s, i.e. twice the product's 10 s kill budget. It is the
  product's heartbeat parameters verbatim (`rate_ms` 2000,
  `restart_after_skipped` 3, `restart_budget` 3);
* `witness` — `ep_wedge behave=yes`: must survive untouched.

Three arms, one scenario:

| arm | knobs | result |
|---|---|---|
| GREEN | default (`startup_grace_ms=60000`) | **PASS** — `startup took 20000 ms … the startup grace (60000 ms) applied`, one incarnation, no FAULT, kill=1 |
| RED (sandbox half) | `SPONGE_STARTUP_GRACE_MS=0` | **FAIL as designed** — 4 incarnations, then `heartbeat restart budget (3) exhausted after 3 restarts - child left stopped` |
| RED (supervisor half) | `SPONGE_SUP_STARTING=0` | **FAIL as designed** — `published <system> kill=2` while the child is still starting: the field's signature, isolated |

Verbatim, GREEN:

```
[init -> sponge_supervisor] sponge_supervisor: armed poll=250ms … ignore_starting=yes
[init -> sponge_supervisor] sponge_supervisor: published <system> kill=1 (1453 bytes)
[init -> system -> witness] witness: alive and well
[init -> system -> slow] ep_wedge: incarnation alive — wedging for 20000 ms
[init -> system] Warning: slow: startup took 20000 ms - the heartbeat budget
                 (10000 ms) would have killed it, the startup grace (60000 ms) applied
[init -> sponge_supervisor] sponge_supervisor: nominal — subtree healthy, no action (10s)
[init -> system -> slow] ep_wedge: incarnation un-wedged after 20000 ms
sponge-startup-grace: PASS - a child whose construct took 20000 ms (budget 10000 ms)
was STARTING, not wedged: it survived its slow start, the supervisor reported no fault
and killed nothing
```

Verbatim, RED (sandbox half) — the churn, bounded by the budget but
with the desktop never up:

```
[init -> system -> slow] ep_wedge: incarnation alive — wedging for 20000 ms
[init -> system -> slow] ep_wedge: incarnation alive — wedging for 20000 ms   (killed, respawned)
[init -> system -> slow] ep_wedge: incarnation alive — wedging for 20000 ms
[init -> system -> slow] ep_wedge: incarnation alive — wedging for 20000 ms
[init -> system] Warning: slow: heartbeat restart budget (3) exhausted after 3
                restarts - child left stopped, escalation is up to whoever owns the config
sponge-startup-grace: RED (sandbox half) - the child was killed while it was still
starting: 4 incarnations after the construct exceeded the 10000 ms heartbeat budget
```

Verbatim, RED (supervisor half) — the supervisor alone, with a patient
sandbox below it:

```
[init -> system] Warning: slow: no heartbeat response after 6000 ms of startup - …
[init -> sponge_supervisor] sponge_supervisor: FAULT max_skipped=3 exited=0 starting=1 state_seen=1
[init -> sponge_supervisor] sponge_supervisor: escalation - fault held for 4000ms before any action
[init -> sponge_supervisor] sponge_supervisor: published <system> kill=2
sponge-startup-grace: RED (supervisor half) - the supervisor killed the subtree while a
child was still starting
```

### 2.2 The product topology, and the number that names the gap

The repro above controls the construct time directly. To find out
whether the *shipped* media hits it, the product media is measured
with a slow boot medium. `run/sponge-desktop-disk-uefi-usb.run` (the
field's own media: UEFI USB stick, `usb_block → part_block → vfs/rump →
cached_fs_rom`) gained `SPONGE_SLOW_STORAGE_KBPS`, which limits the
read/write bandwidth of the boot medium with QEMU's block throttling.
The knob is measurement-only and off by default.

Guest uptime at the scenario's "DE started" marker, and the sandbox's
own startup receipts:

| medium | desktop up at | children flagged by the sandbox |
|---|---|---|
| unthrottled (host NVMe) | **< 10 s** | none |
| 4 MB/s | 20 s | `wm`, `sponge_themed`, `sponge_configd` **8000 ms**, `sponge-de`, `sponge_pkgd` **10000 ms** |
| 2 MB/s | 30 s | `driver_runtime` 10000 ms |
| 1 MB/s | 70 s | none flagged |
| 256 KB/s | > 380 s (bring-up incomplete; `alpha_probe` timed out waiting for pkgd) | none flagged |

Verbatim, 4 MB/s (`startup-grace-product-4MBps.log`; the log was
captured before the budget arithmetic was corrected, so it prints
`8000 ms` where the final code prints the true `10000 ms` — the
measured startup durations, which are what this table is about, are
unchanged):

```
[init -> system] Warning: sponge_themed: startup took 8000 ms - the heartbeat budget
                 (8000 ms) would have killed it, the startup grace (120000 ms) applied
[init -> system] Warning: sponge_configd: startup took 8000 ms - …
[init -> system] Warning: wm: startup took 8000 ms - …
[init -> system] Warning: sponge-de: startup took 10000 ms - …
[init -> system] Warning: sponge_pkgd: startup took 10000 ms - …
```

**That is the gap, with a number: at 4 MB/s five contained children —
including `sponge-de`, the desktop itself — are 8–10 s from their
first heartbeat response against a 10 s kill budget.** The margin is
already gone at a bandwidth a USB 2.0 stick exceeds comfortably. The
measurement's resolution is one heartbeat period (the sandbox learns
of the first response at the next tick), so "8000 ms" means "between
6 s and 8 s".

### 2.3 What the emulation does NOT show — stated, not glossed

QEMU's block throttle limits **bandwidth**. The field's chain is
**latency-bound** (xHCI → usb_block → part_block → ext2/rump →
`cached_fs_rom`), and no host-side bandwidth limit reaches its
effective throughput: at 256 KB/s the whole bring-up needs > 380 s, yet
no single child's construct crosses 10 s, because the children are
*serialised* — each one meets a quiet medium and finishes, while the
system as a whole falls behind. So:

* the **margin collapse** (§2.2) is reproduced on the product media;
* the **churn** is reproduced deterministically (§2.1), where the
  construct time is the free variable a slow chain controls.

That the two did not cross in the same QEMU run is a fact about QEMU's
throttle, not evidence against the defect, and it is why the field
machine is the only place the full loop can be observed end to end.
The 256 KB/s run also shows what a *really* slow chain does to a
system that is NOT churning: bring-up simply takes minutes, and the
first casualty is a probe with its own timeout (`alpha-probe: FAIL
sponge_pkgd did not answer install hello`), not the containment path.

## 3. The fix

**`init` — the classification (fork patch `dda40f3a87`, ledger row #26).**

1. `base::Child` gains a sticky `_heartbeat_responded` flag (set in
   `heartbeat_response()`) and the accessors `heartbeat_responded()`
   and `heartbeat_monitored()`. The second one matters: a heartbeat is
   only ever sent after the child registered its handler, and that
   registration is the last thing before `construct()`.
2. The sandbox's per-child `<heartbeat>` node gains
   `startup_grace_ms` (default **0** = upstream behaviour, opt-in per
   `<start>` node, exactly like `restart_budget`). Within that window a
   monitored-but-never-answered child is **STARTING**: its skip count is
   still reported truthfully, but it does not trigger a restart.
3. When the window expires the child is a wedge again, unchanged. The
   wedge decision is *forced* at that moment rather than waiting for
   the skip count to move, because by then it has long since passed
   `restart_after_skipped`.
4. The sandbox logs the startup duration of any child whose startup
   exceeded the heartbeat budget — the receipt §2.2 is made of, and the
   thing the field lacked.
5. The state report carries `heartbeat_state="starting"` for such a
   child, so a supervisor above the sandbox reads the classification
   instead of guessing from the skip count.

**`sponge_supervisor` — the consumer (repos/sponge).** `_scan_state()`
skips the `skipped_heartbeats` of a child the sandbox marks
`heartbeat_state="starting"`, and counts it separately
(`FAULT … starting=1`) so the exemption is visible in the log rather
than silent. The policy attribute `ignore_starting` (default `yes`)
exists so the acceptance scenario can switch the exemption off and
prove the supervisor's half is real and independent.

**Product media.** Every contained child of the five desktop-disk
scenarios and of `driver_runtime.inc` carries
`startup_grace_ms: 120000`.

### Why 120 s, and why it is bounded

| | |
|---|---|
| kill budget (unchanged) | 10 s |
| grace | **120 s** = 12× the budget |
| justification | the measured worst case on the product media at 4 MB/s is 10 s (`sponge-de`, `sponge_pkgd`); 120 s leaves an order of magnitude for a chain that is latency-bound rather than bandwidth-bound, which QEMU cannot emulate (§2.3) |
| cost when a child truly wedges in construct | the sandbox kills it at 120 s instead of 10 s, and the restart budget still bounds the attempts to 3 |

The window is opt-in, finite, and reported: a child that never becomes
responsive produces `no heartbeat response after N ms - the startup
grace (120000 ms) is exhausted, treating the child as wedged`, so a
real wedge is louder and later, never silent and never unbounded. No
quota was raised, no heartbeat disabled, no containment removed.

## 3a. What the fix broke in the test topology — and what that says

The classification is not only a product fix; it invalidated **two
fault injections that were injecting the wrong fault**, and both are
worth naming because the second one is a statement about what a "wedged
child" means.

`run/sponge-supervisor-takeover.run` wedged its fault child with
`ep_wedge wedge_ms="20000"` — which blocks the entrypoint **inside the
constructor**. After this change that child is, correctly, *starting*
and never wedged, so the supervisor never saw a fault and the scenario
timed out at 120 s of guest time. The fix is not a workaround: the
scenario was injecting a fault the product does not have. The field's
fault was `sponge_pkgd unresponsive(N)` **after a working boot** — a
child that was up, answered, and then stopped. `ep_wedge` gained a
`wedge_after_ms` mode for exactly that: return from the constructor
first, so the child registers its heartbeat handler and answers, then
park the entrypoint later from a `Timer::One_shot_io_timeout` handler.

Two implementation facts cost real time and are recorded so the next
one is not paid again:

* a bare `timer.trigger_once()` on a `Timer::Connection` delivers the
  timeout signal to the **connection's own** handler, not to a
  `Signal_handler` registered next to it — the wedge simply never
  fired. `One_shot_io_timeout` owns the sigh plumbing and routes the
  signal to the component's handler;
* this mode changes the scenario's *timing contract*: the respawned
  child is healthy for `wedge_ms` before it wedges again, so a
  `settle_ms` shorter than `wedge_ms + grace_ms` declares the fault
  "contained" on every round and the console phase becomes structurally
  unreachable — the same race `settle_ms: 20000` exists in
  `run/supervisor.inc` for. The scenario's own precondition assertions
  now state it (and `settle_ms` is 30000 there).

The same correction was applied to the product-topology fault child in
`run/supervisor.inc` (`SPONGE_SUPERVISOR_TEST=control|takeover`):
`wedge_after_ms` 60000 / 9000 instead of a constructor wedge. The
control arm's meaning is preserved — 9000 ms is long enough for the
supervisor to SEE the fault (`skipped_heartbeats` reaches 3 at 8 s on
the product's 2000 ms heartbeat) and short enough that it heals inside
the supervisor's grace window, so no kill may happen — and the
sandbox's own containment still does fire at ~14 s and respawn the
child, which is the half the arm is about.

One more honest note on gate ordering, learned the hard way here and
already recorded for `run/sponge-containment-churn.run`: the run tool
consumes the serial stream sequentially, so gating on two markers that
can arrive in either order blocks until the timeout. The console's
`DISARMED` line is emitted when it *reads* the arming report, which is
asynchronous, so it lands after the supervisor's `CONSOLE RELEASED`.
Those two receipts are now asserted over the accumulated output and the
gate sequence is carried by the rebuild marker itself.

## 4. The supervisor's own thresholds on a slow boot

`boot_grace_ms` (30 s) is the supervisor's only "the supervised
subtree published nothing at all" verdict. It does **not** misfire on a
slow boot, and the reason is structural rather than lucky: a Genode
state report is generated from the sandbox's **config model**, and the
config arrives from the supervisor's own first publish through
`report_rom` — in memory, with no storage on the path. The children's
binaries come later; the report does not wait for them.

Measured on the product media under throttling:

| medium | first supervisor verdict | FAULT lines | kills |
|---|---|---|---|
| unthrottled | nominal (10 s) | 0 | kill=1 |
| 4 MB/s | nominal (10 s) | 0 | kill=1 |
| 1 MB/s | nominal (10 s) | 0 | kill=1 |
| 256 KB/s | nominal (10 s) | 0 | kill=1 |

`state_seen=1` in every case, including the run whose desktop needed
more than 380 s. **`boot_grace_ms` is left at 30 s — measured, not
assumed.**

## 5. Doc corrections this work forced

* `docs/17-runtime-component-topology.md` §5: the sandbox's kill
  budget is **10 s** at `rate_ms=2000` / `restart_after_skipped=3`
  (the skip count lags one heartbeat behind the unanswered count), not
  the "~6 s" previously stated. The same "~6 s" appeared in the
  comments of all five desktop-disk scenarios and in
  `sponge_supervisor`'s README.
* `sponge_supervisor`'s escalation note said "about six seconds after
  it stopped answering"; it is ten, and that difference is exactly the
  size of the margin §2.2 measures disappearing.

## 6. Regression receipts

| Scenario | Arms / knobs | Result |
|---|---|---|
| `sponge-startup-grace` | GREEN | **PASS** — `startup-grace-green.log` |
| `sponge-startup-grace` | `SPONGE_STARTUP_GRACE_MS=0` | **FAIL as designed** — 4 incarnations — `startup-grace-red-sandbox.log` |
| `sponge-startup-grace` | `SPONGE_SUP_STARTING=0` | **FAIL as designed** — kill=2 — `startup-grace-red-supervisor.log` |
| `sponge-containment-churn` | default | **PASS** |
| `sponge-supervisor-takeover` | takeover (default) | **PASS** |
| `sponge-supervisor-takeover` | `SPONGE_TAKE_NO_FAULT=1` (control) | **PASS** |
| `sponge-desktop-disk` | default | **PASS** |
| `sponge-desktop-disk` | `SPONGE_SUPERVISOR_TEST=control` | **PASS** — `SUPERVISOR CONTROL PASS` |
| `sponge-desktop-disk` | `SPONGE_SUPERVISOR_TEST=takeover` | **PASS** — `SUPERVISOR TAKEOVER PASS` |
| `sponge-desktop-disk-uefi-usb` | default | **PASS** |
| `sponge-desktop-disk-uefi-usb` | `SPONGE_SLOW_STORAGE_KBPS=4096` | **PASS** — product boots on a 4 MB/s medium (§2.2) |
| `sponge-pkg-driver` | default | **PASS** |
| `sponge-minimal` | default | **PASS** |

Product-media logs: `startup-grace-product-unthrottled.log`,
`startup-grace-product-4MBps.log`,
`startup-grace-product-1MBps-nograce.log`.

`run/sponge-containment-churn.run` keeps **no** `startup_grace_ms`: its
subject is a child that wedges in its constructor *forever*, and with
a 120 s window the scenario's budget assertion would take six minutes.
That is a deliberate exclusion, not an oversight — the startup grace
covers slow starts, the churn scenario covers permanent wedges, and the
product media runs both mechanisms.

## 7. What the field should observe next

The image on the machine now carries the classification, and the
sandbox says what it sees. On the next boot of the field media:

1. `Warning: <child>: startup took N ms - the heartbeat budget (10000
   ms) would have killed it, the startup grace (120000 ms) applied`
   names every child whose startup exceeds the old budget, with the
   real number. **That N is the field's storage cost per component**
   and closes the half QEMU could not measure (§2.3).
2. If a child never answers, the grace-exhausted warning replaces it
   and the child is killed after 120 s — the bounded leash, visible.
3. `sponge_supervisor: FAULT … starting=<n>` shows how many children
   the sandbox considers to be starting; `kill=N` should stay at 1.
4. If kill=N still climbs, the classification is not the whole story
   and the next measurement is which child reports
   `skipped_heartbeats` **after** it has answered at least one
   heartbeat — that is a genuine wedge, and the state report's
   `heartbeat_state` attribute separates the two cases on the wire.

## 8. Follow-ups, stated rather than implied

* The **bandwidth-vs-latency gap** of §2.3 is unresolved: a guest-side
  delay knob (a `read_delay_ms` in `cached_fs_rom`'s read path, or a
  block-layer delay proxy) would let QEMU emulate the field's
  *effective* throughput and reproduce the churn on the product media
  directly. It was scoped and deliberately NOT shipped: the delay would
  have to live in an entrypoint signal handler, where a blocking sleep
  cannot be taken safely, so the honest version needs a Timer session
  for a ROM server — a real privilege cost for a measurement-only knob.
  Named, not done.
* `restart_budget` was already absent from `repos/os/src/init/init.hsd`
  (row #24 shipped the attribute without documenting it). Row #26 adds
  both attributes to the schema.
* A single restart is still **invisible** in the guest log: init's
  `_schedule_restart()` prints nothing, so "the child was killed once
  and came back" can only be inferred from the child's own repeated
  log lines. The churn scenario detects it by counting them; a
  one-line `log()` in `_schedule_restart()` would make it first-class.