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

🟢 **0.2.0, Archaeocyte** (released 2026-09-16) — the desktop boots and
is interactive on real hardware (LG gram 17ZD90N: panel at its native
2560x1600, windows and in-window interaction, mouse input) and in QEMU
on seL4. Phases 0–15 are complete; Phase 16 (daily-usable desktop
defaults) is delivered with honest disclosures; Phase 17 (Sponge IME)
and Phase 18 (GUI installer) are next. Shipped packages: terminal
(bash/vim + UNIX toolset), Qt6 text editor, file manager, calculator,
mupdf PDF viewer, and Falkon.

Per-phase criteria, dispositions, and sweeps: [`docs/09-roadmap.md`](docs/09-roadmap.md)
and [`docs/roadmap/`](docs/roadmap/); receipts: [`docs/evidence/`](docs/evidence/).
Installation and the complete limitations register:
[`docs/13-installation.md`](docs/13-installation.md).

### Open limitations (honest list)

- **Criterion 18 (Settings control center)** stays open: the provider
  wiring is QEMU-verified, but the real QMenu quick-settings surface is
  **blocked** (red repro `run/sponge-settings-quickmenu.run`) and the
  physical-hardware matrix, one-reboot package persistence, and the
  hardware control-center crash test have not run
  ([detail](docs/roadmap/phase16-scope-expansion.md),
  [evidence](docs/evidence/criterion18-settings.md)).
- **Package persistence and networking**: Alpha installs enable
  pre-staged packages and do not persist across reboots on seL4 media;
  networking is QEMU slirp only ([detail](docs/13-installation.md)).
- **Real hardware**: trackpad and Wi-Fi remain pre-registered gaps ([detail](docs/roadmap/phase15-real-hardware-boot.md)).
- **Phase 16 carry-overs** — background right-click delivery,
  cross-panel tasklist focus, themed-chrome full-sequence flakiness,
  `panel.position` left/right, real-hardware multi-panel regression,
  falkon on seL4, user-supplied background image upload — are registered
  in [`docs/evidence/phase16-index.md`](docs/evidence/phase16-index.md) §6.

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
- [12 - Package Format](docs/12-package-format.md)
- [13 - Installation and Alpha Quick Start](docs/13-installation.md)
- [14 - Boot & Storage Architecture (Proposal)](docs/14-boot-storage-architecture.md)
- [15 - Hardware Compatibility](docs/15-hardware-compatibility.md)
- [16 - Package Authoring Guide](docs/16-package-authoring.md)
- [17 - Runtime Component Topology](docs/17-runtime-component-topology.md)

Per-area detail lives in topic subdirectories: [`docs/roadmap/`](docs/roadmap/)
(per-phase detail), [`docs/plans/`](docs/plans/) (work plans),
[`docs/evidence/`](docs/evidence/) (evidence indexes and logs), [`docs/de/`](docs/de/), [`docs/vct/`](docs/vct/), [`docs/package-format/`](docs/package-format/), [`docs/package-authoring/`](docs/package-authoring/), [`docs/development/`](docs/development/), [`docs/environment/`](docs/environment/), [`docs/hardware/`](docs/hardware/), [`docs/boot-storage/`](docs/boot-storage/), [`docs/topology/`](docs/topology/), and [`docs/patches/`](docs/patches/) (vendored patch files).

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
