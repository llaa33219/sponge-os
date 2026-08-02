# tool/ — Sponge OS developer tooling

All host-side tooling for Sponge OS is written in **Mojo** (Modular's
language). Each tool ships as a `.mojo` source file plus a thin bash
launcher that detects the Mojo SDK and forwards arguments.

## Prerequisites

The Mojo SDK is declared in the repository's `pyproject.toml` and
pinned in `uv.lock`. Materialize it with `uv`:

```bash
uv sync
.venv/bin/mojo --version
```

(For a machine-wide install instead, see the official guide:
<https://mojolang.org/install/>.)

## Why Python interop

Mojo's stdlib is still maturing — `std.pathlib.Path` lacks `resolve()`,
`iter_dir()`, and `name()`. Where the stdlib falls short, these tools
use `Python.import_module("os")` and similar via `from std.python import
Python, PythonObject` (see the `mojo-python-interop` skill). This is
the idiomatic Mojo pattern for missing functionality.

## Tools

| Launcher              | Mojo source              | Purpose                                                      | Status |
|-----------------------|--------------------------|--------------------------------------------------------------|--------|
| `./tool/build`        | `tool/build.mojo`        | Top-level build wrapper (`prepare`, `ports`, `list`, `run`)  | ✅ works |
| `./tool/check-compile`| `tool/check_compile.mojo`| Structural sanity check for a component                      | ✅ works |
| `./tool/patches`      | `tool/patches.mojo`      | Patch ledger manager (`list`, `verify`, `export`, `drop`)    | ✅ works |
| (direct)              | `tool/gen_vct_config.mojo`| Generate a vct config-ROM XML from argv                     | ✅ works |
| (direct)              | `tool/version_bump.mojo` | Bump version in `include/sponge/version.h`                   | ✅ works |

## Mutation boundary

`tool/build prepare`, `tool/build ports`, and `tool/build run` automate
steps against the **vendored** Genode tree at `<repo>/genode/` (creating
`genode/build/x86_64`, appending a marker-delimited block to
`genode/build/x86_64/etc/build.conf`, downloading port sources into
`genode/contrib/`). AGENTS.md §3.5 explicitly permits this. The tools
never create, modify, or delete anything outside the repository —
anything beyond `<repo>/genode/` and the git-ignored `<repo>/var/`
scratch space remains forbidden, and the manual equivalent of every
automated step is documented in `docs/08-development.md` (the control
escape hatch).

## Usage

### build
```bash
./tool/build prepare             # create genode/build/x86_64 + managed build.conf block (idempotent)
./tool/build ports               # download the required Genode port sources (idempotent)
./tool/build list                # list run/*.run scenarios
./tool/build run <scenario>      # make -C genode/build/x86_64 run/<scenario>
./tool/build run --manual <scenario>  # only print the manual commands
```

`prepare` first switches the generated `#KERNEL ?= nova` / `BOARD ?= pc`
template lines in `genode/build/x86_64/etc/build.conf` to
`KERNEL ?= linux` / `BOARD ?= linux` **in place** (the template's own
`ifdef` blocks read those variables before any appended content would
take effect), then appends a marker-delimited block
(`# >>> sponge-os managed block >>>` ... `# <<< sponge-os managed block <<<`)
with the `REPOSITORIES +=` lines for `repos/sponge`, `repos/libports`,
and `repos/gems`, and `MAKE += -j<N>` (N = host CPU count). Re-running
`prepare` detects both edits and does not duplicate anything. Delete the
block to undo the appended part.

`ports` runs `genode/tool/ports/prepare_port` for: `libc stdcxx mesa zlib
libpng expat libdrm x86emu qoost qt6_api qt6_base sel4 sel4_tools grub2`
(needed for the sponge-de Qt6 GUI and base-sel4 boot). It reports per-port
ok/fail at the end and exits non-zero if any port failed. `prepare_port`
skips already-prepared ports, so re-runs are cheap.

`run` requires `genode/build/x86_64` to exist (run `prepare` first) and
propagates the make exit code.

### check-compile
```bash
./tool/check-compile src/vct
./tool/check-compile src/sponge-de
```

### patches
Manages the patch ledger (docs/11-environment.md §4): the Sponge-local
commits on top of the vendored Genode subtree. Read-only against the
repository — `drop` prints manual instructions, it never reverts.

```bash
./tool/patches list           # ledger rows + git resolution status
./tool/patches verify         # ledger vs git reality (exits non-zero on mismatch)
./tool/patches export <dir>   # write each patch as <dir>/<NN>-<slug>.patch
./tool/patches drop <n>       # print the manual revert steps for patch #n
```

The manual equivalent of each subcommand is documented in
docs/11-environment.md §4.1.

### gen-vct-config
Generates the `<config><args>...</args></config>` blob that vct expects
from `init` (see `docs/06-vct.md` §3). Useful for building test fixtures.

```bash
mojo tool/gen_vct_config.mojo install firefox --explain
# Output: <config><args><arg>install</arg><arg>firefox</arg><arg>--explain</arg></args></config>
```

### version-bump
```bash
mojo tool/version_bump.mojo --show     # print current version
mojo tool/version_bump.mojo --patch    # bump 0.0.1 -> 0.0.2
mojo tool/version_bump.mojo --minor    # bump 0.0.1 -> 0.1.0
mojo tool/version_bump.mojo --major    # bump 0.0.1 -> 1.0.0
```

## Why Mojo?

Sponge OS is Genode-based, so OS components are C++ (Genode's only stable
target). Host-side tooling is a different domain: we want a language with
Python-flavored ergonomics, fast startup, and a modern type system. Mojo
fits that role and is used consistently across all `tool/` scripts.

Components that ship inside the OS image (`repos/sponge/src/...`) remain
C++ and are NOT ported to Mojo. The hard boundary is "runs on the host
developer machine" (Mojo) vs "runs inside Genode" (C++).

## Launcher fallback

Every bash launcher in this directory resolves the mojo binary in order:
1. `mojo` on `PATH`.
2. `<repo>/.venv/bin/mojo` — the repo-local install materialized by
   `uv sync` from the committed `pyproject.toml` / `uv.lock`.
3. If neither exists, prints install guidance and exits `127`.

This keeps doc references like `./tool/build` and `./tool/check-compile`
stable regardless of whether Mojo is installed globally, in the repo
virtualenv, or not yet at all.
