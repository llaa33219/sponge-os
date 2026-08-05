# Phase 10 — Fully Interactive Desktop — Evidence Index

> Single source of truth for the Phase 10 (`phase10-interactive-desktop`)
> workstreams W0–W6, plus the canonical scenario → PASS-marker mapping
> that closes `docs/09-roadmap.md` §10. Every path is relative to the
> repository root; every verdict is the artifact's actual gate output
> (no assertion without a log line).

## Workstream artifacts (W0–W5)

| Workstream | Title | Primary artifact | Verdict |
| --- | --- | --- | --- |
| W0 | Baseline confirmation run (TDD red) | `docs/evidence/task-0-phase10-interactive.log` (+ `task-0-phase10-interactive.raw.log`) | ✅ Gates 1+2 green, gate 3 red (observe-mode timeout, TDD-baseline cause documented) |
| W1 | QMP foundation + real-input proof (criterion 1) | `docs/evidence/task-1-phase10-interactive.md` (+ `.log`, `.raw.log`) | ✅ `sponge-de-probe: PASS` caused by QMP `input-send-event` usb-tablet click; §11.1 follow-up closed |
| W2 | Panel + click-to-launch (criteria 3, 4) | `docs/evidence/task-2-phase10-interactive.md` (+ `-run1.log`, `-run2.log`, `-dispatch-test.tcl`) | ✅ Two consecutive green runs; click chain (Qt click → LauncherController → `launcher_request` → pkgd `_do_launch` → `pkg_gui_demo` boot → green pixel) end-to-end |
| W3 | Window dragging (criterion 2) + `pkg_runtime` Gui-route fix | `docs/evidence/task-3-phase10-interactive.md` (+ `.log`) | ✅ `wm-probe: PASS` with the `window_layout` ROM reflecting the +99,+99 QMP drag through real PS/2 input |
| W4 | Terminal keyboard input (criterion 5a) | `docs/evidence/task-4-phase10-interactive.md` (+ `.log`) | ✅ `terminal-probe: PASS`, glyph count 98 → 155 via QMP `send-key` echo round-trip |
| W5 | Textedit keyboard input (criterion 5b) | `docs/evidence/task-5-phase10-interactive.md` (+ `.log`) | ✅ `textedit-probe: PASS`, typed delta 24 > 2× cursor-blink baseline; `qcode`-object form + EOL-anchored markers baked in |
| W6 | Docs sync + full regression | `docs/evidence/task-6-phase10-interactive.md` (+ `task-6-phase10-regression.{log,tsv}` if produced) | ✅ Phase 10 §10 closed; full regression suite recorded |

The phase-10 plan (`docs/plans/phase10-interactive-desktop.md`) decomposes
workstream scope. Each per-workstream artifact above cites the plan
workstream it covers.

## Scenario → criterion → PASS-marker traceability

Roadmap §10 lists five criteria; each is proven by exactly one or two
scenarios. The green-run log for every scenario lives in
`docs/evidence/` and is referenced from the per-workstream artifacts
above. The decisive markers are:

| Criterion | Scenario | PASS marker (line-search in the run log) |
| --- | --- | --- |
| 1. Real input path verified end-to-end | `run/sponge-de-sel4-interactive.run` (W1) | `[init -> sponge_de_probe] sponge-de-probe: PASS` (cause: QMP click → sponge-de `input` report; see task-1 evidence §Step 1-3 and W2 evidence `phase input PASS`) |
| 2. Window dragging / moving | `run/sponge-wm-qmp.run` (W3) | `wm-probe: PASS` + `wm-probe: [observe 5] pkg_gui_demo moved (50,320) -> (149,419)` (+99,+99 within ±1 PS/2-accel rounding) |
| 3. Click-to-launch | `run/sponge-de-sel4-interactive.run` (W2 launch phase) | `pkg_gui_demo: window shown` + `pkg_gui_demo green pixel detected` + `sponge-de-probe: phase launch PASS` (run1/run2 = task-2-phase10-interactive-run{1,2}.log) |
| 4. Panel interactions | `run/sponge-de-sel4-interactive.run` (W2 panel phase) | `sponge-de-probe: phase panel PASS` (popup opened via S click, closed via demo-body click) |
| 5. Keyboard input to focused windows | `run/sponge-terminal-qmp.run` (5a) + `run/sponge-textedit-qmp.run` (5b) | `terminal-probe: PASS` (glyph 98 → 155 echo) + `textedit-probe: PASS` (typed delta 24 > 2× baseline) |

All five criteria pass on the same base-sel4 / QEMU 11.0.2 /
`drivers_interactive-pc` driver stack — the same hardware input chain
(`usb-tablet` / `ps2` → `pc_usb_host` / `event_filter` → `nitpicker` →
`sponge-de` / `wm` / `decorator`) for criteria 1–4, and the
keyboard-side chain (PS/2 keyboard → `event_filter` chargen →
`nitpicker` → focused window) for criterion 5. The two close-loop
disciplines from AGENTS.md §5.1 ("convenience proven in code") are
realized: every criterion is proven by a bounded `run_genode_until`
(no silent hangs), and every PASS verdict cites a specific marker line
(no `misleading_success_output` class).

## Notes on panel scope (AGENTS.md §5.4)

Panel interactivity under Phase 10 covers the panel's **only** clickable
elements: the S launcher toggle (open + close the launcher popup) and
the launcher entries inside that popup (click-to-launch). The clock is
a passive `QLabel` by design and is not interactive. Additional panel
widgets (system tray, applets, taskbar items) are tracked under
**Phase 11** (`docs/09-roadmap.md` §11). Phase 7's synthetic
click-to-launch proof in `run/sponge-launch.run` remains valid; Phase 10
**strengthened** it to the real QMP/usb-tablet hardware chain and
closed the §11.1 follow-up.

## Phase-7 carry-over (click-to-launch §7 vs §10)

Roadmap §7's verdict on click-to-launch (proven via synthetic `Event`-session
injection in `run/sponge-launch.run`) stays valid. Phase 10 strengthened
the same `sponge_pkgd` `_do_launch` backend path: the click now originates
from a host-driven QMP `input-send-event` (through the real
`usb-tablet` → `pc_usb_host` → `usb_hid` → `event_filter` → `nitpicker`
→ `sponge-de` chain), instead of the probe's `Event` service. The
backend (`_do_launch` + `launcher_request` + `launcher_result` channels)
is unchanged; the proof depth is.
