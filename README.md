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

🟢 **Alpha 0.1.0, Archaeocyte** — Phase 7 is done with caveats, and
Phase 10 (fully interactive desktop) is done. The verified release
boots in QEMU on seL4. See
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
- ✅ Alpha packages verified in QEMU: terminal with bash/vim, Qt6 text
  editor, and Sponge file manager (`run/sponge-terminal.run`,
  `run/sponge-textedit.run`, `run/sponge-files.run`)
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