# tool/ — Sponge OS developer tooling

All host-side tooling for Sponge OS is written in **Mojo** (Modular's
language). Each tool ships as a `.mojo` source file plus a thin bash
launcher that detects the Mojo SDK and forwards arguments.

## Prerequisites

Install the Mojo SDK as a Python package with `uv`:

```bash
# In any project dir (or globally)
uv add mojo --prerelease allow
# Then invoke the bundled mojo binary:
.venv/bin/mojo --version
```

Or follow the official install guide:
<https://mojolang.org/install/>

## Why Python interop

Mojo's stdlib is still maturing — `std.pathlib.Path` lacks `resolve()`,
`iter_dir()`, and `name()`. Where the stdlib falls short, these tools
use `Python.import_module("os")` and similar via `from std.python import
Python, PythonObject` (see the `mojo-python-interop` skill). This is
the idiomatic Mojo pattern for missing functionality.

## Tools

| Launcher              | Mojo source              | Purpose                                           | Status |
|-----------------------|--------------------------|---------------------------------------------------|--------|
| `./tool/build`        | `tool/build.mojo`        | Top-level build wrapper (`prepare`, `list`, `run`)| ✅ works |
| `./tool/check-compile`| `tool/check_compile.mojo`| Structural sanity check for a component           | ✅ works |
| (direct)              | `tool/gen_vct_config.mojo`| Generate a vct config-ROM XML from argv         | ✅ works |
| (direct)              | `tool/version_bump.mojo` | Bump version in `include/sponge/version.h`        | ✅ works |

## Usage

### build
```bash
./tool/build list                # list run/*.run scenarios
./tool/build prepare             # Phase 1: prints Genode setup steps
./tool/build run <scenario>      # Phase 1: prints manual make invocation
```

### check-compile
```bash
./tool/check-compile src/vct
./tool/check-compile src/sponge-de
```

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

Every bash launcher in this directory:
1. Checks for `mojo` on `PATH`.
2. If found, execs `mojo tool/<name>.mojo "$@"`.
3. If not found, prints install guidance and exits `127`.

This keeps doc references like `./tool/build` and `./tool/check-compile`
stable regardless of whether Mojo is installed yet.
