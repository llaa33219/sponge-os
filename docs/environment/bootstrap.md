# 11 - Development Environment: Bootstrap (§7)

> **Breadcrumb:** parent [11 - Development Environment & Reproducibility](../11-environment.md); this file holds its §7 (Bootstrap From a Clean Machine), moved verbatim.
> Siblings in `docs/environment/`: [third-party-ports.md](third-party-ports.md) (§5-§6), [env-vars.md](env-vars.md) (§8), [base-sel4-gui.md](base-sel4-gui.md) (§10), [disk-image-p4.md](disk-image-p4.md) (§12-§13).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

## 7. Bootstrap From a Clean Machine

Every step is reproducible. The exact ordered sequence, in one place:

### 7.1 base-linux target (developer feedback, no QEMU)

1. **Install the Genode toolchain 25.05.** Pick the path your distro
   prefers:
   ```bash
   # Arch / CachyOS: AUR
   yay -S genode-toolchain-bin
   # Every other distro: download from genode.org
   wget https://genode.org/files/tool-chain/genode-toolchain-25.05.tar.xz
   sudo tar xPf genode-toolchain-25.05.tar.xz
   ls /usr/local/genode/tool/25.05/bin/genode-x86-gcc   # sanity check
   ls -l /usr/local/genode/tool/current                # should point at 25.05
   ```
   The `-P` flag to `tar` is load-bearing: it preserves the absolute
   paths inside the archive (e.g. `./usr/local/genode/...`), so the
   toolchain ends up exactly where the Genode build system looks for
   it. Without `-P`, you get a `genode/` directory in your current
   working directory and a very confusing build error.

2. **Install host packages** (Tcl/expect for the Genode run tool,
   `qemu-system-x86_64` for non-base-linux runs, `rpcgen` for the
    `libc` port's prepare step, plus build essentials):
    ```bash
    # Arch
    sudo pacman -S tcl expect qemu-system-x86 cmake ninja make rpcsvc-proto mawk
    # Debian / Ubuntu
    sudo apt install tcl expect qemu-system-x86 cmake ninja-build build-essential libc-dev-bin mawk
    ```
    (`rpcsvc-proto` / `libc-dev-bin` provide `rpcgen`. Without it,
    `prepare_port libc` fails during its header-generation step. `mawk`
    is required by `prepare_port ncurses`: the ncurses 5.9 `Caps` header
    generators are invoked as `mawk` by name — `gawk` is not a drop-in
    here, the scripts mis-parse under it.)

3. **Clone Sponge OS**:
   ```bash
   git clone https://<sponge-os-origin>.git ~/sponge-os
   cd ~/sponge-os
   ```
   The clone path can contain almost anything **except spaces** (see
   §8 / Phase 1 lessons in `docs/09-roadmap.md`).

4. **Install the Mojo SDK.** It serves both the host-side tools under
   `tool/` and the Mojo object builds that Genode/seL4 components
   link (`docs/08-development.md` §1.5), so this install covers
   component builds too, not just host tooling. The dependency is
   declared in the committed `pyproject.toml` and pinned in `uv.lock`
   (the pin is load-bearing for KGEN symbol compatibility; see §3
   and `docs/evidence/mojo-ffi-probe.md` §5), so:
   ```bash
   uv sync
   .venv/bin/mojo --version
   ```

5. **Prepare a Genode build directory.** The wrapper script does this
   with one command:
   ```bash
   ./tool/build prepare
   ```
   which calls `genode/tool/create_builddir x86_64` against the
   vendored tree, then updates `genode/build/x86_64/etc/build.conf`:
   it switches the generated `#KERNEL ?= nova` / `BOARD ?= pc` template
   lines to `KERNEL ?= linux` / `BOARD ?= linux` **in place** (the
   template's own `ifdef` blocks read those variables before any
   appended content would take effect), and appends a marker-delimited
   managed block with `REPOSITORIES += sponge/libports/gems` and
   `MAKE += -j<nproc>`. The manual equivalent is:
   ```bash
   cd genode && ./tool/create_builddir x86_64
   cd build/x86_64
   sed -i 's/^#KERNEL ?= nova/KERNEL ?= linux/' etc/build.conf
   sed -i 's/^BOARD ?= pc/BOARD ?= linux/'            etc/build.conf
   echo 'REPOSITORIES += $(GENODE_DIR)/repos/sponge'   >> etc/build.conf
   echo 'REPOSITORIES += $(GENODE_DIR)/repos/libports' >> etc/build.conf
   echo 'REPOSITORIES += $(GENODE_DIR)/repos/gems'     >> etc/build.conf
   echo "MAKE += -j$(nproc)"                           >> etc/build.conf
   ```

6. **Prepare the third-party ports** for a full Sponge OS build:
   ```bash
   ./tool/build ports
   ```
   which runs `prepare_port` for `libc stdcxx mesa zlib libpng expat
   libdrm x86emu qoost qt6_api qt6_base sel4 sel4_tools grub2`
   (idempotent; already-prepared ports are skipped). The manual
   equivalent is `cd genode && ./tool/ports/prepare_port <names...>`.
   This populates `genode/contrib/` (git-ignored) with the sources
   listed in §5. (`qt6-host` is not in this list: the host-tools build
   in the next step fetches it itself.)

7. **Build and install the Qt6 host tools** into `var/qt6-host-tools/`:
   ```bash
   cd genode
   make -f tool/tool_chain_qt6 build MAKE_JOBS=$(nproc)
   make -f tool/tool_chain_qt6 install \
       INSTALL_LOCATION="$PWD/../var/qt6-host-tools" SUDO=
   cd ..
   ```
   (`tool_chain_qt6` is a makefile, not an executable script. The
   `install` target defaults to `/usr/local/...` with `sudo`; the
   overrides above keep everything inside the repository.)
   This is a one-time cost (~6.0 GB build tree under
   `genode/contrib/qt6-host-*`). After it finishes,
   `var/qt6-host-tools/bin/qmake` and
   `var/qt6-host-tools/libexec/moc` exist. `import-qt6.inc` finds them
   via the `QT_TOOLS_DIR` default set in patch #3.

8. **Run the minimal scenario**:
   ```bash
   ./tool/build run sponge-minimal
   ```
   This is equivalent to:
   ```bash
   make -C genode/build/x86_64 run/sponge-minimal
   ```
   On `base-linux`, the run tool spawns `./core` directly (no QEMU).
   Expected output ends with:
   ```
   Genode 26.05
   [init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
   [init -> vct] vct 0.0.1-pre-alpha
   [init -> vct] Sponge OS codename: Archaeocyte
   Run script execution successful.
   ```

### 7.2 base-sel4 target (production kernel, QEMU)

Steps 1 to 4 are identical to §7.1. From step 5:

5. **Same `prepare` step** (the managed block in `etc/build.conf`
   defaults to `KERNEL ?= linux`).

6. **Switch to seL4** in `genode/build/x86_64/etc/build.conf`:
   ```makefile
   KERNEL ?= sel4
   BOARD  ?= pc
   ```
   On a headless server also comment out `QEMU_OPT += -display sdl`
   so QEMU does not require an X display. Run scripts that need
   keyboard input pass `-nographic` themselves.

7. **Install the seL4 build's Python and C toolchain deps**:
   ```bash
   uv pip install future jinja2 ply six lxml pyfdt jsonschema
   ```
   These are the seven modules the seL4 cmake build imports. Without
   them, the kernel build fails very early with an unhelpful
   `ModuleNotFoundError: No module named 'ply'`.

8. **Prepare the seL4-specific ports** plus the bootloader:
   ```bash
   ./tool/build ports
   (`prepare_port` skips anything already prepared; the manual
   equivalent is `./tool/ports/prepare_port sel4 sel4_tools grub2`)
   ```

9. **Run the minimal scenario on seL4 / QEMU**:
   ```bash
   ./tool/build run sponge-minimal
   ```
   Expected QEMU output ends with:
   ```
   Genode 26.05
   699 MiB RAM and 523288 caps assigned to init
   [init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
   [init -> vct] vct — Very Convenient Tool
   [init -> vct] version: 0.0.1-pre-alpha (Archaeocyte)
   Run script execution successful.
   ```

**Why `-m 1G`?** The run script's `append qemu_args " -nographic -m 1G "`
is not decorative. `base-sel4` on QEMU needs at least **1 GiB of
guest RAM**. seL4 reserves a SKIM window (Meltdown mitigation) plus
its own kernel structures; with 256 MiB the kernel fails at boot with
`seL4 failed assertion 'load_paddr' at boot_sys.c:120` because it
cannot find a contiguous physical region large enough for the
~30 MiB boot module. 1 GiB is the smallest QEMU RAM size that leaves
enough contiguous physical memory below 1 GiB.

**Large-payload caveat (Falkon / WebEngine).** The seL4 boot chain
(GRUB → Bender → seL4 core) has a practical ceiling on total boot
module size: Bender's module relocation fails above ~256 MB ("no
memory for relocation found"), and seL4's initial untyped capability
cnode cannot map a single boot module larger than ~256 MB (the kernel
resets during boot module setup). `run/sponge-falkon.run` works around
the Bender limit by packing falkon's ~500 MB Qt6/WebEngine closure into
a separate `falkon_payload.tar` multiboot2 module (not inside
`image.elf`) and serving files via `tar_rom`; however, the seL4 untyped
cnode limit still blocks the separate module. The resolution path
(future work) is disk-based payload staging (block driver + filesystem +
ROM-from-FS server, as Sculpt OS does for depot packages). The Alpha
documents this as a known limitation in `docs/13-installation.md` (todo
19) and `run/sponge-falkon.run`'s header comment.

### 7.3 Media creation host tools (ISO + disk image)

Producing a bootable ISO or disk image of a base-sel4 scenario (the
`RUN_OPT="--include image/iso"` / `RUN_OPT="--include image/disk"`
modes; see `run/sponge-media-smoke.run` for the smoke test and
`run/sponge-alpha.run` for the full desktop) requires six host tools
that the minimal-boot path does not. They are invoked by the Genode
run framework's image plugins (`genode/tool/run/image/iso`,
`genode/tool/run/image/disk`, and `genode/tool/run/iso.inc`):

| Tool | Provided by (apt) | Used by | Purpose |
|------|-------------------|---------|---------|
| `xorriso` | `xorriso` | `image/iso` | Writes the El Torito bootable ISO (mkisofs mode). |
| `sgdisk`  | `gptfdisk` | `image/disk` | Manages the GPT partition table on the disk image. |
| `mcopy`   | `mtools` | `image/disk` | Copies the EFI bootloader into the EFI System Partition. |
| `e2cp`, `e2mkdir` | `e2tools` | `image/disk` | Copies the boot modules into the ext2 content partition. |
| `mkfs.vfat` | `dosfstools` | (EFI partition fallback) | Formats the EFI System Partition when needed. |
| `mkfs.ext2`, `resize2fs` | `e2fsprogs` | `image/disk` | Formats and shrinks the ext2 content partition. |

Install all six on Debian / Ubuntu:

```bash
sudo apt install xorriso gptfdisk mtools e2tools dosfstools e2fsprogs
```

(Arch: `sudo pacman -S xorriso gptfdisk mtools e2tools dosfstools e2fsprogs`.)

The smoke test detects missing tools at the `installed_command` call
site inside the image plugin and exits with a clear error; install the
listed package and re-run. `e2tools` is the one most commonly missing
on minimal server images — it is only needed for the disk-image path,
not the ISO path.

---
