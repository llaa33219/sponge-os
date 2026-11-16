> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 10.

### Phase 10: Sponge DE — Fully Interactive Desktop

#### Goal

The desktop becomes fully operable through real input: mouse and
keyboard work end-to-end through the actual driver chain, windows can
be dragged, and every interactive element (buttons, launcher entries,
the panel) responds to clicks.

#### Completion Criteria

- [x] Real input path verified end-to-end: usb-tablet → pc_usb_host →
  usb_hid → event_filter → nitpicker → sponge-de, driven from the host
  via QMP `input-send-event` instead of the probe's synthetic Event
  injection (this closes the remaining §11.1 follow-up).
  **Scenario:** `run/sponge-de-sel4-interactive.run`. **PASS marker:**
  `sponge-de-probe: PASS` caused by a QMP click → sponge-de `input`
  report (W1 evidence; see
  `docs/evidence/task-1-phase10-interactive.md`).
- [x] Window dragging / moving works and is verified by a run scenario
  (pointer press on the title bar, motion, release; window position
  asserted through the layouter or a Capture pixel check).
  **Scenario:** `run/sponge-wm-qmp.run`. **PASS marker:**
  `wm-probe: [observe 5] pkg_gui_demo moved (50,320) -> (149,419)` (W3
  evidence; see `docs/evidence/task-3-phase10-interactive.md`).
- [x] Click-to-launch delivered: clicking a launcher entry starts the
  package through the start-on-demand lifecycle (the Phase 5 deferral
  is resolved). **Scenario:** `run/sponge-de-sel4-interactive.run` (W2
  launch phase). **PASS marker:** `pkg_gui_demo: window shown` +
  `pkg_gui_demo green pixel detected` +
  `sponge-de-probe: phase launch PASS` (two consecutive green runs in
  `docs/evidence/task-2-phase10-interactive-run1.log` and
  `task-2-phase10-interactive-run2.log`). The §7 synthetic
  click-to-launch proof in `run/sponge-launch.run` stays valid; Phase
  10 strengthened the same `_do_launch` backend path to the real
  QMP/usb-tablet hardware chain.
- [x] Panel interactions wired: launcher menu open/close and the
  launcher entries inside the popup respond to clicks (the panel's
  only clickable elements today — the clock is a passive `QLabel` by
  design). Additional panel widgets (system tray, applets, taskbar
  items) are tracked under **Phase 11** (§11). **Scenario:**
  `run/sponge-de-sel4-interactive.run` (W2 panel phase). **PASS
  marker:** `sponge-de-probe: phase panel PASS` (popup opened via S
  click, closed via demo-body click).
- [x] Keyboard input reaches focused windows (typing in terminal and
  text editor verified by scenario). **Scenarios:**
  `run/sponge-terminal-qmp.run` (5a) + `run/sponge-textedit-qmp.run`
  (5b). **PASS markers:** `terminal-probe: PASS` (glyph 98 → 155 echo
  round-trip) + `textedit-probe: PASS` (typed delta 24 > 2× cursor
  blink baseline; see
  `docs/evidence/task-4-phase10-interactive.md` and
  `docs/evidence/task-5-phase10-interactive.md`).
