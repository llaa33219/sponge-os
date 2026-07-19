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
├── repos/
│   └── sponge/                # The Sponge OS repository itself (Genode repo convention)
│       ├── src/               # Component sources
│       │   ├── vct/           # System management tool
│       │   ├── sponge-de/     # Desktop environment
│       │   └── sponge_launcher/ # Launcher
│       ├── lib/               # Shared libraries
│       └── include/sponge/    # Shared headers
├── tool/                      # Build and development scripts
└── run/                       # Genode run scripts (scenario definitions)
```

The Genode build system assumes a `repos/<name>/` layout. This repository
is designed to be placed (or symlinked) under a Genode source tree's
`repos/` directory. See `docs/08-development.md` for details.

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
- Tool scripts MUST NOT mutate Genode source-tree state directly. They
  may print the next manual step (`tool/build prepare`, `tool/build run`)
  or modify Sponge-owned files only (`tool/version_bump`).
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

### 5.2 Genode Is an External Dependency

This repository does not fork or re-implement Genode. Use Genode upstream
as a dependency, and keep only Sponge OS-specific code in this repo.
Do not depend on Genode internal headers or private APIs.

### 5.3 Caution With Automated Code Generation

When generating scaffolding or boilerplate:

- Mark "implement later" slots with explicit `// TODO(<name>): ...`
- Do not leave non-working code looking as if it works
- Empty implementations should log `Genode::warning("not implemented: ...")`

### 5.4 Documentation Matches Code

If you modify code, update the related documentation. Updating docs
without code, or vice versa, is not allowed. During early scaffolding,
docs may lead the code.

---

## 6. Questions and Discussions

For uncertain decisions, **don't guess** — record them:

- In code: a `// FIXME: <question>` comment
- In issues: a GitHub issue (after the repository goes public)
- In docs: add an "Open Design Questions" section to the relevant `docs/`

The "just make it run" attitude is rejected. Sponge OS must grow on
solid foundations only.