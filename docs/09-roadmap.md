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
Phase 4: Package management (sponge_pkgd)
   |
   v
Phase 5: Integration and automation layer
   |
   v
Phase 6: Leitzentrale integration
   |
   v
Phase 7: Alpha — first usable version
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

- [ ] `sponge_pkgd` backend component implemented.
- [ ] Package metadata format defined.
- [ ] Dependency resolution algorithm implemented.
- [ ] `vct install <pkg>` adds a component under the `init` tree.
- [ ] `--explain` previews the work.
- [ ] `--manual` runs step by step.
- [ ] Packages are read from a package repository (a local directory at
  first).

---

## 7. Phase 5: Integration and Automation Layer

### Goal

Sponge DE grows into a full desktop environment, and the fact that vct
and Sponge DE share the same backends is verified.

### Completion Criteria

- [ ] Sponge DE panel and launcher implemented.
- [ ] Configuration management through the `sponge_configd` backend.
- [ ] Theme system through the `sponge_themed` backend.
- [ ] Sponge DE and vct read and write the same configuration
  (consistency verified).
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

- [ ] Sculpt's Leitzentrale component is included in the build.
- [ ] `vct leitzentrale` starts Leitzentrale.
- [ ] Sponge DE (or a dedicated viewer) shows Leitzentrale as a window.
- [ ] Detection of Leitzentrale changes and synchronization with
  `sponge_configd`.
- [ ] Conflict-resolution UI (keep / revert / merge).

---

## 9. Phase 7: Alpha

### Goal

A minimum version that an everyday user can install and use. The stage
at which one can honestly say "I have used Sponge OS".

### Completion Criteria

- [ ] Installable media (an ISO or a disk image) is provided.
- [ ] Sponge DE is usable automatically after boot.
- [ ] A default app set (browser, terminal, text editor, file manager)
  is installable.
- [ ] vct supports every day-to-day task (install, configure, status).
- [ ] Leitzentrale integration.
- [ ] Installation documentation and quick-start guide.
- [ ] Known limitations are clearly documented.

### Explicitly **Excluded** From Alpha

- Stability guarantees (the possibility of data loss is stated).
- Backup / recovery automation.
- Wide hardware support (initially a limited hardware target).

---

## 10. Beyond Alpha (Section Headings Only)

- Beta: stability, more hardware, backup / recovery.
- Stable: long-term support, a wider app set.
- A community-contribution model is established.

These stages get fleshed out in a separate document after Alpha.

---

## 11. Current Focus

Phase 0–3 are complete. Phase 3 was closed by `run/sponge-de-test.run`:
a headless, kernel-agnostic scenario in which the `sponge_de_probe`
component verifies actual window rendering on nitpicker (Capture pixel
check) and end-to-end input delivery (Event-session injection →
sponge-de input report round-trip), logging `sponge-de-probe: PASS`.

Next up: Phase 4 (package management, `sponge_pkgd`).

Deferred follow-ups (not Phase 3 blockers):

1. **base-sel4 interactive GUI**: the headless test scenario is
   kernel-agnostic and covers Phase 3 on both kernels, but a *visible*
   desktop on base-sel4 still needs the `drivers_interactive-pc`
   driver set (vesa_fb, ps2, usb_hid, event_filter, platform, acpi,
   pci_decode) wired into a QEMU run script, with
   `-device nec-usb-xhci,id=xhci -device usb-tablet` for absolute
   pointer input. Reference: `docs/11-environment.md` and the vendored
   `genode/repos/os/recipes/raw/drivers_interactive-pc/` config set.
2. **Theme parser wiring**: `theme/theme_loader.{h,cc}` and
   `theme/theme_qt.h` are now exercised by sponge-de at runtime
   (default.theme ROM → panel/window styling), completing the Phase 5
   skeleton integration for the system theme layer.

Theme format spec + default theme + parser are done (partial Phase 5
progress, see `docs/10-theme-format.md`).