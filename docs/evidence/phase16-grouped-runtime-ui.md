# P2.4 Grouped Runtime UI Evidence

## Scope

Phase 16 P2.4 requires the grouped simple and terminal runtimes to launch
from the real Sponge DE launcher, reach first paint, and accept real input
with a measurable rendered change. Browser/Falkon acceptance remains P2.6
scope. All results below are from QEMU on `KERNEL=sel4 BOARD=pc`; no
real-hardware claim is made.

## Acceptance runs

| run | result | evidence |
|---|---|---|
| Grouped BIOS, first | `grouped-ui-probe: PASS` and `Run script execution successful.` | textedit first paint at `(50,108 512x493)`, typed delta `133`; terminal first paint at `(50,328 320x240)`, typed delta `17` with baseline `0`, execution delta `37` |
| Grouped BIOS, second (consecutive) | `grouped-ui-probe: PASS` and `Run script execution successful.` | textedit typed delta `133`; terminal typed delta `17` with baseline `0`, execution delta `37` |
| Default BIOS regression | `alpha-probe: PASS` and `Run script execution successful.` | default grouped-off path remains healthy with the launcher at layer 2 and `fbcon=off` |

Command used for the grouped runs:

```sh
SPONGE_GROUPED_UI=on LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/sponge-desktop-disk \
  KERNEL=sel4 BOARD=pc \
  RUN_OPT="--include power_on/qemu --include log/qemu \
           --include boot_dir/sel4 --include image/disk"
```

The default regression used the same command without
`SPONGE_GROUPED_UI`. The preload is a host-only QEMU workaround and is not
a repository dependency.

Raw tool logs (local paths):

- first grouped pass: `/home/luke/.local/share/opencode/tool-output/tool_18ee9c081001UQ274jsIUHCnVl`
- second grouped pass: `/home/luke/.local/share/opencode/tool-output/tool_18eecac0a001Ec6yuMWcepZP8O`
- default regression: `/home/luke/.local/share/opencode/tool-output/tool_18d8a9f4f0010X8NJUXQuQaZs3`

## Defects fixed to reach acceptance

- **Shared-library contamination.** Textedit's 18 prebuilt payload
  libraries were initially copied over `/system/lib`, replacing the fresh
  Qt/libc libraries used by `sponge-de` and `debug` and causing duplicate
  Timer-session failures. Staging now reuses every existing shared library
  and adds only missing destination names. Only
  `libQt6PrintSupport.lib.so` was missing. Reverting the run-script delta
  and then applying the additive-only rule each restored
  `alpha-probe: PASS`.
- **Clipboard configuration.** `clipboard="yes"` had to be removed from
  both the payload `textedit.config` and staged `pkg_textedit.xml`.
  `sponge_pkgd` emits the package metadata's root config, so transforming
  only the payload file left the fatal `/clipboard` ROM request active.
- **Launcher geometry and focus.** Launcher repopulation now hides old
  widgets before `deleteLater()`, fixes rows at 50px, and preserves the
  focused window's front-most assignment. Newly appearing windows receive
  a focus request.
- **Textedit placement.** The layouter label includes the document title.
  A stable `pkg_runtime_simple -> textedit` prefix rule places its 512x493
  frame at `y=80` (outer `y=108`), keeping the whole window on-screen.
  The rule is replicated across all five desktop media scenarios.
- **Terminal runtime staging.** The BIOS scenario stages
  `terminal`/`vfs`/`cached_fs_rom` and removes the unavailable vct bridge
  from the nested terminal VFS config.
- **QMP transport.** The type payload capture is bounded at CR/LF, so
  multi-line input is not consumed into subsequent serial records.
- **Host marker loss.** Grouped gate patterns no longer end in greedy
  `.*`. A QMP-TARGET line co-buffered after a gate line therefore remains
  available for the next `qmp_exec_target` instead of being eaten. This
  fixed intermittent missing focus clicks and missed final PASS markers.
- **Popup-state checks.** Popup close is detected by disappearance of the
  launcher band at x=14 rather than by comparison with a changing desktop
  baseline. Rendered-band appearance is awaited before entry clicks.
- **Input-signal robustness.** The terminal uses `echo grouped-ok` to keep
  real input signal above the unchanged `changed > 2 * baseline`
  anti-false-positive rule. Textedit uses the standalone recipe's bounded
  post-click and cursor-baseline windows.
- **Kernel framebuffer console.** The seL4 early fb console mirrored debug
  glyphs over the composited desktop. All five media scenarios now default
  to `fbcon=off`, with `SPONGE_FBCON=on` as the diagnostic escape hatch and
  no change to serial logging. BIOS applies the token before `run_image`
  assembles the disk, not after the GRUB file has already been packaged.
- **Terminal execution evidence.** Return is dispatched only after the
  typed-input gate. A separate lower-window execution check takes its
  baseline before Return and requires rendered output below the input row,
  so a partial command cannot satisfy acceptance or be captured as success.
  The typed-input delta itself uses a terminal-only `ry+28..48` text band,
  excluding decorator and neighboring-window repaint noise; the
  `changed > 2 * baseline` predicate is unchanged.

## Visual QA

The approved capture set is
`docs/evidence/phase16-grouped-runtime-ui/launcher.png`,
`docs/evidence/phase16-grouped-runtime-ui/textedit.png`, and
`docs/evidence/phase16-grouped-runtime-ui/terminal.png` (all 1024x768 RGB),
produced by `tool_18d79ffc2001sht4W0dWJSGN9P`. The launcher is above textedit, running
bullets are single U+2022 codepoints, textedit is readable, and the terminal
shows the complete `echo grouped-ok`, `grouped-ok` output, and a fresh
prompt. The old glyph bands are gone after the fbcon fix. Independent visual
fidelity and design-system/functional integrity reviews both returned
PASS/HIGH, with only a non-blocking note about long title-bar truncation.

`launcher_result` is updated before validity/matching (the log says
`sponge-de: launched ...` with no timeout warning), and the debug ROM dump
is bounded at the first NUL instead of decoding the whole padded 4096-byte
buffer. The launcher uses layer 2, in front of the default application
layer 3.

## Remaining boundaries

- These are QEMU/seL4 receipts, not hardware certification.
- Browser/Falkon launch is not asserted until P2.6.
- One uninstrumented layer-2 grouped run lost the simple launcher entry
  click and later entered supervisor restart fallout. Two subsequent
  uninstrumented layer-2 runs passed without a source change. The isolated
  event remains unclassified; no retry wrapper or weakened check hides it.
