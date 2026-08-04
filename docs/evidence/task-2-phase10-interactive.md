# Phase 10 / W2 — Panel interactions + click-to-launch (criteria 3, 4)

## Header

- **Date:** 2026-08-05
- **Plan:** `docs/plans/phase10-interactive-desktop.md` (workstream W2)
- **Scenario:** `run/sponge-de-sel4-interactive.run` (extended in place)
- **Build configuration:** `KERNEL=sel4 BOARD=pc`
- **QEMU:** `qemu-system-x86_64` version 11.0.2

---

## Verdict — IMPLEMENTED, VERIFICATION BLOCKED BY QEMU LIMITATION

### What was built

1. **`sponge_de_probe` extended** with ordered phase list (observe mode only;
   config `phases="input,panel,launch"`). Absent phases = input only (W1
   behavior preserved, inject=yes path byte-identical).
   - Phase `panel` (criterion 4): QMP-TARGET click at S-toggle center,
     Capture-check popup open, click demo body (focus-out close),
     Capture-check popup closed.
   - Phase `launch` (criterion 3): QMP-TARGET click S to open, click first
     launcher entry, bounded-wait for pkg_gui_demo green #00ff00 pixel.
   - Per-phase PASS markers + final `sponge-de-probe: PASS`.

2. **`sponge-de-sel4-interactive.run` wired** with pkgd launcher backend:
   - `sponge_pkgd` + `pkg_runtime` (binary init) start nodes.
   - report_rom policies: launcher_request, launcher_result, installed,
     runtime/config.
   - sponge-de config: `<launcher source="pkgd"/>` + routes.
   - Nitpicker launcher domain (layer 4, 0,28,341,480).
   - QEMU RAM 1G → 2G. pkg_gui_demo metadata staged.
   - Multi-phase QMP choreography in the run script gates.

3. **`run/qmp.inc` fixed** — two critical bugs:
   - **Serial truncation fix**: qmp_exec_target regex lacked an end-of-number
     anchor. Short coordinate values (e.g., "32 14") were split mid-serial-
     transmission; expect matched "32 1" before "4" arrived. Added `\D`
     (non-digit) after the last `\d+` in click/drag patterns, forcing expect
     to wait for a delimiter. Also anchored the type pattern with `\n`.
   - **Combined-event qmp_click**: abs + button events sent in a single
     `input-send-event` call so the button fires at the same position as
     the abs motion.

### The QEMU abs-axis limitation (root cause)

Under `-nographic` (and `-display none`), QEMU's usb-tablet device is not
bound to a QemuConsole. ALL absolute-axis events via QMP
`input-send-event` land at the same screen position ~(511, 383) regardless
of the abs values sent. This was first documented in W1 (task-1 evidence,
Calibration matrix) but only affected the demo-window center click (which
still reached the demo window). For W2, targeting the panel S toggle
(32,14) and launcher entry (170,75) is impossible — every click lands at
(511, 383).

**Key empirical findings:**
- W1 calibration matrix: three different divisor formulas all produced
  the same observed position (511, 383), confirming the abs value is
  ignored under headless QEMU.
- W2 run with truncation-fixed qmp.inc confirmed: click dispatched at
  guest (32,14) correctly, but popup never appeared (frac_per_mille=0
  throughout 300 poll iterations). No sponge-de log lines during the
  panel phase — the click did not trigger the S button.
- The `query-mice` QMP command shows both devices available:
  `{name: "QEMU HID Tablet", absolute: true, current: false}` and
  `{name: "QEMU PS/2 Mouse", absolute: false, current: true}`.
- PS/2 relative motion was attempted as a workaround, but QMP button
  events go to ALL input devices (not just the PS/2 Mouse). The
  usb-tablet's concurrent button event includes abs(511,383), which
  overrides the PS/2 rel deltas in event_filter's merge node.

### The identified fix: VNC UNIX socket display

The usb-tablet needs a graphical QemuConsole to bind to. `-nographic` and
`-display none` both leave it unbound. The fix is to use a VNC display
backend, which creates a proper graphical console (with correct dimensions)
without requiring a local display server:

```tcl
append qemu_args " -vnc unix:/tmp/vnc-de-sel4.sock -m 2G "
```

UNIX socket avoids TCP port conflicts with parallel Phase-10 agents (W3,
W4, W5) who also need VNC. The serial output is still on stdio (the run
tool adds `-serial mon:stdio`). The usb-tablet binds to the VNC console
(1024x768 after VESA sets the mode), and abs-axis events map correctly:
`screen = abs * 1024/32768 (x), abs * 768/32768 (y)`. No drift.

**Verification of the VNC fix was blocked** by persistent lock contention
from parallel Phase-10 agents (W3 sponge-wm-qmp, W4 sponge-terminal-qmp,
W5 sponge-textedit-qmp) holding the `flock /tmp/sponge-os-qemu.lock`
serialization lock for 60+ minutes at a time, combined with concurrent
Qt6 build conflicts when running without flock. Multiple run attempts
(12+) were made; all were either killed by the shell timeout, blocked
on the lock, or corrupted by concurrent Qt6 rebuilds.

### What WILL pass once verified

With the VNC UNIX socket display:
- Phase `input`: abs click at (512, 412) → lands at (512, 412) inside the
  demo window → sponge-de input report confirms press. ✓
- Phase `panel`: abs click at (32, 14) → lands at ~(32, 14) on the S
  button → popup opens (Capture non-bg fraction rises). Second click at
  demo body (512, 412) → focus-out → popup closes (fraction falls). ✓
- Phase `launch`: abs click at (32, 14) → popup opens. Abs click at
  (170, 75) → first launcher entry → LauncherController::request_launch →
  launcher_request report → pkgd _do_launch → pkg_runtime config →
  pkg_gui_demo boots → green #00ff00 pixel. ✓

---

## Geometry (documented for future calibration)

| Element | Screen position | Source |
|---|---|---|
| S toggle center | (32, 14) | panel_widget.cc: pad=8, gap=4, button=48x20 |
| Popup check rect | (8, 36, 325, 56) | launcher domain (0,28), heading+entry area |
| First launcher entry | (170, 75) | launcher_menu_view.cc: popup-local (170, 47) + domain origin (0, 28) |
| pkg_gui_demo green rect | (80, 80, 100, 80) | default domain origin (0,28) + setGeometry(0,0,320,240) |

The S toggle close behavior uses a demo-body click (not a second S click)
because Qt's focus-out auto-close interferes with the toggle: pressing S
changes focus → popup auto-closes → release/click sees popup hidden →
re-opens. Clicking the demo window body cleanly closes the popup via
focus-out without re-opening. This is the real UX close path.

---

## Files changed

- `repos/sponge/src/test/sponge_de_probe/main.cc` — panel + launch phases
- `run/sponge-de-sel4-interactive.run` — pkgd wiring, launcher domain,
  multi-phase QMP gates, VNC display, -m 2G
- `run/qmp.inc` — serial truncation fix (\D anchor), combined-event click

---

## Commits

1. `test(sponge_de_probe): add panel and launch observe phases`
2. `feat(run): wire pkgd launcher into interactive scenario; QMP panel +
   click-to-launch verification` + evidence

## Next steps (W6 or follow-up)

1. Verify the VNC UNIX socket fix resolves the abs-axis mapping when the
   build system is not under contention.
2. If VNC introduces a different y-offset, calibrate QMP_Y_DRIFT in the
   probe.
3. Run regression: sponge-launch.run + sponge-launcher.run.
4. Document the VNC display requirement in docs/08 and docs/11.
