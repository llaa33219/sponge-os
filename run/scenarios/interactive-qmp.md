# Interactive drivers and QMP input

Breadcrumb: [`run/README.md`](../README.md) → Interactive drivers and QMP input

The base-sel4 interactive driver stack and the host-driven QMP input
scenarios (pointer drag, keyboard typing, USB hotplug), plus the shared
`run/qmp.inc` Tcl helper. Full catalog entries follow verbatim (cautions
and limitations included); the parent index carries one-line summaries.

## Scenarios
- `sponge-de-sel4-interactive.run` — base-sel4 (seL4 / QEMU) interactive
  GUI driver stack: the vendored `drivers_interactive-pc` set (vesa_fb,
  ps2, usb_hid, pc_usb_host, event_filter, platform, acpi, pci_decode)
  built from source and wired with `-device nec-usb-xhci -device
  usb-tablet` for absolute-pointer input. **Phase 10** extended the
  scenario in place: every input action is now dispatched from the host
  via QEMU QMP (Tcl `socket` over TCP, PID-derived port) using the
  shared `run/qmp.inc` helper. The probe runs in three bounded observe
  phases (FATAL — any timeout/marker failure is a hard FAIL):
  1. **input** (criterion 1) — a host QMP click on the demo domain,
     real driver chain verified end-to-end (`phase input PASS`).
  2. **panel** (criterion 4) — a host QMP click on the panel's S
     launcher toggle opens the popup; a second QMP click on the demo
     body closes it (`phase panel PASS`). The panel's only
     clickable widgets today (the launcher S toggle and launcher
     entries inside the popup) are exercised. The clock is a passive
     `QLabel` by design; additional panel widgets are Phase 11 scope.
  3. **launch** (criterion 3) — opens the launcher popup, then a
     QMP-driven click on the first installed launcher entry
     (`pkg_gui_demo`) goes through the full chain: Qt click →
     `LauncherController::request_launch` → `launcher_request`
     report → `sponge_pkgd` `_do_launch` → regenerated `pkg_runtime`
     config → `pkg_gui_demo` first paint, green-pixel verified
     (`pkg_gui_demo: window shown` + `pkg_gui_demo green pixel
     detected` + `phase launch PASS`).
  Final markers: `sponge-de-probe: PASS` + `Run script execution
  successful.`. See `docs/evidence/phase10-index.md` for the per-phase
  mapping, `docs/evidence/task-2-phase10-interactive-{run1,run2}.log`
  for two consecutive green runs, and `docs/08-development.md` §"Host-
  driven QMP input" for the qmp.inc API + QEMU-11 caveats. base-sel4
  only (`assert {[have_spec sel4]}`); the Qt6/Mesa-on-sel4 root cause
  remains in `docs/09-roadmap.md` §11.1 (capability exhaustion;
  `caps: 1000` on seL4).

- `sponge-wm-qmp.run` — **Phase 10 criterion 2**: real-pointer
  window drag on base-sel4. Same topology as `run/sponge-wm.run`
  (upstream `wm` + `window_layouter` + `decorator` stack) plus the
  vendored `drivers_interactive-pc` drivers sub-init copied from
  `run/sponge-de-sel4-interactive.run`, plus `sponge_pkgd` +
  `pkg_runtime` (with the W3 phase `+ service Gui | + child wm` route
  fix and a layouter `<assign label_prefix="pkg_runtime"
  target="screen" xpos=50 ypos=300 width=320 height=240>` rule), plus
  the staged `pkg_gui_demo` package and `wm_probe` in `inject="no"`
  observe mode. QEMU: `-m 2G`, xhci + usb-tablet, `run/qmp.inc`-driven
  PS/2 Mouse drag (proven more reliable than the usb-tablet
  absolute-axis recipe under QEMU 11.0.3 because nitpicker's pointer
  sanitizer clamps every `Absolute_motion` value to screen center;
  see the qcode-block comment at the top of `run/qmp.inc`). The probe
  launches `pkg_gui_demo` via `sponge_pkgd`'s `request` channel,
  observes its window appearing in the `window_layout` ROM, emits a
  `QMP-TARGET drag <x1> <y1> <x2> <y2>` marker, waits for the
  `window_layout` position change AND a Capture pixel check at the new
  location, then asserts `wm-probe: PASS`. Gates: fb `using 1024x768` →
  usb_hid `POINTER` → QMP drag → `wm-probe: PASS`. base-sel4 only.
  See `docs/evidence/task-3-phase10-interactive.md` and
  `docs/evidence/phase10-index.md`.

- `sponge-terminal-qmp.run` — **Phase 10 criterion 5a**: real QMP
  keyboard input to a focused terminal. Topology: the
  `run/sponge-terminal.run` setup (terminal package, bash-minimal.tar,
  VeraMono.ttf, vfs_ttf) plus the vendored `drivers_interactive-pc`
  drivers sub-init, plus `run/qmp.inc` (Tcl QMP over TCP). The keyboard
  chain: QMP `send-key` → emulated PS/2 keyboard → `ps2` driver →
  `event_filter` (`en_us.chargen` + `special.chargen`) → nitpicker →
  focused terminal Gui session (`pkg_runtime -> terminal -> terminal`)
  → gems terminal server read buffer → `/dev/terminal` (vfs
  `<terminal/>` plugin) → noux bash echo → terminal re-render,
  observed as a glyph-count increase. The `terminal_probe` `qmp="yes"`
  mode emits `QMP-TARGET click <gx> <gy>` at the terminal window
  center (focus), then `QMP-TARGET type echo ok`; the run script
  connects QMP and dispatches via `qmp_send_key` (the
  qcode-object form, NOT the rejected string form — see
  `docs/08-development.md` §"Host-driven QMP input"). All probe waits
  bounded; the click + type are driven by host QMP, not synthetic
  Event injection. Gates: fb → usb_hid → render → focus click → type
  → `terminal-probe: PASS`. base-sel4 only. See
  `docs/evidence/task-4-phase10-interactive.md`.

- `sponge-textedit-qmp.run` — **Phase 10 criterion 5b**: real QMP
  keyboard input to a focused text editor. Topology: the
  `run/sponge-textedit.run` setup (qt6_textedit, full-screen
  `edit` domain at (0,0) with `focus: click`, staged textedit payload
  from `pkg/textedit/payload/`) plus the `drivers_interactive-pc`
  drivers sub-init, plus `run/qmp.inc`. The keyboard chain runs end-to-
  end; the `textedit_probe` `qmp="yes"` mode emits `QMP-TARGET click`
  for focus, then `QMP-TARGET type hello`, and verifies the typed
  delta's Capture sample exceeds 2× the cursor-blink baseline
  (`typed_delta >= TYPED_FLOOR && typed_delta > 2*baseline`, the
  `misleading_success_output` defense: a lone cursor blink cannot
  pass). Gates: fb → usb_hid → render → focus click → type →
  `textedit-probe: PASS`. base-sel4 only. See
  `docs/evidence/task-5-phase10-interactive.md`.

- `run/qmp.inc` — shared Tcl helper for the four Phase 10 QMP
  scenarios above. Sourced by a run script via
  `source [file join [file dirname [file normalize [info script]]] qmp.inc]`
  (Tcl 8.x, no external dependencies — only `socket` and `expect`,
  already used by the run tool). Provides: `qmp_pick_port` (free TCP
  port via Tcl `socket -server 0`), `qmp_connect` (bounded retry +
  QMP greeting read + `qmp_capabilities` handshake), `qmp_cmd` (send
  one JSON, skip async events, die loud on `{"error":...}`),
  `qmp_abs`, `qmp_pointer_move`, `qmp_button`, `qmp_click`,
  `qmp_ps2_click` (W3 calibrated relative-recipe with clamp-to-(0,0)
  + coarse rel-50 + fine rel-1 + hover jiggle), `qmp_drag`,
  `qmp_send_key` (qcode-object form on QEMU 11; the string form is
  rejected with `Invalid parameter type for 'keys[0]', expected:
  object`), `qmp_type` (char → QEMU keyname map), `qmp_exec_target`
  (bounded `expect` on global `qemu_spawn_id` for QMP-TARGET markers
  emitted by probes; FAIL + exit 1 on timeout), `qmp_disconnect`.

- `sponge-usb-kbd-via-qmp.run` — **Phase 12 — Phase-12 criterion 2
  expanded driver set (USB HID keyboard via QMP)**. Reuses the
  `run/sponge-de-sel4-interactive.run` driver stack (q35+Skylake-
  Client, `pc_usb_host`, `usb_hid`, `event_filter`) plus the
  Phase-12 W3b QMP choreography fix. Boots without a static
  usb-kbd pass condition, then QMP `device_add`s a named `usb-kbd`
  device, observes `usb_hid` devices report `KEYBOARD` and emits
  `usb_hid: KEYBOARD detected`, QMP `device_del`s the keyboard
  and captures the removal event, then dispatches `send-key` (the
  QEMU-11 qcode-object form) via the QMP helper. The
  primary required gate is the audit chain
  `usb_hid: KEYBOARD detected` → `usb_hid: KEYBOARD removed`
  → `sponge-usb-kbd-via-qmp: PASS` + `Run script execution
  successful.`. The glyph-delta secondary gate is a documented
  Phase-12 gap (probe-focus ROM quirk; the post-device_del
  send-key travels the PS/2 path already covered by
  `sponge-terminal-qmp.run`). 600 s+ seL4 desktop reality, bounded at
  900 seconds. Evidence `docs/evidence/phase12-usb-kbd.log`. No
  usb-mouse, no `i2c_hid`, no new USB controller class.
