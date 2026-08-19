# Sponge OS

> Like a sponge: many small units come together to form a single organic whole.

Sponge OS is a user-centric, component-based operating system built on top
of the [Genode OS Framework](https://genode.org). Instead of a monolithic
kernel, each function lives as an independent component and communicates
through capability-based IPC, which gives Sponge OS security, robustness,
and modularity at the same time. On top of that, Sponge OS adds a broad
automation layer so everyday users can use the system without facing
Genode's complexity.

---

## Why "Sponge"

A sponge (Porifera) behaves like a single organism, but in reality it is
made of countless independent cells that cooperate to form one living body.
Some cells filter, some provide structure, some defend, and the whole
becomes a living sponge.

That maps closely onto Genode's component model. Many small components
each play their role, and capability-based communication ties them into
one consistent system. Sponge OS extends this philosophy all the way out
to the user experience.

---

## Core Philosophy

Every design decision in Sponge OS starts from the balance of three
principles.

| Principle | Meaning |
|---|---|
| **Convenience** | Everyday users can use the system without needing complex concepts. |
| **Control** | Users who want it can selectively take direct control over **every part** of the system. |
| **Automation** | Repetitive and complex parts are handled by the system itself. |

These three principles do not conflict. Automation is the default; control
is a door that opens for whoever needs it, as far as they need it.

See [`docs/02-philosophy.md`](docs/02-philosophy.md) for the full treatment.

---

## Main Components

### Sponge DE

A very lightweight Qt-based desktop environment, designed to be easy for
users to customize, and built on top of Genode's component model.

### vct (Very Convenient Tool)

The single entry point for managing the whole Sponge OS system. It handles
package installation, component configuration, system status checks, and
similar tasks through simple commands. Most work is automated, so the
user only has to express intent.

### Leitzentrale

The expert control interface used by Sculpt OS. In Sponge OS it is
exposed as an "advanced mode" window reachable from vct, allowing direct
manipulation of the system's individual components.

See [`docs/04-components.md`](docs/04-components.md) for the detailed design.

---

## Current Status

🟢 **Alpha 0.1.0, Archaeocyte** — Phase 7 is done with caveats,
Phase 10 (fully interactive desktop) is done, Phase 11 (DE
customization: configd-driven panel, four shipped themes, themed
window chrome) is done, and Phase 14 (Sponge DE as a
daily-usable desktop) is closed-out with honest disposition notes
in [`docs/09-roadmap.md`](docs/09-roadmap.md) §10 Phase 14 — three
of four completion criteria delivered, the fourth (session
stability wall-clock cycle) is partial because the 30-min scenario
is a sustained workload that the close-out session skips by
design; see
[`docs/evidence/phase14-index.md`](docs/evidence/phase14-index.md).
**Phase 15 (real-hardware boot) is in flight:** 15-1 (bake
profiles `minimal`/`desktop` via `run/bake.inc` + `tool/bake` +
first-boot configd seeding + `vct bake`; UEFI product media via
`tool/dist --firmware uefi`) and 15-2 (usb-mouse envelope,
hardware-matrix update with the D15.11 real-hardware admission
policy, 9-scenario regression sweep all-PASS) are done; 15-3 (the
user's physical USB boot on the LG gram 17ZD90N) is pending — see
[`docs/plans/phase15-real-hardware-boot.md`](docs/plans/phase15-real-hardware-boot.md)
and [`docs/plans/phase15-hardware-boot-protocol.md`](docs/plans/phase15-hardware-boot-protocol.md).
The verified release boots in QEMU on seL4. See
[`docs/13-installation.md`](docs/13-installation.md) for prerequisites,
media commands, the quick-start tour, and the complete limitations register.

The repository currently contains:

- ✅ Project philosophy and architectural principles defined
- ✅ Genode-based directory structure and build system skeleton
- ✅ vct boots as a real Genode component on base-linux (Genode 26.05)
  and on base-sel4 in QEMU
- ✅ vct day-to-day commands: `status`, `component list`, `config`,
  `install`/`remove`/`list`, `theme`, `launch`, `update`, `search`,
  `shutdown`/`reboot`, and `leitzentrale` — with `--json`, `--lang ko`,
  and `--manual` escape hatches
- ✅ Alpha 0.1.0 seL4 disk image and ISO boot in QEMU to the themed desktop
  (`run/sponge-alpha.run` and the media scenarios)
- ✅ Alpha packages verified in QEMU: terminal with bash/vim and a UNIX
  CLI toolset (coreutils, grep, sed, tar, less, findutils, diffutils,
  which — Phase 13), Qt6 text editor, and Sponge file manager
  (`run/sponge-terminal.run`, `run/sponge-textedit.run`,
  `run/sponge-files.run`)
- ✅ Phase 13 packages: Qt6 calculator (`run/sponge-calculator.run`)
  and mupdf PDF viewer with bundled sample document
  (`run/sponge-pdf-view.run`), both source-built and boot-verified on
  base-sel4; authoring guide at `docs/16-package-authoring.md`
- ✅ Falkon is packaged, boots from disk, AND reaches first paint on the Alpha seL4 media (Phase 9 closed the capability-chain blocker); see
  [`docs/13-installation.md`](docs/13-installation.md#6-known-limitations)
- ⚠️ Alpha installs enable pre-staged packages and do not persist across
  reboots on seL4 media; networking is QEMU slirp only
- ✅ Genode 26.05 vendored at `genode/` (pinned to upstream commit
  `492a510242`); one `git clone sponge-os` brings the whole build
- ✅ Sponge DE (Qt6 6.8.3) renders a themed panel + window on nitpicker
  with verified input, integrated with Genode's upstream
  `wm` + `window_layouter` + `decorator` stack (window dragging verified)
- ✅ Package management: `sponge_pkgd` backend, dependency resolution,
  nested `pkg_runtime`, installed-set inspection, and launch lifecycle
  (`run/sponge-pkg-install.run`, `run/sponge-pkg-list.run`,
  `run/sponge-launch.run`)
- ✅ Configuration and theme backends (`sponge_configd`, `sponge_themed`)
  with live theme reload shared by vct and Sponge DE
- ✅ Launcher menu fed by `sponge_pkgd`'s installed-set broadcast
- ✅ Leitzentrale integration: Sculpt's expert interface boots as a
  subsystem, shows as a Sponge DE window via `lz_viewer`, and model
  changes are detected and resolved (`vct leitzentrale keep/revert`)
- ✅ base-sel4 interactive driver stack: `run/sponge-de-sel4-interactive.run`
  boots seL4 on QEMU with vesa_fb, ps2, and USB tablet input
  (`drivers_interactive-pc`), and Sponge DE (Qt6/Mesa softpipe) renders
  the themed desktop on seL4. Phase 10 strengthened every input action
  to the real QMP-driven chain: a host-driven QEMU QMP click reaches
  sponge-de through the real
  `usb-tablet`/`ps2` → `pc_usb_host`/`usb_hid` → `event_filter` → `nitpicker`
  → `sponge-de` driver path, the panel launcher toggle opens + closes
  the launcher popup on host QMP clicks, and clicking a launcher entry
  fires the full click-to-launch chain (`Qt` → `LauncherController`
  → `sponge_pkgd` `_do_launch` → `pkg_gui_demo` first paint). The four
  sister scenarios (`sponge-wm-qmp.run`, `sponge-terminal-qmp.run`,
  `sponge-textedit-qmp.run`) prove window dragging + QMP `send-key`
  keyboard input to a focused terminal + text editor respectively.
  See `docs/evidence/phase10-index.md` and `docs/08-development.md`
  §4.4 (root cause of the former EGL hang: capability exhaustion on
  seL4, fixed by sizing `caps`; see `docs/09-roadmap.md` §11.1).
- ✅ Phase 11 DE customization: `panel.height`, `panel.visible_widgets`,
  `clock.format`, and `launcher.sort_by` are live `sponge_configd`
  keys applied by Sponge DE's new `ConfigController`
  (`run/sponge-panel-config{,-sel4}.run`); four shipped themes
  (`default`/`light`/`dark`/`compact`) with live reload and a hardened
  unknown-theme fallback (`run/sponge-theme.run`); and Sponge-themed
  window chrome — upstream `themed_decorator` fed a Sponge-authored
  theme tar (`./tool/decor_assets`) with the title-bar tint following
  the active theme via the new `sponge_decorator_bridge`
  (`run/sponge-de-themed-chrome.run`, drag-verified on seL4). See
  `docs/evidence/phase11-index.md`.
- ✅ Phase 14 W7 window management: the panel tasklist
  (`repos/sponge/src/sponge-de/panel/tasklist/`) is the deterministic
  minimize/restore/close path for the window stack (U3 — no
  decorative-only minimize). The `TasklistController` subscribes to
  wm's `window_list` and the layouter's `window_layout` reports;
  per-window click writes the layouter's `rules` ROM (in
  `rules="rom"` mode) and emits a `focus_request` report. The
  `TasklistWidget` is rendered as a horizontal strip inside the
  panel QHBoxLayout, with three visual states (Normal-Visible,
  Normal-Visible-Focused, Minimized). The state machine
  (`docs/plans/wm-state-table.md`) is exercised by the
  `run/sponge-wm-tasks.run` acceptance scenario. See
  `docs/05-sponge-de.md` §4.6.
- 🚧 Phase 14 close-out (W12): four completion criteria are checked
  with honest disposition notes in
  `docs/09-roadmap.md` §10 Phase 14 — three delivered, one
  partial. **Delivered:** notifications (`run/sponge-notify.run` →
  `notify-probe: PASS`), clipboard (`run/sponge-clipboard-qtsettext.run` →
  `clipboard-probe: PASS`, qtsettext: clipboard Qt -> server write
  harness PASSED), window management minimize/restore/focus
  (`run/sponge-wm-tasks.run` → `wm-tasks-probe: PASS`), and the
  full 4-way paper-cut disposition matrix (50 rows: Resolved in 14
  / Re-scoped / Blocked / Not-a-defect) per U5, with W11 closing
  every Phase-14-relevant item. **Partial:** session stability —
  the 200-cycle leak-audit probe and the 5-cycle fast-fail variant
  are boot-verified; the 30-min wall-clock cycle is a sustained
  workload that exceeds the W12 20-min/scenario skip rule by
  design (re-verify in a dedicated Phase-15 close-out pass).
  **Partial:** everyday-workflow scenario — `run/sponge-de-workflow.run`
  steps 1-4 PASS (boot → terminal → textedit → cross-component
  clipboard; U2 holds because the `clipboard_qtsettext` harness
  and the workflow probe are separate address spaces); step 5
  (tasklist click on the heavier workflow topology) is blocked by
  a layouter hover-state timing race documented in
  `docs/evidence/phase14-w8-workflow-scenario.md` (the W7 tasklist
  recipe works on the standalone wm-tasks topology). **Falkon
  browser is fully proven** via `run/sponge-falkon-rescue.run`
  (D14.5 Attempt 1 PASS: booted from disk, window pixel-verified,
  fixture page loaded over the `pc_nic`/slirp stack). Phase 15+
  limitations register: every `Re-scoped` paper-cut row carries
  its target phase in the plan appendix and the
  `docs/evidence/phase14-index.md` §5 honest-limitations register.
  W12 regression sweep (6 scenarios, serial `make -j1`):
  `sponge-de-test`, `sponge-alpha`, `sponge-wm-tasks`,
  `sponge-clipboard-qtsettext`, `sponge-notify`,
  `sponge-configd-persist` — all PASS. Full receipts:
  `docs/evidence/phase14-index.md` +
  `docs/evidence/phase14-envelope-*.log`.

Verified vct boot output (base-sel4 on QEMU):

```
Genode 26.05
699 MiB RAM and 523288 caps assigned to init
[init -> vct] vct (0.1.0-alpha / Archaeocyte) starting
[init -> vct] vct — Very Convenient Tool
[init -> vct] version: 0.1.0-alpha (Archaeocyte)
Run script execution successful.
```

See the [`docs/09-roadmap.md`](docs/09-roadmap.md) for the plan.

---

## Documentation Map

All detailed documentation lives in [`docs/`](docs/).

- [01 - Project Overview](docs/01-overview.md)
- [02 - Core Philosophy](docs/02-philosophy.md)
- [03 - System Architecture](docs/03-architecture.md)
- [04 - Component Layout](docs/04-components.md)
- [05 - Sponge DE Design](docs/05-sponge-de.md)
- [06 - vct Design](docs/06-vct.md)
- [07 - Leitzentrale Integration](docs/07-leitzentrale.md)
- [08 - Development Guide](docs/08-development.md)
- [09 - Roadmap](docs/09-roadmap.md)
- [10 - Theme Format](docs/10-theme-format.md)
- [11 - Development Environment & Reproducibility](docs/11-environment.md)
- [13 - Installation and Alpha Quick Start](docs/13-installation.md)
- [14 - Boot & Storage Architecture (Proposal)](docs/14-boot-storage-architecture.md)
- [15 - Hardware Compatibility](docs/15-hardware-compatibility.md)
- [16 - Package Authoring Guide](docs/16-package-authoring.md)

Phase evidence indexes live under `docs/evidence/` (e.g.
`phase13-index.md`, `phase14-index.md`).

Contributors and AI agents must read [`AGENTS.md`](AGENTS.md) first.

---

## License

Sponge OS's own code (everything under `repos/sponge/`, `tool/`, `run/`,
`docs/`, and the other top-level files of this repository) is licensed
under the [Apache License 2.0](LICENSE).

The Genode OS Framework vendored at `genode/` is upstream Genode source
and remains under its own license (GPLv2 with a linking exception; see
`genode/COPYING` and the headers of the individual files). Nothing in
this repository relicenses the vendored tree — the Apache 2.0 grant
applies only to Sponge OS-specific code.