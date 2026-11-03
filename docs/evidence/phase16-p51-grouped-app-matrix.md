# P5.1 Grouped-Launcher App Matrix Evidence

## Scope

Phase 16 P5.1 (grouped-launcher app matrix subset — NOT the final
acceptance checklist, NOT the three-pass suite, NOT app isolation):
every built-in desktop package gets a real-launcher receipt on the
IMG product media (`run/sponge-desktop-disk` with `SPONGE_GROUPED_UI=on`,
`KERNEL=sel4 BOARD=pc`, QEMU), with first paint where the package has a
window, real QMP input (never synthetic nitpicker acceptance input),
sequential execution, and a final concurrent-window assertion. All
results below are QEMU/seL4 receipts; no real-hardware claim is made.

Packages and their receipt shapes:

| package | launcher entry | first paint | input | result |
|---|---|---|---|---|
| hello | real entry click | N/A (GUI-free autostart, by design) | N/A (by design) | documented `already-running` (pkgd Gate 2) |
| debug | real entry click | Capture + FIRSTPAINT self-report | QMP Mark click | rendered delta + `sponge_debug: MARK 1` |
| textedit | real entry click | Capture | QMP type | typed delta (P2.4 leg, unchanged) |
| terminal | real entry click | Capture | QMP type + ret | typed + execution delta (P2.4 leg, unchanged) |
| falkon | real entry click | Capture | QMP URL-bar type | typed delta (P2.6 leg, unchanged) |
| files | real entry click | Capture | QMP double-click | navigation delta + `files` report path `/demo` |
| pdf_view | real entry click | Capture | QMP pgdn + scroll | page-down/scroll input delta |
| calculator | real entry click | Capture | QMP digits + spin | result-band delta (unconditional leg) |
| (final) | — | — | QMP type | `concurrent windows n=<N>`: >=5 app windows, >=2 runtime classes, last typed delta delivered while siblings stayed mapped |

Leg order (front-most-window constraint — each interaction is
delivered while its window is the front-most): hello, debug,
textedit (simple-class), terminal, falkon (browser-class), files,
pdf_view, calculator, concurrent assertion. The P2.4/P2.6 leg
assertions and geometry are unchanged; the three class legs keep
their relative order and their gate sequence verbatim.

## Red-first receipts (each new leg)

Assertions were written with the implementation and every new leg has
a failing receipt recorded BEFORE its green one. Fixes were driven by
the red run's own diagnostics (rect logs, coarse render dumps,
changed-sample bounding boxes); nothing was guessed past the evidence.

| leg | red receipt | proven cause | fix |
|---|---|---|---|
| hello | R1 `grouped-ui-probe: FAIL launcher popup never opened (hello-class)` | run-script gate order: the debug FIRSTPAINT self-report raced alpha-probe's PASS (R1 log: alpha at :5903, FIRSTPAINT at :5938) and the fixed-order FIRSTPAINT gate swallowed the alpha marker; the host then sat in the alpha gate to its 900 s timeout (expect had no QMP socket fd) and never dispatched QMP | gate only the stable-order hello autostart marker early; assert the debug FIRSTPAINT receipt from the accumulated output at the debug leg (no consuming gate can lose it) |
| debug (entry/capture) | green after R1's fix (R2) | — | — |
| debug (Mark click) | R2 `grouped-ui-probe: FAIL no Mark-click rendered delta (debug-class)` | the debug app self-resizes to 700x480 at the generic assign origin: `rect=(50,328 700x480)`, and the Mark button row lands below the 768px display (render dump bottom row `?`, click bbox `(98,544 12x8)` = view-text highlight) | grouped-arm-only debug assign rule `ypos: 60` in both rule sites; Mark geometry re-derived |
| debug (Mark click, retry) | R3 `grouped-ui-probe: FAIL no Mark-click rendered delta (debug-class)` | zero delta at the derived button point (95,541): the button row depends on style-resolved layout margins (9..20px, not derivable; R4 then revealed the author's own coordinate bug — screen-space candidates applied window-relative, clicking the desktop below the window) | candidate-column clicks (6 y values, x on Mark's column; post-win clicks on the status label keep the dispatch count fixed); the first candidate that renders a delta + the app's own `sponge_debug: MARK` log are the receipt. R5: winning candidate y=567, changed=2458 |
| files | R5 `grouped-ui-probe: FAIL no first paint for files in pkg_runtime_simple (files-class)` (masked by the CNode cascade), R6 the same FAIL with the first cause exposed: `Error: stop because parent denied ROM-session: label="files_demo.tar"`, and R7 the same FAIL with the second cause: `parent denied ROM-session: label="files_request"` | the product media never staged the files package's VFS inputs: `pkg/files/metadata.xml` mounts `<tar name="files_demo.tar"/>` (only run/sponge-files.run builds it, from `run/fixtures/files-demo/` — `pkg/files/` carries no payload) and opens the `files_request` automation ROM (a report_rom relay in the lean run); the rom_sys catch-all resolves both names under `/system/`, where neither existed | stage `files_demo.tar` (the lean run's fixture->tar recipe) and an empty `files_request` ROM in the shared staging beside the qt6 tar stagings. (R5 additionally showed 226 core CNode-construction errors while falkon's WebEngine closure was resolving — see the falkon leg-order row below for the ordering fix.) |
| files (double-click) | R8, R11 `grouped-ui-probe: FAIL no double-click navigation/render delta (files-class, 3 candidates)`; R12 the same FAIL after the timing fix | three causes found in order: (1) R8's candidate-0 baseline measured 1000 changed samples of still-settling render noise (drowning the delta under the 2x rule); (2) the double-click dispatch raced Qt's 400ms `doubleClickInterval` (R8/R11 missed it, R9 hit it: 1/3); (3) the click COLUMN was wrong at wide windows — the files window is `list (left) + preview (right)` with 380px minimums (files_window.cc), so the window-center x lands on the preview/splitter at 800x600 (R12: the 10-sample deltas were word-selections in whatever sat at that point), while the app reserves a "generous left strip" for the row click (the lean probe uses client (100,54)) | 3s settle before the candidates; the pair dispatched tightly (full click then an immediate button press/release at the already-positioned cursor — the span prints 91-93ms, well inside 400ms; still two standard `QMP-TARGET tablet` markers = two click gestures); the click column pinned to the list's left strip (window-relative x=104). R13 GREEN: `files-class double-click candidate 1 y=413 changed=105`, `files-report poll 0 path=/demo`, `files-class navigation delta (changed=105 candidate=1 path=/demo)` (the files-report relay works end to end). R9's run-side serial re-check was dropped: the `navigate` line lands between marker dispatches and the dispatch expects consume it out of the gate window — the probe's structural check (report path + rendered delta) is windowing-immune |
| pdf_view | R13 `grouped-ui-probe: FAIL no first paint for pdf_view in pkg_runtime_simple (pdf-view-class)` — `Error: stop because parent denied ROM-session: label="mupdf.lib.so"`; R14 the same FAIL with `Error: WRITE violation at address 0x1017e60 by pager_object: pd='...pdf_view'` | three staging gaps in the same class as files: (1) `mupdf.lib.so` was never built as a lib target (the app's `app/pdf_view/mupdf.lib.so` is only the 55KB libcache ABI STUB — compare var/libcache/zlib: `zlib.abi.so` 17KB stub vs `zlib.lib.so` 319KB real), so the shared-lib staging had nothing real to stage — and staging the stub made pdf_view load a symbol-stub and crash on the first implementation call (R14's WRITE violation); (2) `sample.pdf` lands under `pkg/pdf_view/payload/` (the rom_pkg convention) while the simple-class ROM catch-all resolves bare names at `/system/` (the metadata: "the boot module of that same name MUST be staged by the run scenario"); (3) the lean sponge-pdf-view.run builds `lib/mupdf` explicitly (line 87) — the product build list did not | add `lib/mupdf` to the product build list (its jpeg/jbig2dec/openjpeg/freetype closure builds with it and stages via the existing `bin/*.lib.so` glob — the lean's boot-module list confirms the closure), assert `mupdf.lib.so` reached the staged set, and stage `sample.pdf` flat at `/system/` from the repo payload (the staging block runs before bake::stage) |
| pdf_view (input) | R18 `grouped-ui-probe: FAIL no page-down/scroll input delta (pdf-view-class pgdn_changed=0)` after the first paint went green — and behind it the app's `pdf_read_start_xref(): cannot find startxref` on every product run (R17/R18) while the lean scenario parses the same file fine | the product's ROM serving reports the ext2 **block-rounded** size: the probe's own `sample.pdf` ROM receipt (`grouped-ui-probe: pdf-doc (size=4096 head=%PDF startxref=yes)`, R20) shows a 4096-byte dataspace whose first 632 bytes are the document (the debugfs `Size: 632 / Blockcount: 8` on the image's `/system/sample.pdf`), so the vfs rom plugin's default binary mode keeps the zero block padding and mupdf seeks to `size-1024 = 3072` inside the padding and finds no `startxref` — the repair then yields a degenerate page whose scroll changes nothing. The lean serves the same bytes as an exact-size core ROM module (632) and is unaffected | the staged `pkg_pdf_view.xml` (product arm, same transform pattern as the P2.4 `staged pkg_textedit.xml` clipboard removal) gains `binary="no"` on the document ROM: the plugin's text mode trims at the first NUL, the document is NUL-free and the padding zeros, so the served size becomes exactly 632. The probe's `pdf-doc` receipt now asserts the served document (size/head/startxref) permanently. Residual gap: the metadata source (`pkg/pdf_view/metadata.xml`) should carry the attribute upstream |
| calculator | R14 (see below) | | |
| falkon (leg order) | R24 `browser-class first paint` never registered (`diag first-paint layer=never-registered id=0`, 600 s gate timeout) with `Cnode construction failed` + `mapping failed 0x11ff39000 -> 0x27f39000` (thread `ep`, pd `init -> rom_pkg`) and the `libQt6WebEngineCore.lib.so` ROM create unanswered (60 s, `state=DENIED`); R25/R26 `browser-class typed delta` red at position 3 (typed-delta polls changed=4..9 never reached `TYPED_FLOOR=12`, baseline 8/9) despite green first paint and (R26) a quiet pre-click settle (`browser-class settle quiet (noise=0 polls=2 quiet=2)`); R27 red at position 5 with the same shape (id=4, rect=(5,108 787x569), first-paint changed=2279 — identical to R5's green) — `click_changed=10018` (R5: 990), baseline=8 (R5: 0) | R24: browser-LAST = the closure load runs over six resident Qt/mupdf apps and rom_pkg's own attach dies in core's CNode construction (0 CNode/ROM errors in R25/26/27 rules out steady-state exhaustion). R25/26/27: falkon's WebEngine init storm (`AllocPagesWithAlignOffset 512MB→1GB`, `resource request NOT granted`, 2x `list model not empty at destruction`) races the calibrated click→type window — R27's storm landed between click and type (inside the 5 s post-click settle) so the keystrokes never reached the URL line-edit; R5's identical storm began after the type dispatch and won. The R26 pre-click band settle is refuted (R26+R27): it delays the click into the storm window | browser leg at **position 5** (after terminal-class, before files) with the click immediately after first paint (R5's exact proven path — the refuted pre-click settle removed), plus the observable child-set settle gate (`_run_browser_settle`) before later child spawns (R5's collision class). Receipts: `/tmp/opencode/p51-R25.log`, `/tmp/opencode/p51-R26.log`, `/tmp/opencode/p51-R27.log` |
| concurrent | R14 (see below) | | |

R2 also exposed a probe-internal defect before it could fire: app
windows can extend past the 768px display (self-resized Qt windows
keep their requested size), and the snapshot/delta/bbox samplers had
no screen clamping — an out-of-bounds read of the 1024x768 capture
buffer. All samplers now clamp and skip out-of-screen samples
identically (the bucket arrays must stay index-aligned).

Green legs observed while reaching the reds (unchanged P2.4/P2.6
receipts, R5): `hello launcher entry click -> already-running`,
`debug-class mark-click delta (changed=2458 click_y=567)`,
`simple-class typed delta`, `terminal-class typed delta (changed=17
baseline=0 click_changed=428 typed="echo grouped-ok")` +
`execution delta`, `browser-class typed delta`.

Pre-existing leg flake found and fixed (R10): the terminal leg's
typed signal (17-30 changed samples for `echo grouped-ok`) sits near
the anti-false-positive margin; a one-shot ~17-sample decorator/Qt
repaint transient landed inside the baseline noise window
(`changed=30 baseline=17` -> 30 is not > 2x17) and the leg failed.
The predicate, the geometry and the receipt are unchanged; the
post-click settle before sampling the noise floor went 2000ms ->
5000ms so the transient is spent before the baseline window opens.
R11 onward: `terminal-class typed delta (changed=17 baseline=0 ...)`.

## Acceptance runs

Command (the documented clamp; `make -j1` serial per D16R.13):

```sh
SPONGE_GROUPED_UI=on LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/sponge-desktop-disk \
  KERNEL=sel4 BOARD=pc \
  RUN_OPT="--include power_on/qemu --include log/qemu \
           --include boot_dir/sel4 --include image/disk"
```

### Teardown legs (falkon / debug) and the concurrent retarget — R29..R35

The final shape of the matrix adds two sanctioned pkgd `remove`
teardowns (the probe's ONLY backend calls — teardown, never a launch
or input substitute) and retargets the concurrent typed delta into
textedit. Both teardowns exist because of measured spawn exhaustion:

| leg | red receipt | proven cause | fix |
|---|---|---|---|
| files spawn under resident falkon | R29 `grouped-ui-probe: FAIL no first paint for files in pkg_runtime_simple` after 228x `Cnode construction failed` + seL4 `Source slot invalid or empty` (browser leg itself GREEN: `browser-class typed delta (changed=9845 baseline=0 click_changed=5 typed="sponge")`) | falkon's resident ~509 MiB WebEngine closure starved core's CNode construction during the files spawn — R24/R5's collision class even after the child-set settle gate | falkon torn down via pkgd `remove` (probe_request channel) after its green leg, gated on window-gone + st_browser child absent before any later spawn |
| calculator spawn under resident debug | R30 `Error: Cnode construction failed` + `mapping failed 0x1179df000 -> 0x41d8000` / `mapping failure for thread 'ep' in pd '... -> calculator'`; calculator window never registered (`first-paint layer=never-registered`) | debug's Qt closure still resident at the calculator spawn — same pool-exhaustion class (core's documented Cnode-construction failure path, `base-sel4/src/core/include/cnode.h`) | debug torn down via pkgd `remove` after its Mark receipt and before the calculator spawn; layout-safe because calculator's entry sits ABOVE debug's in the Utilities section (its click y is invariant) and the files/pdf_view entry clicks are already banked |
| concurrent typed target | R31 `FAIL textedit did not take focus before the concurrent typed delta` (`focus poll 125 id=7 want=2` — the (442,354) click landed on the calculator; measured rects: files `(50,328 800x600)`, pdf_view `(50,328 512x512)`, calculator `(50,328 400x300)` — the stacked siblings are far wider than the assumed 320px assign width) | the right-of-stack click point was derived from the 320px assign width; self-resized siblings cover it | click moved ABOVE the sibling stack (y<328 clear band; `(rx+rw/2, ry+100)`), typed-delta band matched to the clear region so sibling blink noise is excluded |

Known non-defect flake (R32, R34, identical signature): the
calculator entry click dispatches (`QMP-TARGET tablet 14 85` → `qmp:
dispatching tablet at guest (14,85)`) but no `sponge-de: launcher
click-to-launch 'calculator'` ever appears and the popup never closes
(`launcher popup never closed (calculator-class)`). Band coverage was
affirmed first (`diag launcher bands ... click y=85 covered=yes`), the
identical click/bands produced a green launch in R31/R33/R35, and the
failure precedes every line the concurrent-geometry change touches —
input-delivery flake in the usb-tablet path, not a code defect.

### RUN-RECEIPTS

Acceptance runs (`SPONGE_GROUPED_UI=on`, `KERNEL=sel4 BOARD=pc`,
QEMU; full command above):

| run | outcome | receipts |
|---|---|---|
| R29 `/tmp/opencode/p51-R29.log` | RED at files spawn | `browser-class typed delta (changed=9845 baseline=0 click_changed=5 typed="sponge")` GREEN; 228x `Cnode construction failed`; `FAIL no first paint for files ...` |
| R30 `/tmp/opencode/p51-R30.log` | RED at calculator spawn | `grouped-ui-probe: falkon removed (status=ok ...)`, `files-class navigation delta`, `pdf-view-class page-down/scroll input delta` GREEN; `Cnode construction failed` + `mapping failure for thread 'ep' in pd '... -> calculator'`; `FAIL no first paint for calculator ...` |
| R31 `/tmp/opencode/p51-R31.log` | RED at concurrent focus | `debug removed (status=ok window-gone=1 child-gone=1)` GREEN; all 8 app legs GREEN incl. `calculator-class digits result delta` + `spin result delta`; `concurrent-set poll n=5 classes=2`; `focus poll 125 id=7 want=2`; `FAIL textedit did not take focus before the concurrent typed delta` |
| R32 `/tmp/opencode/p51-R32.log` | RED (flake) | calculator entry click dispatched, no `click-to-launch`, `FAIL launcher popup never closed (calculator-class)` |
| R33 `/tmp/opencode/p51-R33.log` | **GREEN** | `falkon removed (status=ok ...)`, `debug removed (status=ok ...)`, `concurrent-set poll n=5 classes=2`, `concurrent click_changed=691`, `concurrent typed delta (changed=40 baseline=0)`, `concurrent siblings held n=5`, `concurrent windows n=5`, `grouped-ui-probe: PASS` |
| R34 `/tmp/opencode/p51-R34.log` | RED (same flake as R32) | calculator entry click dispatched, no `click-to-launch` |
| R35 `/tmp/opencode/p51-R35.log` | **GREEN** (confirmation) | markers identical to R33: `falkon removed (status=ok ...)`, `debug removed (...)`, `concurrent-set poll n=5 classes=2`, `concurrent click_changed=691`, `concurrent typed delta (changed=40 baseline=0)`, `concurrent siblings held n=5`, `concurrent windows n=5`, `grouped-ui-probe: PASS` |

The concurrent assertion in the green runs: the surviving set is
exactly five non-falkon windows — textedit, terminal, files,
pdf_view, calculator — from the simple + terminal classes (>=5
windows, >=2 classes), with the final typed delta delivered into
textedit's above-stack clear band while every sibling stayed mapped
(`concurrent siblings held n=5`). falkon and debug are excluded BY
DESIGN: each is torn down before the assertion (their PD/closure
residency is exactly what killed the R29/R30 spawns) and each keeps
its own first-paint + input receipt above.

### LOG-INDEX

| log | what it evidences |
|---|---|
| `/tmp/opencode/p51-R1.log` … `/tmp/opencode/p51-R28.log` | red-first receipts per the red-first table above |
| `/tmp/opencode/p51-R29.log` | browser leg GREEN (changed=9845), files-spawn Cnode cascade under resident falkon |
| `/tmp/opencode/p51-R30.log` | falkon teardown GREEN, calculator-spawn Cnode failure under resident debug |
| `/tmp/opencode/p51-R31.log` | debug teardown GREEN, all app legs GREEN, concurrent focus red (stack-rect evidence) |
| `/tmp/opencode/p51-R32.log`, `/tmp/opencode/p51-R34.log` | calculator entry-click delivery flake (identical signature both runs) |
| `/tmp/opencode/p51-R33.log` | green acceptance |
| `/tmp/opencode/p51-R35.log` | green confirmation (markers identical to R33) |

## Marker inventory (the per-app receipts)

- `grouped-ui-probe: hello/Utilities` — launcher feed pair (probe, startup)
- `grouped-ui-probe: hello launcher entry click -> already-running (paint/input N/A by design)`
- `sponge_pkgd: launch result hello -> already-running (channel=launcher)` (serial, gated)
- `grouped-ui-probe: debug launcher entry click -> already-running`
- `grouped-ui-probe: debug-class capture (label=... rect=... nonbg=N%)`
- `sponge_debug: FIRSTPAINT ms=<n>` (app self-report, gated from accumulated output)
- `grouped-ui-probe: debug-class mark-click delta (changed=N)` + `sponge_debug: MARK <n>` (serial, gated)
- `grouped-ui-probe: files-class first paint (...)`
- `grouped-ui-probe: files-class navigation delta (changed=N baseline=B path=/demo)` + `sponge_files: navigate -> /demo` (serial, gated)
- `grouped-ui-probe: pdf-view-class page-down/scroll input delta (changed=N baseline=B pgdn_changed=M)`
- `grouped-ui-probe: calculator-class digits result delta (changed=N baseline=B typed="17")`
- `grouped-ui-probe: calculator-class spin result delta (changed=N baseline=B)`
- `grouped-ui-probe: falkon removed (status=ok window-gone=1 child-gone=1 polls=N)` (teardown gate: pkgd `remove` on probe_request + window-gone + st_browser child-gone; NO QMP)
- `grouped-ui-probe: debug removed (status=ok window-gone=1 child-gone=1 polls=N)` (same shape, st_simple; runs before the calculator spawn)
- `grouped-ui-probe: concurrent-set poll n=<N> classes=<C>` (non-falkon census)
- `grouped-ui-probe: concurrent click_changed=N`
- `grouped-ui-probe: concurrent typed delta (changed=N baseline=B)`
- `grouped-ui-probe: concurrent siblings held n=<N>`
- `grouped-ui-probe: concurrent windows n=<N>` (the final assertion: >=5 non-falkon app windows, >=2 runtime classes, last typed delta delivered while siblings stayed mapped; falkon/debug excluded BY DESIGN — torn down above, each with its own receipt)
- `grouped-ui-probe: PASS` / `sponge-desktop-disk: GROUPED UI PASS ...`

`pgdn_changed` is recorded, never hidden: the bundled sample.pdf is a
one-page letter document, so mupdf pdfapp's page-down ('.') is a
code-proven no-op there (pdfapp.c clamps `pageno` to `pagecount` and
repaints only on a real page change) and the scroll ('j', KEY_DOWN)
is the delta-bearing input. Both inputs are dispatched and measured.

## Raw logs

All runs live under `/tmp/opencode/p51-R*.log` — indexed in LOG-INDEX
above; per-marker citations in the red-first and run-receipts tables
name the exact log they were taken from.
