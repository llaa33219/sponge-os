# Phase 10 / W5 — QMP keyboard input to a focused text editor (criterion 5b)

## Header

- **Date:** 2026-08-04
- **Plan:** `docs/plans/phase10-interactive-desktop.md` (workstream W5)
- **Scenario:** `run/sponge-textedit-qmp.run` (new)
- **Probe:** `repos/sponge/src/test/textedit_probe/main.cc` (new `qmp="yes"` mode)
- **Build configuration:** `KERNEL=sel4 BOARD=pc`
- **Command:**
  ```bash
  cd /home/luke/sponge-os
  timeout 2400 ./tool/build run sponge-textedit-qmp \
      > docs/evidence/task-5-phase10-interactive.log 2>&1
  ```
  (make exit code 0; scenario prints `Run script execution successful.`)
- **Raw captured log:** `docs/evidence/task-5-phase10-interactive.log`

---

## Verdict — GREEN

Criterion 5b is satisfied: QMP `send-key` keyboard input reaches the
focused textedit window through the real chain

  host QMP send-key → emulated PS/2 keyboard → ps2 driver
  → event_filter (en_us.chargen) → nitpicker (focused edit domain)
  → textedit Gui session → Qt key event → QTextEdit document

and the typed text's re-render is observed via the probe's Capture
check. All pre-existing probe checks (install, broadcast running=no→yes,
render fraction + color diversity, pkgd error paths) run unchanged.

| Gate | Marker | Verdict |
|------|--------|---------|
| 1 | `[init -> drivers -> fb] using 1024x768 (1024x768)` | **PASS** |
| 2 | `usb_hid … QEMU QEMU USB Tablet … POINTER` | **PASS** |
| 3 | `textedit-probe: textedit window detected (100% non-bg, 44 distinct color buckets)` | **PASS** |
| 4 | marker loop: absmove/move×N/press/type×2/end dispatched via QMP | **PASS** |
| 5 | `textedit-probe: PASS` | **PASS** |

Final marker: `Run script execution successful.`

### Measured numbers (final green run)

- Closed-loop pointer positioning: `pointer reached (96,96)` (probe
  bisect view) and `pointer reached (512,424)` (document center) —
  **exact**, no drift (relative-move loop, see below).
- Native-delivery bisect ([8a]): the probe's own Gui view received
  `btn_presses=1` (the focus press) and `key_presses=5` (exactly
  h,e,l,l,o — 150 ms hold-time stays under event_filter's 230 ms
  repeat delay).
- Blink baseline vs typed delta: `cursor-blink baseline delta=0`,
  `typed delta=24 > 2x baseline 0` — the PASS requires
  `typed_delta >= 6 (TYPED_FLOOR) && typed_delta > 2*baseline`. A lone
  cursor blink (≤ ~3 sampled points on the stride-8 grid, usually 0)
  cannot satisfy the floor — the misleading_success_output guard holds.

---

## The keyboard half worked immediately; the pointer war did not

QMP `send-key` → PS/2 keyboard → ps2 driver → event_filter → nitpicker
worked on the first try once the config reached the components (ps2
`verbose_scancodes` showed every scancode; nitpicker's `keystate`
report counted held keys). Verified independently at BIOS level: a QMP
Ctrl-Alt-Del resets a SeaBIOS guest (standalone probe).

The **focus click** was the entire battle. QEMU here is 11.0.2 (W1's
evidence assumed 7.2 semantics), and its absolute-pointer
(usb-tablet) injection under headless operation is broken in every
variant tested:

| Variant | Result |
|---|---|
| `-nographic`, split-call click (W1's recipe) | pointer pinned at (1023,767); nitpicker focus/hover reports show the click lands on the domain *background* (focuses the domain owner without the press ever hitting a view — Qt drops everything) |
| `-nographic`, atomic 4-event batch | no motion registers at nitpicker at all |
| `-display none` (+`mouse_set tablet` via HMP) | corner slam (abs values reinterpreted as PS/2 *relative* deltas) |
| `-vnc 127.0.0.1:N` + `mouse_set tablet` | motion lands correctly, but the button events sync against the VNC console's rest position → press lands in the corner |
| explicit `device:"tablet"` argument | **QEMU 11.0.2 crashes** (connection reset, paused and running) |

The `-vnc` multi-boot variant also exposed that the run tool's
`power_off` is a NOP for QEMU — sequential `run_genode_until` spawns
coexist, so a fixed VNC display collides between spawns. The scenario
therefore uses a **single boot** with all gates observed on the same
spawn (3-argument `run_genode_until`).

### The working design: closed-loop relative positioning

Relative (PS/2 mouse) motion is delivered deterministically. The probe
reads the actual pointer position from nitpicker's `pointer` report ROM
(`<report pointer="yes"/>` + focus/hover/clicked/keystate, relayed via
report_rom) and emits `QMP-TARGET move <dx> <dy>` markers (per-axis
steps clamped to ±100 px) until the pointer is within ±3 px of the
target. One `absmove` marker first materializes the pointer at a known
position (the corner slam is deterministic). The press is a button-only
event, so no position sync can relocate it. The scenario stages a
**custom `event_filter.config`** with the `accelerate` wrapper removed
(recipe otherwise identical) so rel counts map ~1:1 to pixels.

Marker contract (host loop `qmp_drive_until_end`, all waits bounded):
`absmove x y` / `move dx dy` / `press` / `type <string>` / `end`.

---

## Other fixes baked into the scenario

1. **Top-level `parent-provides` gained `service RM`.** The
   sponge-textedit.run topology lacks it; the drivers sub-init's `acpi`
   child needs RM from the parent and silently stalls the whole driver
   chain without it (first red run: fb marker never printed, gate-1
   timeout).
2. **send-key KeyValue schema (qmp.inc latent bug).** QEMU's send-key
   takes `keys` as an array of *objects* (`{"type":"qcode","data":"h"}`),
   not strings. W1 only ever exercised `click`, so this survived.
3. **EOL-anchored marker regexes (qmp.inc latent bug).** Unanchored
   expect patterns match partial serial chunks — observed:
   `QMP-TARGET click 512 4` dispatched as (512,4).
4. **W5-local inline QMP helper.** `run/qmp.inc` is outside W5's file
   scope and was observed torn by a concurrent workstream's edit; the
   scenario carries the procs it needs (committed qmp.inc + fixes 2+3).
   **W6 follow-up:** consolidate fixes 2+3 (and the rel-move/press/drive
   procs if the other QMP scenarios converge on the same design) into
   `run/qmp.inc`.
5. **ps2 `verbose_scancodes`/`verbose_keyboard`** are enabled in the
   scenario's drivers sub-init — every key event is visible on serial
   (permanent diagnostic value, negligible cost).

## Regression

`run/sponge-textedit.run` re-run after the probe change: **PASS**
(`textedit-probe: PASS`, `Run script execution successful.`). The
probe's default path is untouched: all QMP-mode code is guarded by
`_config.valid() && _config.node().attribute_value("qmp", false)`, and
the new session members (focus/clicked/hover/pointer/keystate ROMs, the
bisect Gui connection) are constructed lazily only in qmp mode — with
no config ROM delivered (sponge-textedit.run) none of them exist.

## Evidence artifacts

- `docs/evidence/task-5-phase10-interactive.md` (this file)
- `docs/evidence/task-5-phase10-interactive.log` (full transcript of
  the final green run)

## Reproducibility notes

- The 29 px y-drift caveat from W1 does not apply here: the pointer is
  never positioned via absolute events except the deliberate
  corner-slam materialization; all targeting is closed-loop relative
  with ROM feedback, so landing is exact (observed (96,96) and
  (512,424)).
- `click delta=0` in the log is expected: the press only moves the 2 px
  text cursor, which the stride-8 sample grid usually misses. The
  criterion is proven by the typed delta, not the click delta.
- The scenario is deterministic in two consecutive green runs (the
  first with 600 ms key holds + repeat flood, the second — recorded
  here — with 150 ms holds producing exactly "hello").
