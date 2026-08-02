# 08 - Development Guide

> How to develop, build, run, and test Sponge OS against the vendored
> Genode tree.
>
> The **environment contract** (what is vendored, what is pinned, what
> is git-ignored, and the manual fallback for every automated step)
> lives in [`docs/11-environment.md`](11-environment.md). This document
> is the workflow on top of that contract.

---

## 1. Prerequisites

### 1.1 The Vendored Genode Tree

Sponge OS no longer depends on an external Genode checkout. The
repository carries Genode 26.05 inside it as a `git subtree` at
`genode/`, pinned to upstream commit
`492a51024217fe74ccee1ebdfb81be97046b43eb` (the
`codeberg.org/genodelabs/genode` tag `26.05^{}`). Every patch we have
applied on top is recorded in the patch ledger in
[`docs/11-environment.md`](11-environment.md) §4. The bridge into
Sponge OS is the committed relative symlink
`genode/repos/sponge -> ../../repos/sponge`, which lets the Genode
build system discover `repos/sponge/` from inside the vendored tree
without any per-developer wiring.

Two facts that come out of this:

- **No `git clone genode`.** A contributor only ever clones
  `sponge-os`. The full Genode 26.05 source tree comes with it
  (~50 MB working tree, ~150 MB with the subtree's git history).
- **No `ln -s repos/sponge`.** The symlink is committed. Moving the
  clone to a different path under a different home directory just
  works.

### 1.2 The Genode Toolchain

The Genode toolchain is **not** vendored. Genode's build system
enforces a hard `$(error)` if the compiler does not report exactly the
expected version (`GCC 14.2.0`, `binutils 2.44`), so installing the
toolchain out of band is non-negotiable.

Install once per machine:

```bash
# Arch / CachyOS: AUR (maintained by a Genode Labs employee)
yay -S genode-toolchain-bin

# Every other distro: official prebuilt tarball
wget https://genode.org/files/tool-chain/genode-toolchain-25.05.tar.xz
sudo tar xPf genode-toolchain-25.05.tar.xz
ls /usr/local/genode/tool/25.05/bin/genode-x86-gcc    # sanity check
ls -l /usr/local/genode/tool/current                 # must point at 25.05
```

The `-P` flag to `tar` is load-bearing: it preserves the absolute
paths inside the archive (`./usr/local/genode/...`) so the toolchain
ends up exactly where the Genode build system expects. Without `-P`,
you get a stray `genode/` directory in the cwd and a confusing build
error.

There is **no 26.05 toolchain tarball**. Genode uses a 2-year
toolchain cycle, and 25.05 is the official pairing for 26.05. See
[`docs/11-environment.md`](11-environment.md) §3 for the full pin
table and the rationale.

### 1.3 Host Packages

```bash
# Arch
sudo pacman -S tcl expect qemu-system-x86 cmake ninja make rpcsvc-proto
# Debian / Ubuntu
sudo apt install tcl expect qemu-system-x86 cmake ninja-build build-essential libc-dev-bin
```

`tcl` and `expect` are needed by Genode's run tool; `cmake` and
`ninja` are needed for the seL4 kernel build and the Qt6 library
build; `qemu-system-x86` is needed for any non-base-linux run;
`rpcgen` (from `rpcsvc-proto` / `libc-dev-bin`) is needed by the
`libc` port's prepare step.

### 1.4 Mojo SDK (host-side tooling only)

The host-side tooling under `tool/` is written in Mojo
([`tool/README.md`](../tool/README.md) for tool-by-tool usage).
The dependency is declared in the committed `pyproject.toml` and
pinned in `uv.lock`, so installing it is:

```bash
uv sync
.venv/bin/mojo --version
```

Mojo runs only on the host developer machine. Nothing under
`repos/sponge/src/` ever links Mojo; the language boundary is
"Mojo for the host, C++ for Genode".

---

## 2. Repository Structure Overview

```
sponge-os/
├── README.md              # User-facing introduction
├── AGENTS.md              # Contribution guidelines (top of doc hierarchy)
├── docs/                  # Design documents (incl. this file and 11-environment.md)
├── genode/                # Vendored Genode 26.05 (git subtree, pinned)
│   ├── repos/             # Upstream Genode repos
│   │   └── sponge -> ../../repos/sponge  # committed relative symlink
│   └── tool/, etc/, ...   # Genode's own helpers
├── repos/
│   └── sponge/            # The Sponge OS repo (Genode repo convention)
│       ├── src/           # Component sources (vct, sponge-de, sponge_launcher)
│       ├── lib/           # Shared libraries
│       ├── include/sponge/# Shared headers
│       ├── tool/          # Sponge-owned compiler wrappers (genode-x86-{gcc,g++}-wrapper)
│       └── run/           # Relative symlinks → ../../../run/ (one per .run scenario)
├── tool/                  # Build and dev helpers (Mojo: build, check-compile, ...)
├── run/                   # Genode run scenarios (sponge-minimal, sponge-de, ...)
└── var/                   # Local caches (git-ignored): qt6 host tools, distfiles
```

For the meaning of each directory and which paths are git-ignored, see
`AGENTS.md` §2 and [`docs/11-environment.md`](11-environment.md) §2.

---

## 3. Build

### 3.1 One-command flow (default)

The wrapper script does the whole setup. After `toolchain + clone +
uv sync` are done (§1):

```bash
./tool/build prepare               # writes genode/build/x86_64/etc/build.conf
./tool/build ports                 # fetches all third-party port sources
./tool/build run sponge-minimal    # builds and runs the minimal scenario
```

Each of those commands has a manual equivalent below. Per AGENTS.md
§1.1 the **automation is the default**, the **control door is always
open**, so the manual form is the canonical reference and the wrapper
just glues it together.

### 3.2 Manual flow (the canonical reference)

The wrapper calls into the Genode build system as follows. Run these
from the repo root unless noted.

```bash
# 1. Create the build directory inside the vendored tree.
cd genode && ./tool/create_builddir x86_64
cd build/x86_64

# 2. Switch the kernel from the default pc/nova target to base-linux
#    for the fastest developer feedback.
sed -i 's/^#KERNEL ?= nova/KERNEL ?= linux/' etc/build.conf
sed -i 's/^BOARD ?= pc/BOARD ?= linux/'    etc/build.conf

# 3. Register Sponge OS as a repository.
echo 'REPOSITORIES += $(GENODE_DIR)/repos/sponge' >> etc/build.conf

# 4. Enable parallel builds (the build system does not default to it).
echo "MAKE += -j$(nproc)" >> etc/build.conf

# 5. Fetch the third-party port sources (one-time cost, ~8.5 GB).
#    prepare_port skips ports that are already prepared.
cd ../..
./tool/ports/prepare_port libc stdcxx mesa zlib libpng expat libdrm \
                          x86emu qoost qt6_api qt6_base sel4 sel4_tools grub2

# 6. Build and install the Qt6 host tools into var/qt6-host-tools
#    (~6.0 GB build tree under genode/contrib/qt6-host-*, one-time).
#    tool_chain_qt6 is a makefile, not an executable; it fetches the
#    qt6-host port itself. The INSTALL_LOCATION/SUDO overrides keep
#    the install inside the repository. import-qt6.inc points at this
#    directory by default (patch #3 in docs/11-environment.md).
cd genode
make -f tool/tool_chain_qt6 build MAKE_JOBS=$(nproc)
make -f tool/tool_chain_qt6 install \
    INSTALL_LOCATION="$PWD/../var/qt6-host-tools" SUDO=
cd ..

# 7. Build + run the minimal scenario on the Linux host (no QEMU needed).
make -C genode/build/x86_64 run/sponge-minimal
```

The `./tool/build prepare` wrapper does steps 1 to 4; `./tool/build ports`
does step 5; `./tool/build run <scenario>` does step 7.

Expected output (base-linux, no QEMU) ends with:

```
Genode 26.05
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] vct 0.0.1-pre-alpha
[init -> vct] Sponge OS codename: Archaeocyte
Run script execution successful.
```

### 3.3 Per-component verification (development)

To check whether a specific component's structure is complete
(presence of `target.mk`, `main.cc`, etc.), use `tool/check-compile`:

```bash
./tool/check-compile src/vct
./tool/check-compile src/sponge-de
```

This script only checks file presence; it does not compile anything.
For real compilation feedback, run the Genode build directly:

```bash
make -C genode/build/x86_64 vct                 # build just the vct binary
make -C genode/build/x86_64 sponge-de           # build the DE (needs Qt6 deps)
```

### 3.4 Build cache

The Genode build system supports incremental builds. Components that
have not changed are not recompiled, and the Qt6 host tools installed
at `var/qt6-host-tools/` plus the prepared port sources in
`genode/contrib/` are reused across runs.

### 3.5 Kernel selection

`genode/build/x86_64/etc/build.conf` exposes `KERNEL` and `BOARD`.
The default written by `./tool/build prepare` is `linux`/`linux`,
which boots Genode's `core` as a Linux ELF process and skips the
microkernel build. Other values:

| `KERNEL` | Use |
|---|---|
| `linux`  | Developer feedback (no QEMU, no kernel build). |
| `sel4`   | Production target. Requires the seL4 kernel build (cmake + ninja). |
| `hw`     | Genode's own microkernel. Requires QEMU for x86_64. |
| `nova`   | NOVA hypervisor. |

`BOARD` is the per-kernel board flavor; `pc` for seL4, `linux` for
the Linux dev target, etc.

### 3.6 Building for `base-sel4` (production target)

Switching to seL4 is a one-line `KERNEL`/`BOARD` change in
`etc/build.conf`, but it pulls in three extra ports and a handful of
host-side Python modules that the seL4 kernel build needs. The full
one-time setup:

```bash
# 1. Install the seL4 build's Python deps (uv-managed).
uv pip install future jinja2 ply six lxml pyfdt jsonschema

# 2. Fetch the seL4 kernel, seL4 tools, and grub2 bootloader ports.
#    grub2 is required because base-sel4 boots from a GRUB ISO.
#    './tool/build ports' prepares the full set (sel4 included) and
#    skips anything already prepared.
./tool/build ports
# (manual equivalent: cd genode && ./tool/ports/prepare_port sel4 sel4_tools grub2)

# 3. Switch the kernel in genode/build/x86_64/etc/build.conf:
#       KERNEL ?= sel4
#       BOARD  ?= pc
#    On a headless server also comment out `QEMU_OPT += -display sdl`
#    so QEMU does not require an X display. Run scripts that need
#    keyboard input pass `-nographic` themselves.

# 4. Run the scenario.
./tool/build run sponge-minimal
```

Expected output (QEMU):

```
Genode 26.05
699 MiB RAM and 523288 caps assigned to init
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] vct — Very Convenient Tool
[init -> vct] version: 0.0.1-pre-alpha (Archaeocyte)
Run script execution successful.
```

**seL4-specific RAM requirement.** `base-sel4` on QEMU needs **at
least 1 GiB** of guest RAM. seL4 reserves a SKIM window (Meltdown
mitigation) plus its own kernel structures; with 256 MiB the kernel
fails at boot with `seL4 failed assertion 'load_paddr' at
boot_sys.c:120` because it cannot find a contiguous physical region
large enough for the ~30 MiB boot module. The scenario file sets
`-m 1G` accordingly.

### 3.7 Why Qt6 needs special handling (one paragraph)

The Qt6 cross-build invokes CMake, whose compiler introspection leaks
host include paths (`/usr/include`, `/usr/local/include`) into the
build and breaks it against Genode headers. The Sponge-side fix is
patch #4 in the patch ledger (see
[`docs/11-environment.md`](11-environment.md) §4): the Qt6 cmake
invocation runs through Sponge-owned wrappers under
`repos/sponge/tool/` that strip leaked host `-I/-isystem` paths,
clear `CMAKE_*_IMPLICIT_INCLUDE_DIRECTORIES`, and disable the
`system_doubleconversion` / `system_md4c` Qt features that pull in
unavailable third-party dependencies. The wrappers themselves are
Sponge-owned files at `repos/sponge/tool/`, not upstream patches.

### 3.8 base-sel4 interactive GUI scenario (drivers_interactive-pc)

`run/sponge-de-sel4-interactive.run` boots seL4 on QEMU with the real
interactive-PC driver set (vesa_fb, ps2, usb_hid, pc_usb_host,
event_filter, platform, acpi, pci_decode) and a usb-tablet absolute
pointer — closing roadmap §11 deferred item 1. It is *base-sel4 only*
(`assert {[have_spec sel4]}`); on base-linux use `run/sponge-de.run`
(fb_sdl) instead.

To run it, switch the kernel and prepare two extra host tools the
driver build needs:

```bash
# 1. Switch KERNEL/BOARD in genode/build/x86_64/etc/build.conf:
#       KERNEL ?= sel4
#       BOARD  ?= pc
#    (and, on a headless host, keep `# QEMU_OPT += -display sdl`
#    commented so QEMU does not need an X display).

# 2. The dde_linux USB stack (usb_hid/pc_usb_host) builds the Linux
#    kernel, whose timeconst.h generation needs `bc`; and the seL4
#    kernel cmake needs `pyyaml`. Neither is in §1.3's host-package
#    list yet. See docs/11-environment.md §10 for the full new list.
uv pip install --python .venv/bin/python pyyaml          # seL4 kernel build
# `bc`: distro package, or a portable busybox `bc` on PATH
#       (e.g. download busybox, symlink `bc -> busybox`).

# 3. Run it (prepare_port for linux + jitterentropy is idempotent and
#    already wired into `./tool/build ports`).
./tool/build ports
./tool/build run sponge-de-sel4-interactive
```

Expected headless result (serial log, both assertions matched):

```
[init -> drivers -> fb] using 1024x768 (1024x768)
[init -> drivers -> usb_hid] Connected device: input0 (QEMU QEMU USB
        Tablet at usb-usbbus-0/input0) POINTER
Run script execution successful.
```

**What is and is not verified.** The scenario verifies the *driver
stack* (the actual roadmap item): vesa_fb maps the physical VESA
framebuffer at 1024x768 and is consuming nitpicker's `Capture` session,
and usb_hid binds the usb-tablet as an absolute pointer. Sponge DE's
own Qt6 window is **not** visible yet because the Qt6/Mesa (EGL)
initialization hangs on base-sel4 (`docs/09-roadmap.md` §11.1). The
scenario's run script documents the intended stronger verification
(probe `inject=no` Capture check + QMP `input-send-event` usb-tablet
click) that drops in once Qt6-on-sel4 rendering is fixed. The
interactive escape hatch is `run_genode_until forever` plus
`-display sdl` — the host mouse then reaches the guest through the
usb-tablet bound above.

**Qt6 rebuild on kernel switch.** Switching `KERNEL` between `linux`
and `sel4` rebuilds the Qt6 shared libraries because the Genode `SPECS`
change. If a previous kernel's `genode/build/x86_64/qt6/base/` cmake
tree exists, the rebuild can fail at the `libQt6Widgets` link with
stale-autogen undefined references; delete
`genode/build/x86_64/qt6/base/` once and let it rebuild cleanly.
They exist because the previous host location (`/tmp/opencode/bin/`)
was `tmpfiles`-swept.

---

## 4. Run Scripts (Scenarios)

Each file in `run/` defines one boot scenario. A scenario defines:

- Which kernel base to use.
- Which components to include.
- The configuration of each component (resources, routing).
- (For automated tests) the verification script.

Scenarios:

| Scenario | Purpose | Status |
|---|---|---|
| `run/sponge-minimal.run` | Minimum Sponge OS boot (init + vct) | ✅ base-linux, ✅ base-sel4 |
| `run/sponge-vct-help.run` | `vct help` prints the command summary | ✅ |
| `run/sponge-vct-version.run` | `vct version` prints the version | ✅ |
| `run/sponge-vct-status.run` | `vct status` reads live init state through a sub-init + `report_rom` relay | ✅ |
| `run/sponge-vct-component-list.run` | `vct component list` lists the live component tree | ✅ |
| `run/sponge-de.run` | Sponge DE single-window demo (nitpicker + fb_sdl + sponge-de) | 🟡 (Phase 3 in progress) |
| `run/sponge-de-sel4-interactive.run` | base-sel4 interactive-PC driver set (vesa_fb/ps2/usb_hid/event_filter/platform/acpi/pci_decode) under QEMU with usb-tablet absolute pointer | ✅ driver stack; 🟡 Qt6-on-sel4 rendering (see §3.8) |

The source-of-truth `.run` files live in `run/`. The same files are
discoverable through `repos/sponge/run/` via **committed relative
symlinks** (one per scenario) so the Genode repo discovery picks them
up regardless of which directory the build was launched from.

### 4.1 Anatomy of a working run script

The minimum reliable pattern, learned from `hello_tutorial` and
applied to `sponge-minimal.run`:

```tcl
# 1. Build list MUST include lib/ld, or the dynamic linker is missing
#    from the boot image and every component fails to load with
#    "environment ROM session denied (label=ld.lib.so)".
build { core lib/ld init <your-component> }

create_boot_directory

# 2. install_config accepts either raw XML or the '+' shorthand form.
install_config {
config
+ parent-provides
  + service LOG
  + service PD
  + service CPU
  + service ROM
+ default-route
  + any-service
    + parent
    + any-child
+ default | caps: 200 | ram: 8M
+ start <your-component>
  + config
    + args
      + arg status
-
}

# 3. [build_artifacts] is a run-tool helper that automatically stages
#    every binary the build step produced, including init.xsd (needed
#    for config validation) and ld.lib.so. Do NOT list them manually.
build_boot_image [build_artifacts]

append qemu_args " -nographic -m 1G "

# 4. run_genode_until matches a regex in the boot log within N seconds.
#    (sponge-minimal.run matches the vct codename line.)
run_genode_until {.*Sponge OS codename:.*Archaeocyte.*} 30
```

For how to write run scripts in general, see the
[Genode run documentation](https://genode.org/documentation/developer-resources/run).

### 4.2 Running a scenario

```bash
./tool/build run <scenario>
# Manual equivalent:
make -C genode/build/x86_64 run/<scenario>
```

The run tool builds the requested binaries, stages the boot directory
under `genode/build/x86_64/var/run/<scenario>/`, runs the kernel
(`./core` for base-linux, QEMU for base-sel4 / base-hw), and watches
the log for the success regex. On success it prints
`Run script execution successful.` and exits 0.

### 4.3 Kernel matrix

The same `sponge-minimal.run` boots on both kernels:

| Kernel | Driver | RAM minimum | Notes |
|---|---|---|---|
| `linux` | `./core` directly (no QEMU) | 8 MiB for the default route | Fastest iteration. `core` runs as a Linux ELF process; no QEMU, no kernel build. |
| `sel4`  | QEMU (`-nographic -m 1G`) | 1 GiB guest RAM | Production target. Requires `prepare_port sel4 sel4_tools grub2` plus the seven Python modules (`§3.6`). |
| `hw`    | QEMU | 128 MiB | Genode's own microkernel. Not yet wired into Sponge OS scenarios. |
| `nova`  | QEMU | 256 MiB | NOVA hypervisor. Not yet wired into Sponge OS scenarios. |

---

## 5. Testing

### 5.1 Automated tests

The verification logic embedded in Genode's run scripts is the default
testing tool. A `run/*.run` file uses `run_script` and pattern matching
(`expect`) to verify the expected output. The four vct command
scenarios (`sponge-vct-help.run`, `sponge-vct-version.run`,
`sponge-vct-status.run`, `sponge-vct-component-list.run`) all rely on
this.

### 5.2 Component unit tests

Logic that is unit-testable (for example, vct's argument parser) is
extracted into separate headers so it can be tested outside Genode. A
unit-test framework has not been chosen yet (see `docs/09-roadmap.md`).

### 5.3 Manual verification

- Boot success: the log shows the scenario's success regex (for
  `sponge-minimal`, the `Sponge OS codename: Archaeocyte` line)
  followed by `Run script execution successful.`
- `vct --help` output verification.
- (Sponge DE) check that the window appears on the SDL framebuffer
  output.

---

## 6. Coding Workflow

Summary of `AGENTS.md` §4:

1. **Before changing anything**: read the relevant `docs/` and identify
   which philosophy or constraint is touched.
2. **Make the change**: update code and (if needed) documentation
   together.
3. **Verify**: build, run scenarios, and unit tests pass.
4. **Commit**: one logical change equals one commit. Conventional
   commits format.
5. **PR**: include in the PR body (a) which philosophy is satisfied,
   (b) the number of user steps, and (c) the test results.

---

## 7. Coding Rules Summary

(Excerpt from `AGENTS.md` §3)

- **C++ standard**: follow the version Genode supports (C++17
  recommended).
- **Naming**: classes in `PascalCase`, functions in `snake_case`,
  members in `snake_case_`.
- **Header guards**: `#pragma once`.
- **Smart pointers**: prefer `Genode::Constructible<T>` and
  `Genode::Allocator_avl`.
- **Exceptions**: none (Genode builds with exceptions disabled).
  Constructor failures are propagated through explicit initialization
  patterns.
- **Genode namespace**: `size_t`, `addr_t`, `uint32_t` etc. are NOT in
  the global namespace on real Genode. Always qualify as
  `Genode::size_t`, `Genode::addr_t`. A standalone test harness that
  `using`'s them into global scope will produce code that fails on
  the real framework.
- **Component entry points**: `Component::construct` and (if
  overridden) `Component::stack_size` are declared exactly as the
  framework expects. `stack_size` returns `Genode::size_t`, not bare
  `size_t`.
- **Prohibited**: type bypass, empty catch, monolithic components,
  capability bypass.

---

## 8. Contribution Workflow

### 8.1 Filing Issues

- (Use a separate channel until the repository becomes public.)
- An issue should include: symptoms, reproduction steps, expected
  behavior, actual behavior, environment information.

### 8.2 Submitting a PR

- Prefer small PRs.
- PR body template:
  ```
  ## Summary of changes
  ...
  ## Philosophies satisfied
  - [ ] Convenience: ...
  - [ ] Control: ...
  - [ ] Automation: ...
  ## Number of user steps
  This task asks the user for N interactions.
  ## Tests
  ...
  ```

### 8.3 Review Criteria

- Does the code follow the hard rules in `AGENTS.md`?
- Does the documentation match the code?
- Are tests included?
- Did the number of user steps grow (a convenience regression)?
- For patches against the vendored `genode/` tree, is the patch
  recorded in [`docs/11-environment.md`](11-environment.md) §4 with a
  reason and a "drop when" note?

---

## 9. Documentation Rules

- All user-facing documentation is in English (see `AGENTS.md` §1.3).
- Code comments are in English (consistent with identifiers).
- New components are added to the inventory in
  `docs/04-components.md`.
- Design changes update the relevant `docs/` first
  (see `AGENTS.md` §4.1).
- Changes to the vendored tree, the third-party port set, or the
  environment contract are documented in
  [`docs/11-environment.md`](11-environment.md).

---

## 10. Debugging

- Log output through Genode's `LOG` session (`Genode::log`,
  `Genode::warning`, `Genode::error`).
- When a run script executes, the LOG output is shown on the console.
- (Planned) `vct --verbose` will print backend communication in
  detail.
- (Planned) Leitzentrale will show component state in real time.
- For `base-sel4` boot failures, see the `-m 1G` lesson in §3.6 and
  the SKIM-window explanation in
  [`docs/11-environment.md`](11-environment.md) §7.2.
- For Qt6 build failures, see the patch-ledger entry for patch #4 in
  [`docs/11-environment.md`](11-environment.md) §4.

For difficult debugging issues, first re-read the principles in
`AGENTS.md` §5 "AI Agent-Specific Guidelines" and
`docs/02-philosophy.md`.

---

## 11. Building Distribution Media (Alpha)

This section produces the two installable Alpha media artifacts in
`var/dist/`:

```
sponge-os-0.1.0-alpha-x86_64-sel4.img   (GPT disk image, image/disk)
sponge-os-0.1.0-alpha-x86_64-sel4.iso   (El Torito ISO,   image/iso)
```

Each artifact ships with a `<name>.sha256` sidecar in the
`<hash>  <filename>` format that `sha256sum -c` consumes directly.
Both media boot (in QEMU) to the same `alpha-probe: PASS` marker the
unified desktop scenario gates on — the media are already boot-proven
by `run/sponge-alpha.run`'s `run_genode_until` call inside the make
invocation; nothing re-verifies them post-copy.

### 11.1 One-command flow (default)

```bash
./tool/dist
```

The wrapper ([`tool/dist`](../tool/dist) → [`tool/dist.mojo`](../tool/dist.mojo)):

1. Pre-flight checks every host tool the Genode run framework's
   image plugins invoke (`xorriso`, `sgdisk`, `mcopy`, `e2cp`,
   `e2mkdir`, `mkfs.ext2`, `mkfs.vfat`, `resize2fs`) and prints the
   exact `apt install` line for any missing one, exiting non-zero
   **before** any build runs (loud-and-early failure).
2. Runs the disk-image build, then the ISO build, sequentially,
   streaming each `make` invocation's output. A non-zero make exit
   propagates as the tool's exit code.
3. Cleans `genode/build/x86_64/var/run/sponge-alpha*` between the two
   modes so neither mode's staged boot directory pollutes the other.
4. Copies the artifacts to `var/dist/` with the release names and
   writes the `.sha256` sidecars.
5. Prints a summary table with artifact sizes and sha256 prefixes.

`./tool/dist` never boot-verifies the media itself (the run framework
already does that during the make), and never touches anything
outside the repository (AGENTS.md §3.5).

### 11.2 Manual flow (the canonical reference)

Per AGENTS.md §3.5 every automated step has a documented manual
equivalent. The procedure below reproduces `./tool/dist` step by
step. Run it from the repository root, after `./tool/build prepare`
and `./tool/build ports` have set up `genode/build/x86_64/`.

```bash
# 0. Install the six media host-tool packages once (§1.3 covers the
#    base build packages; this is the extra media set, documented in
#    docs/11-environment.md §7.3).
sudo apt install xorriso gptfdisk mtools e2tools dosfstools e2fsprogs
# (Arch: substitute `pacman -S` for `apt install`.)

# 1. Sanity-check that every host tool the image plugins invoke is
#    actually on PATH. The names are the ones the Genode run tool's
#    `installed_command` proc looks up by `auto_execok`. Missing any
#    of them makes the corresponding image plugin abort inside make.
for tool in xorriso sgdisk mcopy e2cp e2mkdir mkfs.ext2 mkfs.vfat resize2fs; do
    command -v "$tool" >/dev/null || echo "MISSING: $tool"
done

# 2. Build the disk image (.img). The KERNEL/BOARD on the make
#    command line override anything in etc/build.conf for this one
#    invocation, so the same build directory can be used for the
#    Linux developer flow elsewhere. RUN_OPT pulls in the
#    image/disk plugin after the run framework's regular boot test.
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/disk'

# 3. Copy the disk artifact to its release name and write the
#    sha256 sidecar. The image plugin leaves it at
#    genode/build/x86_64/var/run/sponge-alpha.img.
mkdir -p var/dist
cp genode/build/x86_64/var/run/sponge-alpha.img \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.img \
    > sponge-os-0.1.0-alpha-x86_64-sel4.img.sha256)

# 4. Clean the previous mode's staged boot directory so the ISO
#    build does not inherit stale boot modules from the disk build
#    (stale_state guard; `./tool/dist` does the same).
rm -rf genode/build/x86_64/var/run/sponge-alpha*

# 5. Build the ISO (.iso) — same make shape with image/iso instead.
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/iso'

# 6. Copy + sha256 the ISO artifact.
cp genode/build/x86_64/var/run/sponge-alpha.iso \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.iso \
    > sponge-os-0.1.0-alpha-x86_64-sel4.iso.sha256)

# 7. Verify both sidecars.
(cd var/dist && sha256sum -c *.sha256)

# 8. (Optional) Print a summary of what was produced.
ls -lh var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.{img,iso}
```

The release name embeds the version (`0.1.0-alpha`, kept in sync
with `include/sponge/version.h`), the architecture (`x86_64`), and
the kernel (`sel4`). Bump the version in
`include/sponge/version.h` first (`tool/version_bump.mojo`) and the
release name changes everywhere it is referenced.

### 11.3 Why two media formats

The disk image (`.img`) is what you `dd` onto a USB stick for a real
boot (out of scope for the Alpha — QEMU only, see
`docs/09-roadmap.md` §9). The ISO (`.iso`) is what you mount/attach
as a CD-ROM. The Genode run framework produces them via two different
image plugins (`genode/tool/run/image/disk` vs.
`genode/tool/run/image/iso`) that share the boot modules but differ
in the bootloader stage (GRUB on the ISO's El Torito image vs. a GPT
partition table + EFI System Partition on the disk image). Producing
both from the same scenario is a redundancy check that the boot
chain works in either container.

