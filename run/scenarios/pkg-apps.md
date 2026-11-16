# Application packages

Breadcrumb: [`run/README.md`](../README.md) → Application packages

The app packages booted through `sponge_pkgd` (terminal, textedit,
files, calculator, pdf_view, falkon) and the shared
`run/terminal_runtime.inc` include. Full catalog entries follow verbatim
(cautions and limitations included); the parent index carries one-line
summaries.

## Scenarios
- `run/terminal_runtime.inc` — shared Tcl include for the three
  terminal-hosting scenarios (`sponge-terminal.run`,
  `sponge-terminal-qmp.run`, `sponge-usb-kbd-via-qmp.run`).
  Centralizes the bash + UNIX toolset vfs block
  (`server/terminal server/vfs server/cached_fs_rom` +
  `noux-pkg/bash-minimal` + `noux-pkg/vim-minimal` +
  `noux-pkg/coreutils-minimal` + `noux-pkg/grep` + `noux-pkg/sed` +
  `noux-pkg/tar` + `noux-pkg/less` + `noux-pkg/findutils` +
  `noux-pkg/diffutils` + `noux-pkg/which` + `lib/libc lib/libm
  lib/posix lib/ncurses lib/pcre` + `lib/vfs lib/vfs_pipe
  lib/vfs_ttf`), the staged boot modules (`VeraMono.ttf` +
  `pkg_terminal.xml` + `pkg_index.xml`), and the
  `pkg/terminal/metadata.xml` + on-image repo index staging
  (Phase 14 W11 paper-cut #33). Adds a new CLI tool to the
  toolset in one file instead of three; failing to update the
  include previously left a scenario with a stale toolset
  (Phase 13's `coreutils-minimal` carryover hit this exact
  drift in the W1 scenario). Each scenario sources the
  include via:
  ```
  source [file join [file dirname [file normalize [info script]]] \
                      terminal_runtime.inc]
  ```
  then uses `{*}$terminal_runtime::extra_packages` and
  `{*}$terminal_runtime::extra_libs` inside its `build { ... }`
  block, `terminal_stage_vera_metadata "bin"` for the staged
  files, and `terminal_append_boot_modules boot_modules` to
  append the staged files to its `[build_artifacts]` list.
  Errors loudly if `pkg/terminal/metadata.xml` or the
  `ttf-bitstream-vera` port is missing.
  Patterns anchor on `\r*\n` because QEMU's `-nographic` serial emits
  CR CR LF (verified). The `match_max -i $qemu_spawn_id 200000`
  raise inside `qmp_exec_target` is required under log floods (see
  the comment at the call site: the launch-phase green-pixel poll
  spams ~140 KB at 200 ms cadence; the default 2000-byte `match_max`
  window then keeps only the tail of accumulated output and the
  expect arms time out). See `docs/08-development.md` §"Host-driven
  QMP input" for the API summary + the hard-won QEMU 11 lessons.
  Mirrored at `repos/sponge/run/qmp.inc` via a committed relative
  symlink so the Genode build repo-discovery picks it up regardless
  of which directory the build was launched from.

- `sponge-terminal.run` — Phase 7 todo 13: the terminal package
  (`pkg/terminal`), source-built from in-tree components (no depot
  import). The `terminal_probe` drives `sponge_pkgd` to install and
  launch `terminal`, then verifies the running stack: (a) the
  `installed` broadcast carries `terminal` (the `vct list`-equivalent
  check), (b) the terminal window + bash prompt render on nitpicker
  (Capture glyph-pixel check), and (c) a synthetic keystroke round-trips
  (focus click + `Press_char` events via nitpicker's Event service →
  gems terminal server read buffer → `/dev/terminal` via the vfs
  `<terminal/>` plugin → noux bash echo → terminal re-render, observed
  as a glyph-count increase). The package is a nested sub-init
  (`binary: init`) hosting the gems graphical `terminal` server, a `vfs`
  (with the `<terminal/>` plugin that bridges the terminal session to
  `/dev/terminal`), a `cached_fs_rom` (serves `/bin/bash` out of
  `bash-minimal.tar`), and the noux `/bin/bash` child — the
  source-built system_shell composition. Stages `VeraMono.ttf` (the
  terminal's monospace font, via the `vfs_ttf` plugin). Kernel-agnostic
  topology (base-linux native or base-sel4 QEMU); the acceptance run
  uses `KERNEL=sel4 BOARD=pc`. Gates on `terminal-probe: PASS`.

- `sponge-textedit.run` — Phase 7 todo 14: the text-editor package
  (`pkg/textedit`), the depot-repackaged `cproc/pkg/qt6_textedit/
  2025-10-27` (todo 11). The `textedit_probe` drives `sponge_pkgd` to
  install and launch `textedit`, then verifies: (a) the `installed`
  broadcast carries `textedit` with `running="no"` (`vct list`-
  equivalent + lifecycle), (b) after launch the broadcast flips to
  `running="yes"`, (c) the Qt6 text-editor window actually renders into
  nitpicker (Capture non-background-fraction check across the domain —
  the qt6_textedit widget paints a menu bar / toolbar / rich-text area
  everywhere distinct from the `#1e1e2e` nitpicker background; the
  `misleading_success_output` class — a bare exit 0 is not enough, the
  rendered scene must really appear), and (d) pkgd's launch error paths
  return clear statuses (`not-installed`, `already-running`), never a
  crash. The prebuilt `textedit` binary is NOT a Sponge build target
  (no in-tree `src/app/qt6_textedit`); it is staged from
  `pkg/textedit/payload/` (extracted from `cproc/bin/x86_64/qt6_textedit/
  2025-10-12` by `tool/pkg_import`). The separate missing-binary
  failure channel is verified by the scratch scenario
  `run/sponge-textedit-fail.run` (evidence log retained by the Phase 7
  reviewer — see the Phase 7 evidence index).
  Kernel-agnostic topology (base-linux native or base-sel4 QEMU); the
  acceptance run uses `KERNEL=sel4 BOARD=pc`. Gates on
  `textedit-probe: PASS`.

- `sponge-files.run` — Phase 7 todo 15: the file-manager package
  (`pkg/files`), source-built from the in-tree Qt6 component
  `repos/sponge/src/sponge_files` (a single Qt6 Widgets binary, not a
  nested sub-init). The `files_probe` drives `sponge_pkgd` to install
  and launch `files`, then verifies: (a) the installed broadcast
  carries `files` (`vct list`-equivalent) AND the Qt6 file-manager
  window actually renders into nitpicker (Capture non-background-
  fraction AND color-diversity check across the 800x600 "files" domain —
  themed list + preview + accent buttons; the
  `misleading_success_output` class — bare exit 0 is not enough); (b)
  a synthetic double-click on the list's first row navigates into
  `/demo` (asserted via the component's structural `files` report path
  transitioning `/` -> `/demo` — NOT by parsing pixels); (c) copy
  `/demo/notes.txt` -> `/writable/notes.txt` then delete
  `/writable/notes.txt` driven via the request channel (the GUI is the
  manual escape hatch; the request channel is the automation default,
  AGENTS.md §3.3 rule 2); (d) attempt delete in the read-only area
  (`/demo/notes.txt`) is refused — the tar-backed `/demo` is read-only
  by nature so `unlink(2)` returns `EROFS`, and the refusal surfaces
  three ways at once (UI status label + `Genode::log` line + the
  `files` report's `last_action result="refused"`). The fixture area
  (`/demo`) is staged from `run/fixtures/files-demo/` and packed into
  `files_demo.tar` at scenario time. Kernel-agnostic topology
  (base-linux native or base-sel4 QEMU); the acceptance run uses
  `KERNEL=sel4 BOARD=pc`. Gates on `files-probe: PASS`.

- `sponge-falkon.run` — Phase 7 todo 16: the Falkon web browser package
  (`pkg/falkon`), the depot-repackaged `cproc/pkg/falkon_qt6-jemalloc/
  2026-04-22` (todo 11). Falkon is a full Qt6 WebEngine browser (~500MB
  payload, ~1G RAM) — the heaviest Alpha component. The scenario merges
  the textedit GUI topology + the net-probe networking stack (ipxe_nic +
  nic_uplink + pc_rtc + system_rtc). Because falkon's 64-file closure
  exceeds the seL4 boot chain's module-size ceiling (~256MB; Bender's
  relocation limit + seL4's untyped cnode exhaustion), the payload is
  packed into `falkon_payload.tar` and added as a SEPARATE multiboot2
  module (not inside image.elf); a `tar_rom` server reads it and serves
  individual files as ROM sessions (pkg_runtime routes falkon's ROM
  requests there). The `falkon_probe` drives `sponge_pkgd` to install
  and launch falkon, then pixel-verifies the browser window via Capture
  (rendered-fraction + color-diversity). The run script separately
  verifies the host fixture GET (falkon's config arg navigates to
  `http://10.0.2.2:8765/net-fixture.txt` on startup). **KNOWN
  LIMITATION:** the seL4 boot chain cannot currently handle falkon's
  500MB+ payload — even with the tar_rom approach, seL4 resets during
  boot module setup (untyped cnode exhaustion). See
  the Phase 7 todo-16 evidence §2.2 for the full diagnosis
  and the resolution path (disk-based payload). base-sel4 only. Bounded
  timeouts (900s probe + 180s GET check).

- `sponge-falkon-disk.run` — Phase 8 P4: Falkon FROM DISK on base-sel4
  (the ceiling-killer proof, docs/14-boot-storage-architecture.md §4.4
  key property "Falkon boots"). Applies the P2 desktop-from-disk pattern
  to falkon's 509 MiB WebEngine closure — the payload Phase 7 packaged
  (`pkg/falkon/`, 64 ROMs) but could NOT boot because the seL4 boot
  chain has a ~256 MiB boot-module ceiling (Bender relocation + seL4
  untyped cnode, docs/14 §2.1). The payload is staged under
  `/system/pkg/falkon/payload/` on the GENODE ext2 P3 and served as ROM
  sessions by a THIRD `cached_fs_rom` instance (`rom_pkg`, chrooted
  there) — NONE of it is a Tier-0 boot module, so `image.elf` stays at
  ~12 MiB (the P2 Tier-0 roster, unchanged). **This architectural claim
  is PROVEN** (`docs/evidence/p4-falkon-disk.log`): falkon's process
  starts, the dynamic linker resolves the 237 MiB
  `libQt6WebEngineCore.lib.so` from disk via rom_pkg, and lwIP DHCPs
  over the ipxe_nic+nic_uplink stack (address=10.0.2.15). The Tier-0
  `system`-child route rule `label_prefix: pkg_runtime → child rom_pkg`
  catches falkon's payload ROM requests (binary + WebEngine libs + Qt
  tars), forwarded up through the nested init, and serves them from
  disk. `sponge_pkgd` runs WITHOUT `binary_prefix` so falkon's binary
  resolves via `rom_pkg`. The scenario adds the nic stack (`ipxe_nic` +
  `nic_uplink` from todo 12 / `sponge-net-probe.run`) and `pc_rtc` +
  `system_rtc`. The desktop CHROME (sponge-de/wm/decorator/themed) is
  deliberately omitted so falkon is the ONLY Qt renderer — the
  `falkon_probe` pixel check is then unambiguous (no desktop chrome to
  false-positive on, the misleading_success_output guard); the desktop
  is separately proven in P2. **Remaining blocker (orthogonal to the
  boot-chain claim):** base-sel4 gives every child PD a FIXED 8192-slot
  capability CNode (`CSPACE_SIZE_LOG2=13`, platform_pd.cc:199-216),
  independent of the init `caps` quota (30000/80000/200000 all fail
  identically). falkon's WebEngine runtime (render buffers / shared
  memory) exceeds 8192 caps and is stopped ("out of selector" → "denied
  RM-session") after DHCP, before first paint. The fix is a vendored
  base-sel4 patch to `CSPACE_SIZE_LOG2_2ND` (forbidden by the P4
  "no vendored-tree patches" constraint — documented for P5 in the
  evidence log §11). falkon_probe pixel check therefore does not pass
  on this base-sel4 build; the bounded 900s `run_genode_until` timeout
  governs (never a silent hang). The P4 run also delivered a real
  `sponge_pkgd` fix: the generated `parent-provides` now carries `Nic` +
  `Rtc` in the canonical casing (the prior `NIC` was wrong-cased and
  `Rtc` was absent — falkon was stopped at "denied Rtc-session"). Sizing:
  `pkg/falkon/` `caps=200000` (documented as moot on seL4 due to the
  CNode, but correct for a patched/hw kernel); `falkon ram=1G`;
  `pkg_runtime caps=210000 ram=1500M`; `system caps=250000 ram=3000M`;
  `rom_pkg ram=768M`; QEMU `-m 6G`. Failure channel:
  `SPONGE_FALKON_NO_FIXTURE=1` skips the host fixture (bounded GET-check
  failure). base-sel4 only; `RUN_OPT="--include image/disk"`.

- `sponge-calculator.run` — **Phase 13 W3**: the calculator package
  (`pkg/calculator`), a source-built Qt6 app (the upstream
  `qt6_calculatorform` Designer example, built in-tree as
  `app/qt6/examples/calculatorform` from the `qt6_tools` port — no
  depot import). The `calculator_probe` drives `sponge_pkgd` to
  install and launch `calculator`, verifies the `installed` broadcast,
  pixel-verifies the rendered window via Capture (rendered-fraction +
  color-diversity), and exercises pkgd's `not-installed` /
  `already-running` error paths. Kernel-agnostic; gates on
  `calculator-probe: PASS`.

- `sponge-pdf-view.run` — **Phase 13 W4**: the PDF viewer package
  (`pkg/pdf_view`), the source-built mupdf `app/pdf_view` with a
  bundled one-page `pkg/pdf_view/payload/sample.pdf` staged as a boot
  module. `pdf_view` locates the document by `scandir("/")` for the
  first `*.pdf` name in its vfs, so the metadata's
  `<rom name="sample.pdf"/>` mount and the scenario's staged boot
  module must carry the SAME filename (docs/16 §3.2). The
  `pdf_view_probe` installs + launches via `sponge_pkgd` and
  pixel-verifies the rendered page via Capture (a rendered PDF page is
  bright-on-dark against the `#1e1e2e` background). Kernel-agnostic;
  gates on `pdf-view-probe: PASS`.

- `sponge-terminal.run` toolset extension — **Phase 13 W2**: the
  terminal package now mounts the CLI toolset tars
  (`coreutils-minimal`, `grep`, `sed`, `tar`, `less`, `findutils`,
  `diffutils`, `which`) in its vfs, and the `terminal_probe` asserts
  them end-to-end by typing `ls -l /bin` through the synthetic
  Press_char path and requiring the rendered listing's glyph jump
  (docs/plans/phase13-package-ecosystem.md D13.1/D13.2). The same fix
  repaired a latent Phase-7 bug: the metadata's `<env>` nodes used the
  `name`+`value`-attribute form, which libc's
  `populate_args_and_env` parses as the content form with empty
  content, so every variable (including `PATH`) was silently empty —
  external commands could never have run from the Alpha terminal.
  `pkg/terminal/metadata.xml` now uses the content form
  (`<env name="PATH">/bin</env>`); see docs/16 §5 pitfall 1.
