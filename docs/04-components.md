# 04 - Component Layout

> This document lists the components that make up Sponge OS, classifies
> them, and shows how they depend on each other. For the detailed design
> of a specific component, see the dedicated document.

---

## 1. Component Classification

Sponge OS components are divided into three groups by role.

### 1.1 User Interface Components

Components that the user interacts with directly. They serve as entry
points for both everyday users and power users.

| Component | Form | Role | Document |
|---|---|---|---|
| `sponge-de` | GUI (Qt) | Desktop environment (panel, window management, notifications, theme) | [`05-sponge-de.md`](05-sponge-de.md) |
| `sponge_launcher` | GUI (Qt) | Application launcher | [`05-sponge-de.md`](05-sponge-de.md) |
| `vct` | CLI | System management tool | [`06-vct.md`](06-vct.md) |
| `leitzentrale` | GUI (web view) | Expert system control (Sculpt integration) | [`07-leitzentrale.md`](07-leitzentrale.md) |

### 1.2 Backend Service Components

Services called by the user-interface components. The user does not
touch them directly, but they are where "automation" and "control" are
actually implemented.

| Component | Role | Status |
|---|---|---|
| `sponge_pkgd` | Package metadata, dependency resolution, component configuration | not implemented (design stage) |
| `sponge_configd` | Storage, validation, and application of system configuration | not implemented |
| `sponge_automated` | Evaluation and execution of automation rules | not implemented |
| `sponge_themed` | Storage and application of themes (Sponge DE and the whole system) | not implemented |

> ✅ **Backend naming convention (locked):** backend daemons follow the
> `sponge_<name>d` form (the trailing `d` marks the daemon, mirroring
> the Unix convention). The RPC interface used by user-interface
> components lives under `include/sponge/`.

### 1.3 Genode-Provided Components (Reused)

Components that Sponge OS does not implement itself and uses as Genode
upstream provides them.

| Component | Role |
|---|---|
| `init` | System initialization, component tree management |
| `vfs` | Virtual file system |
| `nitpicker` | Window compositor (GUI server) |
| `platform_drv` | Platform driver |
| `ps2_drv` / `usb_drv` | Input device drivers |
| `vesa_drv` / GPU driver | Graphics driver |
| `fb_sdl` (development) | Linux/SDL framebuffer (development host) |
| Other Sculpt components | Leitzentrale, and the like |

---

## 2. Component Dependency Graph

The dependency graph for an early version. Arrows mean "depends on"
(requests a session).

```
                       ┌─────────┐
                       │  init   │  ← ancestor of every component
                       └────┬────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
        ▼                   ▼                   ▼
  ┌──────────┐       ┌────────────┐      ┌──────────────┐
  │vfs       │       │nitpicker   │      │platform_drv  │
  │(file sys)│      │(composer)  │      │(hardware)    │
  └─────┬────┘       └─────┬──────┘      └──────────────┘
        │                   │
        │                   │
        ├───────────┬───────┼───────────────────────┐
        ▼           ▼       ▼                       ▼
  ┌─────────┐ ┌─────────┐ ┌──────────────┐  ┌─────────────┐
  │sponge_  │ │sponge_  │ │ sponge-de    │  │   vct       │
  │pkgd     │ │configd  │ │ (Qt, GUI)    │  │ (CLI)       │
  └────┬────┘ └────┬────┘ └──────┬───────┘  └──────┬──────┘
       │           │             │                  │
       │           │             │                  │
       └─────┬─────┘             │                  │
             │                   │                  │
             └────── RPC ────────┴──────────────────┘
                  (backend ↔ UI)
```

The user-interface components (`vct`, `sponge-de`) call backend services
through RPC. The backends modify the component tree through Genode's
`init` configuration interface.

---

## 3. Component Lifecycle

Sponge OS components follow this lifecycle.

### 3.1 At Boot

1. `init` is started by the kernel.
2. `init` reads its boot configuration (a ROM) and creates the child
   components.
3. Required backend components come up first (`sponge_pkgd`,
   `sponge_configd`).
4. User-interface components come up (`sponge-de`, and `vct` on
   demand).
5. Once the user environment is ready, boot completes.

### 3.2 At Runtime

- `vct` is a short-lived component: it is created and destroyed every
  time the user runs it.
- `sponge-de` is a long-lived component that lives from boot to
  shutdown.
- Backend services live from boot to shutdown and can self-recover.

### 3.3 At Shutdown

- When `init` receives a system-shutdown signal, it cleans up every
  child.
- When `vct` receives a `shutdown` or `reboot` command, it forwards
  the signal to `init`.
- Each component performs cleanup (free memory, close files, flush
  logs) before exiting.

---

## 4. Guide to Adding a New Component

When adding a new component to the Sponge OS repository:

1. Create `repos/sponge/src/<component_name>/`.
2. Write `target.mk`, `main.cc`, and `README.md` (see `AGENTS.md` §3.2
   for the format).
3. If the component exposes a backend RPC, place its interface header
   under `repos/sponge/include/sponge/`.
4. Add the component to the inventory table in `docs/04-components.md`.
5. If needed, write a dedicated design document (`docs/NN-<name>.md`)
   and link it from README and AGENTS.
6. Add a run script under `run/` to verify the new component.

Every new component must:

- Request only the minimum privileges it needs (`AGENTS.md` §1.2).
- Follow the language policy in `AGENTS.md` §1.3.
- Log `Genode::warning("not implemented: ...")` for empty
  implementations (`AGENTS.md` §5.3).

---

## 5. Open Design Questions

Items still unsettled at the early design stage:

- The exact boundary of Leitzentrale integration (whether vct only
  spawns the leitzentrale component, or whether there is deeper
  integration).

### Settled in Phase 4 (kept for the record)

- **IDL style for backend interfaces** — settled: no IDL tool, no
  hand-written RPC stubs. Backend communication uses Report/ROM
  sessions bridged by `report_rom` (the same capability-based IPC the
  rest of the system already uses). The "(RPC)" arrows in
  `docs/03-architecture.md` §4.1 and `docs/06-vct.md` §7 read as
  "capability IPC", which Report/ROM sessions are.
- **Communication path vct / Sponge DE ↔ backends** — settled: direct,
  no intermediate router. `report_rom` itself is the decoupling layer:
  vct writes a request report, the backend answers with a result
  report, both relayed as ROMs. Rationale: vct is already a ROM-poll
  client (`src/vct/init_state.cc`), report_rom is already wired in
  every scenario, structured results (the `--explain` plan, `--json`
  payloads) travel as text natively, and the signal-driven model fits
  the long-lived GUI caller (sponge-de) without blocking. Known
  limitation: report_rom is a single-writer slot, so concurrent callers
  would collide; a request-id plus a backend-side mutex is deferred to
  the phase where concurrent callers actually appear.

These items will be settled by experiments during the early prototype
phase.