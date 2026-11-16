> Part of [09 - Roadmap](../09-roadmap.md) §4.

## 4. Phase 2: vct Minimum Working

### Goal

Inside the boot image, vct runs the following minimum commands end-to-end
against real Genode state (not just the scaffolded log banners):

- `vct --version` — prints the version.
- `vct --help` — prints the help text (concise English summary plus
  detailed English help; `--lang ko` switches to Korean when a
  translation is available).
- `vct status` — reads `init`'s state and prints a summary.
- `vct component list` — shows the component tree (read-only).

The argument parser and the basic dispatch table are already implemented
in Phase 0 (`src/vct/args.{h,cc}`, `src/vct/command_router.{h,cc}`).
Phase 2 wires those commands to real Genode sessions (`Report`,
`Rom_session`) so their output reflects live system state.

### Completion Criteria

- [x] vct boots as an `init` child in the Phase 1 boot image.
- [x] `--help` and `--version` produce the expected output when run
  inside the boot image.
- [x] `status` reads `init`'s state via a `Report` session relayed
  through `report_rom`.
- [x] `component list` shows the live component tree (read-only).
- [x] The `--json` output format is implemented for `status` and
  `component list`.
- [x] `--lang ko` switches the help summary to Korean.
- [x] Automated run scenarios cover the Phase 2 commands:
  `run/sponge-vct-help.run`, `run/sponge-vct-version.run`,
  `run/sponge-vct-status.run`, and `run/sponge-vct-component-list.run`.

### Explicitly **Excluded** From This Phase

- Package install (Phase 4).
- Configuration change (Phase 4).
- Leitzentrale integration (Phase 6).
