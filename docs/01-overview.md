# 01 - Project Overview

> What Sponge OS is, why it exists, and who it is for.

---

## 1. One-line Summary

Sponge OS is a **component-based operating system for everyday users,
built on top of the Genode OS Framework**. It keeps the strong security
and modularity of Genode's architecture intact, and layers a broad
automation stack plus an intuitive user environment on top so the system
can be used in daily life.

---

## 2. Vision

The mainstream operating systems of today sit at two extremes.

- **Easy but huge and opaque OSes** (Windows, macOS, typical Linux
  distributions): users can use them easily, but it is hard to know what
  is happening inside the system or to swap out or control a specific
  piece of it.
- **Robust and modular but expert-oriented OSes** (Sculpt OS, pure
  microkernel systems): the structure is clear and security is excellent,
  but the entry barrier is too high for everyday users.

Sponge OS aims for a **third way**. It keeps Genode's solid component
structure, and adds broad automation plus an intuitive user interface on
top so everyday users can reap the benefits without facing Genode's
complexity.

> **Automation is the default; control is a door that opens for whoever
> needs it, as far as they need it.**

---

## 3. The Origin of the Name: "Sponge"

A sponge (Porifera) looks like one organism, but in reality it is a
symbiosis of countless **independent cells**. Each cell performs its own
role (filtering, structural support, defense, reproduction, ...), and the
whole becomes a living sponge.

That maps precisely onto Genode's component model.

- Each component runs **independently** inside its own address space (the cell)
- Components **cooperate** through capability-based IPC (signals between cells)
- The whole forms a **single coherent system** (the organism)

Sponge OS extends this metaphor all the way to the user experience. The
user deals with the system as if it were a living sponge, and can
inspect or replace individual "cells" at any time.

---

## 4. Target Users

Sponge OS is designed for these users:

| User Type | Expected Way of Use |
|---|---|
| **Everyday user** | Uses vct and Sponge DE without needing complex concepts; relies on automated defaults. |
| **Power user** | Uses vct's detailed options and the Leitzentrale window to control individual components directly. |
| **Developer / researcher** | Modifies Genode run scripts and component configurations directly to build custom systems. |

The primary target is the **everyday user**. The first goal of Sponge OS
is to make sure those users never need to know the word "Genode". The
control surfaces for power users and developers exist as a **selectable
layer on top**.

---

## 5. Relationship with Genode

Sponge OS does **not re-implement or fork Genode**. It uses Genode
upstream as an external dependency, and the Sponge OS repository contains
only:

- Sponge OS-specific components (vct, Sponge DE, sponge_launcher, ...)
- Sponge OS-specific shared libraries and headers
- Build configuration and run scripts (scenario definitions)
- User-facing documentation

Genode's capability model, component framework, and session interfaces
(PD, CPU, LOG, ROM, RM, Timer, GUI, ...) are used as Genode provides them.
Sculpt OS's Leitzentrale is likewise not re-implemented by Sponge OS; it
is exposed as a window.

See [`03-architecture.md`](03-architecture.md) for the details.

---

## 6. Differences from Sculpt OS

Sculpt OS is the "Genode for the road" distribution maintained officially
by the Genode team, and it is an excellent example of Genode turned into
a usable form. Sponge OS drew inspiration from Sculpt OS, but differs in
the following ways:

| Aspect | Sculpt OS | Sponge OS |
|---|---|---|
| Primary target | Skilled users | Everyday users |
| Level of automation | Many decisions left to the user | Daily tasks automated by default |
| DE | Sculpt's management-UI centric | Own Qt-based DE (Sponge DE) |
| System management tool | Leitzentrale web UI | vct CLI (Leitzentrale as advanced mode) |
| Extensibility | Genode standard | Same plus Sponge-specific convenience layer |

Sponge OS does not try to replace Sculpt OS. It is an attempt to **bring
Genode-based systems closer to everyday users**. It keeps Sculpt OS's
discipline while focusing on lowering the entry barrier.

---

## 7. Current Status and Next Steps

🟡 **Pre-Alpha (early design stage)** — but vct boots on real Genode.

What is finished in this stage:

- Definition of project philosophy and architectural principles
- Repository structure and build system skeleton
- Scaffolds for core components (vct, Sponge DE, sponge_launcher)
- Initial design documentation (this directory)
- vct builds and runs as a real Genode component on both base-linux
  and base-sel4, verified by `make run/sponge-minimal` against Genode
  26.05

Next-step goals:

1. Wire vct's `status` command to live component state via a Genode
   `Report` session (Phase 2)
2. Add per-subcommand run scenarios (`sponge-vct-help.run`,
   `sponge-vct-version.run`)
3. Single-window prototype of Sponge DE

See [`09-roadmap.md`](09-roadmap.md) for the full schedule.