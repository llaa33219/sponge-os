# Phase 10 — Sponge DE: Fully Interactive Desktop (Work Plan)

> Status: approved structure, execution pending. Created 2026-08-04.
> Roadmap reference: `docs/09-roadmap.md` §10 (lines 418–442).

## Context

**Goal (docs/09-roadmap.md §10):** the desktop becomes fully operable through
real input — mouse and keyboard work end-to-end through the actual driver chain
(usb-tablet/ps2 → pc_usb_host/usb_hid/ps2 → event_filter → nitpicker →
sponge-de/applications), windows can be dragged, and every interactive element
responds to clicks. Input is driven from the host via QEMU QMP
(`input-send-event`, `send-key`), replacing synthetic in-guest Event injection
for the Phase-10 proofs.

**Click-to-launch framing (no roadmap contradiction):** Phase 7 (todo 10)
delivered click-to-launch and verified it with **synthetic** clicks injected by
`launch_probe` via nitpicker's Event service (`run/sponge-launch.run`,
`launch-probe: PASS`). Phase 10 does not re-deliver the feature; it
**re-verifies the same `_do_launch` backend path over the real QMP/usb-tablet
hardware chain and wires the launcher into the interactive desktop scenario**
(`run/sponge-de-sel4-interactive.run`). Roadmap §7's verdict stays valid; §10's
criterion is a stronger proof of the same capability.

**Scope guards:**

- `themed_decorator` as desktop decorator is **Phase 11** — out of scope.
- The open design question in `docs/05-sponge-de.md` §7 (nitpicker vs Sponge DE
  window-placement role split) must **not** be silently settled — Phase 10
  keeps using upstream wm/layouter/decorator exactly as `run/sponge-alpha.run`
  does.
- No changes to vendored `genode/` tree. No new host tools (Tcl `socket` is
  built in).

**Verified ground truth (confirmed by reading the files):**

- **W0 baseline (2026-08-04, `docs/evidence/task-0-phase10-interactive.log`):
  gate 3 PASSED unexpectedly** — the `<config | inject: no>` shorthand at
  `run/sponge-de-sel4-interactive.run:317` does NOT reach the probe's config
  ROM; the probe ran with `inject=true` (default) and injected synthetically.
  W1 gains a step 0: diagnose the config-delivery quirk (inspect the generated
  scenario config XML under the build dir's `var/`), make `inject="no"` take
  effect from the run-script side (element form or correct attribute syntax),
  re-run to establish the TRUE red baseline, then implement QMP. The original
  analysis below (observe-mode skeleton, greenfield QMP, etc.) remains valid.

- `run/sponge-de-sel4-interactive.run` gate 3 passes today only via the
  probe's **synthetic** injection (the `inject="no"` config never arrives —
  see the W0 note above). The criterion-1 proof requires the real driver
  path, which no host-side injection exercises yet.
- `sponge_de_probe` already has the observe-mode skeleton
  (`repos/sponge/src/test/sponge_de_probe/main.cc:238-286`): `inject=no`
  watches the `sponge_de_input` ROM for 900×100ms; the `_fail` message
  literally says "usb-tablet injection missing".
- `run/sponge-power.run:228-244` is the in-repo precedent for raw `expect` on
  the global `qemu_spawn_id`; the Genode run tool spawns QEMU under expect
  (`genode/tool/run/power_on/qemu:163`).
- Zero QMP usage exists in repo or Genode tool — greenfield. No shared `.inc`
  convention exists in `run/`; upstream precedent for shared run-script code is
  `genode/repos/libports/run/qt6_common.inc` (vendored). `${genode_dir}` is available in run
  scripts; `[info script]` resolves the run file's own path.
- `run/sponge-textedit.run` and `run/sponge-terminal.run` exist with
  per-package probes; both use nitpicker domains with `focus: click`. Repo
  convention is **one package/concern per scenario**.
- `run/sponge-alpha.run:577-583`: `pkg_runtime` has **no explicit Gui route** —
  launched GUI packages resolve Gui via `any-service → parent → any-child →
  nitpicker`, bypassing the wm (undecorated, undraggable). Layouter `assign`
  only covers `sponge-de -> Sponge DE Demo` (line 517).
- `sponge_pkgd` launcher channel is opt-in via `<launcher_request/>` in its
  config (`repos/sponge/src/sponge_pkgd/main.cc:1610-1627`); `_do_launch`
  (line 1104) is shared by vct `request` and `launcher_request` channels.
- Invocation convention (docs/08-development.md): `./tool/build run
  <scenario>`; manual equivalent `make -C genode/build/x86_64 run/<scenario>
  KERNEL=sel4 BOARD=pc`. seL4 scenarios require `KERNEL=sel4 BOARD=pc` in
  `genode/build/x86_64/etc/build.conf` (or the make-level overrides).
- Evidence convention (docs/evidence/INDEX.md): `task-<n>-<phase>-<slug>.{md,log}`
  artifacts plus an index table.
- `run/sponge-alpha.run` is base-sel4 only (`assert {[have_spec sel4]}`,
  line 58). All Phase-10 scenarios target **base-sel4** (the real-input
  criterion names the seL4 interactive chain; the synthetic base-linux variants
  remain as the fast regression tier).

## Scenario Architecture Decision

**Decision: extend one, create three — four seL4/QMP scenarios total,
following the repo's one-concern-per-scenario convention. NOT one
mega-scenario.**

| Scenario | Criteria | Action |
|---|---|---|
| `run/sponge-de-sel4-interactive.run` | 1 (real input), 3 (click-to-launch), 4 (panel) | **Extend in place**: add QMP, pkgd+pkg_runtime+pkg_gui_demo wiring, multi-phase probe |
| `run/sponge-wm-qmp.run` (new) | 2 (window drag) | New: wm stack + drivers + QMP drag on a launched, decorated package window |
| `run/sponge-terminal-qmp.run` (new) | 5 (keyboard, terminal) | New: sponge-terminal.run topology + drivers + QMP `send-key` |
| `run/sponge-textedit-qmp.run` (new) | 5 (keyboard, text editor) | New: sponge-textedit.run topology + drivers + QMP `send-key` |

**Tradeoff evaluation (why not one mega-scenario):**

- *Boot time:* a merged boot (drivers + wm + pkgd + terminal + textedit +
  sponge-de + 3–4 Qt6 softpipe renders) would need 600s+ gates and ~3G RAM;
  `sponge-launch.run` already needs 600s for just two Qt6 paints. Four focused
  scenarios run in the same total wall time but fail independently.
- *Caps/RAM budget:* each merged child multiplies the seL4 3× cap-accounting
  problem (§11.1); focused scenarios keep per-boot budgets at proven sizes
  (alpha-scale, 2G QEMU).
- *image.elf staging (docs/11 §10.4, ~256 MiB boot-module ceiling):* terminal
  (bash-minimal.tar, VeraMono.ttf) + textedit payload + Qt6 closure in one
  image.elf risks the ceiling; split scenarios each stay far below it.
- *Debuggability/regression clarity:* per-criterion PASS markers map 1:1 to
  roadmap checkboxes; a mega-scenario failure is ambiguous. The existing suite
  (sponge-wm, sponge-launch, sponge-terminal, sponge-textedit) stays untouched
  as the fast synthetic regression tier.

**Shared infrastructure:** one new `run/qmp.inc` (Tcl, sourced via
`[file dirname [info script]]`) — justified as the first piece of shared
run-script code in `run/`, following the upstream `qt6_common.inc` precedent.
Documented in `run/README.md` and `docs/08-development.md`.

**QMP keyboard choice:** `send-key` (QEMU human key names, atomic
press+release, optional hold-time) over raw `input-send-event` key events — no
scancode/qkey-code tables to maintain, and events flow through the same
emulated PS/2 keyboard → ps2 driver → event_filter chargen (en_us.chargen,
already staged) → nitpicker → focused window. `input-send-event` is used only
for the usb-tablet absolute pointer (the criterion-1 chain).

**Marker choreography (probe ↔ host contract):** probes in QMP mode log
machine-parseable target lines on the serial console: `QMP-TARGET click <gx>
<gy>`, `QMP-TARGET drag <x1> <y1> <x2> <y2>`, `QMP-TARGET type <string>`. The
run script does a bounded `expect` on `qemu_spawn_id` for the marker
(sponge-power.run precedent), executes the action via the QMP TCP socket, then
resumes `run_genode_until` for the PASS marker. Every wait is bounded — fail
loud, never hang.

## Task Dependency Graph

| Task | Depends On | Reason |
|---|---|---|
| W0: Baseline red-run | None | Establishes current gate-3 failure before any change (TDD red) |
| W1: QMP foundation + criterion 1 | W0 | Needs confirmed red baseline; creates `run/qmp.inc` everything else uses |
| W2: Panel + click-to-launch (criteria 3,4) | W1 | Same probe + same scenario file as W1; uses qmp.inc |
| W3: Window drag (criterion 2) + pkg_runtime Gui-route fix | W1 | Uses qmp.inc + QMP-TARGET contract; independent files from W2 |
| W4: Terminal keyboard (criterion 5a) | W1 | Uses qmp.inc `qmp_type`; independent files |
| W5: Textedit keyboard (criterion 5b) | W1 | Uses qmp.inc `qmp_type`; independent files |
| W6: Docs sync + evidence + full regression | W2, W3, W4, W5 | Roadmap checkboxes require all five criteria green |

## Parallel Execution Graph

```
Wave 1 (start immediately):
└── W0: baseline red-run of sponge-de-sel4-interactive (read-only confirmation)

Wave 2 (after W0):
└── W1: run/qmp.inc + probe QMP-TARGET marker + interactive-scenario criterion 1
        (CRITICAL PATH — everything else needs qmp.inc)

Wave 3 (after W1 — fire all four IN PARALLEL, disjoint file sets):
├── W2: criteria 3+4 in sponge-de-sel4-interactive.run + sponge_de_probe phases
├── W3: criterion 2, run/sponge-wm-qmp.run + wm_probe observe mode + alpha route fix
├── W4: criterion 5a, run/sponge-terminal-qmp.run + terminal_probe qmp mode
└── W5: criterion 5b, run/sponge-textedit-qmp.run + textedit_probe qmp mode

Wave 4 (after Wave 3):
└── W6: docs/09 checkboxes, run/README.md, docs/08 §QMP, evidence index,
        full regression suite re-run

Critical Path: W0 → W1 → W2 → W6
```

NOTE: scenario runs must be sequential (no concurrent `make` in
`genode/build/x86_64`) — code edits may parallelize, boots must serialize.

## Tasks

### W0: Baseline confirmation run (TDD red)

Run `run/sponge-de-sel4-interactive.run` exactly as-is (build.conf at
`KERNEL=sel4 BOARD=pc`) and record the result. Expected: gates 1 (vesa_fb
`using 1024x768`, 90s) and 2 (usb_hid `QEMU QEMU USB Tablet … POINTER`, 90s)
PASS; gate 3 (`sponge-de-probe: PASS`, 120s) **times out** because `inject="no"`
observe mode waits ~90s for a press nobody injects. If gate 3 unexpectedly
passes, stop and investigate — something else is injecting input and the whole
QMP premise needs re-examination. Record the outcome (including the exact
timeout/failure line) in `docs/evidence/task-0-phase10-interactive.log`. No
code changes.

- Category: `quick`; Skills: []
- **Depends On:** None
- **Acceptance Criteria:** Evidence log exists showing gates 1–2 green and
  gate 3 red (or, if green, a written diagnosis that changes W1's premise).

### W1: QMP foundation + real-input proof (criterion 1)

1. **Create `run/qmp.inc`** — reusable Tcl helper, sourced by run scripts via:
   ```tcl
   source [file join [file dirname [file normalize [info script]]] qmp.inc]
   ```
   Procs:
   - `qmp_connect port` → `socket 127.0.0.1 $port` (Tcl socket is TCP-only,
     matching `-qmp tcp:`), line-buffered; read the QMP greeting; send
     `{"execute":"qmp_capabilities"}`; read the `{"return":{}}` ack; return
     channel. Bounded reads (wrap in a read-with-deadline helper; a stuck QMP
     socket must fail loud).
   - `qmp_cmd chan json` → send one JSON command, skip async `{"event":...}`
     lines, return the response; die with a clear message on `{"error":...}`.
   - `qmp_abs x y` → scale guest coords to QMP abs range `0..32767` for the
     1024x768 framebuffer (`x*32767/1024`, `y*32767/768`; starting point —
     calibrate in step 3).
   - `qmp_pointer_move chan x y`, `qmp_button chan btn down|up`,
     `qmp_click chan x y` (move → btn-left down → up, each an
     `input-send-event`), `qmp_drag chan x1 y1 x2 y2 steps` (move → small
     on-title-bar jiggle to advance decorator hover (mirrors wm_probe's
     synthetic pattern) → press → N interpolated abs motions → release),
     `qmp_send_key chan keyname`, `qmp_type chan string` (char→keyname map:
     `a-z`→letter, `0-9`→digit, space→`spc`, `\n`→`ret`, `-`→`minus`,
     `.`→`dot`; reject unmapped chars loudly).
   - `qmp_exec_target chan timeout_s` → bounded `expect` on global
     `qemu_spawn_id` for `QMP-TARGET (click|drag|type) …`, parse, dispatch to
     the procs above; on timeout print FAIL + `exit 1`.
   - `qmp_disconnect chan`.
2. **Extend `repos/sponge/src/test/sponge_de_probe/main.cc`:** in `inject=no`
   observe mode, when the observe window starts, log `QMP-TARGET click <gx>
   <gy>` with the global coordinates of the demo-window click point (window
   center: demo domain origin (192,172) + half of 640x480 → (512,412); verify
   against the domain config, not the stale (512,460) footer note). Log
   observed press coordinates in the PASS path for calibration. Default
   behavior (`inject=yes`, used by `run/sponge-de-test.run`) must be
   byte-identical.
3. **Extend `run/sponge-de-sel4-interactive.run`:** add
   `append qemu_args " -qmp tcp:127.0.0.1:${qmp_port},server=on,wait=off "`
   with `set qmp_port [expr {20000 + ([pid] % 20000)}]` (PID-derived port
   avoids collisions between sequential/parallel runs); source `qmp.inc`;
   replace gate 3 with: `qmp_connect` → bounded `qmp_exec_target` (300s —
   probe emits the marker only after its render phase) → `run_genode_until
   {.*sponge-de-probe: PASS.*} 120`. Fix the header/footer comments that claim
   a synthetic injected click (they contradict `inject="no"`); update the
   footer's "Remaining follow-up" note to point at the now-implemented
   mechanism.
4. **Calibration loop (dev-time, recorded in evidence):** first QMP run —
   compare intended vs observed click coords in the probe/sponge-de log; if
   off by more than ±8px, adjust `qmp_abs` (e.g. `/1023` vs `/1024`, rounding)
   and re-run. Final scaling formula goes in a comment in `qmp.inc` and the
   evidence log.
5. Verify `run/sponge-de-test.run` still passes (probe default unchanged).

**Files:** create `run/qmp.inc`; edit
`repos/sponge/src/test/sponge_de_probe/main.cc`; edit
`run/sponge-de-sel4-interactive.run`.

- Category: `deep`; Skills: [`debugging`] (empirical calibration loop and
  serial-log correlation is runtime debugging)
- **Depends On:** W0
- **Acceptance Criteria:** `./tool/build run sponge-de-sel4-interactive`
  (sel4/pc) prints all three gates + `Run script execution successful`, with
  the probe PASS caused by a QMP usb-tablet click (evidence log shows
  `QMP-TARGET click` → observed press at the demo window within ±8px of
  target). `run/sponge-de-test.run` still passes.

### W2: Panel interactions + click-to-launch over the real path (criteria 3, 4)

1. **Wire the launcher backend into `run/sponge-de-sel4-interactive.run`**
   (mirroring `run/sponge-launch.run:93,132-135,171` and
   `run/sponge-alpha.run:585-610`):
   - Add `sponge_pkgd` + `pkg_runtime` (binary `init`) start nodes; pkgd config
     carries `<launcher_request/>`; stage `pkg_gui_demo` metadata ROMs +
     payload as in sponge-launch.run; bump `pkg_runtime` to `caps: 2000 | ram:
     256M` (Qt6 GUI child on seL4 needs the §11.1 headroom; pkgd's generator
     already emits the 1000-cap GUI floor).
   - report_rom policies: `sponge-de -> installed` ROM ← pkgd `installed`
     broadcast; `sponge-de -> launcher_request` report → pkgd ROM;
     `pkgd -> launcher_result` report → sponge-de ROM; `pkg_runtime -> config`
     ROM ← pkgd `runtime` report; pkg metadata ROM policies (copy from
     sponge-launch.run).
   - sponge-de config gains `<launcher source="pkgd"/>` and routes `ROM label:
     installed`, `Report label: launcher_request`, `ROM label:
     launcher_result`, `Report label: launcher` to report_rom (alpha lines
     604-606 pattern).
   - QEMU `-m 1G` → `-m 2G` (second Qt6 softpipe renderer).
   - pkg_runtime Gui: `any-service → parent → any-child` resolves to nitpicker
     here (no wm in this scenario) — correct for this topology; pkg_gui_demo
     maps into the default domain.
2. **Extend `sponge_de_probe` with an ordered phase list** (observe mode only;
   config `<config inject="no" render_iters="1800"
   phases="input,panel,launch"/>`; absent `phases` = `input` only, preserving
   W1 behavior):
   - **Phase `input`**: as delivered in W1.
   - **Phase `panel`** (criterion 4): emit `QMP-TARGET click <Sx> <Sy>` at the
     launcher "S" toggle center (panel domain ypos 0, height 28 → e.g. (14,14);
     confirm against panel_widget geometry). Capture-check the launcher popup
     appeared (non-background-fraction increase in the popup rect directly
     below the panel, left edge — the "Sponge Launcher" frameless popup; rect
     determined empirically, asserted with margin). Emit a second click on the
     same point; Capture-check the popup closed (fraction returns to
     ≈background). Also covers "panel buttons / menu elements respond to
     clicks" — the S toggle is the panel's only clickable element today (clock
     is a passive QLabel by design; additional panel widgets are Phase 11
     scope — state this explicitly in docs/09 §10 notes).
   - **Phase `launch`** (criterion 3): emit click on S to open, then
     `QMP-TARGET click <entry_x> <entry_y>` on the first launcher menu entry
     (geometry from `repos/sponge/src/sponge-de/launcher/launcher_menu_view.cc`
     category-grouped QPushButtons; fixed offsets documented in the probe with
     tolerance). Then
     wait (bounded) for pkg_gui_demo's green `#00ff00` window pixel via
     Capture (same check as `launch_probe`), which can only appear if the full
     chain ran: Qt click → `LauncherController::request_launch()` →
     `launcher_request` report → pkgd `_do_launch` → pkg_runtime config
     regeneration → component boot → first paint. Log per-phase markers
     (`sponge-de-probe: phase panel PASS` etc.) and the final
     `sponge-de-probe: PASS`.
   - The run script additionally gates on pkg_gui_demo's `window shown` boot
     marker (independent corroboration, `misleading_success_output` defense)
     and pkgd's launch-ok log line.
3. All waits bounded; the QMP choreography is sequential `qmp_exec_target`
   calls matching the phase markers.
4. Regression: `run/sponge-launcher.run`, `run/sponge-launch.run` must stay
   green (no shared-code changes intended; sponge-de code untouched — wiring
   is scenario-side; pkgd untouched).

**Files:** edit `run/sponge-de-sel4-interactive.run`,
`repos/sponge/src/test/sponge_de_probe/main.cc`. No sponge-de or pkgd changes
expected; if the empirical popup geometry forces a `launcher_menu_view.cc`
tweak, keep it minimal and justify in the commit body.

- Category: `deep`; Skills: [`debugging`]
- **Depends On:** W1
- **Acceptance Criteria:** The extended scenario prints both boot gates, all
  three probe phases, pkg_gui_demo's `window shown` marker, and
  `sponge-de-probe: PASS` — with every click originating from host QMP
  `input-send-event`. `run/sponge-de-test.run`, `run/sponge-launch.run`,
  `run/sponge-launcher.run` still pass.

### W3: Window dragging over the real path (criterion 2) + pkg_runtime Gui-route fix

1. **Fix the pkg_runtime Gui route (config-only):** in `run/sponge-alpha.run`
   add `+ service Gui | + child wm` to the pkg_runtime route (before
   `any-service`), and add a layouter assign rule covering launched-package
   labels — check `genode/repos/gems/src/app/window_layouter` README for
   whether `assign` supports `label_prefix`; if yes use `+ assign |
   label_prefix: pkg_runtime | target: screen`, otherwise set a `<default>`
   target in the layouter rules. Apply the identical fix to
   `run/sponge-desktop-disk.run` if its pkg_runtime route mirrors alpha's
   (verify before editing; its system-init config lives in a staged
   `system.config`). Commit separately with the why in the body (launched GUI
   packages bypass the wm → undecorated, undraggable).
2. **Extend `repos/sponge/src/test/wm_probe/main.cc` with an observe mode**
   (`<config inject="no"/>`): skip the synthetic drag; drive pkgd's `request`
   channel to launch pkg_gui_demo (pattern exists in
   launch_probe/terminal_probe); wait for its window in the layouter
   `window_layout` ROM; compute the title-bar center from window_layout +
   `decorator_margins` (motif decorator: top=20, sides=4 — the synthetic
   probe's documented math at main.cc:40-58,92-93); log `QMP-TARGET drag <x1>
   <y1> <x2> <y2>` (+100,+100 drag); observe the window_layout ROM position
   change AND the Capture pixel check at the new location; `wm-probe: PASS`.
   Default `inject=yes` behavior unchanged (sponge-wm.run stays the synthetic
   regression).
3. **Create `run/sponge-wm-qmp.run`** (base-sel4 only, `assert {[have_spec
   sel4]}`): the `run/sponge-wm.run` topology (nitpicker + wm +
   window_layouter + decorator + sponge-de with panel→nitpicker direct / other
   Gui→wm) **plus** the drivers sub-init copied verbatim from
   `run/sponge-de-sel4-interactive.run` (vesa_fb/ps2/pc_usb_host/usb_hid/
   event_filter + chargen staging) **plus** sponge_pkgd + pkg_runtime with the
   fixed Gui→wm route + staged pkg_gui_demo **plus** wm_probe `inject="no"`.
   QEMU `-m 2G`, xhci + usb-tablet, QMP socket per qmp.inc. Gates: fb mode
   marker → usb_hid POINTER marker → bounded `qmp_exec_target` for the drag →
   `run_genode_until {.*wm-probe: PASS.*} 300`.
4. Verify the drag exercises the real chain: QMP abs motion → usb-tablet →
   usb_hid → event_filter → nitpicker pointer → decorator title-bar → layouter
   drag rule → window move. The QMP drag includes the pre-press hover jiggle
   (decorator hover seq) exactly as the synthetic probe documents.
5. Regression: `run/sponge-wm.run` (synthetic, unchanged) and
   `run/sponge-alpha.run` (route fix is inert for non-GUI hello) must stay
   green; re-run `run/sponge-desktop-disk.run` only if it was edited.

**Files:** create `run/sponge-wm-qmp.run`; edit
`repos/sponge/src/test/wm_probe/main.cc`, `run/sponge-alpha.run`, possibly
`run/sponge-desktop-disk.run`.

- Category: `deep`; Skills: [`debugging`]
- **Depends On:** W1
- **Acceptance Criteria:** `make -C genode/build/x86_64 run/sponge-wm-qmp
  KERNEL=sel4 BOARD=pc` prints `wm-probe: PASS` with the window_layout ROM
  showing the +100,+100 move after a QMP drag; `run/sponge-wm.run` and
  `run/sponge-alpha.run` still pass.

### W4: Keyboard to a focused terminal (criterion 5a)

1. **Extend `repos/sponge/src/test/terminal_probe/main.cc` with a QMP mode**
   (`<config qmp="yes"/>`): run the existing install → launch →
   broadcast/render checks unchanged; then, instead of the synthetic focus
   click + `Press_char` injection, log `QMP-TARGET click <gx> <gy>` at the
   terminal window center (focus), then `QMP-TARGET type echo ok\n`; observe
   the gems terminal read buffer / glyph-count increase via the existing
   mechanism; `terminal-probe: PASS`. Default behavior unchanged.
2. **Create `run/sponge-terminal-qmp.run`**: `run/sponge-terminal.run`
   topology + drivers sub-init + QMP socket (same staging: bash-minimal.tar,
   VeraMono.ttf, vfs_ttf). Keyboard events flow: QMP `send-key` → emulated
   PS/2 keyboard → ps2 driver → event_filter (chargen en_us) → nitpicker →
   focused terminal Gui session → gems terminal → `/dev/terminal` → noux bash
   echo → re-render. Gates: fb marker → usb_hid POINTER marker (also proves
   drivers up before ps2 input matters) → terminal probe's existing render
   marker → bounded `qmp_exec_target` (focus click) → bounded
   `qmp_exec_target` (type) → `run_genode_until {.*terminal-probe: PASS.*}
   300`. Keep the typed string within `qmp_type`'s mapped subset (`echo ok` +
   Return).
3. Regression: `run/sponge-terminal.run` unchanged and re-run.

**Files:** create `run/sponge-terminal-qmp.run`; edit
`repos/sponge/src/test/terminal_probe/main.cc`.

- Category: `unspecified-high`; Skills: []
- **Depends On:** W1
- **Acceptance Criteria:** Scenario prints `terminal-probe: PASS` with the echo
  round-trip caused solely by QMP `send-key` events; `run/sponge-terminal.run`
  still passes.

### W5: Keyboard to a focused text editor (criterion 5b)

1. **Extend `repos/sponge/src/test/textedit_probe/main.cc` with a QMP mode**
   (`<config qmp="yes"/>`): keep the existing checks (installed broadcast
   `running=no→yes`, render fraction, error paths); then log `QMP-TARGET click
   <gx> <gy>` at the document-area center of the edit domain (focus + cursor
   placement), then `QMP-TARGET type hello` (plain letters only — no
   Return/accelerators). Verify via Capture that the document region's
   rendered fraction **increases beyond the cursor-blink baseline**: sample
   the region twice before typing (baseline delta from cursor blink), sample
   after typing, require `typed_delta > 2×baseline_delta` (guards the
   `misleading_success_output` class — a blink alone must not PASS).
   `textedit-probe: PASS`. Default behavior unchanged.
2. **Create `run/sponge-textedit-qmp.run`**: `run/sponge-textedit.run`
   topology (full-screen `edit` domain at (0,0) with `focus: click`, per its
   lines 117-132) + drivers sub-init + QMP socket + staged textedit payload
   from `pkg/textedit/payload/`. Gates: fb marker → usb_hid marker → render
   marker → two bounded `qmp_exec_target` calls → `run_genode_until
   {.*textedit-probe: PASS.*} 300`.
3. Regression: `run/sponge-textedit.run` unchanged and re-run.

**Files:** create `run/sponge-textedit-qmp.run`; edit
`repos/sponge/src/test/textedit_probe/main.cc`.

- Category: `unspecified-high`; Skills: []
- **Depends On:** W1
- **Acceptance Criteria:** Scenario prints `textedit-probe: PASS` with the
  document-region delta attributable only to QMP `send-key` input;
  `run/sponge-textedit.run` still passes.

### W6: Docs sync, evidence, and full regression

1. **Docs sync (AGENTS.md §5.4 — code and docs land together):**
   - `docs/09-roadmap.md` §10: flip all five Phase-10 checkboxes with scenario
     traceability (criterion → scenario → PASS marker); add a note that panel
     interactivity covers the S toggle + launcher (the panel's only clickable
     elements; more widgets = Phase 11) and that §7 synthetic click-to-launch
     remains valid (Phase 10 strengthened the proof).
   - `run/README.md`: update the `sponge-de-sel4-interactive.run` entry (QMP
     real input + launcher + panel phases); add entries for
     `sponge-wm-qmp.run`, `sponge-terminal-qmp.run`, `sponge-textedit-qmp.run`;
     document the `run/qmp.inc` shared-helper convention; **remove the
     "Planned additions" QMP bullet** (now delivered).
   - `docs/08-development.md`: new subsection on host-driven QMP input
     (qmp.inc API, QMP-TARGET marker contract, PID-derived port, coordinate
     scaling + calibration, send-key vs input-send-event rationale, manual
     interactive-viewing escape hatch remains).
   - `docs/11-environment.md`: record QMP-over-TCP usage (no new host tools —
     Tcl `socket` builtin); add local-patch/evidence rows only if something
     was actually patched (expected: none).
   - `README.md`: update the interactive bullet (real QMP-driven input,
     dragging, keyboard).
   - `docs/evidence/`: create `phase10-index.md` (same table format as
     INDEX.md) + per-task artifacts `task-<n>-phase10-interactive.{md,log}`
     for W0–W5 + the suite results for W6. Do not reference `.omo/` paths.
2. **Full regression re-run** (sequential, clean `var/run` between runs): `sponge-de-test`,
   `sponge-wm`, `sponge-launch`, `sponge-launcher`, `sponge-terminal`,
   `sponge-textedit`, `sponge-alpha`, `sponge-de-sel4-interactive`,
   `sponge-wm-qmp`, `sponge-terminal-qmp`, `sponge-textedit-qmp`,
   `sponge-pkg-gui`, `sponge-pkg-lifecycle` (+ `sponge-desktop-disk` only if
   W3 edited it). Record results in the phase-10 evidence index; any red
   scenario is root-caused and fixed before the roadmap checkboxes land.
3. PR-body numbers per AGENTS.md §5.1 (convenience proven in code): e.g.
   "desktop fully driven by 1 command (`./tool/build run
   sponge-de-sel4-interactive`); 0 Genode concepts required of the user; every
   automation bypassable (`run_genode_until forever` + SDL viewing note)".

- Category: `writing`; Skills: []
- **Depends On:** W2, W3, W4, W5
- **Acceptance Criteria:** All listed scenarios green in the evidence index;
  roadmap §10 checkboxes flipped with traceability; no doc references to
  `.omo/`; run/README "Planned additions" QMP bullet removed.

## Commit Strategy

One logical change per commit (conventional commits). Land in dependency order:

1. `test(sponge_de_probe): emit QMP-TARGET click marker in observe mode`
2. `feat(run): add run/qmp.inc — QMP helper for host-driven guest input` (incl.
   docs/08 QMP subsection)
3. `feat(run): drive sponge-de-sel4-interactive input via QMP usb-tablet`
   (criterion 1; closes §11.1 follow-up; fixes stale injected-click comments) +
   `docs(evidence): task-1 phase10 real-input log`
4. `test(sponge_de_probe): add panel and launch observe phases`
5. `feat(run): wire pkgd launcher into interactive scenario; QMP panel +
   click-to-launch verification` (criteria 3, 4) + evidence
6. `fix(run): route pkg_runtime Gui sessions through the wm in sponge-alpha`
   (+ sponge-desktop-disk if applicable)
7. `test(wm_probe): add observe mode driven by QMP-TARGET drag markers`
8. `feat(run): add sponge-wm-qmp — real-pointer window drag on base-sel4`
   (criterion 2) + evidence
9. `test(terminal_probe): add QMP keyboard mode` + `feat(run): add
   sponge-terminal-qmp` (criterion 5a) — may be two commits
10. `test(textedit_probe): add QMP keyboard mode with blink-baseline pixel
    check` + `feat(run): add sponge-textedit-qmp` (criterion 5b) — may be two
    commits
11. `docs(roadmap): close phase 10 — checkboxes, run/README, evidence index,
    regression results`

TDD discipline per workstream: run the scenario first (red), make the minimal
change, re-run to green; every gate is a bounded PASS marker — never a silent
hang.

## Success Criteria

1. `run/sponge-de-sel4-interactive.run` passes with all input driven by host
   QMP (criteria 1, 3, 4).
2. `run/sponge-wm-qmp.run` passes — window moved +100,+100 by a real-pointer
   drag, asserted via window_layout ROM + Capture (criterion 2).
3. `run/sponge-terminal-qmp.run` and `run/sponge-textedit-qmp.run` pass — QMP
   `send-key` text reaches the focused window (criterion 5).
4. Full regression list green; roadmap §10 checkboxes flipped; docs and
   evidence synced; no `.omo/` references in durable docs.
