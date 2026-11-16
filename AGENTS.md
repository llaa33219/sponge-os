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
├── genode/                    # Genode base tree (git submodule -> sponge-os-genode, see §5.2)
│   ├── repos/                 # Upstream Genode repositories (base, os, libports, ...)
│   └── repos/sponge -> ../../repos/sponge  # Relative symlink wiring Sponge in
├── repos/
│   ├── sponge/                # Umbrella Sponge repository (Genode repo convention)
│   │   ├── src/               # Component sources
│   │   │   ├── vct/           # System management tool
│   │   │   └── sponge_launcher/ # Launcher
│   │   ├── lib/               # Shared libraries
│   │   ├── tool/              # Sponge-owned build helpers (compiler wrappers, ...)
│   │   └── include/sponge/    # Shared headers
│   ├── sponge-wm/             # WM trio, protocol, and native WM probes (submodule)
│   ├── sponge-de/             # DE shell, settings/providers, and DE probes (submodule)
│   └── sponge-compositor/     # Mojo-first compositor scaffold (submodule)
├── tool/                      # Build and development scripts
├── run/                       # Genode run scripts (scenario definitions)
└── var/                       # Local caches (git-ignored): qt6 host tools, distfiles
```

The Genode build system assumes a `repos/<name>/` layout. The umbrella
`repos/sponge` is wired through the committed relative symlink
`genode/repos/sponge -> ../../repos/sponge`. The three split repositories
are pinned as git submodules and registered directly through
`REPOSITORIES += $(SPONGE_ROOT)/repos/sponge-{wm,de,compositor}` in
`tool/build.mojo`'s managed `build.conf` block, so no new Genode fork
patch or extra symlink is needed. Clone with `git clone --recursive`
(or run `./tool/build prepare`, which initializes all submodules). See
`docs/08-development.md` for the build flow and `docs/11-environment.md`
for the full environment contract.

---

## 3. Coding Rules

### 3.1 C++ (Adapters and Existing Components)

New Sponge-owned components default to Mojo (§3.5, §3.6). C++ stays
first-class for existing components, for thin Genode adapters at the
Mojo/Genode boundary, and wherever a recorded reason applies. All C++
code in the repository follows these rules:

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

Every component directory must contain the following files. Two
layouts are valid: the C++ layout for existing components and any
component with a recorded C++ reason, and the Mojo-first layout for
new components (§3.6).

C++ component (existing convention, still valid):

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

Mojo-first component (`.mojo` sources plus a thin `main.cc` adapter):

```
src/<component>/
├── target.mk              # Genode build target definition
├── main.cc                # Thin C++ entry adapter (Component::construct glue)
├── <name>.mojo            # Mojo sources (see §3.5 and §3.6)
├── README.md              # Component description (optional)
└── ...                    # Component-specific sources
```

`target.mk` keeps the adapter C++ on `SRC_CC` and links the
Mojo-produced objects through `SRC_O` (Genode's `generic.mk` folds
`SRC_O` into the link set alongside `SRC_CC`; the objects come from
`mojo build --emit object`, whose experimental status is noted in
§3.6):

```makefile
TARGET   := <component_name>
SRC_CC   := main.cc          # thin C++ entry adapter only
SRC_O    := <name>.o         # Mojo-produced objects
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

### 3.5 Mojo (Preferred Default Language)

- **Default language**: Mojo (Modular's language) is the preferred and
  default language for Sponge-owned code, both host-side tooling and
  Genode/seL4 components. Language choice defaults to Mojo; C++ in
  Sponge-owned code is justified by §3.1 and §3.6 (adapters, existing
  components, recorded reasons). The binding decision record is
  `docs/plans/mojo-language-policy.md`.
- All scripts under `tool/` are written in **Mojo**. Each tool ships as
  a `.mojo` source file with a thin bash launcher that
  execs `mojo tool/<name>.mojo "$@"`.
- Tool scripts MAY automate setup steps against the **vendored** tree at
  `genode/` (creating build directories, writing `etc/build.conf`,
  running `prepare_port`) because that tree is part of this repository.
  Tool scripts MUST NOT create, modify, or delete anything **outside**
  the repository (e.g. an external Genode checkout, `/usr/local`,
  another home-directory path). Every automated step must also be
  documented as a manual step in `docs/08-development.md` (control
  escape hatch).
- **Initial in-OS subset**: Mojo code that runs inside Genode stays
  within the proven subset of `docs/evidence/mojo-ffi-probe.md`.
  Python interop and POSIX I/O are **forbidden** in this subset because
  neither is proven in the libc-free in-OS setting. Async and pure-Mojo
  Genode session plumbing are equally unproven: designs that need them
  probe them first or keep that seam in the thin C++ adapter (§3.6).
- Before writing any Mojo, load the `mojo-syntax` skill
  (`modular/skills@mojo-syntax`) and follow the current-document
  workflow of §3.6. Mojo evolves rapidly and pretrained patterns go
  stale.

### 3.6 Mojo Components and Migration

- **Native Mojo over wrappers**: when a feature can be written natively
  in Mojo or obtained by binding an existing C/C++ library, prefer the
  native Mojo implementation where feasible. Feasibility is judged
  against the proven subset in `docs/evidence/mojo-ffi-probe.md`, not
  against wishlists. A binding stays legitimate when the subset cannot
  express the work.
- **New components default to native Mojo.** A new component in the
  umbrella `repos/sponge/` or a split Sponge-owned repository starts in
  Mojo unless a recorded reason says otherwise (unsupported subset, a
  Genode framework surface that only C++ can serve, or a dependency
  that is itself C++).
- **Existing Sponge-owned C/C++ SHOULD migrate incrementally** to Mojo
  where the required subset is supportable, with run scenarios green at
  every step. Each migrated unit lands as its own logical change
  (§4.2) and keeps the tree bootable. Migration is opportunistic and
  per-unit, triggered by touching that unit or by a dedicated migration
  task. The Qt-based Sponge DE carries **no** immediate migration
  mandate.
- **Exceptions, not migration targets**: thin C++ Genode adapters (the
  `Component::construct` entry glue and the `SRC_O` link seam of §3.2)
  and everything under `genode/` plus upstream Qt code stay as they
  are. Pure-Mojo Genode session plumbing is unproven, so the C++
  adapter is the honest seam until a probe receipt says otherwise.
- **No big-bang rewrite.** There is no repository-wide C++-to-Mojo
  conversion event, no freeze on C++ commits, and no deadline that
  forces one. §3.5 and this section govern new and existing code so the
  tree stays green throughout.
- **Current-document workflow, mandatory before writing any Mojo**:
  (1) load the `mojo-syntax` skill; (2) consult the current official
  Mojo documentation (<https://mojolang.org/docs/>) for the constructs
  in play; (3) write the code; (4) verify by build and by the relevant
  run scenario (§4.2). Pretrained Mojo knowledge is treated as stale by
  default.
- **Honest caveats (binding)**: `mojo build --emit object` is
  experimental; KGEN runtime symbols are version-sensitive (pin the
  toolchain, re-probe on every bump); no freestanding stdlib profile
  exists; async, Python interop, POSIX I/O, and pure-Mojo Genode
  session plumbing are unproven and must not be claimed or relied on
  without their own probe receipts.
- Evidence and policy:
  [`docs/evidence/mojo-ffi-probe.md`](docs/evidence/mojo-ffi-probe.md)
  (what is proven on seL4 QEMU: `mojo-ffi-probe: PASS sum=42` and
  `mojo-ffi-probe: PASS add=42 list-sum=45` behind a `Genode::Heap`
  KGEN shim, not full Mojo runtime coverage) and
  [`docs/plans/mojo-language-policy.md`](docs/plans/mojo-language-policy.md)
  (binding decision record).

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

### 5.2 Genode Is Vendored as a Submodule, Pinned, and Never Re-implemented

The Genode source tree lives in the dedicated fork repository
**`github.com/llaa33219/sponge-os-genode`** (upstream 26.08 + the
Sponge patch set) and is consumed here as the `genode/` **git
submodule**, pinned to a specific fork commit. (History note:
pre-2026-10-17 this was a `git subtree`; the split kept the tree
byte-identical. The old history resolves on the
`legacy/main-pre-genode-split` branch.) Rationale: reproducibility and
stability must not depend on an external, mutable checkout.

Rules:

- **Pin, don't float.** The submodule tracks a specific fork commit.
  Upgrades are deliberate (merge upstream in the fork, record it in the
  fork's commit message, then bump the submodule pin here in a commit
  that says so), and verified before merging.
- **Local patches are first-class.** Any Sponge-specific change to the
  fork tree is an ordinary commit on the fork's main, and MUST be
  recorded in the patch ledger in `docs/11-environment.md` (what, where,
  why, and how to drop it when upstream absorbs the fix). The submodule
  pin is bumped here to bring it in.
- **Do not re-implement Genode.** Vendoring is not a fork in spirit:
  keep Sponge OS-specific code in `repos/sponge/`, keep patches to the
  fork tree minimal, and do not depend on Genode internal headers
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

### 5.7 Living Document Structure

- Living reference documents target **fewer than 500 lines**. When a file
  exceeds that size or becomes difficult to maintain, split it into a
  concise parent and topic-focused subdocuments.
- Keep the original parent path and every original numbered `## N.`
  heading as a short stub with a child link, so existing `docs/NN.md §N`
  references remain meaningful. Children link back to their parent.
  Preserve moved content verbatim unless a separate editorial change is
  justified; do not duplicate full status narratives across files.
- Historical plans and evidence are exempt from mechanical splitting:
  their integrity as decision and receipt records comes first.
- Machine-consumed documents keep their required content in the parent:
  the `docs/11-environment.md` patch-ledger table, the
  `docs/15-hardware-compatibility.md` cross-product ledger and summary,
  and `docs/09-roadmap.md` §11.1. Run the relevant validators after any
  restructuring.
- Verify content preservation, relative links and anchors, file sizes,
  and `./tool/build verify` before committing a document split.

---

## 6. Questions and Discussions

For uncertain decisions, **don't guess** — record them:

- In code: a `// FIXME: <question>` comment
- In issues: a GitHub issue (after the repository goes public)
- In docs: add an "Open Design Questions" section to the relevant `docs/`

The "just make it run" attitude is rejected. Sponge OS must grow on
solid foundations only.