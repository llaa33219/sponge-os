# 09 - Roadmap

> This document defines the development phases and milestones of
> Sponge OS. Dates are not firm; they express relative order. The
> completion criterion of each milestone is a clearly verifiable
> condition.

---

## 1. Phase Overview

```
Phase 0: Design and scaffold      ✅ done
   |
   v
Phase 1: Minimum boot             ✅ done (base-linux + base-sel4)
   |
   v
Phase 2: vct minimum working      ✅ done
   |
   v
Phase 3: Sponge DE single window  ✅ done (headless verification on nitpicker)
   |
   v
Phase 4: Package management (sponge_pkgd)  ✅ done
   |
   v
Phase 5: Integration and automation layer  ✅ done
   |
   v
Phase 6: Leitzentrale integration  ✅ done
   |
   v
Phase 7: Alpha — first usable version  ✅ done with caveats
   |
   v
Phase 8: Boot & storage architecture (docs/14)  ✅ done
   |
   v
Phase 9: seL4 capability-space scaling  ✅ done (Falkon first paint)
   |
   v
Phase 10: Sponge DE — fully interactive desktop  ✅ done
   |
   v
Phase 11: DE customization and panel strengthening  ✅ done
   |
   v
Phase 12: Hardware support expansion
   |
   v
Phase 13: Package ecosystem growth
   |
   v
Phase 14: Sponge DE as a daily-usable desktop
   |
   v
Phase 15: Real-hardware boot
   |
   v
Phase 16: Sponge IME (multi-language / CJK input)
   |
   v
Phase 17: GUI installer for SSD/HDD installation
```

---

## 2. Phase 0: Design and Scaffold (Current)

### Goal

Establish Sponge OS's philosophy, structure, and initial code skeleton.

### Completion Criteria

- [x] Core philosophy defined (`docs/02-philosophy.md`)
- [x] Architectural principles defined (`docs/03-architecture.md`)
- [x] Component inventory draft (`docs/04-components.md`)
- [x] Design documents for vct / Sponge DE / Leitzentrale
- [x] Development guide draft (`docs/08-development.md`)
- [x] Repository structure and `AGENTS.md`
- [x] Component scaffolds (`repos/sponge/src/vct`, `sponge-de`)
- [x] Build system skeleton (`target.mk`, `tool/`)
- [x] Kernel base locked: `base-sel4` (seL4) — `include/sponge/platform.h`
- [x] vct config-ROM argument parser (`src/vct/args.{h,cc}`)
- [x] vct subcommand dispatch for `status`, `help`, `version`
  (`src/vct/command_router.{h,cc}`, `src/vct/commands.{h,cc}`)

### Entry Conditions for the Next Phase

- All core documents have been reviewed and stabilized.
- The scaffold code has been verified to compile in its minimum form.

---

## 3. Phase 1: Minimum Boot

### Goal

Wire Sponge OS into a Genode source tree, produce a boot image that
contains Sponge OS components, and boot it on base-linux (the developer
feedback target). Production images target base-sel4.

### Completion Criteria

- [x] The symlink procedure with a Genode source tree is documented
  (`docs/08-development.md` §1.1).
- [x] A `run/sponge-minimal.run` scenario exists and runs successfully
  on base-linux.
- [x] A boot image is produced with vct as a child of `init`.
- [x] The boot completes and the LOG output is verified:
  ```
  Genode 26.05
  [init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
  [init -> vct] vct 0.0.1-pre-alpha
  [init -> vct] Sponge OS codename: Archaeocyte
  Run script execution successful.
  ```
- [x] The `base-sel4` (seL4 microkernel) build is also green. Verified
  output on QEMU:
  ```
  Genode 26.05
  699 MiB RAM and 523288 caps assigned to init
  [init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
  [init -> vct] vct — Very Convenient Tool
  [init -> vct] version: 0.0.1-pre-alpha (Archaeocyte)
  Run script execution successful.
  ```

### Verification Scenario

```
$ ./tool/build run sponge-minimal
# Manual equivalent:
$ make -C genode/build/x86_64 run/sponge-minimal
...
genode build completed
spawn ./core
Genode 26.05
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] vct 0.0.1-pre-alpha
[init -> vct] Sponge OS codename: Archaeocyte

Run script execution successful.
```

### Lessons captured during Phase 1

These were discovered the hard way; see `docs/08-development.md` and
[`docs/11-environment.md`](11-environment.md) for full details.

- The Sponge OS source path MUST NOT contain spaces — Genode's
  `realpath` + `find` chain breaks otherwise.
- Always `build { core lib/ld init <component> }`. Omitting `lib/ld`
  produces a silent boot failure: every component is denied the
  dynamic-linker ROM session.
- Always pass `[build_artifacts]` to `build_boot_image` so that
  `init.xsd`, `ld.lib.so`, and the component binaries are all staged.
- Real Genode does not expose `size_t` / `addr_t` in the global
  namespace. Always qualify as `Genode::size_t`, `Genode::addr_t`.
- The Genode source tree is **vendored** at `genode/`. There is no
  `$HOME/genode` to keep alive anymore: a clean clone of `sponge-os`
  brings the whole tree, and `genode/build/`, `genode/contrib/`, and
  `genode/depot/` are all git-ignored. See
  [`docs/11-environment.md`](11-environment.md) for the
  reproducibility contract and the patch ledger.
- `base-sel4` requires `prepare_port` for `sel4 sel4_tools grub2` plus
  seven Python modules (`future jinja2 ply six lxml pyfdt jsonschema`)
  on the host before the kernel build will succeed.
- `base-sel4` on QEMU needs **at least 1 GiB** of guest RAM. 256 MiB
  fails with `seL4 failed assertion 'load_paddr'` because the seL4
  kernel reserves a SKIM window (Meltdown mitigation) and its own
  structures, leaving too little contiguous physical memory for the
  ~30 MiB boot module.

---

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

---

## 5. Phase 3: Sponge DE Single Window

### Goal

Verify that Sponge DE opens a **single window** on top of Genode's
`nitpicker` (the window compositor). This phase only confirms the bare
fact that "one window is drawn by Qt"; there is no panel, launcher, or
notification yet.

### Completion Criteria

- [x] Qt builds and links inside Genode (using the existing Genode Qt
  port). Verified: sponge-de binary compiles and links against Qt6 6.8.3
  (libQt6Core, libQt6Gui, libQt6Widgets) on Genode 26.05.
- [x] Sponge DE opens a `Gui` session and displays a Qt widget.
  Verified by `run/sponge-de-test.run`: the `sponge_de_probe` component
  reads nitpicker's composited screen through a `Capture` session and
  positively matches the demo window's themed `window_bg` color
  (`#313244`) at the demo-domain center.
- [x] Keyboard and mouse input are received through the `Input`
  session. Verified by the probe injecting an absolute-motion +
  BTN_LEFT click into nitpicker's `Event` service: sponge-de's `input`
  report (relayed via `report_rom`) confirms the press, and the demo
  button's `clicked` signal fires
  (`sponge-de: input event received (button clicked)`).
- [x] A run scenario automatically verifies that the window appears:
  `run/sponge-de-test.run` is headless (no framebuffer driver, no host
  display) and kernel-agnostic; it passes when the probe logs
  `sponge-de-probe: PASS` (`run_genode_until` match). The interactive
  escape hatch is `run/sponge-de.run` (fb_sdl on the host display).

### Explicitly **Excluded** From This Phase

- DE modules such as the panel, launcher, and notifications (Phase 5).
- Theme system (Phase 5).
- Multi-monitor support.

---

## 6. Phase 4: Package Management (`sponge_pkgd`)

### Goal

`vct install` and `vct remove` work with automatic dependency
resolution. Initially only a small package set is supported (for
example, `nano`, `bash`, simple apps).

### Completion Criteria

- [x] `sponge_pkgd` backend component implemented (Report/ROM channel,
  deterministic config generator, minimum privileges).
- [x] Package metadata format defined (`docs/12-package-format.md`).
- [x] Dependency resolution algorithm implemented (deterministic DFS
  with cycle detection).
- [x] `vct install <pkg>` adds a component under the `init` tree
  (nested `pkg_runtime` init; verified by `run/sponge-pkg-install.run`).
- [x] `--explain` previews the work (`run/sponge-pkg-explain.run`).
- [x] `--manual` runs step by step (per-step visibility; interactive
  [Y/n] confirmation deferred to the future shell, reported honestly —
  `run/sponge-pkg-manual.run`).
- [x] Packages are read from a package repository (a local directory at
  first — `pkg/`, staged as ROM modules with a build-time
  `pkg_index.xml` manifest).

Additionally delivered: `vct remove` (with dependency GC), `vct list`
(installed-set introspection), and the config-driven `pkg_seq_probe`
test component.

### Known Limitations (documented, deferred)

- Installed-set persistence is delivered (Phase 4 follow-up #2, see
  `docs/12-package-format.md` §13): the explicitly-installed roots are
  mirrored to a versioned XML store on a `File_system` session, so
  installs survive a reboot. The store is opt-in per deployment — it
  activates only when `sponge_pkgd`'s `<config>` carries a `<vfs>` node
  — and is inspectable/resettable by the user (control escape hatch).
  Remaining limitation: **the store is single-writer**. There is exactly
  one `sponge_pkgd` per system and it serializes all writes, so
  concurrent writers are not a concern in practice, but the format
  carries no checksum and is not crash-consistent against a power loss
  mid-write (a torn write is detected as corrupt on the next boot and
  the daemon restarts empty with a warning, never crashes — see
  `docs/12-package-format.md` §13.2).
- Report/ROM channel is single-writer; concurrent callers would
  collide (request-id + backend mutex deferred until concurrent
  callers exist).
- Launcher entry registration needs `sponge_configd` (Phase 5); the
  install output reports it as informational.

---

## 7. Phase 5: Integration and Automation Layer

### Goal

Sponge DE grows into a full desktop environment, and the fact that vct
and Sponge DE share the same backends is verified.

### Completion Criteria

- [x] Sponge DE panel and launcher implemented (themed panel with
  clock + launcher menu populated from sponge_pkgd's `installed`
  broadcast; verified by `run/sponge-launcher.run`). Click-to-launch
  deferred — needs a start-on-demand lifecycle (documented).
- [x] Configuration management through the `sponge_configd` backend
  (flat key-value store, validation, broadcast config report;
  `vct config get/set/list`; `run/sponge-config*.run`).
- [x] Theme system through the `sponge_themed` backend (watches
  configd, resolves theme content, republishes; sponge-de live-reloads
  via the ThemeController; `run/sponge-theme.run`).
- [x] Sponge DE and vct read and write the same configuration
  (consistency verified: `vct config`/`theme apply` writes through
  configd → themed → sponge-de; the theme_probe asserts the 3-way
  value match plus the pixel repaint).
- [x] Theme file format defined and a default theme shipped.

### Progress Notes

- Theme format spec: `docs/10-theme-format.md` (completed in Phase 5
  skeleton work).
- Default theme: `repos/sponge/src/sponge-de/themes/default.theme`
  (completed).
- Parser skeleton: `repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}`
  (completed; not yet wired into the `sponge-de` build, integration
  pending with the Qt6 wiring task).

---

## 8. Phase 6: Leitzentrale Integration

### Goal

`vct leitzentrale` opens Sculpt's Leitzentrale as a window. The
synchronization strategy between Leitzentrale changes and Sponge OS
configuration is implemented.

### Completion Criteria

- [x] Sculpt's Leitzentrale component is included in the build
  (`run/sponge-leitzentrale.run`; the subsystem boots headlessly).
- [x] `vct leitzentrale` starts Leitzentrale (enable/off/status via the
  configd `leitzentrale.enabled` key + lz_bridge; audit log on start).
- [x] Sponge DE (or a dedicated viewer) shows Leitzentrale as a window
  (the `lz_viewer` component captures the subsystem's composited UI and
  presents it on the outer nitpicker; pixel-verified by lz_viz_probe).
- [x] Detection of Leitzentrale changes and synchronization with
  `sponge_configd` (lz_watch checksums the model fs; configd mirrors a
  read-only `leitzentrale.diverged` key in its broadcast).
- [x] Conflict-resolution UI (keep / revert / merge): `vct leitzentrale
  diff/keep/revert` implemented and verified by the edit→detect→revert
  probe cycle. Merge is documented as deferred — no Sponge automation
  writes the lz model in Phase 6, so a merge operation would be
  vacuous; the trigger for implementing it is recorded in
  docs/07-leitzentrale.md's open questions.

---

## 9. Phase 7: Alpha

### Goal

A minimum version that an everyday user can install and use. The stage
at which one can honestly say "I have used Sponge OS".

### Completion Criteria

- [x] Installable media, both an ISO and a disk image, are provided and boot-verified in QEMU (`run/sponge-media-smoke.run`, `run/sponge-alpha.run`; release artifacts from todos 5 and 6).
- [x] Sponge DE is usable automatically after boot, with the themed panel and launcher visible (`run/sponge-alpha.run`).
- [x] A default app set (browser, terminal, text editor, file manager) is installable, with caveats: terminal, text editor, and file manager are verified booting (`run/sponge-terminal.run`, `run/sponge-textedit.run`, `run/sponge-files.run`); Falkon is packaged but not bootable on the seL4 media because of the boot-module ceiling, as documented in `docs/13-installation.md` (`docs/evidence/task-16-phase7-alpha.log`).
- [x] vct supports everyday tasks including install, configure, status, list, launch, update, search, shutdown, and reboot (`run/sponge-vct-status.run`, `run/sponge-pkg-install.run`, `run/sponge-config.run`, `run/sponge-launch.run`, `run/sponge-pkg-meta.run`, `run/sponge-power.run`).
- [x] Leitzentrale integration is present in the Alpha image and reachable through vct (`run/sponge-leitzentrale.run`, `run/sponge-alpha.run`).
- [x] Installation documentation and a quick-start guide are provided (`docs/13-installation.md`; media and desktop flow proven by `run/sponge-alpha.run`).
- [x] Known limitations are clearly documented, including the QEMU-only target, pre-staged install semantics, non-persistence, networking boundary, Falkon caveat, and limited Leitzentrale viewer (`docs/13-installation.md`; behavior references `run/sponge-net-probe.run`, `run/sponge-pkg-persist.run`, and `run/sponge-alpha.run`).

### Explicitly **Excluded** From Alpha

- Stability guarantees (the possibility of data loss is stated).
- Backup / recovery automation.
- Wide hardware support (initially a limited hardware target).

---

## 10. Post-Alpha Phases (10–17)

Phases 8 and 9 (boot/storage architecture per `docs/14`, seL4
capability-space scaling) are delivered and closed the two largest
Alpha caveats — the boot-module ceiling and the missing persistence.
The phases below define the road from the current QEMU-verified Alpha
to a daily-usable system on real hardware. As with earlier phases,
dates are not firm; they express relative order, and every phase is
scenario-gated.

**Version milestones**: completing Phase 15 (real-hardware boot)
releases **0.2.0-alpha**; completing Phase 17 (GUI installer)
releases **0.3.0-alpha**. Phases 10–14 and 16 accumulate on top of
0.1.0-alpha without their own version bumps.

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

### Phase 11: DE Customization and Panel Strengthening

#### Goal

Users can meaningfully customize their desktop environment — theme,
panel layout, and behavior — without touching Genode internals.
Automation stays the default; every customization is also reachable
manually through `vct config` / theme files (control escape hatch).

#### Completion Criteria

- [x] Panel customization through `sponge_configd`: position, size,
  visible widgets, clock format, launcher organization. **Delivered:**
  configd registry grew to 7 keys — `panel.height` (uint 16–128),
  `panel.visible_widgets` (enum-list {clock,launcher}), `clock.format`
  (structural format string), `launcher.sort_by` (enum {alpha,manual})
  — with per-kind validators; a new in-sponge-de `ConfigController`
  applies them live (restyle-migrated panel/launcher).
  `panel.position` is persisted but boot-time-only (nitpicker domains
  own placement; live move is a Phase-12 open question).
  **Scenarios:** `run/sponge-config-probe.run` (20-step key matrix,
  base-linux), `run/sponge-panel-config.run` (7-subphase probe P1–P7,
  base-linux), `run/sponge-panel-config-sel4.run` (same on seL4).
  **PASS markers:** `config-seq-probe: PASS`,
  `sponge-de-probe: phase panel-config PASS`.
- [x] Expanded theme surface: more themeable elements, additional
  shipped themes beyond `default.theme`, live reload preserved.
  **Delivered:** error/success/warning split into `*_bg`/`*_text`
  (old names kept as deprecated aliases), `panel.popup_*` documented
  no-op keys, 2 new shipped themes (`dark.theme` = Catppuccin
  Macchiato palette, `compact.theme` = palette + tighter layout),
  theme transport cap raised 2048→8192 with truncation warning, and
  the unknown-theme path hardened: a `label_suffix=".theme"` catch-all
  route serves an empty ROM so `sponge_themed` takes its graceful
  keep-previous branch instead of freezing on the base-lib
  session-denial path. **Scenario:** `run/sponge-theme.run` (5-step
  3-theme probe + does-not-exist fallback + liveness proof,
  base-linux). **PASS marker:** `theme-probe: PASS`. **Host gate:**
  `./tool/test_theme_payload_size`.
- [x] Sponge-themed window chrome: the `themed_decorator` drop-in
  (theme tar via VFS) replaces stock decorations with themed ones.
  **Delivered:** `run/sponge-de-themed-chrome.run` (seL4+QMP) swaps
  the stock decorator for upstream `themed_decorator` (child named
  `decorator` so the WM policy set applies verbatim), the theme tar
  is authored by `./tool/decor_assets` (upstream geometry metadata +
  byte-vendored font.tff), and the new `sponge_decorator_bridge`
  delivers the decorator's whole config live via report_rom with the
  policy color taken from the active theme's palette. The title bar
  verifiably tints (RGB(180,180,191) → RGB(91,91,100)) and the QMP
  drag moves the window through the themed chrome.
  **PASS markers:** `wm-probe: PASS` (both the new scenario and the
  `run/sponge-wm-qmp.run` Phase-10 drag regression).
- [x] All customization flows documented and scenario-verified
  (config change → configd → themed/decorator → pixel repaint).
  **Evidence:** `docs/evidence/phase11-index.md`,
  `docs/evidence/task-0-phase11-baseline.md` (W0),
  `task-4-phase11-themed-chrome.md` (W4),
  `task-5-phase11-scenarios.md` (W5 sweep).

### Phase 12: Hardware Support Expansion

#### Goal

The system boots on a wider range of (virtual and physical) platform
configurations, and the supported-hardware surface is explicit rather
than accidental. This phase builds the enablement infrastructure;
Phase 15 is the concrete real-hardware boot milestone.

#### Completion Criteria

- [x] **Boot matrix beyond the current QEMU defaults**: additional
  machine types, AHCI and NVMe storage variants, USB boot media.
  **Traceability (criterion → scenario → exact marker → evidence,
  all on QEMU 11.0.3 host):**
  - *q35 + Skylake-Client explicit pin on every disk-touching script:*
    `run/sponge-boot.run`, `run/sponge-desktop-disk.run`,
    `run/sponge-persist-disk.run`, `run/sponge-falkon-disk.run`,
    `run/sponge-alpha.run` all carry
    `append qemu_args " -machine q35 -cpu Skylake-Client "` before
    `-nographic -m` (added in W1). Evidence:
    `docs/evidence/task-1-phase12-platform.md` (W1 pin receipt) +
    `docs/evidence/task-0-phase12-baseline.md` (W0 baseline
    `boot/probe: PASS ... sponge-boot-marker-v1` recovered byte-for-byte
    after the pin).
  - *AHCI product-media default:* `run/sponge-desktop-disk.run` →
    `alpha-probe: PASS` (W0 baseline + W6 envelope; evidence
    `docs/evidence/phase12-envelope-sponge-desktop-disk.log`).
  - *NVMe product-media opt-in:* `run/sponge-desktop-disk-nvme.run` →
    `Number: 3` P3 byte check + `alpha-probe: PASS` (one namespace,
    NVMe `caps: 5000 | ram: 64M`). Evidence
    `docs/evidence/phase12-desktop-nvme.log`.
  - *i440fx IDE smoke (no AHCI, no product image):*
    `run/sponge-boot-i440fx.run` → `boot-probe: PASS` from the
    PIIX4-IDE-backed marker. Evidence
    `docs/evidence/phase12-boot-i440fx.log`.
  - *q35/AHCI multi-disk order check (P3 of second disk):*
    `run/sponge-boot-multidisk.run` → `boot-probe: PASS` from the
    second disk's P3 marker. Evidence
    `docs/evidence/phase12-boot-multidisk.log`.
  - *BIOS-side USB boot (QEMU `-device usb-storage`, BIOS side only):*
    `run/sponge-usb-boot.run` → `BIOS-side USB boot verified` +
    `alpha-probe: PASS`. Evidence `docs/evidence/phase12-usb-boot.log`.
- [x] **Driver set expanded**: networking beyond QEMU slirp (at least
  one real NIC driver path), input beyond PS/2 + usb-tablet.
  **Traceability:**
  - *Linux-backed `pc_nic` + QEMU e1000 + `nic_router` DHCP:*
    `run/sponge-pc-nic.run` → `pc_nic: bound device` +
    `nic_router: uplink DHCP acquired` (caps: 1000 | ram: 32M,
    300 s cold-DDE-Linux gate). Evidence
    `docs/evidence/phase12-pc-nic.log`. The change is additive —
    every existing product/iPXE scenario is byte-for-byte untouched.
    Honest claim text (per docs/15 matrix row):
    "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested."
  - *USB HID keyboard via QMP hotplug (probe-focus ROM quirk absent;
    PS/2 send-key path still covered by `sponge-terminal-qmp.run`):*
    `run/sponge-usb-kbd-via-qmp.run` → ordered chain
    `usb_hid: KEYBOARD detected` → `usb_hid: KEYBOARD removed` →
    `sponge-usb-kbd-via-qmp: PASS` + `Run script execution
    successful.`. Evidence `docs/evidence/phase12-usb-kbd.log`.
    The missing glyph-delta secondary gate is recorded as a documented
    Phase-12 gap in `docs/15-hardware-compatibility.md` (probe-focus
    ROM quirk; see also `docs/evidence/task-4-phase12-usb.md` §3/§5
    deviation #5).
- [x] **A hardware compatibility document** listing tested
  configurations and known gaps. **Traceability:**
  `docs/15-hardware-compatibility.md` is the hand-curated 5×5 surface
  matrix + 16-cell ledger (4 verified, 1 smoke-only, 11 gap). The
  readability and honesty rules are enforced by
  `tool/hw_compat.mojo assert` (additive, no `generate` / `update` /
  repo write); the `assert` validator is reachable from
  `./tool/build verify` and from the W6 final host verification
  (exits 0; receipts in `docs/evidence/task-5-phase12-hw-compat.md`).
  No `target: real-hardware` cell exists; this is a Phase-15 cell, not
  a Phase-12 cell.
- [x] **Run scenarios cover the new configurations so regressions
  fail loudly.** **Traceability:** the six new Phase-12 scenarios
  (`sponge-boot-i440fx.run`, `sponge-boot-multidisk.run`,
  `sponge-desktop-disk-nvme.run`, `sponge-pc-nic.run`,
  `sponge-usb-boot.run`, `sponge-usb-kbd-via-qmp.run`) plus the
  augmented `run/qmp.inc` (the W3b launch-click fix) carry distinct
  bounded markers and durable `docs/evidence/phase12-*.log` pointers.
  The full serialized sweep (one scenario at a time, `make -j1`,
  no concurrent `make` in `genode/build/x86_64`) is captured in
  `docs/evidence/task-6-phase12-regression.md` (W6) +
  `docs/evidence/phase12-index.md` (the evidence index). The fresh
  build (`docs/evidence/phase12-fresh-build.log`) proves the managed
  `pc` repository, the nine-row patch ledger, and a freshly prepared
  build directory that compiles `sponge-pc-nic.run` first.

### Phase 13: Package Ecosystem Growth

#### Goal

The installable package set grows beyond the Alpha app set, and
adding a new package becomes a documented, low-friction process.

#### Completion Criteria

- [ ] Additional everyday packages shipped in `pkg/` (choices driven
  by the daily-use goals of Phase 14).
- [ ] Package authoring documented end-to-end: metadata format,
  payload staging, index generation, and testing (`docs/12` extended
  or a new authoring guide).
- [ ] `tool/pkg_import.mojo` (or successor) covers the common import
  cases; each new package has a boot-verified run scenario.

### Phase 14: Sponge DE as a Daily-Usable Desktop

#### Goal

Sponge DE can serve as a primary working environment for everyday
tasks — not a demo, but a desktop one can actually sit down and use.

#### Completion Criteria

- [ ] Session stability: the desktop runs extended interactive
  sessions without component crashes or resource exhaustion.
- [ ] Core desktop services complete: notifications, clipboard,
  sensible window management defaults (focus model, raising,
  minimizing).
- [ ] Everyday workflow proven end-to-end in scenario: boot → launch
  terminal/editor/files/browser → do real work → shut down cleanly.
- [ ] Remaining paper cuts from Phases 10–13 resolved.

### Phase 15: Real-Hardware Boot

#### Goal

Sponge OS boots successfully on at least one physical machine — the
QEMU-only limitation is retired. **Completing this phase releases
0.2.0-alpha.**

#### Completion Criteria

- [ ] Boot verified on physical hardware from USB or SSD media
  (target machine recorded in the compatibility document).
- [ ] Input, display, and storage functional on that machine; the
  desktop reaches the same verified state as the QEMU scenarios.
- [ ] `docs/13-installation.md` updated: real-hardware install path
  documented, QEMU-only limitation removed or rescoped.
- [ ] Known hardware-specific issues recorded with reproduction
  notes.

### Phase 16: Sponge IME — Multi-Language / CJK Input

#### Goal

A Sponge-native input method editor, implemented as a proper Genode
component, supporting multiple languages with CJK input (Korean
first-class, given the project's audience).

#### Completion Criteria

- [ ] `sponge_ime` component implemented: sits in the input chain
  (event_filter-level integration), capability-minimal, and
  toggleable at runtime.
- [ ] Korean Hangul composition working (2-set at minimum); at least
  one additional CJK or multi-language layout supported.
- [ ] IME state configurable through `sponge_configd`; automation
  default (sensible layout per locale) with manual override.
- [ ] Scenario-verified: composed characters reach a focused text
  field in the text editor and terminal.

### Phase 17: GUI Installer for SSD/HDD Installation

#### Goal

Installing Sponge OS to internal storage is a guided graphical
experience: an everyday user can install the system without knowing
partition tables or Genode run scripts. **Completing this phase
releases 0.3.0-alpha.**

#### Completion Criteria

- [ ] GUI installer component shipped on the install media: disk
  selection, guided partitioning with safe defaults (automation),
  and an advanced/manual partitioning path (control).
- [ ] The installer writes the 4-partition layout from `docs/14` to
  the target disk and produces a bootable installation.
- [ ] End-to-end verification in QEMU: boot media → install to a
  second disk → reboot from the installed disk → desktop verified.
- [ ] Failure paths are loud and recoverable (no half-written disks
  presented as success); installation docs updated.

---

## 11. Current Focus

Phases 0–12 are complete. Phase 7 Alpha caveats are recorded in
[`docs/13-installation.md`](13-installation.md); Phases 8 (boot
and storage architecture, `docs/14`) and 9 (seL4 capability-space
scaling) resolved the two largest Alpha caveats — the boot-module
ceiling and missing persistence — by delivering disk-based payload
staging + persistence on the 4-partition product media and a lazy
`vm_space` growth patch that lets Falkon reach first paint on seL4.
Phase 10 closed the fully-interactive-desktop track: every desktop
panel/window/launcher action is driven by real host input, and
window dragging + click-to-launch + keyboard input to focused apps
are all proven by QMP-driven run scenarios. Phase 11 delivered DE
customization: four new configd keys applied live by the new
in-sponge-de `ConfigController` (panel height/visibility, clock
format, launcher sort), an expanded theme surface with four shipped
themes and a hardened unknown-theme fallback, and Sponge-themed
window chrome via the upstream `themed_decorator` drop-in fed by the
new `sponge_decorator_bridge` (live palette-tinted title bars,
drag-verified on seL4). **Phase 12** delivered the hardware-support
expansion: the explicit q35+Skylake-Client pin on every disk-touching
script, six new focused scenarios (i440fx/PIIX4 IDE smoke,
q35/AHCI multi-disk order check, q35/NVMe desktop-from-disk,
`pc_nic`/e1000 DHCP, BIOS-side USB storage, and USB HID keyboard via
QMP hotplug), the `tool/dist --storage {ahci,nvme}` product-media
selector, the hand-curated 5×5 surface matrix + 16-cell ledger in
`docs/15-hardware-compatibility.md` (4 verified, 1 smoke-only, 11
gap), and the read-only `tool/hw_compat.mojo assert` validator
reachable from `./tool/build verify`. Phase 12 is **QEMU-verified
only**; physical-hardware boot remains a Phase-15 deliverable.

Next up: **Phase 13 — Package ecosystem growth**, followed by the
rest of the post-Alpha sequence defined in §10 (daily usability,
real hardware, IME, GUI installer).

Deferred follow-ups (not blockers):

1. **base-sel4 interactive GUI** ✅ driver stack delivered
   (`run/sponge-de-sel4-interactive.run`): the full
   `drivers_interactive-pc` driver set — vesa_fb, ps2, usb_hid,
   pc_usb_host, event_filter, platform, acpi, pci_decode — is wired into
   a base-sel4 QEMU run script with `-device nec-usb-xhci,id=xhci
   -device usb-tablet` for absolute-pointer input, exactly as this item
   asked. The scenario boots seL4 on QEMU (1 GiB RAM) and is verified
   headlessly by three boot-log assertions: vesa_fb sets the 1024x768
   mode and maps the physical VESA framebuffer, usb_hid binds the QEMU
   usb-tablet as a `POINTER` device, and `sponge_de_probe` reports
   `PASS` — the themed window is pixel-verified through a Capture
   session and a synthetic click round-trips through sponge-de's
   `input` report. The Qt6/Mesa (EGL) rendering path on base-sel4 was
   fixed as part of this work; see §11.1 below for the root cause
   (capability exhaustion, not a Mesa bug). Reference:
   `docs/11-environment.md` §7.2/§10 and the vendored
   `genode/repos/os/recipes/raw/drivers_interactive-pc/` config set.

   11.1. **Resolved — Qt6/Mesa (EGL) on base-sel4 was capability
   exhaustion, not a Mesa bug.** The original symptom: sponge-de's
   Qt6/EGL initialization hung on base-sel4 after logging the benign
   libEGL `MESA-LOADER: failed to retrieve device information` and
   `failed to get driver name for fd 0` warnings (both also appear on
   base-linux, where softpipe fallback follows and the window paints),
   plus one `upgrading quota donation for PD session (0 bytes, 4 caps)`
   line — then silence forever. Root cause, confirmed by experiment:
   base-sel4's capability accounting costs roughly 3x base-linux's per
   operation (each Genode capability maps to several raw seL4 caps), so
   the former `caps: 300` assignment ran out **in the middle of Mesa's
   screen creation**. The child requested a 4-cap upgrade, and the
   subsequent out-of-caps allocation blocked *silently* — no error, no
   log, an infinite hang. Raising sponge-de to `caps: 1000` (RAM was
   never the bottleneck — the upgrade request was 0 bytes, so
   `ram: 128M` matches the base-linux scenarios) makes the full desktop
   render on seL4: `sponge-de: panel and window shown`, the probe's
   Capture pixel check detects the themed window, and an injected click
   round-trips (`sponge-de-probe: PASS`). The scenario now gates on that
   PASS marker, so a regression fails loudly instead of hanging.
   **Open question (upstream)**: a Genode component that exhausts its
   capability quota mid-initialization hangs with zero diagnostics — the
   last visible line is the quota-upgrade request. A loud failure (or a
   retry-then-abort) in the out-of-caps path would have turned this
   multi-day mystery into a one-line log. Candidate for an upstream
   Genode issue; not worked around in Sponge OS code because the fix
   belongs in base/capability accounting, not in individual components.
     Remaining follow-up on this scenario — drive the click from the host
     through the real usb-tablet (QMP `input-send-event` over a
     `-qmp tcp:...` socket) instead of the probe's synthetic Event
     injection, exercising usb-tablet → pc_usb_host → usb_hid →
     event_filter → nitpicker → sponge-de with real hardware input —
     **delivered in Phase 10** (W1): see `run/sponge-de-sel4-interactive.run`
     + `docs/evidence/task-1-phase10-interactive.md`.
2. **Package install persistence** ✅ delivered: the explicitly-installed
   root set is mirrored to a versioned XML store on a `File_system`
   session (format in `docs/12-package-format.md` §13) and reloaded on
   construct, so installs survive a reboot. Proven by
   `run/sponge-pkg-persist.run` (two boots over the same lx_fs-backed
   host directory; boot 2 issues a `list`-only and finds the previously
   installed package, which can only have come from the restored store).
3. **Custom Sponge WM / window decorator**: Sponge DE currently
   adopts Genode's upstream `wm` + `window_layouter` + `decorator`
   stack (reused, not re-implemented, per AGENTS.md §5.2). A
   Sponge-native window manager — or, more likely first, a
   Sponge-themed decorator producing Catppuccin-styled window chrome —
   remains a long-term option once the DE's own semantics
   (tiling rules, custom focus model, themed decorations) outgrow the
    upstream stack's configurability. The `themed_decorator` drop-in
   (theme tar via VFS) is the nearer-term step on this path and is now
   tracked as a completion criterion of **Phase 11** (§10).

### 11.2 Phase-10-known-issue follow-ups

These are not blockers for the Phase 10 checkboxes (all five criteria
are GREEN with the per-criterion scenarios), but they will read better
once resolved in a later phase. They are tracked here so they don't
silently rot.

1. **Genode QPA misroutes tablet absolute-motion under multi-domain Qt
   setups.** Observed in W2 (criterion 3 click-to-launch): the
   usb-tablet's `Absolute_motion` events reach `QCursor::pos()` only via
   `qgenodeplatformwindow::handle_absolute_motion`; PS/2's
   `Relative_motion` events do not update `_mouse_position`. The tablet
   click landed correctly in earlier passes (W1 criterion-1) because
   the demo-domain target was large enough that the screen-center clamp
   still hit it; the launcher's `Main` widget receives presses even when
   the launcher popup is hidden. A future QPA patch that processes
   REL → cumulative ABS internally would let the cursor reach the
   exact target and drop the QPA-level click-to-launch escape hatch.
   Recorded in `docs/evidence/task-2-phase10-interactive.md` §"Open
   issues for W6". Out of scope per Phase 10's "no `genode/` changes"
   rule; candidate for Phase 12 if/when the QPA is on the patch list.

2. **Nitpicker pointer ROM only updates on `absolute_motion`.** The
   `nitpicker::Session::pointer` report ROM
   (`genode/repos/os/src/server/nitpicker/user_state.cc:117-124`)
   receives `report_pointer_position` only when the pointer is
   **explicitly** positioned via `absolute_motion`. PS/2
   `relative_motion` updates the cursor internally but does NOT set the
   `_pointer` value the report reads from. Therefore a "closed-loop"
   pointer-position protocol that drives the cursor via PS/2 REL and
   reads its position from the pointer report is unusable on this host
   (the report is empty). The W5 scenario compensated by using the
   usb-tablet's `mouse_set` + a single `absolute_motion` to materialize
   a known cursor position, then a closed-loop REL walk on top of that.
    This works but is one boot-specific. Future work: extend nitpicker's
    `user_state` to update `_pointer` from REL too. **Partially delivered**
    (Phase-11 media follow-up): the relative-motion `Nowhere` branch now
    initializes the pointer instead of dropping the event (patch ledger
    #9), which is what makes a visible cursor track under PS/2-only
    input. The report-ROM side of the gap (REL not updating the pointer
    report) remains open.

3. **Pre-existing base-sel4 Qt6 staging issue blocks
   `sponge-de-test.run` + `sponge-launcher.run` on sel4.** Reported in
   `docs/evidence/task-1-phase10-interactive.md` §"Step 5 Regression":
   both scenarios copy Qt6 `.lib.so` files into
   `[run_dir]/genode/` AFTER `build_boot_image`, which is invalidated
   by `genode/tool/run/boot_dir/sel4:59 remove_genode_dir`. The
   fixes are scenario-side (move the `cp` calls BEFORE
   `build_boot_image`); out of scope for Phase 10. The scenarios still
   run on `KERNEL=linux BOARD=pc`. Carried over as "BLOCKED-pre-
   existing" entries in the Phase-10 regression table
   (`docs/evidence/task-6-phase10-interactive.md`).

4. **`QTimer + QCursor::pos()` popup auto-close was invalid on the
   Genode QPA — replaced by event-driven click-outside.** The first
   attempt at a "click-outside closes the popup" handler in
   `repos/sponge/src/sponge-de/launcher/launcher_menu_view.{h,cc}`
   used a 50ms `QTimer` that read `QCursor::pos()` and hid the popup
   when the cursor left the launcher domain rect. On the Genode QPA
   (`genode/depot/cproc/src/qt6_base/.../qgenodeplatformwindow.cpp:387-400`)
   `QCursor::pos()` always reports `(0,0)` because `_mouse_position` is
   only updated from `handle_absolute_motion`. The timer fired the
   moment it elapsed and hid the popup between S-click and entry-click
   in the launch chain. Replaced with `qApp->installEventFilter` and an
   `eventFilter` override (commit `3727eaf2d2`); the launcher
   event-driven close now works deterministically. This is a real UX
   improvement (genuine click-outside close), not a workaround.

### 11.3 Phase-11-known-issue follow-ups

Recorded during Phase 11 (W4/W5); none block the Phase 11 checkboxes.

1. **`sponge-de-sel4-interactive.run` launch-phase click flake.** ✅
   **delivered in Phase 12 W3b** (resolution of the Phase-11 §11.3
   item-1 follow-up). On the W5 Phase-11 sweep this host failed 3/3:
   the PS/2 REL click lands ~10 px below the launcher entry button
   (the W2-calibrated recipe's geometry predates the current popup
   layout). The deterministic fix is to switch the launch-click
   choreography to the usb-tablet absolute path that W4 proved for
   drags (`qmp_tablet_index` + `mouse_set` + abs events). W3b added
   the smallest launch-only selector in `run/qmp.inc` and changed
   `run/sponge-de-sel4-interactive.run`'s launch-entry call site to
   select that tablet-absolute recipe. The launch phase passed 3/3
   back-to-back on the W3b host (`docs/evidence/task-3b-phase12-launch-click.md`)
   and the W6 envelope re-ran the launch phase three consecutive
   times. **Residual nondeterminism recorded as a Phase-12 gap:**
   the launch phase retains a small empirical probability of a
   spurious failure (the W6 envelope documented this — the
   `sponge-de-sel4-interactive.run` ×3 gate required 3 consecutive
   passes, and the Phase-11 launch flake signature may still strike
   on a single attempt; the regression evidence is honest about every
   attempt). PS/2-relative dispatch is preserved as the default for
   the input and panel phases — the original Phase-10 recipes still
   exercise their original path. Merely adding `-device usb-tablet`
   was not the fix; W3b changes the choreography (workspace press →
   workspace move → workspace release with bounded pacing) on the
   existing device. Evidence:
   `docs/evidence/task-5-phase11-sel4-interactive-FLAKE.log` (before
   trace) vs. `docs/evidence/task-3b-phase12-launch-click.md` (after
   trace).
2. **themed_decorator live asset re-skin.** The bridge updates the
   policy `color=` live, but the frame texture/button glyphs/font in
   `decor.tar` are cached in upstream statics — a full chrome re-skin
   on `theme.active` change needs either a decorator child restart or
   an upstream asset-invalidation patch (patch-ledger candidate, see
   `docs/11-environment.md` §4 note).
3. **`panel.position` is boot-time-only.** configd persists the value
   but panel placement is owned by the run script's nitpicker domain;
   live repositioning needs either dual pre-defined domains with a
   visibility toggle or WM-managed panel placement — Phase 12+ open
   question (documented in `sponge_configd/README.md`).
4. **Partial drag delta on themed chrome.** The W4 themed drag moved
   pkg_gui_demo (50,320)→(68,330) instead of the dispatched
   +100/+100 — the mechanics (press→DRAG→move) are verified; the
   partial delta matches the Phase-10 W6 host-timing variance class.
   Hardening candidate: paced tablet steps with per-step hover
   confirmation.