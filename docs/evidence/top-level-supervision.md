# Top-level supervision: detect → kill → console takeover → recover

**Date:** 2026-10-17
**Scenario:** `run/sponge-supervisor-takeover.run`
**Components:** `sponge_supervisor`, `sponge_console`, `test/takeover_probe`,
`test/ep_wedge` (new `wedge_ms` mode)
**Verdict:** the gap in `docs/17-runtime-component-topology.md` §5
("Broker fault containment — top level") is **closed**, with one
residual gap recorded honestly in §4 below.

---

## 1. What was missing

The top-level `init` cannot run a `<heartbeat>`. Genode's heartbeat
takes its Timer session from `env.parent()`
(`genode/repos/base/src/lib/timeout/timer_connection.cc:43`), and for
the top-level init the parent is **core**, which provides no Timer. The
session is denied and init stops. Everything above the `system` sandbox
therefore had no clock, no observation channel and no authority: a
wedged `system` child was unkillable from outside its own subtree.

The field motivation is on record — a scroll storm on real hardware
froze the whole desktop including the mouse, and nothing recovered it.

## 2. Mechanics, verified before anything was built

Every one of these was read in the vendored tree first, because each
one could have made the whole design impossible.

| Question | Answer | Source |
|---|---|---|
| Can a Tier-0 component get a Timer on base-sel4? | **Yes.** The constraint applies only to `init`'s *own* heartbeat, which asks its parent. A *component* at Tier-0 asks `init`, which routes to the `timer` child. Measured in the product run: `[init -> timer] Using timer 2 -> using GSI 2 edge triggered` | `timer_connection.cc:43`; `sponge-boot` disk log |
| Can a nested init publish a state report its parent can read? | **Yes.** A `<report>` node in its own config makes it construct a `Reporter` on Report service `"state"` | `os/src/init/main.cc:52-67` |
| What does that report contain? | `<child name=… binary=… [state="incomplete"] [exited=N] [skipped_heartbeats=N]>` — HID-serialised, not XML | `os/src/lib/sandbox/child.cc:367-427` |
| Is a Tier-0 init child's config relocatable from another Tier-0 component via report_rom? | **Yes** — this is the pkgd pattern verbatim (`pkgd` → `pkg_runtime`) | `sponge-desktop-disk.run:466` |
| Does a config rewrite restart all children? | **No.** `Start_model::matches()` compares only `name` and `version`; a changed `version` abandons and recreates exactly that child | `os/src/lib/sandbox/config_model.h:171` |
| Can a Tier-0 component take the display while nitpicker holds the fb? | The fb's **only** writer is `vesa_fb`/`boot_fb`, a Capture *client*. Taking the display = being the only Gui client. Two capture clients writing the device is what `docs/11-environment.md` row 12 measured as permanent corruption | `driver/framebuffer/boot/main.cc:115-120` |

### The flat-ISO HPET blocker, re-measured

`docs/17` §5 records `run/sponge-heartbeat-restart.run` as blocked
because flat ISO boots deny the HPET IO_MEM. Re-measured on this host,
unchanged:

```
[init] Warning: timer: no route to service "IO_MEM" (label="timer -> ")
[init -> timer] Error: stop because parent denied IO_MEM-session: label="", base=0xfed00000, size=0x1000
```

The **disk** path is unaffected — the same HPET at the same address,
found by the same ACPI driver, on the image `image/disk` builds:

```
[init -> acpi] Found HPET hw_id=0x1  vendor_id=0x8086 space_id=0x0 addr=0xfed00000
[init -> timer] timers=3, clock 10000000fs , 64bit counter , legacy routing support vendor=0x8086
[init -> timer] Using timer 2 -> using GSI 2 edge triggered
```

`sponge-supervisor-takeover.run` is therefore a **disk** scenario. The
flat-topology blocker is untouched and still registered.

## 3. The red → green run

`make -j1 -C genode/build/x86_64 run/sponge-supervisor-takeover KERNEL=sel4 BOARD=pc`

Every line below is a verbatim receipt from that run, in order. The
fault is a `driver` child that wedges its entrypoint for 20 s on **every**
incarnation, so the kill genuinely cannot fix it and the supervisor is
forced to reach the console.

```
[init -> console]            sponge_console: ready (role=console label=console, awaiting console_state)
[init -> sponge_supervisor]  sponge_supervisor: armed poll=250ms fault_skipped=3 grace=4000ms console_hold=6000ms ...
[init -> sponge_supervisor]  sponge_supervisor: published <system> kill=1 (1966 bytes)
[init -> sponge_supervisor]  sponge_supervisor: phase=1 (initial publish)

  --- phase 1: the desktop is on the display ---------------------------------
[init -> system -> witness]  witness: alive and well
[init -> system -> driver]   ep_wedge: incarnation alive — wedging for 20000 ms
[init -> system -> desktop]  sponge_console: role=desktop label=desktop 1024x768 owns the display
[init -> takeover_probe]     takeover_probe: desktop-up state=desktop centre=0x1e3a6e

  --- phase 2: DETECTED, from the subtree's own state report -----------------
[init -> sponge_supervisor]  sponge_supervisor: FAULT max_skipped=3 exited=0 state_seen=1 state_age_ms=250

  --- phase 3: KILLED, whole subtree torn down and respawned -----------------
[init -> sponge_supervisor]  sponge_supervisor: published <system> kill=2 (1966 bytes)
[init -> sponge_supervisor]  sponge_supervisor: phase=2 (fault detected, subtree killed)
[init -> system -> driver]   ep_wedge: incarnation alive — wedging for 20000 ms   <-- SECOND incarnation
[init -> system -> desktop]  sponge_console: role=desktop label=desktop 1024x768 owns the display

  --- phase 4: CONSOLE owns the display --------------------------------------
[init -> sponge_supervisor]  sponge_supervisor: FAULT max_skipped=6 exited=0 state_seen=1 state_age_ms=250
[init -> sponge_supervisor]  sponge_supervisor: published <system_console> kill=3 (1410 bytes)
[init -> sponge_supervisor]  sponge_supervisor: phase=3 (unrecoverable - console owns the display)
[init -> console]            sponge_console: ARMED role=console label=console 1024x768 owns the display
[init -> takeover_probe]     takeover_probe: console-up state=console centre=0x601010

  --- phase 5: CONSOLE RELEASED, then the desktop is restored -----------------
[init -> sponge_supervisor]  sponge_supervisor: console hold elapsed, still faulted
[init -> sponge_supervisor]  sponge_supervisor: CONSOLE RELEASED — display handed back, waiting for the console session to close
[init -> sponge_supervisor]  sponge_supervisor: phase=4 (console hold elapsed)
[init -> console]            sponge_console: DISARMED — display released
[init -> sponge_supervisor]  sponge_supervisor: published <system> kill=4 (1966 bytes)
[init -> sponge_supervisor]  sponge_supervisor: phase=1 (desktop restored)
[init -> takeover_probe]     takeover_probe: pixel state=desktop centre=0x1e3a6e
```

```
supervisor-takeover: PASS - wedge detected, subtree killed, console owned the display, desktop restored
Run script execution successful.
```

### Measured timings

| Interval | Configured | Measured |
|---|---|---|
| wedge → detection (`skipped_heartbeats` ≥ 3 @ 500 ms heartbeat) | ~1.75 s | `state_age_ms=250` at the FAULT line |
| detection → kill published | 1 tick (250 ms) | same tick (`published … kill=2`) |
| kill → console armed (grace window) | 4000 ms | 4.0 s |
| console hold (still faulted) | 6000 ms | 6.0 s |
| console release → desktop rebuilt (settle window) | 3000 ms | 3.0 s |

The second `ep_wedge: incarnation alive` line is the kill receipt: that
line can only appear if the `<start>` version really changed and the
child was abandoned and recreated. The `takeover_probe` centre-pixel
readouts are the takeover receipts: `0x1e3a6e` → `0x601010` →
`0x1e3a6e`, read from the composited screen through a Capture session,
not from a log.

## 4. The control run, and the false positive it found

`SPONGE_TAKE_NO_FAULT=1 make … run/sponge-supervisor-takeover`

The control run exists to assert the half that matters most: a
supervisor that restarts a healthy system is worse than one that does
nothing, because it destroys the user's session on a timer.

The **first** control run failed, and it found a real defect:

```
[init -> sponge_supervisor] sponge_supervisor: FAULT max_skipped=0 exited=0 state_seen=1 state_age_ms=10250
```

A **healthy** system, fault declared on report age alone. The cause is
structural, not a threshold that could be tuned: a Genode state report
is event-driven
(`os/src/lib/sandbox/state_reporter.h:76` — even with `child_ram`
reporting the periodic timer only re-reports when a child's RAM
accounting *changed*), so a quiet sandbox is indistinguishable from a
dead one by age. Report-age staleness was removed from the detector
entirely, not merely re-thresholded.

What survives is the one staleness statement that is sound: a sandbox
that has published **nothing at all** since it was created is not
publishing. That is a boot-time check with a 30 s budget.

Second finding from the same run: `exited` had to become
**edge-triggered** against a baseline recorded while healthy, because
Genode keeps an exited child in the report with its `exited` attribute
until it is abandoned — a plain "any exit" test latches permanently.

Control run after both fixes:

```
supervisor-takeover: CONTROL - healthy system observed, waiting for a second liveness line
[init -> sponge_supervisor] sponge_supervisor: nominal — subtree healthy, no action (10s)
supervisor-takeover: CONTROL PASS - no fault injected, no takeover
Run script execution successful.
```

`run_genode_until` can only assert that a marker *appeared*, so the
control uses the run tool's accumulated `output` global for the negative
assertions and `exit 1` on either:

```tcl
if {[string first "sponge_supervisor: FAULT"    $output] >= 0} { … exit 1 }
if {[string first "sponge_supervisor: contained" $output] >= 0} { … exit 1 }
```

## 5. Regressions

| Scenario | Kernel | Result |
|---|---|---|
| `sponge-supervisor-takeover` | sel4 | **PASS** |
| `sponge-supervisor-takeover` (control) | sel4 | **PASS** |
| `sponge-minimal` | linux | **PASS** |
| `sponge-de-test` | linux | **PASS** (`sponge-de-probe: PASS`) |
| `sponge-desktop-disk` | sel4 | **PASS** (`alpha-probe: PASS`) |
| `sponge-desktop-disk-uefi-usb` | sel4 | **PASS** (all four structural gates) |
| `sponge-heartbeat-restart` | sel4 | still blocked by the pre-existing flat-ISO HPET issue, unchanged |

## 6. Defects found and fixed while building this

These are the things that were wrong before the first PASS and are now
right. They are listed because each is a class of mistake that a
control run and a pixel oracle are the only things that catch.

| # | Defect | Caught by | Fix |
|---|---|---|---|
| 1 | Config relay: the report's `report:` policy must name the writer's **full session label** (`sponge_supervisor -> system_config`), not a truncated one | the sandbox never started | exact labels in the policy |
| 2 | `Attached_rom_dataspace::node()` resolves to the document's first element child, so forwarding a `<system>` wrapper hands init an element type it does not know and it silently applies nothing | no children appeared | the supervisor strips the wrapper and forwards the inner `<config>` |
| 3 | A child with an explicit `<route>` gets **no** `default-route` fallback; a denied environment session is fatal | `console: PD environment session denied` | every child carries explicit PD/CPU/LOG/RM/ROM routes |
| 4 | `<child name="X"/>` resolves against the *current* sandbox's own children, not the parent's | `desktop: Gui-session denied` | nested-init children use `<parent/>`; `Gui` added to the sandbox's `parent-provides` |
| 5 | `Gui::Connection::buffer(mode)` must precede `framebuffer.dataspace()` | `Uncaught exception … Invalid_dataspace` | order made load-bearing in the constructor |
| 6 | A `Gui` client whose screen is a **stack local** closes its session on return | the probe saw only the background | the screen is a member that outlives the paint |
| 7 | A freshly respawned subtree is briefly healthy before a persistent fault re-wedges; accepting the first healthy report declared "contained" and the console phase was unreachable | the console gate timed out | `settle_ms` window required in both CONTAIN and CONSOLE |
| 8 | The same settle discipline is needed on the *release* path, or the desktop is rebuilt while the console's session is still open | reasoning, not observation | a `RELEASE` phase that waits `settle_ms` after disarming |
| 9 | Report-age staleness fires on healthy systems | the control run | detector rewritten; §4 |
| 10 | `exited` is sticky in Genode's report | the same run | edge-triggered against a healthy baseline |
| 11 | `run_genode_until` without `$qemu_spawn_id` **power-cycles the VM**, so a multi-gate scenario restarts from scratch at every marker | the scenario rebooted at gate 2 | gates chain on `$qemu_spawn_id` |
| 12 | Gate order matters: the run tool consumes the stream sequentially, so a marker that appears earlier can never satisfy a later gate | `witness` timed out | gates ordered by actual emission order |
| 13 | `Attached_rom_dataspace::sigh` delivers no content — `update()` is still required | `valid=0 size=0` | `update()` before reading |
| 14 | A bare-word `.config` boot module is validated as **HID** by the run tool, and a `.config` handed to `init` must be proper XML | `Error: invalid HID syntax` | write HID, convert with `genode/tool/hid format --output-xml` — the same dance `sponge-desktop-disk.run` performs for `/system/init/system.config` |

## 7. Residual gap, stated honestly

**A wedged `system` *init* is still not observable.** The supervisor
detects wedged *children* of the sandbox (rising `skipped_heartbeats`)
and children that have exited, and it detects a sandbox that publishes
nothing at all within its 30 s boot budget. It cannot detect a sandbox
whose own init has stopped publishing, because Genode's state report is
event-driven and there is no "still alive" signal to wait for. Closing
that needs either a periodic report mechanism upstream or a
`<heartbeat>` on the sandbox's *own* children with a monitor that
forces a report on every tick — both are upstream questions, not
configuration. Registered as a `[ ]` row in
`docs/17-runtime-component-topology.md` §5.

## 8. Follow-up: the product rollout (closed, with four new findings)

The mechanism proved here was rolled into the five desktop-disk product
scenarios. That rollout changed this component in three measured ways —
the **escalation grace** (act only when the supervised sandbox's own
containment has failed), the **`ignore_exited`** policy (a boot probe's
clean exit is not a fault) and a **`element_text()` fix** (a config
forwarded truncated starts three children out of eleven). Both the fault
run and the control run above still PASS with all three in place, which
is what keeps them honest rather than historical.

Receipts, the four product-only defects, the escalation rule and the
env-gated product proof: [`product-supervision.md`](product-supervision.md).

Also not done, and deliberately so:

- The console shows the state the supervisor knows. It does not yet show
  a log tail — that needs the log stream to reach a component above the
  subtree, which is the same relay problem in a different shape.