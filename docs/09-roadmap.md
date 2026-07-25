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
Phase 7: Alpha — first usable version  🟡 current
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

Phase 0–6 are complete. Phase 6 integrated Sculpt's Leitzentrale: the
subsystem boots under `lz_runtime`, `vct leitzentrale` toggles it via
`sponge_configd`, the `lz_viewer` dedicated viewer shows it as a window
on the Sponge desktop, and `lz_watch` + configd detect and resolve
model changes (keep/revert).

Next up: Phase 7 (Alpha — the first usable version: installable media,
Sponge DE usable after boot, a default app set installable, vct
covering day-to-day tasks, installation docs, and documented known
limitations).

Deferred follow-ups (not blockers):

1. **base-sel4 interactive GUI** ✅ driver stack delivered
   (`run/sponge-de-sel4-interactive.run`): the full
   `drivers_interactive-pc` driver set — vesa_fb, ps2, usb_hid,
   pc_usb_host, event_filter, platform, acpi, pci_decode — is wired into
   a base-sel4 QEMU run script with `-device nec-usb-xhci,id=xhci
   -device usb-tablet` for absolute-pointer input, exactly as this item
   asked. The scenario boots seL4 on QEMU (1 GiB RAM) and is verified
   headlessly by two boot-log assertions: vesa_fb sets the 1024x768 mode
   and maps the physical VESA framebuffer, and usb_hid binds the QEMU
   usb-tablet as a `POINTER` device (the last driver to come up, so it
   also implies acpi → pci_decode → platform → pc_usb_host succeeded).
   The Qt6/Mesa (EGL) rendering path on base-sel4 is a separate,
   newly-discovered limitation documented in §11.1 below, so Sponge DE's
   own window is not yet visible on base-sel4; the interactive escape
   hatch is `run_genode_until forever` + `-display sdl`. Reference:
   `docs/11-environment.md` §7.2/§10 and the vendored
   `genode/repos/os/recipes/raw/drivers_interactive-pc/` config set.

   11.1. **Known limitation — Qt6/Mesa (EGL) hangs on base-sel4.**
   Sponge DE's Qt6/EGL initialization hangs on base-sel4: the component
   logs its first few lines (including the benign libEGL
   `MESA-LOADER: failed to retrieve device information` warnings that on
   base-linux are followed by a softpipe fallback and a painted window),
   requests one tiny PD-quota upgrade, and then never reaches Qt widget
   creation. This is orthogonal to the driver stack (vesa_fb/ps2/usb_hid
   all come up and stay up around the hung sponge-de). The same hang
   blocks the headless `run/sponge-de-test.run` from exercising its
   base-sel4 path: that scenario's Qt6 shared-library staging
   (`cp` into `run_dir/genode/`) is base-linux-specific (base-sel4 packs
   a single `image.elf`), so Qt6-on-seL4 rendering had never actually
   been run before this work. Fixing it needs Mesa/EGL-on-seL4
   investigation (likely the loader blocking on an absent DRM device, or
   a softpipe-on-seL4 init issue) and is tracked as the next follow-up.
   Once fixed, `run/sponge-de-sel4-interactive.run` already documents
   the intended stronger verification: probe `inject=no` Capture pixel
   check + a host-side QMP `input-send-event` usb-tablet click observed
   through sponge-de's `input` report.
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
   (theme tar via VFS) is the nearer-term step on this path.