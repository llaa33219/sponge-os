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
| `./tool/bake`         | `tool/bake.mojo`         | Bake-profile inspector (`--list`, `--show`) + post-build P3 injector (`--img`, idempotent, sgdisk-verified, D15.5 size-budget) — Phase 15 W2b | ✅ works |
| `./tool/build`        | `tool/build.mojo`        | Top-level build wrapper (`prepare`, `ports`, `list`, `run`, `verify`) | ✅ works |
| `./tool/check-compile`| `tool/check_compile.mojo`| Structural sanity check for a component                      | ✅ works |
| `./tool/patches`      | `tool/patches.mojo`      | Patch ledger manager (`list`, `verify`, `export`, `drop`)    | ✅ works |
| `./tool/dist`         | `tool/dist.mojo`         | Alpha distribution media builder (`.img` + `.iso` into `var/dist/`) with `--storage {ahci,nvme,usb}` (Phase 12 + Phase 15 15-3) + `--bake-profile {minimal,desktop,none}` + `--firmware {bios,uefi}` (Phase 15 W2/W4; `--firmware uefi` produces a UEFI .img + skips the .iso; `--storage usb` is UEFI-only and produces the 15-3 USB-stick artifact) | ✅ works |
| `./tool/hw_compat`    | `tool/hw_compat.mojo`    | Read-only hardware-compatibility validator (`assert`, `help`) — validates `docs/15-hardware-compatibility.md` (Phase 12) | ✅ works |
| `./tool/mkdata`       | `tool/mkdata.mojo`       | Grow SPONGE-DATA (P4) onto an image/disk `.img` (docs/14 §4.3; idempotent) | ✅ works |
| `./tool/pkg_import`   | `tool/pkg_import.mojo`   | Host-side Genode depot → Sponge pkg/ repackager (Phase 7 todo 11) | ✅ works |
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
./tool/build verify              # Phase 12 host verification: ./tool/patches verify + ./tool/hw_compat assert (each must exit 0)
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

### dist (Phase 12 storage selector + Phase 15 W2 bake-profile + W4 firmware + 15-3 USB)
```bash
./tool/dist                              # build the default product media (env/config-driven)
./tool/dist --storage ahci               # default: AHCI product-media (current behavior)
./tool/dist --storage nvme               # opt-in: one-namespace NVMe product media
./tool/dist --storage usb                # Phase 15 15-3: USB-stick product media (UEFI only;
                                         #   see --firmware uefi for the QEMU-status note)
./tool/dist --storage usb --firmware bios # rejected before build: BIOS branch is ahci/nvme only;
                                         #   the BIOS-side USB-stick attach is the Phase 12
                                         #   `sponge-usb-boot.run` precedent, not a new product
./tool/dist --storage=unknown            # rejected before build, with usage line
./tool/dist --bake-profile minimal       # pass SPONGE_BAKE_PROFILE=minimal to make (smallest media)
./tool/dist --bake-profile desktop       # default: everyday-default media (every pre-staged package + Falkon)
./tool/dist --bake-profile none          # bake.inc escape hatch (today's hello-only behavior)
./tool/dist --bake-profile=bogus         # rejected before build, with usage line
./tool/dist --firmware bios              # default: BIOS/GRUB2 boot chain (only verified path)
./tool/dist --firmware uefi              # Phase 15 W4 + 15-3: UEFI/OVMF product media
                                         #   (structural-gate only; .img-only by design — no .iso;
                                         #   the QEMU UEFI boot is expected to hit the W1 OVMF
                                         #   core-init hang; real-hardware verification is 15-3)
./tool/dist --firmware=bogus             # rejected before build, with usage line
./tool/dist --print-only                 # print the make+env+mkdata commands without running
```

The `--storage {ahci,nvme,usb}` selector was added across Phase 12
W2 and Phase 15 15-3. `ahci` is the default and preserves the
current product-media behavior and artifact naming; `nvme` selects
the one-namespace NVMe product path
(`run/sponge-desktop-disk-nvme.run`); `usb` (UEFI only) selects the
15-3 USB-stick product path (`run/sponge-desktop-disk-uefi-usb.run`)
with a Tier-0 xHCI + `usb_block` storage chain. Invalid values fail
loudly with a concise English error and usage line before any build
starts. The `--storage usb --firmware bios` combination is rejected
with the precise reason (the BIOS-side USB-stick attach is the
Phase 12 `sponge-usb-boot.run` precedent which boots the existing
ISO from a USB stick — it is not a new product image). The ISO /
live media path is unchanged regardless of `--storage`.

The `--bake-profile {minimal,desktop,none}` selector was added in
Phase 15 W2 (docs/plans/phase15-real-hardware-boot.md D15.3/D15.4/D15.8).
The value is passed as `SPONGE_BAKE_PROFILE=<name>` in the environment
of every `make` invocation, which `run/bake.inc` reads inside each
product-media run script before `build_boot_image` (the primary
staging-time mechanism — staging-time is what makes bake uniform
across `.img`/`.iso`/UEFI media). `desktop` is the default; `none`
is `run/bake.inc`'s escape hatch that reproduces today's hardcoded
hello-only behavior (AGENTS.md §1.1). Invalid values fail loudly
before any build. The summary table prints the active profile.

The `--firmware {bios,uefi}` selector was added in Phase 15 W2
(D15.16) and **wired to live scenarios in W4**. `bios` is the
default and the only verified path on the 17ZD90N-VX7BK target
machine. `uefi` selects one of three Phase-15 run scenarios:

* `--storage ahci` (default) → `run/sponge-desktop-disk-uefi.run`
  (the Sponge-side UEFI recipe, D15.13; handcrafted GPT with
  P1=ESP + P2 absent + P3=GENODE + P4=SPONGE-DATA; the SATA
  UEFI envelope).
* `--storage nvme` → `run/sponge-desktop-disk-uefi-nvme.run`
  (the target-machine NVMe envelope, D15.1; the .img is the
  bootloader, a separate NVMe disk is the desktop).
* `--storage usb` (Phase 15 15-3) →
  `run/sponge-desktop-disk-uefi-usb.run` (the USB-stick product
  media; Tier-0 xHCI + `usb_block` storage chain with
  port-tolerant class: 0x3 / class: 0x8 policies on a single
  pc_usb_host serving both storage and HID input).

All three UEFI scenarios pass the host-side structural gates
(sgdisk -p / mdir / e2ls; the USB variant additionally asserts
`usb_block` presence in P3 /system/bin/). The QEMU boot of any
is EXPECTED to hit the W1 OVMF core-init hang — the scenario's
acceptance is host-side structural verification + honest gap
recording, NOT a QEMU boot PASS (D15.16, 2026-08-18 pivot). The
real-hardware diagnostic on 15-3 is the path that determines
whether the `uefi` flag gains a working implementation.
`--firmware uefi` produces ONLY the `.img` (no `.iso` — El
Torito is BIOS-only). `--storage usb` is UEFI-only; the
combination `--storage usb --firmware bios` is rejected with
the precise reason. Invalid firmware values (e.g.
`--firmware=bogus`) fail loudly before any build. The manual
equivalent for storage is the per-scenario
`make -C genode/build/x86_64 run/<scenario>.run` invocation;
for bake-profile, the same with `SPONGE_BAKE_PROFILE=<name>`
in the environment.

Reproducibility (R15.4): two consecutive `tool/dist --bake-profile <X>`
builds must produce images with the same staged-content manifest.
Byte-identical sha256 requires `mkfs.ext2` to seed deterministic
timestamps, which the vendored tree does not yet do; staged-content
manifest equality (the `bake_manifest.json` embedded in the image,
verified by `tool/bake.mojo`'s idempotency check) is the gate we
currently meet. See `docs/plans/phase15-real-hardware-boot.md` §"R15.4"
and the W1 evidence log.

### bake (Phase 15 W2b — post-build P3 injector)
```bash
./tool/bake --list                       # print profiles under pkg/bake/ (name + description)
./tool/bake --show minimal               # parse profile; print packages, config, theme, payload sizes
./tool/bake --show bogus                 # loud error (exit 2) on unknown profile or config_version!=1
./tool/bake --img <file> --profile minimal         # inject profile into .img's GENODE P3 (idempotent)
./tool/bake --img <file> --profile minimal --dry-run # print the staging plan without writing
./tool/bake --img <file> --profile desktop         # + Falkon + textedit + pdf_view payloads
./tool/bake --img <file> --profile bogus           # loud error before any disk write
./tool/bake help                          # full usage
```

`tool/bake.mojo` is the post-build half of the bake machinery (D15.8);
`run/bake.inc` is the staging-time half consumed by every product-media
run script. `--list` and `--show` are read-only and fast (no image
involved). `--img` extracts P3 to a temp file with `dd`, mutates the
ext2 with `e2cp`/`e2mkdir`/`e2ls` (e2tools — the same host-side tool
set `image/disk` uses), then writes it back with `dd ... conv=notrunc`
so any P4 (`tool/mkdata`'s SPONGE-DATA) survives the write.

The injector enforces:
* **D15.10 config_version=1** (loud error on anything else — bumps
  are breaking-only).
* **D15.5 size budget** (minimal ≤ 1 GiB, desktop ≤ 2 GiB total
  `.img`; refusal happens BEFORE any write, exit 2).
* **R15.3 T1 defense** (refuses to inject a package whose
  `<binary>` is neither in `pkg/<name>/payload/` nor in the image's
  `/system/bin` — baked metadata without its binary is the plan's
  trap T1).
* **Idempotency** (re-running with the same profile compares the
  current `bake_manifest.json` against what we would write —
  identical profile + packages + theme + payloads-present →
  no-op, exit 0).
* **sgdisk pre/post** (P3 must be present and named `GENODE`
  before; first/last sectors + name unchanged after — the
  misleading-success-output defense, mirroring `tool/mkdata`).

Exit codes: 0 ok (created or already-present), 1
usage/host-tool/io failure, 2 profile/validation/budget refusal
(mirrors `tool/mkdata`'s scheme).

The manual equivalent of every `--img` step is the documented
`dd → e2mkdir → e2cp → dd (NOTRUC) → sgdisk` sequence in
`docs/08-development.md §11` plus `tool/mkdata`'s partition-verification
check (AGENTS.md §3.5 control escape hatch). The fast-iteration
profile-switch story is: `./tool/dist` once, then `--img` many
times against the produced `.img` to compare profile outcomes.

### hw_compat (Phase 12 compatibility validator)
```bash
./tool/hw_compat assert          # validate docs/15-hardware-compatibility.md read-only; exit 0 on green
./tool/hw_compat help            # usage
```

The `assert` validator parses the hand-curated 5×5 surface matrix +
16-cell tuple ledger in `docs/15-hardware-compatibility.md`, resolves
every `scenario`/`evidence` path, requires the exact marker for every
`verified`/`smoke-only` cell, requires QEMU-version + `boot_time_seconds`
+ `budget_seconds` per cell, requires the USB-boot evidence to contain
`BIOS-side USB boot verified`, rejects any `target: real-hardware`
with exit 2 and the exact message `real hardware is a Phase 15
deliverable; not a Phase 12 cell`, and requires the matrix to have
exactly 4 verified, 1 smoke-only, and 11 gap cells. The tool is
**read-only** — there is no `generate`, `update`, `write`, or
auto-population path. See `docs/evidence/task-5-phase12-hw-compat.md`
for the per-failure-class validator receipts.

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
mojo tool/version_bump.mojo --minor    # bump 0.0.1 -> 1.0.0
mojo tool/version_bump.mojo --major    # bump 0.0.1 -> 1.0.0
```

### pkg-import
```bash
# Re-fetch the depot pkg + its transitive deps (one-time per pkg):
export GNUPGHOME="$PWD/var/scratch/gnupg"   # see docs/08 §12.1 setup
./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27

# Re-package into pkg/<name>/:
./tool/pkg_import cproc/pkg/qt6_textedit/2025-10-27 --bin-version 2025-10-12
```

Imports a Genode depot pkg archive into a Sponge `pkg/<name>/` directory:
reads the depot `runtime` metadata, downloads the matching `bin` archive
on demand, stages every payload the runtime declares, and writes
`metadata.xml` + `SOURCE` (depot pin + sha256). See
[docs/08-development.md §12](../docs/08-development.md) for the full
contract and the manual escape hatch.

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
