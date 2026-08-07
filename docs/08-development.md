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

#    The terminal package (pkg/terminal, todo 13) additionally needs the
#    bash, vim, and ncurses ports (ncurses' Caps generation requires the
#    host `mawk` tool — see docs/11-environment.md §7):
#        ./tool/ports/prepare_port bash vim ncurses

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
| `run/sponge-de-sel4-interactive.run` | base-sel4 interactive-PC driver set (vesa_fb/ps2/usb_hid/event_filter/platform/acpi/pci_decode) under QEMU with usb-tablet absolute pointer; Phase 10 extended it with QMP-driven real input + click-to-launch over the real driver chain + panel popup toggle | ✅ Phase 10 closed (criteria 1, 3, 4 — see §4.4 and `docs/evidence/phase10-index.md`); the underlying Qt6-on-sel4 capability-exhaustion root cause in §3.8 stays resolved |
| `run/sponge-wm-qmp.run` | **Phase 10 criterion 2**: real QMP PS/2 Mouse window drag on the upstream wm/layouter/decorator stack + sponge_pkgd-launched `pkg_gui_demo` | ✅ base-sel4 only (driver stack + wm stack + QMP) |
| `run/sponge-terminal-qmp.run` | **Phase 10 criterion 5a**: real QMP `send-key` keyboard input to a focused terminal session (gems terminal + noux bash) | ✅ base-sel4 only |
| `run/sponge-textedit-qmp.run` | **Phase 10 criterion 5b**: real QMP `send-key` keyboard input to a focused Qt6 textedit session | ✅ base-sel4 only |
| `run/qmp.inc` | shared Tcl QMP helper (sourced via `[file dirname [info script]]`); provides `qmp_connect`, `qmp_click`, `qmp_ps2_click`, `qmp_drag`, `qmp_send_key`, `qmp_type`, `qmp_exec_target`, etc. — see §4.4 for the API | n/a |
| `run/sponge-boot.run` | **Phase 8 P1**: Tier-0 storage-chain smoke (mount GENODE ext2, serve a ROM from disk via `cached_fs_rom`, read back `/system/marker.txt`; gates on `boot-probe: PASS`). base-sel4 only; `RUN_OPT='--include image/disk'`. NVMe variant via `SPONGE_BOOT_NVME=1`. | ✅ (commit `d3473f61f6`) |
| `run/sponge-desktop-disk.run` | **Phase 8 P2**: the FULL Alpha desktop booted FROM DISK (image.elf ≤ 12 MiB; Qt6 desktop served from `/system` via `cached_fs_rom`); gates on `alpha-probe: PASS`. The disk-served half of the product `.img`. base-sel4 only; `RUN_OPT='--include image/disk'`. | ✅ (commit `e7f8b9a458`) |
| `run/sponge-persist-disk.run` | **Phase 8 P3**: persistence on SPONGE-DATA — `sponge_pkgd`'s installed-set store survives a reboot when backed by a writable ext2 on P4 (added by `tool/mkdata`); two-boot adversarial proof. base-sel4 only; `RUN_OPT='--include image/disk'`. | ✅ (commit `f25a81dcbe`) |
| `run/sponge-falkon-disk.run` | **Phase 8 P4**: Falkon's 509 MiB WebEngine payload booted FROM DISK (architecture PROVEN — image.elf stays at ~12 MiB; ldso resolves the 237 MiB WebEngine lib from disk; lwIP DHCPs to 10.0.2.15). First paint is blocked by a base-sel4 capability-space issue (NOT a boot/storage issue); see `docs/14` §12.4 and `docs/evidence/p4-cspace-fix.log`. base-sel4 only; `RUN_OPT='--include image/disk'`. | 🟡 architecture proven; first paint blocked (commit `bae5423d1b`) |

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

### 4.4 Host-driven QMP input (`run/qmp.inc`)

Phase 10 (`docs/plans/phase10-interactive-desktop.md`) delivers the
fully-interactive desktop by driving guest input from the host through
QEMU QMP instead of the probe's synthetic `Event`-session injection.
The shared helper `run/qmp.inc` (sourced by
`sponge-de-sel4-interactive.run`, `sponge-wm-qmp.run`,
`sponge-terminal-qmp.run`, and `sponge-textedit-qmp.run`) is a Tcl 8.x
source file with no external dependencies — only Tcl's built-in
`socket` (TCP, matches `-qmp tcp:`) and `expect` (already used by the
Genode run tool).

Source convention (from the symlink-resolved run directory):

```tcl
source [file join [file dirname [file normalize [info script]]] qmp.inc]
```

Enable the QMP listener in QEMU args:

```tcl
set qmp_port [qmp_pick_port]               ;# or [expr {20000 + ([pid] % 20000)}]
append qemu_args " -qmp tcp:127.0.0.1:${qmp_port},server=on,wait=off "
```

Then, after `run_genode_until` confirms the guest is ready:

```tcl
set qmp_chan [qmp_connect $qmp_port]
qmp_exec_target $qmp_chan 120              ;# bounded expect for QMP-TARGET marker
run_genode_until {.*<scenario>: PASS.*} 120 $qemu_spawn_id
qmp_disconnect $qmp_chan
```

#### `qmp.inc` API (full list)

| Proc | Args | Purpose |
|---|---|---|
| `qmp_pick_port` | — | Free TCP port via `socket -server 0`; kernel-assigned; avoids TIME_WAIT collisions on sequential QEMU respawns. |
| `qmp_connect` | `port` | Bounded retry (100 × 50 ms), read QMP greeting, send `qmp_capabilities`, return channel. |
| `qmp_cmd` | `chan json` | Send one JSON command, skip async `{"event":...}` lines, die loud (exit 1) on `{"error":...}` or 10 s read timeout. |
| `qmp_abs` | `x y` | Scale guest (1024×768) coords to QEMU usb-tablet axes (0..32767): `ax=(x*32767+512)/1024`, `ay=(y*32767+384)/768`. |
| `qmp_pointer_move` | `chan x y` | Emit `input-send-event` abs events (per-axis, two-event batch). |
| `qmp_button` | `chan btn {down\|up}` | Emit `input-send-event` btn event (`button` key, "left"/"right"/"middle"). |
| `qmp_click` | `chan x y` | `qmp_pointer_move` + `qmp_button left down` + `qmp_button left up`. Under QEMU `-nographic` the click is NOT precisely targeted — see below. |
| `qmp_ps2_click` | `chan x y` | The W3-calibrated PS/2 Mouse click: clamp to (0,0) → 5× rel-50 + 4× rel-50 → fine rel-1 walk → hover jiggle (+1/-1) → 200 ms press hold → release. ±1 px precision with the vendored `event_filter` `<accelerate>` chain (or 1:1 rel-to-px with the custom config used by `sponge-de-sel4-interactive.run`). |
| `qmp_drag` | `chan x1 y1 x2 y2` | Move to start, hover jiggle, press, walk to end (50/1 split), release. |
| `qmp_send_key` | `chan keyname` | Emit a single QEMU key down/up via `send-key` with the **qcode-object form** (NOT the rejected string form — see QEMU 11 lesson below). |
| `qmp_type` | `chan string` | Char→keyname map (`a-z`, `0-9`, `space→spc`, `\n→ret`, `-→minus`, `.→dot`) + `qmp_send_key` per char; unmapped chars fail loud. |
| `qmp_exec_target` | `chan timeout_s` | Bounded `expect` on global `qemu_spawn_id` for the `QMP-TARGET` marker, dispatches to the matching proc (click/drag/type/move), raises `match_max -i $qemu_spawn_id 200000` first. FAIL + exit 1 on timeout. |
| `qmp_move_rel` | `chan dx dy` | Emit per-axis `REL_*` motion events (PS/2 PATH), with `after` pacing — see lesson (a) below. |
| `qmp_disconnect` | `chan` | Close the QMP TCP channel. |

#### `QMP-TARGET` marker contract (probe → host)

Probes in observe mode log these lines on the serial console; the run
script's `qmp_exec_target` consumes and dispatches them:

```
QMP-TARGET click <gx> <gy>           # PS/2 REL recipe by default
QMP-TARGET tablet <gx> <gy>          # usb-tablet-abs recipe (W4 terminal scenario style)
QMP-TARGET drag <x1> <y1> <x2> <y2>  # window drag (motion in two phases)
QMP-TARGET type <string>             # keyboard input
QMP-TARGET move <dx> <dy>            # closed-loop PS/2 REL move (W5)
```

Patterns anchor on `\r*\n` (NOT `\r?\n`) because QEMU's `-nographic`
serial emits `CR CR LF` line endings (verified by `od -c` on a
captured line). Marker ordering inside `qmp_exec_target`:

1. `tablet` listed first (the `click` regex matches `-click` as a
   suffix of some line names; listing `click` first would shadow it).
   Order matters because expect dispatches the FIRST matching arm.
2. `click` second — every other click (input/panel/launch) uses the
   PS/2 recipe (`qmp_ps2_click`).
3. `drag`, `type`, `move` follow in protocol order.

The dispatch contract is verified by
`docs/evidence/task-2-phase10-interactive-dispatch-test.tcl`
(plain `tclsh`, no deps — runs in <1 s).

#### Three hard-won QEMU 11.0.2 lessons

(a) **`match_max` must be raised under log floods.** Tcl/expect's
default per-spawn-id buffer for regex matching is 2000 bytes. The
launch-phase green-pixel poll in `sponge_de_probe` spams
`sponge-de-probe: launch green poll N frac_per_mille=0` every 200 ms;
2000 iters × ~70 B = ~140 KB of poll spam pushed into
`qemu_spawn_id`. Under that flood, expect's match window keeps only
the TAIL of accumulated output — between markers, the buffer tail is
the poll-spam line, not the QMP-TARGET marker; the expect arms find
nothing to match and time out at 120 s. The fix (commit `b5bd9b0307`):
`qmp_exec_target` raises `match_max -i $qemu_spawn_id 200000` (200 KB)
immediately before the `expect` block; the run script re-raises it
after `global qemu_spawn_id` is set and before each `run_genode_until`
gate consumes data. The 200 KB cover any single inter-marker window
plus the green-poll flood. See the comment block at the
`match_max` raise in `qmp.inc` for the rationale. (`sponge_de_probe`
also reduces source-cadence: log only poll 0, every 50th iteration,
and the final iteration.)

(b) **`run_genode_until` gate patterns must NOT have a trailing `.*`
when markers follow the gate.** Example:

```
{.*sponge-de-probe: phase input PASS.*}    ;# trailing .* is GREEDY
```

The trailing `.*` consumes the entire rest of the buffer after the
match — swallowing the next QMP-TARGET marker that arrives in the same
read chunk. Discovered via the W2 panel-phase "off-by-one" cascade:
the input-PASS `run_genode_until` swallowed the panel S-click marker,
so the 2nd `qmp_exec_target` arm matched the panel close click (which
arrived AFTER the "waiting" message) and the launch markers cascaded
forward. Fix (commit `a9ca5ffd9e`): drop the trailing `.*`, keep the
leading `.*`. The leading `.*` only consumes output BEFORE the marker
(bounded); the trailing wildcards are the smell.

(c) **Event pacing (`after`) is required for PS/2 REL bursts.** QEMU
11's emulated PS/2 controller drops unsynchronized bursts of
`input-send-event` REL events into a single accumulated delta
(observed: 50× rapid `rel(-50)` collapses into 2× effective motion on
this host). The QMP `mouse_set` + `input-send-event` style used by the
window-drag recipe needs an `after 5` (or so) between consecutive
per-axis REL events for the controller to deliver them as discrete
motions. The qmp.inc `qmp_ps2_click` proc paces per-axis events with
`after 5`; this is the empirically-tuned value for QEMU 11.0.2 on
Linux. Bumping it slower (`after 20`) is safe; faster (`after 0`)
silently loses events.

#### Two known QEMU 11 quirks (the beyond-`qmp.inc` caveats)

- **`send-key` strings are rejected.** QEMU 11 (and earlier — this
  is stable across versions) wants the `keys` argument as an array of
  objects `{"type":"qcode","data":"<name>"}`, NOT the older string
  form `["<name>"]`. The string form fails with `Invalid parameter
  type for 'keys[0]', expected: object` (see
  `docs/evidence/task-4-phase10-interactive.md` §Root cause 3). All
  scenarios in `run/qmp.inc` use the qcode-object form.

- **`input-send-event` BTN events are NOT delivered to untargeted
  devices.** QEMU 11 broadcasts untargeted abs/btn events across all
  pointer devices; if the PS/2 mouse is the current device (default
  under `-nographic`, `query-mice` shows `current:true`), the abs
  events are reinterpreted as PS/2 *relative* deltas (a slam to a
  corner — see the W4 evidence §Root cause 5). Fix: dispatch
  `mouse_set <tablet-index>` via HMP to make the usb-tablet the
  current device BEFORE the abs/btn events. `qmp_tablet_click` /
  `qmp_hmp` in `qmp.inc` implement the device-targeted recipe
  (used by `sponge-terminal-qmp.run`, criterion 5a).

#### Pointer model — usb-tablet-abs vs PS/2-REL tradeoffs

Under QEMU 11.0.2 `-nographic`:

- **usb-tablet absolute motion:** the click DOES reach sponge-de
  through the real usb-tablet → pc_usb_host → usb_hid → event_filter →
  nitpicker → sponge-de chain (criterion 1's proof is intact), but
  nitpicker's pointer sanitizer
  (`genode/repos/os/src/server/nitpicker/main.cc:780-810`) treats
  `Input::Absolute_motion` coordinates as raw pixels. Every abs value
  in 0..32767 lands off-screen on a 1024x768 display and is then
  **clamped to screen center (~512, 384)** — independent of the
  divisor you choose for `qmp_abs`. This is the
  `~29 px y-drift (384 - 412 = -28)` mystery from W1: it is
  `screen-center-y - intended-y`, not a per-event QEMU translation
  offset. W1's "click worked" because the demo domain
  (192,172,640,480) is large enough that the clamp point (512, 384)
  falls inside it.

- **PS/2 Mouse relative motion:** avoids the abs clamping entirely.
  The pointer walks the path explicitly — `qmp_ps2_click` and
  `qmp_drag` use event_filter's `<accelerate>` chain
  (`sensitivity_percent=1000, max=50, curve=127`, the standard
  recipe in
  `genode/repos/os/recipes/raw/drivers_interactive-pc/event_filter.config`)
  which gives a non-linear LUT: `rel-1` → 1 px, `rel-50` → ~100 px,
  `rel-100` → ~150 px. The clamp-to-(0,0) + coarse + fine sequence
  lands the pointer ±1 px of any absolute target.

  The PS/2 path exercises a strictly-equivalent hardware input chain
  (QMP → emulated PS/2 controller → ps2 driver → event_filter →
  nitpicker → sponge-de / wm / decorator) — just the PS/2 branch of
  `event_filter`'s input handling instead of the usb branch.

The interactive scenario (`sponge-de-sel4-interactive.run`) uses the
**PS/2 RECIPE for every click**, including the criterion-1 input click.
The criterion-1 chain is still proven (the click reaches sponge-de's
`input` report), and the precise targeting makes all panel/launch
markers land inside their respective rects.

#### Recipe-matches-`event_filter.config` caveat

`qmp_ps2_click`'s coarse walk (`5 × rel-50` ≈ 100 px per event)
assumes the vendored event_filter `<accelerate>` chain
(`rel-50 → ~100 px`). The custom staged `event_filter.config` used
by `sponge-de-sel4-interactive.run` *removes* the `<accelerate>`
wrapper (see the run script's `event_filter.config` block), so
`rel-50 → 50 px` (1:1). With the 1:1 mapping, the recipe's coarse
walk covers half the intended distance — the `FIRST_ENTRY` target in
`sponge_de_probe` is doubled (target (170, 73) → target (340, 170))
so the recipe's halved walk lands on the geometric-correct center.
Future users reusing `qmp_ps2_click` from `sponge-de-sel4-
interactive.run`'s event_filter.config (1:1) MUST match the recipe to
their `event_filter.config` — either restore `<accelerate>` to get
the standard 100-px coarse walk, or compensate the recipe's halved
walk by doubling the target coordinates as the scenario does.

#### Manual interactive viewing (control escape hatch)

For a human to drive the running guest from the host instead of the
script-driven QMP choreography, append the QEMU flags to spawn QEMU
with the SDL display and replace the final `run_genode_until` with
`run_genode_until forever`:

```bash
# Manual equivalent (host-side mouse + keyboard reach the guest):
make -C genode/build/x86_64 run/sponge-de-sel4-interactive \
  KERNEL=sel4 BOARD=pc QEMU_OPT="-display sdl"
```

(The QEMU args are placed BEFORE the run script's own
`append qemu_args "-qmp tcp:..."` — the `-qmp tcp:` is still added,
but on QEMU 11 the SDL display and the QMP listener both work in the
same guest.) Human input from the SDL window reaches the same driver
chain (event_filter is enabled in the boot config), so this proves the
script-driven QMP path is equivalent to a real user's mouse.

### 4.5 Phase-11 DE customization (configd keys, themes, window chrome)

Phase 11 (`docs/plans/phase11-de-customization.md`) makes the desktop
customizable through `sponge_configd` without touching Genode
internals. The live data path:

```
vct config <key> <value>
  -> sponge_configd (validated, broadcast on the "config" report)
  -> sponge-de's ConfigController (<config source="configd"/> gate)
  -> panel / launcher re-styled live (Qt GUI thread, marshalled)
```

New keys (validation in `sponge_configd/main.cc`, full table in
`repos/sponge/src/sponge_configd/README.md`):

| Key | Type | Default | Effect |
|---|---|---|---|
| `panel.height` | uint 16–128 | `28` | Panel thickness; launcher toggle tracks it |
| `panel.visible_widgets` | enum-list of `clock,launcher` | `clock,launcher` | Hide/show panel widgets live |
| `clock.format` | printable-ASCII format string | `HH:mm` | Qt time format; invalid values fall back to `HH:mm` with a warning on the panel side |
| `launcher.sort_by` | enum `alpha`,`manual` | `alpha` | Launcher entry ordering (manual = pkgd broadcast order) |
| `panel.position` | enum `top`,`bottom`,`left`,`right` | `bottom` | **Boot-time only** (nitpicker domain owns placement; change the run script domain and reboot) |

Automation is the default (`vct config panel.height 40` just works);
the control escape hatch is the same channel (`vct config` per key)
plus the theme files themselves.

Themes: four shipped (`default`, `light`, `dark`, `compact` under
`repos/sponge/src/sponge-de/themes/`); `vct theme apply <name>`
switches live. The unknown-theme path is hardened: a
`label_suffix=".theme"` catch-all route hands `sponge_themed` an
empty ROM, so a typo keeps the previous theme and the daemon stays
alive (proven by the 5-step probe in `run/sponge-theme.run`). The
transport cap is 8192 bytes — guard it with
`./tool/test_theme_payload_size` after editing a theme.

Window chrome: `run/sponge-de-themed-chrome.run` replaces the stock
decorator with upstream `themed_decorator`. The chrome assets live in
`decor.tar`, authored by `./tool/decor_assets` (metadata + PNGs +
byte-vendored `font.tff` under `tool/decor_assets_data/` — edit
`metadata.txt` and re-run the tool to redesign the frame geometry;
that is the documented manual step). The title-bar tint follows the
active theme live: `sponge_decorator_bridge` watches the theme ROM
and republishes the decorator's whole config (libc + vfs + policy
color) through report_rom. Note that the tar *assets* are boot-time
(static in upstream); only the policy color is live (roadmap §11.3).

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
`var/dist/` per the Phase 8 boot/storage architecture
([`docs/14`](14-boot-storage-architecture.md) §8):

```
sponge-os-0.1.0-alpha-x86_64-sel4.img   (4-partition disk image: the product)
sponge-os-0.1.0-alpha-x86_64-sel4.iso   (El Torito ISO: live/eval mode)
```

The `.img` is the **real product**: a Tier-0 `image.elf` (≤ 80 MiB)
that mounts the GENODE ext2 partition and serves the full Qt6 desktop
from `/system` via `cached_fs_rom` (P2, `run/sponge-desktop-disk.run`)
PLUS a fourth GPT partition `SPONGE-DATA` (added by `tool/mkdata`,
docs/14 §4.3) that backs `sponge_pkgd`'s installed-set store so
installs survive reboot (P3, `run/sponge-persist-disk.run`). The `.iso`
is the **live/eval media**: the Phase 7 boot-modules composition
(`run/sponge-alpha.run`) on an El Torito image; Tier 2 is a RAM
filesystem, so nothing persists.

Each artifact ships with a `<name>.sha256` sidecar in the
`<hash>  <filename>` format that `sha256sum -c` consumes directly.
Both media boot (in QEMU) to the same `alpha-probe: PASS` marker — the
media are already boot-proven by the run scenario's `run_genode_until`
call inside the make invocation; nothing re-verifies them post-copy.

### 11.1 One-command flow (default)

```bash
./tool/dist
```

The wrapper ([`tool/dist`](../tool/dist) → [`tool/dist.mojo`](../tool/dist.mojo)):

1. Pre-flight checks every host tool the Genode run framework's image
   plugins AND `tool/mkdata` invoke (`xorriso`, `sgdisk`, `mcopy`,
   `e2cp`, `e2mkdir`, `mkfs.ext2`, `mkfs.vfat`, `resize2fs`,
   `truncate`) and prints the exact `apt install` line for any missing
   one, exiting non-zero **before** any build runs (loud-and-early
   failure).
2. Builds the **product `.img`** from `run/sponge-desktop-disk.run`
   with `RUN_OPT='--include image/disk'`.
3. Runs `tool/mkdata` on the produced `.img` to add the SPONGE-DATA P4
   (docs/14 §4.3 — `truncate` + `sgdisk` delete/move/new/hybrid +
   `mkfs.ext2 -E offset`). This is the partition that backs pkgd's
   installed-set store. Idempotent (a re-run on an image that already
   has P4 is a verified no-op).
4. Verifies the partition table (the `misleading_success_output`
   defense — never trust the build exit code alone): `sgdisk -p` must
   show ≥ 4 partitions with P4 named SPONGE-DATA.
5. Builds the **live/eval `.iso`** from `run/sponge-alpha.run` with
   `RUN_OPT='--include image/iso'` — the Phase 7 boot-modules
   composition.
6. Copies the artifacts to `var/dist/`, writes the `.sha256` sidecars,
   and prints a summary table.

`./tool/dist` never boot-verifies the media itself (the run framework
already does that during the make), and never touches anything outside
the repository (AGENTS.md §3.5).

**Control doors (AGENTS.md §1.1 — automation is the default; a door is
always open):**

- `./tool/dist --no-data` — produce the `.img` WITHOUT the SPONGE-DATA
  P4 (3-partition image; installs will NOT persist on this media).
  Useful when iterating on the disk-served desktop alone.
- `./tool/dist --data-size 256` — produce a 256 MiB SPONGE-DATA P4
  instead of the default 1024 MiB.
- `./tool/mkdata <img>` — run only the P4 grow/repartition step on an
  existing 3-partition `.img`. Idempotent. See `tool/README.md` and
  docs/14 §4.3 for the full contract.

### 11.2 Manual flow (the canonical reference)

Per AGENTS.md §3.5 every automated step has a documented manual
equivalent. The procedure below reproduces `./tool/dist` step by
step. Run it from the repository root, after `./tool/build prepare`
and `./tool/build ports` have set up `genode/build/x86_64/`.

```bash
# 0. Install the seven media host-tool packages once (§1.3 covers the
#    base build packages; this is the extra media set, documented in
#    docs/11-environment.md §7.3). truncate is required by tool/mkdata
#    (the docs/14 §4.3 P4 grow step).
sudo apt install xorriso gptfdisk mtools e2tools dosfstools e2fsprogs coreutils
# (Arch: substitute `pacman -S` for `apt install`.)

# 1. Sanity-check that every host tool the image plugins + mkdata
#    invoke is actually on PATH.
for tool in xorriso sgdisk mcopy e2cp e2mkdir mkfs.ext2 mkfs.vfat \
            resize2fs truncate; do
    command -v "$tool" >/dev/null || echo "MISSING: $tool"
done

# 2. Build the product .img — the disk-served desktop scenario.
#    sponge-desktop-disk self-loads boot_dir/sel4, power_on/qemu, and
#    log/qemu via ensure_plugin_loaded, so the bare RUN_OPT override
#    is sufficient.
make -C genode/build/x86_64 run/sponge-desktop-disk \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/disk'

# 3. Grow the SPONGE-DATA P4 onto the produced .img (docs/14 §4.3).
#    tool/mkdata is idempotent: a re-run on an image that already has
#    P4=SPONGE-DATA is a verified no-op.
./tool/mkdata genode/build/x86_64/var/run/sponge-desktop-disk.img

# 4. Verify the partition table shows 4 partitions (misleading_success_
#    output defense — the build exit code alone is not enough).
sgdisk -p genode/build/x86_64/var/run/sponge-desktop-disk.img

# 5. Copy + sha256 the disk artifact.
mkdir -p var/dist
cp genode/build/x86_64/var/run/sponge-desktop-disk.img \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.img \
    > sponge-os-0.1.0-alpha-x86_64-sel4.img.sha256)

# 6. Clean the previous mode's staged boot directory so the ISO build
#    does not inherit stale boot modules from the disk build
#    (stale_state guard; `./tool/dist` does the same — note the two
#    scenarios differ, so this clean is per-scenario and does not
#    touch the disk build's run_dir).
rm -rf genode/build/x86_64/var/run/sponge-alpha*

# 7. Build the live/eval .iso — the alpha boot-modules composition.
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/iso'

# 8. Copy + sha256 the ISO artifact.
cp genode/build/x86_64/var/run/sponge-alpha.iso \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.iso \
    > sponge-os-0.1.0-alpha-x86_64-sel4.iso.sha256)

# 9. Verify both sidecars.
(cd var/dist && sha256sum -c *.sha256)

# 10. (Optional) Print a summary of what was produced.
ls -lh var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.{img,iso}
```

The release name embeds the version (`0.1.0-alpha`, kept in sync
with `include/sponge/version.h`), the architecture (`x86_64`), and
the kernel (`sel4`). Bump the version in
`include/sponge/version.h` first (`tool/version_bump.mojo`) and the
release name changes everywhere it is referenced.

### 11.3 Why two media formats

Per docs/14 §8: the disk image (`.img`) is the real product. Four
partitions (BIOS-boot + ESP + GENODE + SPONGE-DATA), full persistence —
this is what `tool/dist` optimizes and what "install to USB" means. The
ISO (`.iso`) is the live/eval mode: Tier 2 is a plain RAM filesystem
(`<ram/>` in the Tier-2 vfs), so everything works but nothing persists.
This matches every live-OS convention and keeps the ISO useful for
evaluation without promising persistence a read-only optical medium
cannot deliver. The Genode run framework produces the two via two
different image plugins (`genode/tool/run/image/disk` vs.
`genode/tool/run/image/iso`); `tool/mkdata` then grows SPONGE-DATA onto
the disk image's `--include image/disk` output (the image/disk tool
packs P1+P2+P3 with no free sectors, so P4 cannot be added with a bare
`sgdisk --new` — see docs/14 §4.3 for the grow/repartition sequence).

### 11.4 Building only one of the two media

The two scenarios are independent. To build only one:

```bash
# Only the product .img (no P4):
make -C genode/build/x86_64 run/sponge-desktop-disk \
    KERNEL=sel4 BOARD=pc RUN_OPT='--include image/disk'
./tool/mkdata genode/build/x86_64/var/run/sponge-desktop-disk.img   # add P4

# Only the live/eval .iso:
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc RUN_OPT='--include image/iso'
```

The legacy boot-modules `.img` (Phase 7's `sponge-alpha` image/disk
output) is reachable by running the alpha scenario with `image/disk`:
it is the developer regression shape, not the product. It lacks the
SPONGE-DATA P4 (no persistence) and packs the whole desktop into boot
modules (subject to the ~256 MiB ceiling, docs/14 §2).

## 12. Importing Depot Packages (Alpha)

Phase 7 introduces host-side depot interop: instead of building every
binary from in-tree source, some Alpha packages are repackaged from
[cproc's published Genode depot archives](https://depot.genode.org/cproc/)
into Sponge `pkg/<name>/` directories. The pinned depot references and
their SHA-256 fingerprints live in `docs/11-environment.md` §5. This
section documents the importer tool and its manual equivalent.

**What is a depot package, and what is not.** A Genode depot *pkg*
archive (`<user>/pkg/<recipe>/<version>`) is a recipe that lists
transitive *src*, *raw*, and *api* dependencies and a `runtime` file
describing how the binary expects to be wired at boot (libc vfs,
required sessions, content ROMs). The *pkg* archive itself contains no
binaries — those live in a separate *bin* archive
(`<user>/bin/<arch>/<recipe>/<version>`) that the importer downloads on
demand. Neither archive is fetched at Sponge runtime (Metis amendment
A1: all binaries are baked into the boot image at build time;
`sponge_pkgd` never contacts a depot).

### 12.1 One-command flow (default)

```bash
# Step 1 (once per machine): set up an in-repo GNUPGHOME holding the
# cproc pubkey. Modern GnuPG (2.4+) with use-keyboxd silently ignores
# `--keyring <file>` on the depot tool's verify path, which makes the
# in-tree pubkey at genode/repos/gems/sculpt/depot/cproc/pubkey
# unreachable. Importing it into a fresh in-repo keybox under
# var/scratch/gnupg sidesteps that without writing outside the repo
# (AGENTS.md §3.5).
mkdir -p var/scratch/gnupg && chmod 700 var/scratch/gnupg
export GNUPGHOME="$PWD/var/scratch/gnupg"
gpg --no-tty --import genode/repos/gems/sculpt/depot/cproc/pubkey

# Step 2: fetch the pkg + its transitive deps into genode/depot/ and
# genode/public/ (signature-verified against the in-tree pubkey).
./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27

# Step 3: repackage the depot pkg + its binary archive into pkg/textedit/.
# The importer downloads the matching bin archive on demand.
./tool/pkg_import cproc/pkg/qt6_textedit/2025-10-27 \
    --bin-version 2025-10-12
```

The wrapper ([`tool/pkg_import`](../tool/pkg_import) →
[`tool/pkg_import.mojo`](../tool/pkg_import.mojo)):

1. Verifies the pkg is downloaded under `genode/depot/<ref>/`.
2. Reads the `runtime` metadata (binary name, ram/caps, required
   sessions, content ROMs) and hashes the original `pkg.tar.xz`.
3. Resolves the matching `bin/<arch>/<recipe>/<version>` archive by
   trying the recipe-name, binary-name, and pkg-recipe-name paths in
   order; downloads it on demand via `genode/tool/depot/download`.
4. Stages every content ROM the runtime declares into
   `pkg/<name>/payload/`, copying files already present in the local
   `genode/depot/` tree (raw archives, the bin archive, plus any
   `*.lib.so` or `*.tar` in sibling bin archives). Missing entries are
   recorded in `pkg/<name>/PAYLIST` for follow-up staging.
5. Generates `pkg/<name>/metadata.xml` per
   [docs/12-package-format.md §4](../docs/12-package-format.md) with an
   inline `<config>` carrying the libc+vfs boilerplate (modeled on
   `run/sponge-launcher.run:126-136`) and a `<sessions>` block mirrored
   from the runtime's `<requires>`. The caps floor is raised to 1000
   for GUI apps (per `docs/09-roadmap.md` §11.1 — the Qt-on-seL4
   capability-exhaustion lesson).
6. Writes `pkg/<name>/SOURCE` recording the depot user/pkg/version plus
   the SHA-256 of both the pkg archive and the bin archive
   (reproducibility, `docs/11-environment.md` §1).
7. Builds everything under `pkg/<name>.tmp/` and atomically renames to
   `pkg/<name>/` on success, so a bogus depot reference never leaves a
   partial package directory behind (the failure channel).

`./tool/pkg_import` never touches anything outside the repository, and
never re-implements the depot tool (it shells out to
`genode/tool/depot/download` exactly as a contributor would by hand,
AGENTS.md §3.5).

### 12.2 Manual flow (the canonical reference)

Per AGENTS.md §3.5 every automated step has a documented manual
equivalent. The procedure below reproduces `./tool/pkg_import` step by
step. Run it from the repository root after `./tool/build prepare` and
`./tool/build ports` have set up `genode/build/x86_64/`.

```bash
# 0. One-time PGP setup (see Step 1 above for the rationale).
mkdir -p var/scratch/gnupg && chmod 700 var/scratch/gnupg
export GNUPGHOME="$PWD/var/scratch/gnupg"
gpg --no-tty --import genode/repos/gems/sculpt/depot/cproc/pubkey

# 1. Fetch the pkg + its transitive src/raw/api dependencies.
./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27

# 2. Fetch the matching binary archive. The pkg-vs-bin path mapping is
#    not always 1:1 (cproc publishes `falkon_qt6-jemalloc` under the
#    bin path `falkon_qt6`). When the pkg version does not map to a
#    published bin version, list the available versions and pick one:
#      curl -s https://depot.genode.org/cproc/bin/x86_64/qt6_textedit/
./genode/tool/depot/download cproc/bin/x86_64/qt6_textedit/2025-10-12

# 3. Verify both archives landed under genode/depot/:
ls genode/depot/cproc/pkg/qt6_textedit/2025-10-27/
ls genode/depot/cproc/bin/x86_64/qt6_textedit/2025-10-12/

# 4. Compute the SHA-256 of each depot archive (for the SOURCE record).
sha256sum genode/public/cproc/pkg/qt6_textedit/2025-10-27.tar.xz
sha256sum genode/public/cproc/bin/x86_64/qt6_textedit/2025-10-12.tar.xz

# 5. Read the runtime metadata to learn the binary name, required
#    sessions, and the content ROMs that must be staged.
cat genode/depot/cproc/pkg/qt6_textedit/2025-10-27/runtime

# 6. Create the Sponge package directory. Mirror the runtime's binary
#    name, quota, and sessions into metadata.xml (see
#    docs/12-package-format.md §4 for the schema). Copy every content
#    ROM from the local depot tree into payload/. Write SOURCE
#    recording the depot pin + sha256.
mkdir -p pkg/textedit/payload
# (edit pkg/textedit/metadata.xml by hand following docs/12 §4.1)
# (edit pkg/textedit/SOURCE by hand following the format in 12.3 below)
cp genode/depot/cproc/bin/x86_64/qt6_textedit/2025-10-12/textedit \
   pkg/textedit/payload/
cp genode/depot/cproc/raw/qt6_textedit/2025-09-19/textedit.config \
   pkg/textedit/payload/
# (also copy every *.lib.so and *.tar listed in the runtime <content>
#  from sibling bin/ archives into payload/)

# 7. Stage the package into a run scenario alongside the other boot
#    modules — see run/sponge-launcher.run:208-216 for the staging
#    pattern. The scenario's build_boot_image then includes the
#    payload files as boot modules.
```

### 12.3 SOURCE record format

Every imported package carries a `SOURCE` file. The importer writes it
automatically; the manual flow writes it by hand. The format:

```
# Sponge OS package source record (reproducibility).
depot_user: cproc
depot_pkg_recipe: qt6_textedit
depot_pkg_version: 2025-10-27
depot_pkg_archive_sha256: <64-hex-char sha256 of the pkg.tar.xz>
depot_bin_archive_ref: cproc/bin/x86_64/qt6_textedit/2025-10-12
depot_bin_archive_sha256: <64-hex-char sha256 of the bin.tar.xz>
depot_url: https://depot.genode.org
imported_by: tool/pkg_import
```

The records match the depot-pin table in
`docs/11-environment.md` §5 so a contributor can verify any imported
binary by re-running `sha256sum` against the locally-downloaded
archive.

### 12.4 Caveats

- The cproc depot's pubkey uses SHA-1 self-signatures. The depot tool's
  preferred verifier is Sequoia PGP (`sq`), which carries an explicit
  `--policy-as-of 2013-01-31` workaround for SHA-1 keys. When `sq` is
  absent the depot tool falls back to GnuPG, and GnuPG 2.4+ with
  `use-keyboxd` silently ignores `--keyring <file>`. The in-repo
  `GNUPGHOME` setup in Step 1 is the supported workaround. Installing
  `sq` (`sequoia-sq` on Arch, `sequoia-pgp` on Debian) is the
  upstream-preferred alternative.
- The pkg archive's `runtime` declares every library ROM the binary
  expects at boot, but the depot's pkg download path only pulls the
  pkg/src/raw/api archives — not the per-library bin archives. The
  importer downloads the *main binary's* bin archive automatically;
  transitive library bins must be fetched separately (the importer
  records them in `pkg/<name>/PAYLIST`).
- The depot's pkg-vs-bin versions are independent: the
  `cproc/pkg/qt6_textedit/2025-10-27` package version (the packaging
  metadata version) does not match its latest
  `cproc/bin/x86_64/qt6_textedit/2025-10-12` (the binary build
  version). The importer takes an explicit `--bin-version` for this
  reason.


