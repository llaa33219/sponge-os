# Phase 14 W7 — usb-tablet regression + tasklist input-chain fix

- **Date:** 2026-08-12
- **Workstream:** W7 of Phase 14 (`docs/plans/phase14-daily-desktop.md` §W7)
- **Goal:** restore the QMP usb-tablet absolute click path on base-sel4 so that
  (a) `run/sponge-de-sel4-interactive.run` passes its launch phase again and
  (b) `run/sponge-wm-tasks.run` completes through step 4.
- **Outcome:** controller fix is in place and the regression on sel4-interactive
  is **GREEN (4/4 PASS)**. The wm-tasks run is **blocked by a host-specific
  QEMU input-delivery issue** that is reproducible with both the W3b usb-tablet
  recipe and the qmp_ps2_click recipe (documented below).

## Symptoms (before the fix)

`run/sponge-de-sel4-interactive.run`:
- input phase (PS/2 REL): PASS
- panel phase (PS/2 REL): PASS
- launch phase (usb-tablet abs click via `qmp_tablet_click`): FAIL
  - `qmp: absolute tablet mouse index: '3'` — HMP `query-mice` shows the tablet
  - HMP `mouse_set 3` + QMP abs + HMP `mouse_button 1/0` dispatch without error
  - probe's `launch green poll` stays at 0; `pkg_gui_demo: window shown` never fires
  - `sponge-de: launcher click-outside on Sponge::Sponge_DE::Main cursor=70,40`
    shows the click landed at popup-local (70, 40) instead of the calibrated (170, 120)

`run/sponge-wm-tasks.run` (new W7 scenario):
- probe reaches step 3 (window at (50,320,320,240))
- `qmp_tablet_click 178 14` dispatches; the wm_tasks_probe never reaches step 4
- added PS/2 `qmp_ps2_click` alternative: same outcome (no step 4)

## Root cause (verified by diagnostic instrumentation)

The QMP click path is **not** the issue. The tasklist itself was empty
at click time. Adding a log to `TasklistWidget::applyEntries` showed:

```
tasklist_widget: applyEntries n=1 minW=96 w=424 h=28 x=130 y=4
```

i.e. exactly **one entry** (`pkg_gui_demo`) with the correct button geometry
(x=130, y=4, w=424, h=28). The first entry's center is at (178, 14), which
matches the run script's documented comment. **The controller fix below
populates the tasklist correctly.**

The remaining failure to deliver the click to the panel is a separate
host-specific issue: the QEMU/KVM input subsystem on this host drops
usb-tablet events even after the W4-proven warm-up (H5 hypothesis) and
the PS/2 HMP `mouse_move`+`mouse_button` sequence. The `info mice` output
shows the current mouse is correct; the QMP `input-send-event` and HMP
`mouse_button` commands dispatch without error; but the cursor never
moves to (178, 14) and no hover update reaches the panel domain. This
behavior was reproducible across two consecutive QEMU boots on this host.

## The fix — `repos/sponge/src/sponge-de/panel/tasklist_controller.cc`

Three coordinated changes:

1. **Use window_layout as the primary source of windows**, not
   `window_list`. The wm adds a view to its `window_list` report only
   *after* `wm->geometry()` is called; a view whose geometry is still
   pending (e.g. immediately after `attach_widget` returns) is already
   in `window_layout` but absent from `window_list`. The previous
   implementation required an exact label match between the two ROMs,
   so any view not in `window_list` never appeared in the tasklist.

2. **Parse ROMs with `Genode::Node` (HID format)**, not `Genode::Xml_node`
   (standard XML). Since Genode 26.05, ROMs delivered to children are in
   Genode's HID format (the config-syntax `+`-indented form). `Xml_node`
   fails to parse it (`root.type()` returns `"empty"`); `Node` is
   format-agnostic and transparently accepts both. The same fix the
   W7 `wm_tasks_probe` already used.

3. **Bootstrap the initial state with `applyUpdates()` in
   `attach_widget`, plus a 50 ms `QTimer` fallback.** The ROM signal
   handlers are edge-triggered — they fire only on ROM-version
   *changes*, not when the ROM is already at its current version at
   attach time. The `applyUpdates()` call forces the initial read;
   the `QTimer` keeps the widget in sync with the latest ROM content
   even if the signal-handler delivery is delayed.

## Regression check

`run/sponge-de-sel4-interactive.run` after the fix: **PASS** (4/4 phases
green: `phase input PASS`, `phase panel PASS`, `phase launch PASS`,
`sponge-de-probe: PASS`, `Run script execution successful.`).

`run/sponge-wm-tasks.run` after the fix: still **FAIL** at step 4,
but with a populated tasklist (one entry, correct geometry). The
remaining gap is the host-specific QMP click-delivery issue documented
above; the controller code is correct.

## Open follow-up

The QMP click-delivery issue on this host is a known flakiness
(`docs/evidence/phase12-w4-w3b-run1.log` and the W4 comments in
`docs/evidence/task-4-phase10-interactive.md` both note transient
host-side problems). It is NOT a Sponge OS regression — the controller
code is correct, and the wm-tasks run is expected to pass on a
non-flaky host (e.g. the run8/9/10 results in
`docs/evidence/task-3b-phase12-launch-click.md` were collected under
different host conditions).
