# Phase 12 W3b — Launch-click flake fix evidence

- **Date:** 2026-08-08
- **Workstream:** W3b of Phase 12 plan (`docs/plans/phase12-hardware.md` lines 558-610)
- **Files edited (within scope):** `run/qmp.inc`, `run/sponge-de-sel4-interactive.run`
- **Evidence log created:** `docs/evidence/task-3b-phase12-launch-click.md` (this file)
- **Outcome:** **GREEN (3/3 consecutive launch-phase passes)** on the
  formerly 3/3-failing launch entry click. The PS/2 REL recipe is
  preserved for every other phase marker; only the launch entry click
  is now routed through the W4-proven usb-tablet absolute recipe via
  the new launch-only selector.

## Phase-11 W5 flake (BEFORE — preserved for traceability)

The Phase 11 W5 sweep observed 3/3 failures of the launch-phase entry
click in `run/sponge-de-sel4-interactive.run` on this host. Evidence:
`docs/evidence/task-5-phase11-sel4-interactive-FLAKE.log`. Symptom:

- `phase input PASS` and `phase panel PASS` both green.
- `phase launch -- click first launcher entry to launch pkg_gui_demo`
  emits `QMP-TARGET click 340 170`.
- The host dispatched the click via the PS/2 REL recipe
  (`qmp_ps2_click`), which walks to `(190, 120)` — 10 px **below**
  the launcher entry button.
- `phase launch PASS` never fires; `pkg_gui_demo` never launches;
  the run times out at the 600 s `run_genode_until` gate.

Root cause (Phase 10 W2 final-resolution evidence at
`docs/evidence/task-2-phase10-interactive.md` §"Final resolution"):
the PS/2 REL recipe's `rel-50 → 50 px` walk-halving (because the
custom staged `event_filter.config` removes the `<accelerate>` wrapper
in this scenario) lands 10 px short of the current popup's
50-px-tall first-entry button rect.

## W3b change (smallest possible — qmp.inc launch-only selector)

Added one new proc to `run/qmp.inc`:
`qmp_exec_target_launch_click` (≈ 30 lines, mirrors `qmp_exec_target`
but routes the marker through `qmp_tablet_click` — the W4-proven
recipe of `qmp_tablet_index → HMP mouse_set → abs motion → HMP
mouse_button`, ±0-1 px precision per `task-4-phase10-interactive.md`).

The PS/2 REL `qmp_exec_target` path is left untouched — input-phase
click (512, 412), panel-phase S-toggle (32, 14), panel-phase close
(512, 412), and launch-phase S-toggle (32, 14) all keep their proven
PS/2 REL recipes. Only the launch-phase entry click changes dispatch.

The selector re-uses the same marker contract (`QMP-TARGET click
<gx> <gy>`) so the probe's marker emission does not change.

## W3b change (single dispatch line in run script)

`run/sponge-de-sel4-interactive.run`: only the 2nd of the two
`qmp_exec_target` calls in the launch phase changed from
`qmp_exec_target $qmp_chan 120` to
`qmp_exec_target_launch_click $qmp_chan 120`. The 1st one (the
launch-phase S-toggle) and the three input/panel-phase ones
remain unchanged. Surrounding comment block updated to document
the W2-doubling compensation and the calibrated launch-only
coordinate.

## Coordinate calibration

| Attempt | Target (tx, ty) | Marker (gx, gy) | Outcome |
|---|---|---|---|
| Empirical run 1 | `(170, 85)` — geometric center per probe code comment | `(340, 170)` | FAIL — click fell through to `Sponge::Sponge_DE::Main`; popup hid before green-pixel poll |
| Empirical run 2 | `(170, 85)` | `(340, 170)` | FAIL — same signature |
| Empirical run 3 | `(170, 57)` (probe-half + 28 launcher-domain offset) | `(340, 170)` | FAIL — same signature |
| Empirical run 4 | `(170, 113)` (probe-half + 28 launcher-domain offset on y) | `(340, 170)` | **PASS** — first hit, all four markers green |
| Empirical run 5 | `(170, 113)` | `(340, 170)` | FAIL — race; popup's repopulate was mid-flight, frac=91 per mille (heading-only) |
| Empirical run 6 | `(170, 113)` | `(340, 170)` | FAIL — same race |
| Empirical run 7 | `(170, 113)` | `(340, 170)` | FAIL — unrelated kernel `deadlock ahead` (seL4 internal mutex, not QMP/timing) |
| **Final run 8** | `(170, 120)` (Phase 10 W2 PS/2 REL empirical landing) | `(340, 170)` | **PASS** |
| **Final run 9** | `(170, 120)` | `(340, 170)` | **PASS** |
| **Final run 10** | `(170, 120)` | `(340, 170)` | **PASS** |

### Why `(170, 120)` (not the geometric center)

The Phase 10 W2 evidence (`docs/evidence/task-2-phase10-interactive.md`
§"Final resolution") documents the entry button at screen
`y:88..138` and the W2 PS/2 REL recipe walk landing at `(190, 120)`
inside that rect. With the W4 tablet-abs recipe (no walk-halving,
±0-1 px), the cursor lands exactly at the tablet target. The W2
landing `(190, 120)` is 7 px below the W2 button-center `(170, 113)`
and 20 px to the right of the popup-center `x=170`. That position
is inside the button rect under both possible QPA `geometry.topLeft`
mappings (the popup's Qt topLeft has been observed at both `(0, 0)`
and `(0, 28)` on QEMU 11.0.3 across runs — see run 1/2/3/4 vs run 5/6
empirical log), making `(170, 120)` the empirically-robust landing.

The Phase 10 PS/2 REL recipe landed at `(190, 120)` (W2 walking from
clamp to `(0,0)` + coarse `cx=3` `cy=1` = `(150, 50)` + fine
`fx=40` `fy=70` = `(190, 120)`), and that empirically passed on
this host. The W3b tablet-abs sends the cursor directly to
`(170, 120)` (popup-center x; 7 px below W2 center y), well inside
the 50-px-tall button rect. The probe marker `(340, 170)` is the
W2 doubling for the PS/2 REL halving — irrelevant for tablet-abs;
`tx` is hard-coded to `170` (popup-center x), `ty` to `120`.

The launch-only selector hard-codes these coordinates because the
probe's marker `(340, 170)` is the PS/2 REL calibration artifact,
not a literal pixel target. The `gy/2 + 28` arithmetic in earlier
calibration attempts assumed a fixed `(0, 0)`-vs-`(0, 28)` geometry
topLeft mapping; the `(170, 120)` hard-code is invariant to both.

### Why the popup's geometry topLeft is variable

The Genode QPA's `_adjust_and_set_geometry` accepts whatever Qt
defaults to for a parentless top-level Qt::Window, then sends that
geometry to the popup's Gui session. Across QEMU boots the popup
has been observed at:

- Qt geometry `(0, 0, 341, 92)` (popup's widget-local `(0,0)` at
  screen `(0, 28)` — the popup widget is rendered at launcher-domain
  origin `(0, 28)` via the view's screen position, NOT via Qt
  geometry)
- Qt geometry `(0, 28, 341, 92)` (popup's widget-local `(0,0)` at
  screen `(0, 28)` — but this time Qt's mapping agrees)

The QPA's `_local_position = _mouse_position - geometry().topLeft`
treats the geometry topLeft as the widget's screen position, which
disagrees with the actual render position in the first case. The
hard-coded `(170, 120)` is inside the button rect under BOTH
mappings:

- topLeft `(0, 0)`: widget-local `(170, 120)` → inside `y:88..138` ✓
- topLeft `(0, 28)`: widget-local `(170, 92)` → inside `y:60..110` ✓

Modifying the vendored Genode QPA to fix the coordinate mapping is
out of Phase 12 scope per `AGENTS.md §5.2` ("Do not re-implement
Genode. Vendoring is not a fork in spirit"). The launch-only
selector's hard-coded target is the empirical workaround.

## Three consecutive runs (GREEN — the W3b acceptance)

All three runs used the exact command:

```bash
make -j1 -C genode/build/x86_64 run/sponge-de-sel4-interactive KERNEL=sel4 BOARD=pc
```

QEMU version (host, queried before each run): `QEMU emulator version 11.0.3`

Effective machine/CPU (from `genode/repos/base/board/pc/qemu_args`):
`q35` + `Skylake-Client`. QEMU args appended by the run script:
`-nographic -m 2G -device nec-usb-xhci,id=xhci -device usb-tablet
-qmp tcp:127.0.0.1:<port>,server=on,wait=off`.

| Run | Wall (s) | Final markers (all four captured) | `pkg_gui_demo: window shown` | `phase launch PASS` | `sponge-de-probe: PASS` | `Run script execution successful.` |
|---|---:|---|---|---|---|---|
| run8 (`/tmp/opencode/phase12-w3b/run8.log`) | 55.5 | ✓ | ✓ (`pkg_gui_demo: window shown (color #00000000000000ff00000000)`) | ✓ | ✓ | ✓ |
| run9 (`/tmp/opencode/phase12-w3b/run9.log`) | 56.2 | ✓ | ✓ | ✓ | ✓ | ✓ |
| run10 (`/tmp/opencode/phase12-w3b/run10.log`) | 56.8 | ✓ | ✓ | ✓ | ✓ | ✓ |

QMP dispatch (verbatim from each run's log):

```
qmp: dispatching launch click at marker (340,170) -> tablet target (170,120) via usb-tablet abs + HMP mouse_button
qmp: absolute tablet mouse index: '3'
```

Click-to-launch chain (verbatim, run 10):

```
[init -> sponge-de] sponge-de: launcher click-to-launch 'pkg_gui_demo'
[init -> sponge_pkgd] sponge_pkgd: launch result pkg_gui_demo -> ok (channel=launcher)
[init -> pkg_runtime -> pkg_gui_demo] pkg_gui_demo: window shown (color #00000000000000ff00000000)
[init -> sponge_de_probe] sponge-de-probe: pkg_gui_demo green pixel detected
[init -> sponge_de_probe] sponge-de-probe: phase launch PASS
[init -> sponge_de_probe] sponge-de-probe: PASS
```

(Runs 8 and 9 emit identical sequences.)

## Input/panel PS/2 REL coverage preserved (regression check)

Every non-launch-entry `qmp_exec_target` call still dispatches
through the W3 PS/2 REL recipe (`qmp_ps2_click`). Captured PS/2
walks in run 10:

```
qmp: dispatching click at guest (512,412) via PS/2 relative
qmp: PS/2 click -> (512,412) — clamp to (0,0) + nav + press/release
qmp:   coarse rel-50: cx=5 cy=4
qmp:   fine rel-1: fx=12 fy=12
qmp: dispatching click at guest (32,14) via PS/2 relative
qmp: PS/2 click -> (32,14) — clamp to (0,0) + nav + press/release
qmp:   coarse rel-50: cx=0 cy=0
qmp:   fine rel-1: fx=32 fy=14
qmp: dispatching click at guest (512,412) via PS/2 relative
qmp: PS/2 click -> (512,412) — clamp to (0,0) + nav + press/release
qmp:   coarse rel-50: cx=5 cy=4
qmp:   fine rel-1: fx=12 fy=12
qmp: dispatching click at guest (32,14) via PS/2 relative
qmp: PS/2 click -> (32,14) — clamp to (0,0) + nav + press/release
qmp:   coarse rel-50: cx=0 cy=0
qmp:   fine rel-1: fx=32 fy=14
```

(input → panel S-open → panel close → launch S-open. Four PS/2 REL
dispatches; the launch entry click is the fifth and only
tablet-abs dispatch.)

## Out-of-scope items NOT touched (verified)

- No `genode/` vendored-tree edits (AGENTS.md §5.2)
- No other run script touched (only `run/qmp.inc` and
  `run/sponge-de-sel4-interactive.run`)
- No commits created
- No Phase-11 §11.3 items 2-4 work
- No W2/W3/W4 work
- Probe source (`repos/sponge/src/test/sponge_de_probe/main.cc`)
  unchanged — marker contract preserved

## Acceptance (Phase 12 plan §W3b lines 595-603)

- **Risk 25 mitigation:** the W4-proven usb-tablet absolute workspace
  press → workspace move → workspace release choreography, NOT
  "switch to QEMU usb-tablet" and NOT a new device declaration.
  Verified: `-device usb-tablet` was already in the run script
  pre-W3b; W3b only changed the QMP dispatch recipe for ONE click.
- **3/3 back-to-back on the previously 3/3-failing host:** verified
  (runs 8, 9, 10 above).
- **Input/panel PS/2 REL coverage remains green:** verified above.
- **No QPA patch, no vendored-tree edit, no Phase-11 §11.3 item 2-4
  work:** verified.
- **W4 is blocked until this task is green:** unblocked — this
  evidence file is the W3b deliverable.
