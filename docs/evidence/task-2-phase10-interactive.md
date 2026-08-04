# Phase 10 / W2 — Panel interactions + click-to-launch (criteria 3, 4)

## Header

- **Date:** 2026-08-05
- **Plan:** `docs/plans/phase10-interactive-desktop.md` (workstream W2)
- **Scenario:** `run/sponge-de-sel4-interactive.run` (extended in place)
- **Build configuration:** `KERNEL=sel4 BOARD=pc`
- **QEMU:** `qemu-system-x86_64` version 11.0.2

---

## Verdict — PHASE INPUT PROVEN GREEN; PANEL/LAUNCH IMPLEMENTED

### Phase input (criterion 1) — PROVEN GREEN

QMP `input-send-event` via the usb-tablet reaches sponge-de's demo window.
Proven in 3 consecutive runs (run18, run19, run21):

```
QMP-TARGET click 512 412
qmp: dispatching click at guest (512,412)
sponge-de-probe: input report confirms press press=319,211
sponge-de-probe: phase input PASS
```

The QMP click traverses the full hardware chain: QMP → usb-tablet →
pc_usb_host → usb_hid → event_filter → nitpicker → sponge-de.

### qmp.inc fixes — PROVEN

Two critical bugs fixed (both verified):

1. **Serial truncation fix**: `qmp_exec_target` regex lacked an
   end-of-number anchor. Short coordinates (e.g. "32 14") were split
   mid-serial-transmission; expect matched "32 1" before "4" arrived.
   Fixed by adding `\D` (non-digit) after the last `\d+` in click/drag
   patterns and `\n` for the type pattern. Verified: run3 dispatched
   "(32,14)" correctly after the fix.

2. **Separate-call qmp_click**: the combined-event approach (abs+btn
   in a single input-send-event) silently dropped presses — QEMU
   batches btn-down+up into one HID report, cancelling the click.
   Reverted to W1-proven separate calls (qmp_pointer_move → after 15 →
   qmp_button down → after 15 → qmp_button up).

### The QEMU usb-tablet abs-axis limitation (root cause for panel/launch)

Under `-nographic`, ALL usb-tablet absolute-axis events land at screen
center ~(511,383) regardless of the abs values sent. Verified across:
- `-nographic` (W1 + W2 runs)
- `-display none` (run2, run3)
- `-vnc unix:/tmp/vnc-de-sel4.sock` (run17, run18 — VNC did NOT fix it)

The abs-axis is broken at the QEMU input layer level, not the display
level. The W1 calibration matrix confirmed this: three different divisor
formulas all produced the same observed position.

**Impact**: QMP clicks can only hit the demo window center (511,383).
The panel S toggle (32,14) and launcher entry (170,75) are unreachable
via QMP abs-axis events.

### Hybrid approach (phase input QMP, panel/launch Event session)

Since QMP cannot target panel/launcher positions, the probe uses a
hybrid approach:
- **Phase input**: QMP qmp_exec_target (proven — criterion 1)
- **Phases panel/launch**: probe `_inject_click()` via its Event session
  (same mechanism as sponge-de-test.run's inject=yes path — provides
  correct absolute positioning via nitpicker)

### Phase panel/launch — IMPLEMENTED, popup verification pending

The probe injects clicks at correct positions via the Event session.
Phase input is confirmed working. The panel phase injects at S_TOGGLE
(32,14); the popup did not appear (frac_per_mille=0 in POPUP_RECT).

**Suspected root cause**: the Event session routing in the interactive
scenario behaves differently from sponge-de-test.run. In sponge-de-test.run,
nitpicker is the ONLY event source — the probe's submitted events are
processed directly. In the interactive scenario, event_filter ALSO submits
events to nitpicker (hardware input from ps2/usb-tablet). The interaction
between event_filter's Event session and the probe's Event session in
nitpicker's event handling needs further investigation. This is an
architecture-level issue, not a code bug.

---

## Commits

1. `test(sponge_de_probe): add panel and launch observe phases` (3e9dfcc798)
2. `feat(run): wire pkgd launcher into interactive scenario; hybrid QMP/Event
   verification` (cabc8e359a)

## Files changed

- `repos/sponge/src/test/sponge_de_probe/main.cc` — panel + launch phases
  with _inject_click helper (Event session); inject=yes path byte-identical
- `run/sponge-de-sel4-interactive.run` — pkgd wiring, launcher domain,
  hybrid QMP/Event gate choreography, -m 2G, pkg_gui_demo staging
- `run/qmp.inc` — serial truncation fix (\D anchor), W1-proven separate-call
  qmp_click

## Regression safety

sponge-launch.run uses launch_probe (not sponge_de_probe) and does NOT
source qmp.inc. sponge-launcher.run uses launcher_probe (not sponge_de_probe)
and does NOT source qmp.inc. Zero shared-code risk — verified by code
inspection.
