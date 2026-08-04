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
Phase 10: Sponge DE — fully interactive desktop
   |
   v
Phase 11: DE customization and panel strengthening
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

### Phase 10: Sponge DE — Fully Interactive Desktop

#### Goal

The desktop becomes fully operable through real input: mouse and
keyboard work end-to-end through the actual driver chain, windows can
be dragged, and every interactive element (buttons, launcher entries,
the panel) responds to clicks.

#### Completion Criteria

- [ ] Real input path verified end-to-end: usb-tablet → pc_usb_host →
  usb_hid → event_filter → nitpicker → sponge-de, driven from the host
  via QMP `input-send-event` instead of the probe's synthetic Event
  injection (this closes the remaining §11.1 follow-up).
- [ ] Window dragging / moving works and is verified by a run scenario
  (pointer press on the title bar, motion, release; window position
  asserted through the layouter or a Capture pixel check).
- [ ] Click-to-launch delivered: clicking a launcher entry starts the
  package through the start-on-demand lifecycle (the Phase 5 deferral
  is resolved).
- [ ] Panel interactions wired: launcher menu open/close, panel
  buttons, and clock/menu elements respond to clicks.
- [ ] Keyboard input reaches focused windows (typing in terminal and
  text editor verified by scenario).

### Phase 11: DE Customization and Panel Strengthening

#### Goal

Users can meaningfully customize their desktop environment — theme,
panel layout, and behavior — without touching Genode internals.
Automation stays the default; every customization is also reachable
manually through `vct config` / theme files (control escape hatch).

#### Completion Criteria

- [ ] Panel customization through `sponge_configd`: position, size,
  visible widgets, clock format, launcher organization.
- [ ] Expanded theme surface: more themeable elements, additional
  shipped themes beyond `default.theme`, live reload preserved.
- [ ] Sponge-themed window chrome: the `themed_decorator` drop-in
  (theme tar via VFS) replaces stock decorations with themed ones —
  the near-term step from deferred item 3 below.
- [ ] All customization flows documented and scenario-verified
  (config change → configd → themed/decorator → pixel repaint).

### Phase 12: Hardware Support Expansion

#### Goal

The system boots on a wider range of (virtual and physical) platform
configurations, and the supported-hardware surface is explicit rather
than accidental. This phase builds the enablement infrastructure;
Phase 15 is the concrete real-hardware boot milestone.

#### Completion Criteria

- [ ] Boot matrix beyond the current QEMU defaults: additional machine
  types, AHCI and NVMe storage variants, USB boot media.
- [ ] Driver set expanded: networking beyond QEMU slirp (at least one
  real NIC driver path), input beyond PS/2 + usb-tablet.
- [ ] A hardware compatibility document listing tested configurations
  and known gaps.
- [ ] Run scenarios cover the new configurations so regressions fail
  loudly.

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
QEMU-only limitation is retired.

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
partition tables or Genode run scripts.

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

Phases 0–7 are complete, with Alpha caveats recorded in
[`docs/13-installation.md`](13-installation.md). Phases 8 and 9 are
also complete: the boot/storage architecture (`docs/14`) delivered
disk-based payload staging and persistence on the 4-partition product
media, and the seL4 capability-space work closed the capability-chain
blocker so Falkon boots from disk and reaches first paint on seL4.
The two largest Alpha caveats — the boot-module ceiling and missing
persistence — are resolved.

Next up: **Phase 10 — the fully interactive desktop**, followed by the
post-Alpha sequence defined in §10 (customization, hardware support,
packages, daily usability, real hardware, IME, GUI installer).

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
    event_filter → nitpicker → sponge-de with real hardware input — is
    now tracked as a completion criterion of **Phase 10** (§10).
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