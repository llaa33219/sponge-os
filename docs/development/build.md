> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §3. Build

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

# 3. Register the umbrella and split Sponge OS repositories.
echo 'SPONGE_ROOT := $(abspath $(GENODE_DIR)/..)' >> etc/build.conf
echo 'REPOSITORIES += $(GENODE_DIR)/repos/sponge' >> etc/build.conf
echo 'REPOSITORIES += $(SPONGE_ROOT)/repos/sponge-wm' >> etc/build.conf
echo 'REPOSITORIES += $(SPONGE_ROOT)/repos/sponge-de' >> etc/build.conf
echo 'REPOSITORIES += $(SPONGE_ROOT)/repos/sponge-compositor' >> etc/build.conf

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
./tool/check-compile repos/sponge-de/src/sponge-de
```

This script only checks file presence and build-seam shape; it does not
compile anything. C++ layouts still require `target.mk` and `main.cc`.
Mojo-first layouts are recognized and additionally checked for a non-empty
`SRC_O` declaration and for each local `.mojo` source having a matching
`.o` entry (stem match, including continuation lines). Entries using make
expressions are reported but not expanded.

Mojo objects are emitted through `tool/mojo_objects`, which resolves the
pinned 1.0.0b2 compiler from the repository venv and always runs `nm -u`.
The manual equivalent for a component object is:

```bash
./tool/mojo_objects --emit object --relocation-model=pic --target-cpu=x86-64 \
  -o /tmp/scene.o repos/sponge-compositor/src/sponge_compositor/mojo/scene.mojo
```

The baseline CPU flag prevents host-CPU AVX instructions from entering the
seL4 component. `nm -u` output is deliberately visible so a new runtime
symbol cannot slip through unnoticed.

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
[`docs/11-environment.md`](../11-environment.md) §4): the Qt6 cmake
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
