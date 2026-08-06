# 11 - Development Environment & Reproducibility

> The single source of truth for the development environment contract.
> Read this once, follow the bootstrap section, and you have a build
> that is bit-for-bit identical to every other Sponge OS developer's
> build.

---

## 1. Purpose

Sponge OS does not float. Every byte of Genode that compiles into a
Sponge OS image is **vendored**, **pinned by commit**, and **carried
inside this repository**. Reproducibility is a hard requirement, not a
nice-to-have: a contributor on a clean machine must reach the same boot
log as everyone else, and "Genode released a new version" must never be
a surprise.

How it got this way. Before the restructure, Sponge OS used an
**external, mutable** Genode checkout at `/home/luke/genode`. The
checkout had **five uncommitted patches** that never made it into a
reviewable place. The compiler wrappers that scope the Qt6 build away
from the host `/usr/include` headers lived in `/tmp/opencode/bin/`, a
directory that `systemd-tmpfiles` happily sweeps on every reboot. The
run scripts under `repos/sponge/run/` were absolute symlinks to
`/home/luke/sponge-os/run/`, which silently breaks the moment any
contributor's home directory is named differently. The repository
itself was not under version control at all, so the project's history
was "whatever happened to be on disk". The documentation pointed at
Genode's floating `main` branch. Every one of those failure modes is
now gone, replaced by a single git tree that pins everything that
matters and rejects anything that drifts.

The restructure is captured in the following commits on the `main`
branch:

| Commit  | Subject |
|---------|---------|
| `faf6772787` | chore(repo): initial import (pre-vendoring baseline) |
| `894fa063ca` | docs(agents): replace external-Genode policy with vendored-subtree policy |
| `976a487b3c` | feat(genode): vendor Genode 26.05 as git subtree at genode/ |
| `4831b2db0d` | fix(genode): stdcxx port: replace dead gcc mirror with ftp.gnu.org |
| `80356d3f08` | build(genode): disable qttools in qt6 host-tools build |
| `69dde3ae96` | build(genode): parameterize QT_TOOLS_DIR with repo-local default |
| `595d5fc4f6` | fix(genode): qt6 cmake build: block host-header leakage via sponge-owned wrappers |
| `70900e8528` | feat(tool): add sponge-owned compiler wrappers + wire repos/sponge into vendored tree |
| `6afb6f6b34` | fix(run): replace absolute run-script symlinks with relative ones |

---

## 2. Repository Layout

The repository root is `sponge-os/`. Everything below lives inside it.

```
sponge-os/
├── README.md                   # User-facing introduction
├── AGENTS.md                   # Contribution guidelines (top of doc hierarchy)
├── docs/                       # All detailed documentation (including this file)
├── genode/                     # Vendored Genode 26.05 (git subtree, pinned)
│   ├── repos/                  # Upstream Genode repositories
│   │   ├── base                # base, base-linux, base-hw, base-sel4, base-nova, ...
│   │   ├── os                  # init, drivers, services
│   │   ├── libports            # libc, stdcxx, qt6_*, mesa, libpng, zlib, ...
│   │   ├── gems                # nitpicker, scout, leitzentrale, ...
│   │   ├── ports               # third-party port recipes (.port/.hash files)
│   │   └── sponge -> ../../repos/sponge   # relative symlink (committed)
│   ├── tool/                   # Genode's own build helpers (create_builddir, ports/...)
│   ├── contrib/                # NOT vendored; populated by tool/ports/prepare_port
│   ├── depot/                  # NOT vendored; build output staging area
│   ├── build/                  # NOT vendored; per-target build directories
│   └── Makefile, etc/, ...     # upstream Genode 26.05 contents
├── repos/
│   └── sponge/                 # Sponge OS repository (Genode repo convention)
│       ├── src/                # Component sources
│       │   ├── vct/            # System management CLI
│       │   ├── sponge-de/      # Desktop environment
│       │   └── sponge_launcher/# Launcher
│       ├── lib/                # Shared libraries
│       ├── include/sponge/     # Shared headers
│       ├── tool/               # Sponge-owned compiler wrappers (Genode build hooks)
│       └── run/                # Relative symlinks to ../../../run/ (one per scenario)
├── tool/                       # Sponge-owned host-side tooling (Mojo)
├── run/                        # Genode run scenarios (.run files)
└── var/                        # Local caches (git-ignored): qt6 host tools, distfiles
```

Two layout facts deserve attention:

- `genode/repos/sponge` is a **committed relative symlink** to
  `../../repos/sponge`. It is the bridge that lets Genode's build
  system find Sponge OS components without a contributor ever having
  to wire anything up by hand. Because the symlink is relative, the
  whole tree is movable: cloning `sponge-os` to a path with any name
  under any home directory just works.
- `genode/build/`, `genode/contrib/`, and `genode/depot/` are all
  excluded by `genode/.gitignore` (which covers `/build`, `/contrib`,
  and `/depot` already). `genode/var/` is added by the root
  `.gitignore` so the qt6 host-tools cache and any per-subtree scratch
  space stay out of version control. The root `.gitignore` also covers
  `/var/`.

---

## 3. Pinned Versions

Every piece of the build that has a version has a pin, and every pin
has a source. No exceptions.

| Component | Version | Pin / Fingerprint | Source / Upgrade Path |
|-----------|---------|-------------------|------------------------|
| **Genode OS Framework** | 26.05 | upstream commit `492a51024217fe74ccee1ebdfb81be97046b43eb` (`codeberg.org/genodelabs/genode` tag `26.05^{}`) | vendored at `genode/` via `git subtree`; upgrade via `git subtree pull --prefix=genode` (recorded as a dedicated commit) |
| **Genode toolchain** | 25.05 (GCC 14.2.0, binutils 2.44) | `/usr/local/genode/tool/25.05/`, with `/usr/local/genode/tool/current -> 25.05` | external; installed by `sudo tar xPf genode-toolchain-25.05.tar.xz` from <https://genode.org/download/tool-chain> (or `genode-toolchain-bin` AUR on Arch/CachyOS). **No 26.05 toolchain tarball was published**; 25.05 is the official pairing. Genode uses a 2-year toolchain cycle. |
| **Qt6** | 6.8.3 | `qt6_base` port (`qt6_base.hash = 67348e71a70138daef52b157470f5796f758507f`) and `qt6_api` port (`qt6_api.hash = 55bfba5647db8f93f91a61a61ee0548de108348c`) | host tools produced by `make -f tool/tool_chain_qt6 build` + `install INSTALL_LOCATION=<repo>/var/qt6-host-tools SUDO=` (build tree ~6.0 GB under `genode/contrib/qt6-host-*`, installed tools ~80 MB at `var/qt6-host-tools`), referenced by `QT_TOOLS_DIR ?= $(abspath $(GENODE_DIR)/../var/qt6-host-tools)` in `genode/repos/libports/lib/import/import-qt6.inc` (Sponge patch #3) |
| **QEMU** | 11.0.2 | host package | managed by the operating-system package manager; no Sponge-side pinning needed |
| **GNU Make** | system package | n/a | managed by the operating-system package manager |
| **Mojo SDK** | pinned by `uv.lock` (currently 1.0.0b2) | `pyproject.toml` + `uv.lock` (committed); `uv sync` materializes the project-local `.venv` | used by host-side tooling under `tool/`; not used at runtime inside Genode |
| **Python modules (seL4 kernel build)** | as-needed | `future jinja2 ply six lxml pyfdt jsonschema pyyaml` | `uv pip install future jinja2 ply six lxml pyfdt jsonschema pyyaml` into the same `.venv`; only required when `KERNEL ?= sel4`. (`pyyaml` is imported by seL4's `tools/config_gen.py`.) |
| **Python modules (Qt6 host-tools build)** | as-needed | (via `tool_chain_qt6`, system packages) | only required for the one-time Qt6 host-tools build (§7.1 step 7). |
| **`bc`** | system package | n/a | required only when building the `drivers_interactive-pc` USB stack on base-sel4: `dde_linux`'s `usb_hid` / `pc_usb_host` build the Linux kernel, whose `include/generated/timeconst.h` is generated with `bc`. Install via the distro package manager, or drop a portable `busybox` on `PATH` with a `bc -> busybox` symlink. Not needed for base-linux runs. |
| **CMake + Ninja** | system package | n/a | required for the seL4 kernel build and for the Qt6 library build |
| **Tcl / expect** | system package | n/a | required by Genode's run tool to drive the QEMU interaction |

The pin on Genode is the **single most important** number in this
document. The commit `492a51024217fe74ccee1ebdfb81be97046b43eb` is the
upstream tag `26.05^{}` from <https://codeberg.org/genodelabs/genode>
(Genode's canonical home on Codeberg; GitHub is archived as of May
2026). Every patch listed in §4 sits on top of this commit and nothing
else. Bumping to 26.06, when it ships, is a deliberate `git subtree
pull --prefix=genode` whose commit message records the upstream bump.

---

## 4. Patch Ledger

The vendored tree at `genode/` is **Genode 26.05 pristine** plus the
Sponge-specific commits listed below. Each row gives the commit
subject, the SHA-short prefix you see in `git log`, the **what**, the
**where**, the **why**, and a note on how to drop the patch when
upstream absorbs the fix.

| # | Commit | Subject | What / Where | Why | Drop When |
|---|--------|---------|--------------|-----|-----------|
| 1 | `4831b2db0d` | fix(genode): stdcxx port: replace dead gcc mirror with ftp.gnu.org | `genode/repos/libports/ports/stdcxx.port`: `URL(gcc)` switched from `http://ftp.fu-berlin.de/...` to `https://ftp.gnu.org/gnu/gcc/...`; `.hash` updated to match | The `ftp.fu-berlin.de` mirror stopped serving the GCC tarball, so `prepare_port stdcxx` failed at the download step on a fresh machine. | Upstream updates the `stdcxx.port` URL itself, which would re-bump the hash. |
| 2 | `80356d3f08` | build(genode): disable qttools in qt6 host-tools build | `genode/tool/tool_chain_qt6`: `BUILD_qttools=ON` switched to `OFF` | `qttools` is unneeded for the Core/Gui/Widgets-only DE Sponge OS builds, and was the failing part of the host-tools build on a fresh machine. | Sponge OS gains a Qt tools dependency (Designer, Linguist, …) at which point upstream's default is fine. |
| 3 | `69dde3ae96` | build(genode): parameterize QT_TOOLS_DIR with repo-local default | `genode/repos/libports/lib/import/import-qt6.inc`: `QT_TOOLS_DIR ?=` pointing at `<repo>/var/qt6-host-tools`. Amended by a follow-up commit: the default is derived from the file's own location (`$(dir $(realpath ...))/../../../../../var/qt6-host-tools`) instead of `$(GENODE_DIR)`, because `GENODE_DIR` is not exported to per-target sub-makes and would expand to `/var/qt6-host-tools` there. | Upstream hardcodes `/usr/local/genode/tool/25.05/qt6.8.3`, which does not exist when the host tools are built from source into the repo-local cache. | Upstream parameterizes `QT_TOOLS_DIR`. |
| 4 | `595d5fc4f6` | fix(genode): qt6 cmake build: block host-header leakage via sponge-owned wrappers | `genode/repos/libports/src/qt6/base/target.mk` (cmake invocation): compiler pointed at the wrappers `repos/sponge/tool/genode-x86-{gcc,g++}-wrapper`, which strip leaked host `-I/-isystem /usr/include` and `/usr/local/include` paths; clear `CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES` / `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES`; add `-D__GNUCLIKE_BUILTIN_STDARG` and `-include sys/cdefs.h` for FreeBSD-derived libc headers; use qtbase's bundled `md4c` include; disable `system_doubleconversion`, `system_md4c`, `textmarkdownreader`, `textmarkdownwriter` Qt features. Amended by a follow-up commit: the wrapper directory is derived from the file's own location (`SPONGE_TOOL_DIR`) instead of `$(GENODE_DIR)` — same sub-make visibility reason as patch #3. | CMake's compiler introspection leaked host include paths into the Qt6 cross-build, breaking it against Genode headers, and the disabled features pulled in third-party dependencies that are not available on the Genode side. | Upstream either fixes the cmake invocation to never see host headers, or Sponge OS no longer uses Qt6 (unlikely). |
| 5 | `70900e8528` | feat(tool): add sponge-owned compiler wrappers + wire repos/sponge into vendored tree | new files `repos/sponge/tool/genode-x86-gcc-wrapper` and `genode-x86-g++-wrapper` (the bash filter scripts patch #4 leans on); `genode/repos/sponge` relative symlink to `../../repos/sponge` | The wrappers used to live in `/tmp/opencode/bin/` (a `tmpfiles`-swept path). Reboot wiped them and broke the build with a generic "no such compiler" error. Putting them under version control makes them part of the reproducible environment. | Never (these are Sponge-owned artifacts, not upstream patches). |
| 6 | `93ded092f1` | feat(genode): sel4: size-aware on-demand CNode backing for large child-PD CSpaces | `genode/repos/base-sel4/`: `src/core/include/cnode.h` (CNode ctor allocates backing sized to the requested CNode instead of one 4 KiB page), `src/core/include/untyped_memory.h` (size-aware untyped selector addressing), `src/include/base/internal/capability_space_sel4.h` (enlarged per-PD CSpace), `src/core/spec/{x86_64,arm,arm_v8a}/platform.cc` (allocator wiring; x86_64 path proven, other arches kept upstream-shaped) | Falkon-class dynamic workloads exhaust the fixed 8192-slot per-PD CSpace (each child-PD CNode backed by exactly one 4 KiB untyped page → 128 slots at seL4's 32-byte CTE). The fix makes the backing size-aware and on-demand, WITHOUT the fixed 16K-pool carve that regressed ahci DMA (diagnosis + canaries in `docs/evidence/c1-dma-safe-backing.log`). | Upstream resolves the long-standing `platform.cc:108` `XXX` ("allocate intermediate CNodes ... here") with dynamic CNode allocation. |
| 7 | `19303468ff` | feat(genode): sel4: lazy vm_space lower-level CNode growth (131072 mappings/PD) | `genode/repos/base-sel4/`: `src/core/include/vm_space.h` (`LEAF_CNODE_SIZE_LOG2` 7→9 on x86_64 → `NUM_VM_SEL_LOG2` 15→17; `_ensure_leaf` constructs 3rd/4th-level CNodes lazily on first use; no-op-safe destruct), `src/core/platform.cc` (core virt reservation capped to 256 MiB), `src/core/spec/x86_64/platform.cc` (16K pool +32 MiB for lazy vm leaves) | The per-PD mapping pool was a static 32768-entry tree (`NUM_VM_SEL_LOG2=15`) that falkon (~100k frames) and rom_pkg (~130k) both exhaust ("mapping cache full / out of selector"). Eager enlargement starved core heap or crashed Platform(); the fix constructs lower-level CNodes lazily on first use, growing to 131072 entries without boot-time pressure (canaries in `docs/evidence/c2-lazy-vmspace.log`). | Upstream resolves `platform.cc:108` `XXX` / grows vm_space dynamically. |
| 8 | `dc4a9f342f` | fix(genode): bash/ncurses ports: replace 502-ing ftpmirror.gnu.org with ftp.gnu.org | `genode/repos/ports/ports/bash.port` and `genode/repos/libports/ports/ncurses.port`: `URL(...)` switched from `https://ftpmirror.gnu.org/...` to `https://ftp.gnu.org/gnu/...`; the two `.hash` files regenerated via `tool/ports/update_hash` | `ftpmirror.gnu.org` answers 502 Bad Gateway (mirror redirector outage), so `prepare_port bash ncurses` failed at the download step on a fresh machine; `ftp.gnu.org` serves the identical tarballs (SHA-256 unchanged). Same failure class as patch #1. | Upstream switches the two ports to a working mirror itself (which would re-bump the hashes). |

A patch is **never** silently absorbed into Sponge OS. When a row in
this table becomes obsolete, the corresponding commit is reverted (or
the patch is dropped by re-running `git subtree pull`) in its own
commit on `main`, the patch ledger row is removed, and the commit
message says so.

### 4.1 Managing the ledger: tool/patches

The ledger table above is the source of truth, and
[`tool/patches`](../tool/patches) (Mojo source:
[`tool/patches.mojo`](../tool/patches.mojo)) is the convenience layer on
top of it. It answers the four recurring questions about the patch set
without the contributor having to remember the git incantations:

```bash
./tool/patches list                # ledger rows + whether git resolves each commit
./tool/patches verify              # ledger vs git reality; exits non-zero on mismatch
./tool/patches export <dir>        # write each patch as a .patch file into <dir>
./tool/patches drop <n>            # print the manual revert steps for patch #n
```

- `list` prints one `#<n> <sha-prefix> <subject>` line per row, then
  resolves each prefix with `git log -1 --format=%H%n%s <sha>` and
  reports whether the commit exists and whether its full SHA starts
  with the ledger prefix.
- `verify` checks, per row, that the commit exists, that it is an
  ancestor of `HEAD` (`git merge-base --is-ancestor`), and reports the
  paths it touches (`git show --format= --name-only`). Touched paths
  are **reported, not policed**: patch #5 legitimately adds
  `repos/sponge/tool/` wrappers next to the `genode/` symlink, so there
  is no genode-only rule to enforce. `verify` exits non-zero if any
  row's commit is missing.
- `export <dir>` runs `git format-patch -1 <sha> --stdout` per row and
  writes `<dir>/<NN>-<slugified-subject>.patch` so the patch set can be
  carried or re-applied independently of this repository. The
  directory is created if missing.
- `drop <n>` **prints instructions only** — the exact
  `git revert <full-sha>` command, the reminder to remove ledger row
  `#n` from the table above, and the note that the removal must be its
  own commit on `main` (AGENTS.md §5.2). Dropping a patch is a
  deliberate act; the tool guides, the human commits. Auto-revert is
  deliberately not implemented.

**Manual equivalents** (the control escape hatch, per AGENTS.md §3.5 —
every automated step is also a documented manual step):

| Subcommand | Manual equivalent |
|------------|-------------------|
| `list` | Read the table above; `git log --oneline -- genode/` |
| `verify` | `git show -s <sha>` (exists), `git merge-base --is-ancestor <sha> HEAD` (ancestor), `git show --stat <sha>` (touched paths) |
| `export <dir>` | `git format-patch -1 <sha> --stdout > <dir>/<name>.patch` per row |
| `drop <n>` | `git revert <sha>`, then edit the table above and commit both changes together |

The tool is read-only against the repository: it never reverts,
commits, or edits the ledger itself.

---

## 5. Third-Party Ports

Genode builds against a set of third-party C/C++ libraries. The source
trees for those libraries are **NOT vendored**. Genode's
`tool/ports/prepare_port` script fetches them on demand and verifies
them by SHA-256. The hash files live next to the `.port` files and
travel with the vendored Genode tree.

The full port set that a Sponge OS build pulls in, with the SHA-256
fingerprint of each:

| Port | Fingerprint | Origin | Notes |
|------|-------------|--------|-------|
| `qt6-host` | `59fffed65168110202f765ac56d4434ba21b0f14` | `https://download.qt.io/archive/qt/6.8/6.8.3/single/qt-everywhere-src-6.8.3.tar.xz` | The Qt6 source tree used to build the host tools (`qmake`, `moc`, `rcc`, …) into `var/qt6-host-tools/`. ~6.0 GB on disk after build. |
| `qt6_base` | `67348e71a70138daef52b157470f5796f758507f` | `https://codeberg.org/cproc/qt6_base.git` @ `issue5873` | The cross-compiled Qt6 base modules. |
| `qt6_api` | `55bfba5647db8f93f91a61a61ee0548de108348c` | `https://codeberg.org/cproc/qt6_api.git` @ `issue5854_2` | The Genode-side Qt6 API stubs. |
| `libc` | `d6a3665f0d2778ce8928c66302f1694cdc0d8480` | Genode-internal | The minimal libc used by userland components. |
| `stdcxx` | `41f7b34917a64abd5f045066a0072654b39a7b39` | `https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz` | libstdc++ headers and runtime. **Current** post-patch fingerprint; the superseded pre-patch fingerprint was `8ea10de2dbbbf60f50e5fc6be1ed0fc5c781f4db` (mirrored from `ftp.fu-berlin.de`). |
| `mesa` | `3bc576bbd58d2df375b56c010ab9ee0c431569d1` | Mesa upstream | Used by the `mesa_gpu-softpipe` library that sponge-de links. |
| `sel4` | `54809b28d155db2e1ab83120937bde016a56fcb9` | `https://github.com/seL4/seL4` | The seL4 microkernel source. |
| `sel4_tools` | `820e7e64ff2ef5dacbdedf71c33927fb2de2884f` | `https://github.com/seL4/seL4_tools` | Build infrastructure for seL4 (`elfloader-tool`, `ramdisk.cmake`, ...). |
| `grub2` | `eb7172dee270fbd9f1bc862d46725fd1fb21d1ea` | GNU GRUB upstream | The bootloader used to produce the `base-sel4` ISO image. |
| `libpng` | `0be8174fb4e22b291eeb4a7d48473ffb1b3dd7d2` | libpng upstream | Pulled in by Sponge DE's image-loading path. |
| `zlib` | `7012a4d3c0949afdcac1c6567cfda7f84163e0a1` | zlib upstream | Pulled in transitively by libpng and others. |
| `expat` | `9e709d49f9245c0df1849e9a89879c38010e1192` | libexpat upstream | XML parsing. |
| `libdrm` | `9859ccc883beb4b97ce02ba028a3661a43701001` | libdrm upstream | Direct Rendering Manager userspace library. |
| `freetype` | `35b677031728f8d9728d69ac44623909c1fad92d` | `https://sourceforge.net/projects/freetype/files/freetype2/2.3.9/freetype-2.3.9.tar.gz` (GPLv2) | FreeType 2.3.9 font rasterization library. Required by the `qt6_textedit` runtime (todo 14): the upstream `cproc/pkg/qt6_textedit/runtime` `<content>` block lists `freetype.lib.so` as one of the common Qt GUI ROMs the binary expects at boot. Built by the `lib/freetype` target (libports). |
| `jpeg` | `cecaf6804d1785610c7cafb7b7abb5858c0af122` | `http://www.ijg.org/files/jpegsrc.v9d.tar.gz` (JPEG, independent) | libjpeg 9d from the Independent JPEG Group. Required by the `qt6_textedit` runtime (todo 14): the upstream `<content>` block lists `jpeg.lib.so` for Qt's image-format plugins (libqjpeg). Built by the `lib/jpeg` target (libports). |
| `x86emu` | `8a1c3aa1d592fd3e5a1b0798471f8364870b79ae` | x86emu upstream | x86 instruction emulator used by the `softpipe` software rasterizer. |
| `qoost` | `014d68ce23644076c30c2dc03ee70c8fa04698d7` | qoost upstream | QtObjectSystemTester-style helpers used by some libports components. |
| `linux` | `e4aad15aa6e3267bf6f8ac2b1b51766c03a8d82b` | Linux 6.18.19 (`dde_linux/ports/linux.port`) | Linux kernel source used by the DDE-Linux USB stack (`usb_hid`, `pc_usb_host`) that the base-sel4 interactive scenario (`run/sponge-de-sel4-interactive.run`) pulls in. Only fetched when those drivers are built; large (~140 MB tarball). |
| `jitterentropy` | `jitterentropy-3.4.1` | `smuellerDD/jitterentropy-library` v3.4.1 | Entropy source linked by the DDE-Linux USB drivers (`virt_lx_emul` / `pc_lx_emul`). Only fetched with the `linux` port above. |
| `dde_ipxe` | `a7206d6c2a1b2de7fa7fde6733a3500411a64de2` | `https://github.com/ipxe/ipxe.git` @ `c4bce43c3c4d3c5ebb2d926b58ad16dc9642c19d` | The iPXE NIC driver stack (GPLv2). Provides the e1000 NIC driver (`dde_ipxe/src/driver/nic` → `ipxe_nic`) used by the base-sel4 networking probe (`run/sponge-net-probe.run`). Only fetched when the dde_ipxe repository is in `REPOSITORIES`. |
| `lwip` | `d4911a43269263cd9376b6bc3ad98d28b749b0e3` | `https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_1_2_RELEASE.tar.gz` (v2.1.2, BSD) | The lwIP TCP/IP stack, used as the in-process socket provider for libc-based Genode components via the `vfs_lwip` plugin. `run/sponge-net-probe.run` (todo 12) wires it as `<dir name="socket"><lwip dhcp="yes"/></dir>` in `fetchurl`'s vfs. |
| `dde_rump` | `19d6de3e1210c7f1c033d46d4178694d263cf0c1` | `https://github.com/ssumpf/rump.git` @ `28945d1af5f66c98a3884453adf84ede4ca1b702` (plus sparse checkouts of `justincormack/netbsd-src` for arch-specific libc atoms) | The NetBSD rump kernel, used by the `vfs_rump` plugin (`lib/vfs_rump`, `repos/dde_rump/lib/mk/vfs_rump.mk`) to provide ext2fs/ffs/msdos/cd9660/ntfs/udf filesystems. Required by the base-sel4 storage chain (`run/sponge-boot.run`, docs/14 §5): `<rump fs="ext2fs"/>` mounts the GENODE partition. Prepared in Phase 8 P1; `repos/dde_rump` is added to `REPOSITORIES` by `tool/build prepare`. |
| `bash` | `20c34a63efce74be02c9eb92bd8bfb53cd5122e5` | `https://ftp.gnu.org/gnu/bash/bash-5.3.tar.gz` (GPLv3) | GNU bash, built as the `noux-pkg/bash-minimal` package (`bash-minimal.tar`) that ships inside the `terminal` package (todo 13). Genode's noux bash build does **not** link readline/ncurses. **Current** post-patch fingerprint (patch #8); the superseded pre-patch fingerprint was `93dd45640aa0cbd1a850deb4bee4cf6a332e61bb` (mirrored from `ftpmirror.gnu.org`). |
| `vim` | `abada0b43ca034d23ba39f3281bff997f08fc884` | `https://github.com/vim/vim/archive/v7.3.tar.gz` (Vim license) | Vim 7.3, built as the `noux-pkg/vim-minimal` package (`vim-minimal.tar`) that ships inside the `terminal` package (todo 13). Links ncurses (`--with-tlib=ncurses`). |
| `ncurses` | `b259f9aa8136195dc38361357e62355a6fe066e8` | `https://ftp.gnu.org/gnu/ncurses/ncurses-5.9.tar.gz` (MIT) | The ncurses 5.9 terminal library, required by the `vim-minimal` noux package. Lives in the `libports` repo (not `ports`). Its `Caps` header generation invokes `mawk` by name — see the `mawk` host-tool requirement in §7. **Current** post-patch fingerprint (patch #8); the superseded pre-patch fingerprint was `5fb8a84ea7e768167f325dccabde30f2e6e56f72` (mirrored from `ftpmirror.gnu.org`). |
| `stb` | `ab8f505722d9668c907cabba084f96b886985056` | `https://github.com/nothings/stb.git` (v2.19, MIT) | stb single-file header libraries (stb_image), used by the alpha desktop scenario's image-decode path (`run/sponge-alpha.run`). Lives in the `libports` repo. Added to the default port set after the alpha scenario's `check_ports` flagged it missing on a fresh machine. |
| `ttf-bitstream-vera` | `cd3684816b73c4361e11236f9e63302f99b9b1ff` | `http://ftp.gnome.org/pub/GNOME/sources/ttf-bitstream-vera/1.10/ttf-bitstream-vera-1.10.tar.bz2` (Bitstream Vera Fonts Copyright) | Bitstream Vera font set (v1.10), staged as a runtime ROM by the alpha desktop scenario (`run/sponge-alpha.run`). Lives in the `libports` repo. Added to the default port set after the alpha run stage flagged it missing on a fresh machine. |

In addition to the `prepare_port` set above, two **Genode depot packages**
are imported host-side into `pkg/<name>/` by `tool/pkg_import` (todo 11).
The depot is **not** a port: it is fetched by `genode/tool/depot/download`,
verified by PGP against the in-tree pubkey at
`genode/repos/gems/sculpt/depot/cproc/pubkey`, and re-packaged locally.
The depot archives never leave the build host — `sponge_pkgd` does not
fetch at runtime (Metis amendment A1).

| Depot archive | Version | SHA-256 of `<user>/<type>/<recipe>/<version>.tar.xz` | Used by | Notes |
|---|---|---|---|---|
| `cproc/pkg/falkon_qt6-jemalloc` | `2026-04-22` | `23e6a2b6be18b71cb7efc5a33018d3fa9b978310a9daf13ac0eae0f2fbc00b1e` | `pkg/falkon/` (todo 16) | Pinned by `genode/repos/gems/sculpt/deploy/falkon_web_browser` line 28. The pkg recipe name (`falkon_qt6-jemalloc`) differs from the underlying binary recipe name (`falkon_qt6`); `tool/pkg_import` resolves the correct `bin/x86_64/falkon_qt6/<version>` archive. The full transitive closure is ~1.8 GB (1 pkg + 25 src + 2 raw + 43 api archives). |
| `cproc/pkg/qt6_textedit` | `2025-10-27` | `61d207c2fab6f2901e50cbbc714f5522997823d78e1323bd6da638ccd8fdfa1f` | `pkg/textedit/` (todo 14) | **The in-tree recipe hash at `genode/repos/libports/recipes/pkg/qt6_textedit/hash` says `2026-05-27`, but the depot has not published that version (404 as of the todo-11 spike).** `tool/pkg_import` therefore takes an explicit version on the command line and uses the depot's latest available (`2025-10-27`). Re-running the spike after a future depot publish should restore the recipe pin. The corresponding `bin/x86_64/qt6_textedit/2025-10-12` is the latest prebuilt binary version; pkg-vs-bin versions are independent in the depot. |

Populated by:

```bash
./tool/build ports           # Sponge-side alias (preferred)
# or, manually:
cd genode && ./tool/ports/prepare_port <port-name>
```

What does and does not go through `prepare_port`:

- `genode/contrib/` (the downloaded sources, ~8.5 GB total for the
  full port set above) is **git-ignored**. It is rebuilt on demand by
  `prepare_port` against the SHA-256 fingerprints in the table.
- `genode/build/` (the build artifacts, ~6.7 GB for a full base-linux
  build, more for base-sel4 with a debug seL4) is **git-ignored**.
- The Qt6 host-tools build tree (`var/qt6-host-tools/`, ~6.0 GB) lives
  under the **root** `var/`, which is also git-ignored. It is built
  once by `genode/tool/tool_chain_qt6` and reused across builds.

The git-ignore covers the three big non-source directories:

- `genode/.gitignore`: `/build`, `/contrib`, `/depot`, plus
  `/repos/{allwinner,imx,riscv,rpi,world,zynq}` (irrelevant upstream
  BSPs).
- root `.gitignore`: `/var/`, `/build/`, `/out/`, `genode/var/`, plus
  agent and editor scratch files.

---

## 6. What is Deliberately NOT Vendored

Three categories of artifact stay outside the repository on purpose.
Each one is recoverable from a public source by SHA-256 (or by
distribution package manager), so losing the local copy is recoverable
but not free.

1. **Genode third-party port sources (`genode/contrib/`).** These
   total ~8.5 GB on disk. They are pinned by upstream `.port` /
   `.hash` files and re-fetched by `./tool/build ports` (or
   `genode/tool/ports/prepare_port`). Vendoring them would bloat the
   repo by an order of magnitude for no reproducibility gain. The
   SHA-256 hash IS the reproducibility contract.

2. **The Genode toolchain (`/usr/local/genode/tool/25.05/`).**
   External to the repo. The compiler version is enforced by Genode's
   build system via a hard `$(error)` if `$(CC)` reports the wrong
   version. There is no 26.05 toolchain tarball; Genode uses a 2-year
   toolchain cycle and the 25.05 release is the official pairing for
   26.05. Install once per machine from
   <https://genode.org/download/tool-chain> (Arch users: the
   `genode-toolchain-bin` AUR package is maintained by a Genode Labs
   employee).

3. **QEMU and other host packages** (`qemu-system-x86_64` 11.0.2, GNU
   Make, Tcl/expect, cmake, ninja). These come from the operating
   system package manager. Pinning host-package versions is the job of
   the developer's distro, not this repository.

4. **The Mojo SDK** (`pyproject.toml` + `uv.lock`, `uv sync`). Mojo is a
   host-only language used by `tool/*.mojo`. The exact release is pinned
   by the committed `uv.lock` (currently 1.0.0b2) and materialized into
   a project-local `.venv`; it never touches Genode.

---

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

4. **Install the Mojo SDK** for the host-side tools under `tool/`.
   The dependency is declared in the committed `pyproject.toml` and
   pinned in `uv.lock`, so:
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

## 8. Environment Variables & Build Knobs

The knobs that affect a Sponge OS build, grouped by who sets them.

| Knob | Set By | Default | Purpose |
|------|--------|---------|---------|
| `GENODE_DIR` | `create_builddir`-generated `etc/build.conf` | absolute path of the `genode/` vendored tree | Every Genode build rule eventually resolves to `$(GENODE_DIR)/...`. The tool wrappers under `repos/sponge/tool/` and the `import-qt6.inc` makefile both rely on it. |
| `QT_TOOLS_DIR` | `import-qt6.inc` (patch #3) | `$(abspath $(GENODE_DIR)/../var/qt6-host-tools)` | Tells the Qt6 cross-build where to find the host `qmake`, `moc`, `rcc`, etc. Override on the make command line or in `etc/build.conf` if you keep the host tools elsewhere. |
| `KERNEL` | `etc/build.conf` | `linux` (set by `./tool/build prepare`) | One of `linux`, `sel4`, `hw`, `nova`. `linux` skips the microkernel build entirely (no QEMU needed for the run). `sel4` triggers the cmake kernel build and QEMU. |
| `BOARD` | `etc/build.conf` | `linux` (set by `./tool/build prepare`) | Board flavor for the chosen kernel. For seL4 the meaningful value is `pc`; for the Linux dev target it is `linux`. |
| `REPOSITORIES` | `etc/build.conf` | `["base", "os", "demo", "ports", "libports", ...]` | Genode repo list, **in shadowing order**. The front-most repo wins when two repos declare the same path. `./tool/build prepare` appends `sponge/libports/gems` so Sponge OS components, the Qt6 libs, and the Leitzentrale source are all visible. |
| `MAKE += -j<N>` | `etc/build.conf` (added by `prepare`) | `-j$(nproc)` | Parallel build. |
| `QEMU_OPT` | `etc/build.conf` | platform-dependent | QEMU flag pass-through. Headless servers want `# QEMU_OPT += -display sdl`. |
| `QEMU_ARGS` | run scripts | per-scenario | Per-scenario QEMU args. The run scripts in `run/` append `-nographic -m 1G` for seL4 scenarios; `sponge-de.run` appends only `-nographic` because it needs the SDL framebuffer. |
| `CONTRIB_DIR` | Genode build system | `genode/contrib/` | Where `prepare_port` drops fetched sources. |
| `BUILD_DIR` | Genode build system | `genode/build/x86_64` | Build artifact output root. |

### 8.1 Order of repo shadowing

`REPOSITORIES` is a **list, and order matters**. The Genode build
system picks the first match it finds for any `<lib>` or `<src>`
reference, so the Sponge OS repo must be listed after the upstream
repos whose files it overrides (currently nothing) and before any
repos whose files Sponge OS wants to override (currently also nothing).
The `./tool/build prepare` step appends `sponge/libports/gems` plus
`pc`/`dde_linux` to the list in the right order; do not reorder it
manually without a reason. `pc` and `dde_linux` are needed by the
base-sel4 interactive GUI scenario (`run/sponge-de-sel4-interactive.run`):
they contribute `pc_platform`, `pc_usb_host`, and `usb_hid`. They are
harmless on base-linux (their targets are only built on demand).
`dde_ipxe` (the iPXE-based NIC driver, `ipxe_nic`) is appended to
`REPOSITORIES` for the networking scenarios (`run/sponge-net-probe.run`,
todo 12). `ports` is appended for the `terminal` package (todo 13): it
contributes the `noux-pkg/bash-minimal` and `noux-pkg/vim-minimal`
build targets (which depend on the prepared `bash`/`vim`/`ncurses`
ports — `ncurses` itself lives in `libports`, already in the list).
The full managed block in `genode/build/x86_64/etc/build.conf`
is therefore: `sponge`, `libports`, `ports`, `gems`, `pc`, `dde_linux`,
`dde_ipxe`.
Like `pc`/`dde_linux`, `dde_ipxe` is harmless on base-linux (its
`REQUIRES = x86` target only builds on demand). `ports` is likewise
harmless when no noux target is built.

### 8.1.1 `base-$(KERNEL)` goes AFTER `repos/base` (Sponge prepare fix)

Upstream's `create_builddir` template prepends
`REPOSITORIES += $(GENODE_DIR)/repos/base-$(KERNEL)` before
`repos/base`. With that order, forwarding-only target directories —
`base-sel4/src/timer/hpet` contains only a `target.mk` that includes
`repos/base`'s `target.inc` — shadow `repos/base`'s buildable variant
of the same target, and the build fails at link time with
`cannot find component.o` (the target's own `component.cc` lives next
to the `.inc` in `repos/base`, not in the forwarding dir; `vpath`
covers only the forwarding dir). `./tool/build prepare` therefore
moves the kernel repository after the `base`/`os`/`demo` block, in
place and idempotently (marked with a `# Kernel-specific repository
(moved after repos/base:` comment). The kernel library imports are
unaffected: `lib/import/import-*.mk` exists only in the kernel repo,
so it wins regardless of order.

### 8.2 Why `make -j$(nproc)` is set by `prepare`

The Genode build system does not default to parallel builds; it
expects each developer to set their own `MAKE` flag. A cold rebuild of
the base tree is the difference between a coffee break and lunch, so
`prepare` sets `-j$(nproc)` (the host CPU count). Override per-run by
passing `MAKE=` on the command line.

---

## 10. base-sel4 Interactive GUI — Extra Requirements & Gotchas

The base-sel4 interactive GUI scenario
(`run/sponge-de-sel4-interactive.run`, roadmap §11 item 1) drives real
PC hardware drivers (vesa_fb, ps2, usb_hid, pc_usb_host, event_filter,
platform, acpi, pci_decode) that the base-linux developer flow never
builds. It therefore pulls in extra host requirements and a couple of
gotchas beyond §7.2:

### 10.1 Extra host requirements

- **`bc`** — the DDE-Linux USB stack builds the Linux kernel, whose
  `include/generated/timeconst.h` is generated by `bc`. Install from the
  distro, or drop a portable `busybox` on `PATH` with a `bc -> busybox`
  symlink (this is how the Sponge development host satisfies it without
  root). Only needed when `usb_hid` / `pc_usb_host` are built.
- **`pyyaml`** — the seL4 kernel cmake's `tools/config_gen.py` imports
  `yaml`. Add it to the `.venv` alongside the §3 modules:
  `uv pip install --python .venv/bin/python pyyaml`.
- **`linux` + `jitterentropy` ports** — fetched by
  `./tool/build ports` (both are now in `tool/build.mojo`'s
  `port_list()`). The `linux` port is ~140 MB.

### 10.2 Extra repositories

`tool/build prepare` now also appends `pc` and `dde_linux` to the
managed `REPOSITORIES` block (they provide `pc_platform`,
`pc_usb_host`, and `usb_hid`). On a build directory created before this
change, re-run `./tool/build prepare` or append the two lines by hand
(see §8.1).

### 10.3 Gotcha — Qt6 rebuild on kernel switch

The Qt6 shared libraries are kernel-independent in ABI but the Genode
build system rebuilds them when `KERNEL` changes (because `SPECS`
change). If `genode/build/x86_64/qt6/base/` holds a cmake tree from the
*other* kernel, the rebuild can fail at the `libQt6Widgets` link with
stale-autogen undefined references (`QGridLayout::*`, `QSizePolicy::*`).
Fix: `rm -rf genode/build/x86_64/qt6/base` once and rebuild. (The libs
rebuild cleanly from scratch; the failure is a stale cmake/autogen
artifact, not a source issue.)

### 10.4 Gotcha — base-sel4 packs a single image.elf

Unlike base-linux (where boot modules live as individual files in
`run_dir/genode/` and `./core` reads them at launch), base-sel4 packs
all boot modules into one `image.elf` at `build_boot_image` time. Run
scripts that add modules *after* `build_boot_image` (the base-linux
pattern, e.g. `cp` into `run_dir/genode/`) silently miss the image on
base-sel4. Any extra module (Qt6 shared libs, chargen files, driver
config ROMs, themes) must be copied into `bin/` **before**
`build_boot_image` and passed in the module list
(`build_boot_image $boot_modules` where `boot_modules` =
`[build_artifacts]` + the extras). `run/sponge-de-sel4-interactive.run`
is the reference implementation; `run/sponge-de-test.run`'s Qt6-lib
staging is base-linux-specific (see `docs/09-roadmap.md` §11.1).

### 10.5 Phase 10 — QEMU QMP usage (no new host tools)

The four Phase 10 input scenarios
(`run/sponge-de-sel4-interactive.run`, `run/sponge-wm-qmp.run`,
`run/sponge-terminal-qmp.run`, `run/sponge-textedit-qmp.run`) drive
guest input through QEMU QMP, exposed by the shared Tcl helper
`run/qmp.inc`. The host side uses only:

- **Tcl `socket`** — Tcl 8.x built-in, TCP-only (matches QEMU's
  `-qmp tcp:...` form, the only form QMP supports).
- **Tcl `expect`** — already used by the Genode run tool for the
  `qemu_spawn_id` log drain.

No new host tools, no new Python dependencies, no Mojo helpers — the
helper is ~700 lines of plain Tcl + `expect`. QEMU's QMP socket is
exposed via `-qmp tcp:127.0.0.1:<port>,server=on,wait=off`; the
scenario picks an ephemeral port via `qmp_pick_port` (Tcl
`socket -server 0`) to avoid TIME_WAIT collisions between sequential
QEMU respawns. **Reproducibility note:** the QEMU QMP wire protocol
has been stable across recent QEMU versions, but specific QEMU 11.0.2
quirks (see below) cause some recipes to differ from older guides — a
Phase 10 host running QEMU 7.2 will see the abs-axis clamp on a
different y-center; the PS/2 REL recipe is robust across versions.

#### QEMU 11.0.2 quirks discovered (relevant to any QMP scenario)

These are observed-behavior caveats that the `run/qmp.inc` procs work
around; documenting them here so the next QEMU-upgrade doesn't silently
regress Phase 10.

1. **`send-key` strings rejected.** QEMU 11 wants `keys` as
   `{"type":"qcode","data":"<name>"}` per key; the older string form
   `["<name>"]` fails with `Invalid parameter type for 'keys[0]',
   expected: object`. `qmp_send_key` emits the object form.
2. **`input-send-event` BTN events not delivered to untargeted
   devices.** Untargeted abs events reach BOTH pointer devices; if
   the PS/2 mouse is current (default under `-nographic`), abs events
   are reinterpreted as PS/2 relative deltas — the pointer slams a
   corner. Fix: HMP `mouse_set <tablet-index>` to make the usb-tablet
   current BEFORE the abs/btn events.
3. **`-nographic` broadcasts untargeted input.** Combined with (2),
   every PS/2 REL event also reaches the usb-tablet, but the
   usb-tablet ignores REL. No data corruption, but the dispatch
   order matters — `mouse_set` first, then any abs/btn.
4. **Serial lines are CR CR LF.** QEMU's `-nographic` serial emits
   lines terminated with `CR CR LF` (verified by `od -c` on a
   captured line), not just `CR LF`. Expect patterns anchor on
   `\r*\n` (NOT `\r?\n`).
5. **PS/2 Mouse is the default current pointer.** `query-mice`
   shows the PS/2 mouse with `current:true` on a fresh
   `-nographic` boot. PS/2 REL dispatches go to the PS/2 mouse;
   usb-tablet events need `mouse_set` first (see (2)).
6. **No "click" recipe works without device-targeting under
   `-nographic`.** Synthesizing the criterion-1 click via
   `input-send-event` (untargeted BTN events) reaches no guest
   device; only the device-targeted path (HMP `mouse_button` after
   `mouse_set 3`) succeeds — verified by `usblog ... PRESS BTN_LEFT`
   and nitpicker's focus ROM flipping to the terminal domain.
   See `docs/08-development.md` §4.4 for the full set of procs.

The QEMU 11.0.2 used in this development environment is the only
QEMU version Phase 10 was verified against; a Phase 12 / Phase 15
upgrade to a newer QEMU should re-run the four Phase 10 scenarios as
the first gate.

---

## 12. Disk image P4 (SPONGE-DATA) creation

The Sponge OS install media carries a fourth GPT partition,
**SPONGE-DATA**, that backs the writable user-area stores (the pkgd
installed-set at `/store/installed.xml`, the future configd store, and
user files under `/home`) — `docs/14-boot-storage-architecture.md` §4.3,
§6. The Genode run framework's `image/disk` plugin
(`genode/tool/run/image/disk`) produces only P1 (BIOS boot) + P2 (ESP)
+ P3 (GENODE ext2): it packs the image as `header + P3 + backup-GPT-gap`
with **no free sectors** (it `resize2fs -M`-shrinks P3 and
`--move-second-header`s the backup GPT to the image end). P4 therefore
cannot be added by a bare `sgdisk --new=4`; the image must be grown and
repartitioned post-build.

### 12.1 tool/mkdata

`tool/mkdata` (`tool/mkdata.mojo`, AGENTS.md §3.5 Mojo host-side helper)
runs the `docs/14` §4.3 sequence on an already-produced `.img`:

```
truncate -s +<data_bytes> <img>
sgdisk --delete=3 <img>                  ;# drop P3 entry (bytes stay)
sgdisk --move-second-header <img>        ;# backup GPT -> new end
sgdisk --new=3:<old_first>:<old_last> <img>
sgdisk --change-name=3:GENODE <img>
sgdisk --new=4:<p4_first>:<p4_last> <img>
sgdisk --change-name=4:SPONGE-DATA <img>
mkfs.ext2 -E offset=<p4_byte_off> -L SPONGE-DATA -F <img> <p4_kib>K
sgdisk --hybrid <img>
```

Usage:

```
./tool/mkdata <img>                  # add a 1024 MiB SPONGE-DATA P4
./tool/mkdata <img> --data-size 256  # 256 MiB P4
```

The tool is **idempotent**: a second run on an image that already has a
P4 named `SPONGE-DATA` is a verified no-op (it detects P4 up front and
exits 0 without re-running the destructive sequence). If P4 exists under
a *different* name the tool errors loudly (exit 2) rather than
overwriting — fail-loud, never silent corruption (AGENTS.md §1.4).

Host tools required (subset of §7.3): `sgdisk` (gptfdisk),
`mkfs.ext2` (e2fsprogs), `truncate` (coreutils) — the same tools
`image/disk` already uses. The run scenario
`run/sponge-persist-disk.run` invokes `tool/mkdata` between
`build_boot_image` and `run_genode_until`.

### 12.2 `mkfs.ext2 -E offset=<bytes>` — formatting a partition inside an image

The key technique is `mkfs.ext2`'s `-E offset=<bytes>` extended option.
It makes `mkfs.ext2` `lseek()` to `<bytes>` inside the image file and
write the ext2 superblock + structures there, **without** touching the
GPT or any other partition. The trailing `<size>K` argument limits the
filesystem to the partition extent so mkfs does not run into the backup
GPT. `-F` forces creation on a regular file (mkfs.ext2 otherwise
expects a block device).

Two subtleties this tool handles:

1. **sgdisk auto-alignment.** `sgdisk --new` aligns partition starts to
   2048-sector (1 MiB) boundaries by default, so the partition may land
   a few sectors past the requested `p4_first`. `tool/mkdata` re-queries
   P4's actual first/last sectors *after* `--new=4` and computes the
   mkfs offset from the **actual** sectors — otherwise the ext2
   superblock would be written at the pre-alignment offset and the
   partition would not match its filesystem (silent corruption). The
   tool prints the alignment adjustment when it occurs.

2. **Re-applying the P3 name.** `sgdisk --delete=3` removes the P3
   entry entirely (the ext2 *bytes* stay on disk, but the GPT name is
   gone). The sequence re-applies `--change-name=3:GENODE` after
   `--new=3` so P3 keeps its label. `docs/14` §4.3 omits this step in
   its sketch; the tool adds it because the acceptance gate checks P3's
   name (`sgdisk -l`).

### 12.3 Hybrid MBR 3-entry limit

`sgdisk --hybrid` (the last step, also run by `image/disk`) builds the
hybrid MBR from the **first three** GPT partitions (P1-P3). P4
(SPONGE-DATA) therefore exists in the **GPT only** — it is visible to
`sgdisk -l`, to GRUB (which boots from P1/P2), and to any Linux host,
but **not** listed by MBR-only tools (e.g. legacy `fdt`/`fdisk -l`
modes, very old BIOSes). This is acceptable for Sponge OS: the boot
chain never reads the MBR partition entries (GRUB loads from the BIOS
boot + ESP partitions), and the Control-philosophy inspection tool is
`sgdisk -l` (ext2 is universally readable on the host). If a future
deployment needs P4 in the MBR (e.g. a legacy toolchain), the fix is a
small vendored patch to `image/disk` adding `--hybrid 1:2:3:4` semantics
— but sgdisk's hybrid MBR spec caps at 3 entries, so the real fix would
be dropping hybrid MBR entirely (pure-GPT boot), recorded in the §4
patch ledger if taken.

---

## 13. References

- Genode OS Framework: <https://genode.org>
- Genode documentation: <https://genode.org/documentation>
- Genode 26.05 source (canonical): <https://codeberg.org/genodelabs/genode>
- Genode toolchain download: <https://genode.org/download/tool-chain>
- Genode run framework:
  <https://genode.org/documentation/developer-resources/run>
- This document is referenced by:
  - `AGENTS.md` §5.2 (vendoring rules)
  - `docs/08-development.md` (the development flow, which points here
    for the environment contract)
  - `docs/09-roadmap.md` Phase 1 lessons (which now point at the
    vendored tree)
