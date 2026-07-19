# 03 - System Architecture

> This document explains the Genode component model that Sponge OS is built
> on, and the structure of the Sponge OS-specific layers above it.

---

## 1. Foundation: the Genode Component Model

Everything in Sponge OS runs on top of Genode's component model.
Understanding Genode is a prerequisite for understanding Sponge OS. This
section summarizes only what is directly relevant to Sponge OS. For the
rest, see the [Genode documentation](https://genode.org/documentation).

### 1.1 Components

In Genode, a **component** is an independent execution unit that owns
its own address space. It is similar to a Linux process, but with
stronger isolation and a capability-based communication model.

- Each component owns its own virtual memory space.
- A component cannot access another component's memory directly.
- Communication happens only through the formal interfaces called
  **sessions**.

### 1.2 Capabilities

A **capability** is an object that represents the permission "this
component may do X to that component's Y". It is similar to a Unix file
descriptor, but more refined.

- A capability must be **explicitly received** by a component before use.
- A parent passes capabilities to its children.
- Without a capability, a component cannot communicate with another.

This structure makes the "least privilege" principle fall out naturally.

### 1.3 Sessions

Inter-component communication happens through **sessions**. Genode
provides several standard session types:

| Session Type | Purpose |
|---|---|
| `PD` | Protection Domain (creating and managing components) |
| `CPU` | Thread creation and scheduling |
| `LOG` | Log output |
| `ROM` | Read-only data (binaries, configuration) |
| `RM` | Memory Region Map |
| `Timer` | Timers |
| `Gui` | Graphical output (windows) |
| `Input` | Keyboard and mouse input |
| `File_system` | File system access |

A component requests the sessions it needs from its parent (a `session`
RPC). The parent routes each request to an appropriate service provider.

### 1.4 The Parent-Child Tree

Genode organizes components as a **parent-child tree**. The topmost
parent is usually `init`, and every component is a direct or indirect
child of `init`.

```
init
├── platform_drv        # hardware drivers
├── vfs                 # virtual file system
├── nitpicker           # window compositor (GUI server)
├── sponge-de           # Sponge DE
├── vct                 # system management tool
├── firefox             # user application
└── ...
```

Each component may have its own children, to which it passes capabilities.
This tree and the routing configuration are the core concepts Sculpt OS's
Leitzentrale manipulates.

---

## 2. Sponge OS Layered Structure

Sponge OS adds the following layers on top of Genode:

```
┌──────────────────────────────────────────────────────────────┐
│                  User                                        │
└──────────────┬───────────────────────────────────┬───────────┘
               │                                   │
        ┌──────▼──────┐                    ┌───────▼───────┐
        │  Sponge DE  │                    │     vct       │
        │  (Qt-based) │                    │ (sys. mgmt.)  │
        └──────┬──────┘                    └───────┬───────┘
               │                                   │
               │           ┌───────────────────────┤
               │           │                       │
               │    ┌──────▼──────┐       ┌────────▼────────┐
               │    │ Leitzentrale│       │  Sponge-native  │
               │    │  (exposed   │       │  backend        │
               │    │  as window) │       │  services       │
               │    └──────┬──────┘       └────────┬────────┘
               │           │                       │
        ┌──────▼───────────▼───────────────────────▼──────┐
        │       Genode Component Framework                │
        │  (init, capability IPC, sessions, ...)          │
        └──────────────────────┬──────────────────────────┘
                               │
                      ┌────────▼────────┐
                      │   Kernel Base   │
                      │     seL4        │
                      └─────────────────┘
```

### 2.1 User Interface Layer

The layer the user touches directly.

- **Sponge DE**: the desktop environment (panel, launcher, window
  management, notifications). Detailed design in
  [`05-sponge-de.md`](05-sponge-de.md).
- **vct**: the system management CLI. Detailed design in
  [`06-vct.md`](06-vct.md).
- **Leitzentrale**: Sculpt OS's expert management UI, exposed in
  Sponge OS as an "advanced mode" window reachable through vct.
  Detailed design in [`07-leitzentrale.md`](07-leitzentrale.md).

### 2.2 Backend Service Layer

Sponge OS-specific services called by the user-interface layer. Shared
by vct and Sponge DE, implemented as Genode components of their own.

- **Package manager backend**: package metadata, dependency graph,
  component configuration.
- **Configuration manager**: storage, application, and validation of
  system configuration.
- **Automation engine**: evaluation and execution of automation rules.

These backends are Genode components that talk to the user-interface
layer over capability-based IPC. Because vct and Sponge DE share the
same backends, both interfaces behave consistently.

### 2.3 Genode Framework Layer

The parts that Sponge OS does not re-implement. `init`, capability IPC,
session interfaces, and the rest are used as Genode upstream provides
them.

### 2.4 Kernel Base

Genode can run on several microkernels (or its own `base-hw`). Sponge
OS has chosen **`base-sel4` (the seL4 microkernel)** as its kernel base.
The choice is driven by seL4's formal verification and its strong
security properties, which align with the foundational principles that
Sponge OS inherits from Genode.

Early development may also use `base-linux` on Linux hosts as a
development convenience, but real Sponge OS images target seL4. The
integration for `base-sel4` was completed as part of the Phase 1
milestone (see `docs/09-roadmap.md`): both kernels boot the same
`sponge-minimal.run` scenario.

---

## 3. Major Component Inventory

Components that Sponge OS implements directly or includes in its
configuration:

| Component | Role | Status |
|---|---|---|
| `vct` | System management CLI | scaffold |
| `sponge-de` | Desktop environment (starts as one component, gradually split) | scaffold |
| `sponge_launcher` | Application launcher | scaffold |
| `sponge_pkgd` | Package manager backend (planned) | not implemented |
| `sponge_configd` | Configuration manager backend (planned) | not implemented |
| `sponge_automated` | Automation engine (planned) | not implemented |

Genode-provided components (reused):

- `init` — system initialization and component tree management
- `vfs` — virtual file system
- `nitpicker` — window compositor
- `platform_drv` / drivers — hardware abstraction
- Sculpt's `leitzentrale` — expert management UI

---

## 4. Data Flow Examples

### 4.1 `vct install firefox`

```
vct
  └─(RPC)─→ sponge_pkgd
              ├─ fetch: package metadata
              ├─ compute: dependency graph
              ├─ generate: component config (node to add under init)
              ├─ verify: no conflict with user changes
              └─(RPC)─→ init
                          ├─ create: firefox component
                          ├─ wire: ROM, Gui, Input, File_system session routing
                          └─ start: run firefox
              └─(RPC)─→ sponge_configd
                          └─ register: add launcher entry
```

The user can preview every step with `--explain`, and run them
individually with `--manual`.

### 4.2 Opening the Leitzentrale Window

```
vct leitzentrale
  └─(RPC)─→ init
              └─ create: leitzentrale component
                          ├─ ROM: current component tree (read)
                          ├─ RPC: init's configuration interface (write)
                          └─ Gui: window output
```

Leitzentrale is not a mere viewer: it receives the right to modify
`init`'s configuration directly. Where vct offers "convenient
automation", Leitzentrale offers "raw but complete control".

---

## 5. Architectural Principles Summary

All Sponge OS code must obey the following:

1. **Follow Genode conventions**: `target.mk`, `Component::construct_`,
   session-based communication.
2. **Sponge OS-specific code lives only under `repos/sponge/`**: Genode
   itself is not modified.
3. **Separate user interface from backend**: vct and Sponge DE call
   backend services, and the backends do the real work. This keeps the
   two interfaces consistent.
4. **Gradual separation**: even when a component starts as a single
   process, keep its internal boundaries loose so that it can be split
   later.
5. **Automation must be transparent**: every automation action must be
   inspectable through logs and `--explain`.

Violations of the above fall under the "absolute prohibitions" in
`AGENTS.md`.