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

🟡 **Early design stage (Pre-Alpha)** — booting on real Genode, both kernels.

The repository currently contains:

- ✅ Project philosophy and architectural principles defined
- ✅ Genode-based directory structure and build system skeleton
- ✅ vct and Sponge DE component scaffolds
- ✅ vct boots as a real Genode component on base-linux (Genode 26.05)
- ✅ vct boots as a real Genode component on base-sel4 (seL4, the production target)
- 🔜 IPC interface definitions between components
- 🔜 A minimum working demo

Verified vct boot output (base-sel4 on QEMU):

```
Genode 26.05
699 MiB RAM and 523288 caps assigned to init
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] vct — Very Convenient Tool
[init -> vct] version: 0.0.1-pre-alpha (Archaeocyte)
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

Contributors and AI agents must read [`AGENTS.md`](AGENTS.md) first.

---

## License

Not yet decided. Will be chosen during early development.