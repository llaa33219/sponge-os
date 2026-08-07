# AGENTS.md — Sponge OS Contribution Guidelines

> This file is mandatory reading for **every person and AI agent** contributing
> to Sponge OS. Check this document before writing code, sending a PR, or
> making a design decision.

---

## 0. Document Hierarchy (Priority)

When decisions conflict, apply the following priority order:

1. **This document (AGENTS.md)** — immutable principles
2. **`docs/02-philosophy.md`** — concrete interpretation of the three philosophies
3. **`docs/03-architecture.md`** — architectural constraints
4. Per-component design documents (`docs/05~07`)
5. Conventions of existing code

Changes that conflict with the above must first update the corresponding
document, or be discussed with the maintainers, before proceeding.

---

## 1. Hard Rules

### 1.1 The Three Philosophies Are Not a Trade-off

| Philosophy | Meaning | Violation Example |
|---|---|---|
| **Convenience** | Lowers the entry barrier for everyday users | Error messages that use only jargon |
| **Control** | Every automation must be bypassable manually | An automation with no escape hatch |
| **Automation** | Repetitive and complex work is handled by the system | Asking the user to make a manual decision every time |

All three must hold **simultaneously**. Trade-off frames such as "sacrifice
control for convenience" or "sacrifice convenience for control" are
rejected. The concrete design rule is: `automation = default,
control = a door that is always open`.

### 1.2 Do Not Break Genode's Philosophy

- A component runs **only inside its own address space**. It must not
  access another component's memory directly.
- Communication happens **only through capability-based IPC**. Do not
  create hidden channels.
- A component requests **only the minimum-privilege** sessions it needs.
  It must not receive more capability than necessary.
- Parent-to-child configuration must be **explicit and inspectable**.
  Do not introduce hidden global state.

### 1.3 Language Policy

- **Code, identifiers, error messages (logs)**: English
- **User-facing messages (UI, vct output)**: English primary. A Korean
  translation MAY be provided as an option via the `--lang ko` flag
  where applicable.
- **Documentation (`docs/`, READMEs)**: English
- **Commit messages**: English (conventional commits)

Comments inside source files remain English.

### 1.4 Absolute Prohibitions

- ❌ Type bypass (`as any`, abuse of `reinterpret_cast`, `void*` casts)
- ❌ Empty catch / exception handlers
- ❌ "Behind the user's back" automation that hides control (the user must be able to disable it)
- ❌ Monolithic components (multiple responsibilities packed into one component)
- ❌ Global state that bypasses Genode's capability model
- ❌ Default UX that exposes Genode internals directly to the user

---

## 2. Repository Structure

```
sponge-os/
├── README.md                  # User-facing introduction (short)
├── AGENTS.md                  # This file
├── docs/                      # All detailed documentation
├── genode/                    # Vendored Genode source tree (git subtree, see §5.2)
│   ├── repos/                 # Upstream Genode repositories (base, os, libports, ...)
│   └── repos/sponge -> ../../repos/sponge  # Relative symlink wiring Sponge in
├── repos/
│   └── sponge/                # The Sponge OS repository itself (Genode repo convention)
│       ├── src/               # Component sources
│       │   ├── vct/           # System management tool
│       │   ├── sponge-de/     # Desktop environment
│       │   └── sponge_launcher/ # Launcher
│       ├── lib/               # Shared libraries
│       ├── tool/              # Sponge-owned build helpers (compiler wrappers, ...)
│       └── include/sponge/    # Shared headers
├── tool/                      # Build and development scripts
├── run/                       # Genode run scripts (scenario definitions)
└── var/                       # Local caches (git-ignored): qt6 host tools, distfiles
```

The Genode build system assumes a `repos/<name>/` layout. The vendored
tree at `genode/` satisfies this via the committed relative symlink
`genode/repos/sponge -> ../../repos/sponge`, so no external Genode
checkout is ever required. See `docs/08-development.md` for the build
flow and `docs/11-environment.md` for the full environment contract.

---

## 3. Coding Rules

### 3.1 C++ (Genode Components)

- **C++ standard**: follow the version supported by Genode (currently C++17 recommended).
- **Naming conventions**:
  - Classes / structs: `PascalCase` (e.g. `VctCommandRouter`)
  - Functions / methods: `snake_case` (Genode convention)
  - Member variables: `snake_case_` (trailing underscore, Genode convention)
  - Constants / macros: `UPPER_SNAKE_CASE`
- **Header guards**: use `#pragma once`
- **Smart pointers**: prefer Genode's `Genode::Constructible<T>` and
  `Genode::Allocator_avl`. Minimize raw `new` / `delete`.
- **Exceptions**: Genode builds with exceptions disabled. Constructor
  failures are propagated through explicit initialization patterns.
- **Genode namespace**: primitive typedefs like `size_t`, `addr_t`,
  `uint32_t` are NOT in the global namespace in real Genode. Always
  qualify them as `Genode::size_t`, `Genode::addr_t`, etc. A standalone
  test harness that `using`'s them into global scope will hide this
  distinction and produce code that fails on the real framework.
- **Component entry points**: declare `Component::construct` and (if
  overridden) `Component::stack_size` exactly as the framework expects.
  `stack_size` returns `Genode::size_t`, not bare `size_t`.

### 3.2 Component Directory Layout

Every component directory must contain the following files:

```
src/<component>/
├── target.mk              # Genode build target definition
├── main.cc                # Entry point (Component::construct_)
├── README.md              # Component description (optional)
└── ...                    # Component-specific sources
```

`target.mk` follows this form:

```makefile
TARGET   := <component_name>
SRC_CC   := main.cc <other .cc files>
LIBS     := base # additional libraries
INC_DIR  := $(PRG_DIR)/include
```

### 3.3 vct Extension Rules (Important)

When adding a new feature (subcommand) to vct, you **must** follow these:

1. **Automation first**: the user should not have to make per-step decisions.
   Provide a sensible default behavior.
2. **Escape hatch**: the same task must be reachable manually, either
   through a `--manual` flag or a finer-grained subcommand.
3. **Help text**: `vct <command> --help` prints a concise English summary
   followed by detailed English help. A Korean translation may be
   requested with `--lang ko` where the runtime supports it.
4. **Machine-readable output**: support a `--json` flag.
5. **Leitzentrale integration**: if the feature manipulates the same
   targets as a Leitzentrale expert control, it must use the same backend
   interface.

### 3.4 Sponge DE Rules

- **Minimize Qt module dependencies**: link only the modules you need,
  not the whole of Qt.
- **Separate themes**: do not hardcode visual elements in code; put them
  in theme files.
- **Component isolation**: each DE part (panel, launcher, notifications)
  should be designable as a separate Genode component. Start with a
  single component, but keep boundaries loose.

### 3.5 Host-Side Tooling (Mojo)

- All scripts under `tool/` are written in **Mojo** (Modular's language).
  Each tool ships as a `.mojo` source file with a thin bash launcher that
  execs `mojo tool/<name>.mojo "$@"`.
- The hard boundary is "runs on the host developer machine" (Mojo) vs
  "runs inside Genode" (C++). Never write OS components in Mojo; Genode
  only targets C++.
- Tool scripts MAY automate setup steps against the **vendored** tree at
  `genode/` (creating build directories, writing `etc/build.conf`,
  running `prepare_port`) because that tree is part of this repository.
  Tool scripts MUST NOT create, modify, or delete anything **outside**
  the repository (e.g. an external Genode checkout, `/usr/local`,
  another home-directory path). Every automated step must also be
  documented as a manual step in `docs/08-development.md` (control
  escape hatch).
- Always follow the `mojo-syntax` skill (`modular/skills@mojo-syntax`)
  for current syntax — Mojo evolves rapidly and pretrained patterns go
  stale.

---

## 4. Workflow

### 4.1 Before Changing Anything

1. Read the relevant documents in `docs/` first.
2. Identify which philosophy or constraint the change touches.
3. If a design change is needed, update the documents first.

### 4.2 After Changing Anything

1. **Build verification**: ensure `tool/build` (or the standard Genode build) passes.
2. **Run scenarios**: add or update a `run/` scenario that exercises the change.
   A correct run script must build `lib/ld` (the dynamic linker) alongside
   `core init <component>` and pass `[build_artifacts]` to `build_boot_image`
   so that `init.xsd`, `ld.lib.so`, and the component binary are all staged
   in the boot directory. Missing any of them produces silent boot failures.
3. **Doc sync**: if behavior changed, update the relevant `docs/` too.
4. **Commit unit**: one logical change equals one commit.

### 4.3 Commit Messages

Conventional commits:

```
type(scope): subject

body
```

- `type`: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `build`
- `scope`: `vct`, `sponge-de`, `run`, `docs`, `tool`, ...
- Example: `feat(vct): add 'install' subcommand with --manual flag`

---

## 5. AI Agent-Specific Guidelines

Extra care points when working on this project as an AI agent:

### 5.1 "Convenience" Must Be Proven in Code

Claims like "user-friendly" must be backed by code. Specifically:
measure how many commands or clicks complete a task, check whether the
user must know Genode terminology, and put those numbers in the PR body.

### 5.2 Genode Is Vendored, Pinned, and Never Re-implemented

This repository **vendors** the Genode source tree at `genode/` via
`git subtree`, pinned to an upstream release (currently **26.05**,
upstream commit `492a510242`). Rationale: reproducibility and stability
must not depend on an external, mutable checkout.

Rules:

- **Pin, don't float.** The vendored tree tracks a specific upstream
  release. Upgrades are deliberate (`git subtree pull --prefix=genode`),
  recorded in the commit message, and verified before merging.
- **Local patches are first-class.** Any Sponge-specific change to the
  vendored tree is an ordinary commit on top of the subtree, and MUST be
  recorded in the patch ledger in `docs/11-environment.md` (what, where,
  why, and how to drop it when upstream absorbs the fix).
- **Do not re-implement Genode.** Vendoring is not a fork in spirit:
  keep Sponge OS-specific code in `repos/sponge/`, keep patches to the
  vendored tree minimal, and do not depend on Genode internal headers
  or private APIs from Sponge components.
- **Third-party port sources** (`genode/contrib/`) are NOT vendored.
  They are pinned by upstream `.port`/`.hash` files (SHA-256) and
  re-fetched via `tool/ports/prepare_port`. The exact port set and
  fingerprints are documented in `docs/11-environment.md`.

### 5.3 Caution With Automated Code Generation

When generating scaffolding or boilerplate:

- Mark "implement later" slots with explicit `// TODO(<name>): ...`
- Do not leave non-working code looking as if it works
- Empty implementations should log `Genode::warning("not implemented: ...")`

### 5.4 Documentation Matches Code

If you modify code, update the related documentation. Updating docs
without code, or vice versa, is not allowed. During early scaffolding,
docs may lead the code.

### 5.5 Privilege Escalation: Ask the User, Never `sudo`

If a task needs a host change that requires elevated privileges —
installing a system package, writing outside the repository and the
user's home, touching system services — an AI agent MUST NOT attempt
it with `sudo` (or any other privilege-escalation path). Stop and
**ask the user to run it**, presenting the exact command. Rationale:
`sudo` can block on an interactive password prompt, and host-level
mutations outside the repository exceed the mutation boundary spirit
of §3.5 even where technically possible. Read-only host inspection
never needs privileges; document the needed package in the docs and
request the install.

### 5.6 Orchestration Artifacts

The directory `.omo/` (containing `plans/`, `drafts/`, `evidence/`,
`ledger/`, `boulder`, and similar) is git-ignored orchestration scratch:
transient working state for the planning and execution loop, never part
of the durable record. Anything worth keeping must be promoted to a
tracked location.

- Plans and decision logs go to `docs/plans/`.
- Design-significant engineering evidence (regression writeups,
  capability analyses, capability-chain diagnoses, similar) goes to
  `docs/evidence/`.

Durable documentation under `docs/` and `README.md` must never cite paths
under `.omo/`. If such a reference exists, fix it: rewrite to the new
tracked path, or drop the cross-reference when the underlying artifact
is no longer durable. Raw per-run logs (large transcripts under
`.omo/evidence/`) are not promoted by default; only curated writeups that
explain why a design decision was made.

---

## 6. Questions and Discussions

For uncertain decisions, **don't guess** — record them:

- In code: a `// FIXME: <question>` comment
- In issues: a GitHub issue (after the repository goes public)
- In docs: add an "Open Design Questions" section to the relevant `docs/`

The "just make it run" attitude is rejected. Sponge OS must grow on
solid foundations only.