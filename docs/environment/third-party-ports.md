# 11 - Development Environment: Third-Party Ports (§5-§6)

> **Breadcrumb:** parent [11 - Development Environment & Reproducibility](../11-environment.md); this file holds its §5 (Third-Party Ports) and §6 (What is Deliberately NOT Vendored), moved verbatim.
> Siblings in `docs/environment/`: [bootstrap.md](bootstrap.md) (§7), [env-vars.md](env-vars.md) (§8), [base-sel4-gui.md](base-sel4-gui.md) (§10), [disk-image-p4.md](disk-image-p4.md) (§12-§13).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

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
| `libnl` | `3.2.25` (`dde_linux/ports/libnl.port`) | `https://github.com/thom311/libnl/releases/download/libnl3_2_25/libnl-3.2.25.tar.gz` (LGPL-2.1) | libnl 3.2.25, the netlink library the DDE-Linux Wi-Fi stack's `wpa_driver_nl80211` binds to. Pulled in by the `pc_wifi` **driver package** (`repos/dde_linux/src/driver/wifi`); see docs/12 §4.5. |
| `wpa_supplicant` | `2.11` (`dde_linux/ports/wpa_supplicant.port`) | `https://w1.fi/releases/wpa_supplicant-2.11.tar.gz` (BSD-3-Clause) | wpa_supplicant 2.11, compiled by the `pc_wifi` driver package into the same binary. Its **firmware blobs are deliberately NOT vendored** — see docs/15-hardware-compatibility.md §5. |
| `openssl` | `1.1.1w` (`libports/ports/openssl.port`) | `https://www.openssl.org/source/openssl-1.1.1w.tar.gz` (OpenSSL licence) | libcrypto/libssl, linked by `wpa_supplicant` and therefore by `driver/wifi`. Only fetched for the Wi-Fi driver stack. |
| `bash` | `20c34a63efce74be02c9eb92bd8bfb53cd5122e5` | `https://ftp.gnu.org/gnu/bash/bash-5.3.tar.gz` (GPLv3) | GNU bash, built as the `noux-pkg/bash-minimal` package (`bash-minimal.tar`) that ships inside the `terminal` package (todo 13). Genode's noux bash build does **not** link readline/ncurses. **Current** post-patch fingerprint (patch #8); the superseded pre-patch fingerprint was `93dd45640aa0cbd1a850deb4bee4cf6a332e61bb` (mirrored from `ftpmirror.gnu.org`). |
| `vim` | `abada0b43ca034d23ba39f3281bff997f08fc884` | `https://github.com/vim/vim/archive/v7.3.tar.gz` (Vim license) | Vim 7.3, built as the `noux-pkg/vim-minimal` package (`vim-minimal.tar`) that ships inside the `terminal` package (todo 13). Links ncurses (`--with-tlib=ncurses`). |
| `ncurses` | `b259f9aa8136195dc38361357e62355a6fe066e8` | `https://ftp.gnu.org/gnu/ncurses/ncurses-5.9.tar.gz` (MIT) | The ncurses 5.9 terminal library, required by the `vim-minimal` noux package. Lives in the `libports` repo (not `ports`). Its `Caps` header generation invokes `mawk` by name — see the `mawk` host-tool requirement in §7. **Current** post-patch fingerprint (patch #8); the superseded pre-patch fingerprint was `5fb8a84ea7e768167f325dccabde30f2e6e56f72` (mirrored from `ftpmirror.gnu.org`). |
| `stb` | `ab8f505722d9668c907cabba084f96b886985056` | `https://github.com/nothings/stb.git` (v2.19, MIT) | stb single-file header libraries (stb_image), used by the alpha desktop scenario's image-decode path (`run/sponge-alpha.run`). Lives in the `libports` repo. Added to the default port set after the alpha scenario's `check_ports` flagged it missing on a fresh machine. |
| `ttf-bitstream-vera` | `cd3684816b73c4361e11236f9e63302f99b9b1ff` | `http://ftp.gnome.org/pub/GNOME/sources/ttf-bitstream-vera/1.10/ttf-bitstream-vera-1.10.tar.bz2` (Bitstream Vera Fonts Copyright) | Bitstream Vera font set (v1.10), staged as a runtime ROM by the alpha desktop scenario (`run/sponge-alpha.run`). Lives in the `libports` repo. Added to the default port set after the alpha run stage flagged it missing on a fresh machine. |
| `coreutils` | `c910bf8c769e6d35cbf803f24e1ecae709aff392` | `https://ftpmirror.gnu.org/coreutils/coreutils-8.29.tar.xz` (GPLv3) | GNU coreutils 8.29, built as `noux-pkg/coreutils-minimal` (`coreutils-minimal.tar`: cat/cp/ls/mkdir/mv/rm/rmdir/sort/tail/md5sum) — the Phase 13 CLI toolset inside `pkg/terminal` (docs/plans/phase13-package-ecosystem.md D13.2). Its gnulib build regenerates `lib/iconv_open-*.h` with `gperf` — see the `gperf` host-tool requirement in §7. |
| `grep` | `a08c4f7925aa5af491cb243e3e37cf5dd8bb9c06` | `https://ftpmirror.gnu.org/grep/grep-3.1.tar.xz` (GPLv3) | GNU grep 3.1, built as `noux-pkg/grep` (`grep.tar`). Links `pcre`. Phase 13 CLI toolset. |
| `sed` | `08ac8610ea1a59e7562c66800acecc55a47e93b3` | `https://ftpmirror.gnu.org/sed/sed-4.4.tar.xz` (GPLv3) | GNU sed 4.4, built as `noux-pkg/sed` (`sed.tar`). Links `pcre`. Phase 13 CLI toolset. |
| `tar` | `11ef1926fb0a9097dd097d4672ad8567c7ec8c96` | `https://ftpmirror.gnu.org/tar/tar-1.30.tar.xz` (GPLv3) | GNU tar 1.30, built as `noux-pkg/tar` (`tar.tar`). Phase 13 CLI toolset. |
| `less` | `609181c8cfc59adfaa6a8e94bf3622df1a7eb3f0` | `https://ftpmirror.gnu.org/less/less-487.tar.gz` (GPLv3) | less 487 pager, built as `noux-pkg/less` (`less.tar`). Links `ncurses`. Phase 13 CLI toolset. |
| `findutils` | `48d3049df9b99b9672e36a1b0f15f425e37af65c` | `https://ftpmirror.gnu.org/findutils/findutils-4.6.0.tar.gz` (GPLv3) | findutils 4.6.0 (find/xargs), built as `noux-pkg/findutils` (`findutils.tar`). Phase 13 CLI toolset. |
| `diffutils` | `f2f34ac57062c071d127b8fc40a051278f2f3aef` | `https://ftpmirror.gnu.org/diffutils/diffutils-3.3.tar.xz` (GPLv3) | diffutils 3.3 (diff/cmp/diff3/sdiff), built as `noux-pkg/diffutils` (`diffutils.tar`). Phase 13 CLI toolset. |
| `which` | `1b179015279594d0dd81beb5831bbb2d893221b9` | `https://ftpmirror.gnu.org/which/which-2.21.tar.gz` (GPLv3) | which 2.21, built as `noux-pkg/which` (`which.tar`). Phase 13 CLI toolset. |
| `mupdf` | `5b492514a8e9ba4db6bbf54dee54b94f068f0dae` | `http://mupdf.com/downloads/archive/mupdf-0.9-source.tar.gz` (GPLv2) | MuPDF 0.9 PDF rendering library, linked by `app/pdf_view` (`pkg/pdf_view`, Phase 13 W4). Lives in the `libports` repo. |
| `openjpeg` | `721d101479429b2435349579ba5f2bdea84430b9` | `https://github.com/uclouvain/openjpeg/archive/version.1.4.tar.gz` (BSD) | OpenJPEG JPEG-2000 codec, a mupdf image-decoder dependency. |
| `jbig2dec` | `68226914536c0fdb1fb42fd87685989a43a8ea92` | `https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs924/jbig2dec-0.15.tar.gz` (GPLv3) | JBIG2 decoder, a mupdf image-decoder dependency. |
| `pcre` | `11003e42ac571da33b5e657a6500bc2b93884a8a` | `http://sourceforge.net/projects/pcre/files/pcre/8.30/pcre-8.30.tar.bz2` (BSD) | PCRE regex library, linked by `noux-pkg/grep` and `noux-pkg/sed`. |
| `qt6_tools` | `cdc19bb65058adf68bc97433df640b996e312621` | `https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qttools-everywhere-src-6.8.3.tar.xz` (GPLv3) | Qt6 tools module — only its `examples/designer/calculatorform` sources are used, mirrored by `recipes/src/qt6_calculatorform` and built as `app/qt6/examples/calculatorform` for `pkg/calculator` (Phase 13 W3). |

In addition to the `prepare_port` set above, two **Genode depot packages**
are imported host-side into `pkg/<name>/` by `tool/pkg_import` (todo 11).
The depot is **not** a port: it is fetched by `genode/tool/depot/download`,
verified by PGP against the in-tree pubkey at
`genode/repos/gems/sculpt/depot/cproc/pubkey`, and re-packaged locally.
The depot archives never leave the build host — `sponge_pkgd` does not
fetch at runtime (Metis amendment A1).

> **Incident note (2026-10-18, submodule split):** during the
> subtree→submodule migration the `genode/contrib/` tree (git-ignored,
> re-fetchable by design) was damaged by a full tmpfs mid-`mv`. All 37
> ports were re-fetched and hash-verified except `libc`: its only
> upstream (`ftp-archive.freebsd.org` `12.0-RELEASE/src.txz`) was
> unreachable at the time, so the port content was reconstructed from
> the `freebsd/freebsd-src` git tag `release/12.0.0` (the same source
> content the release tarball packages) and then run through the port's
> own patch/include-assembly steps (`prepare_port` with pre-satisfied
> download stamps). The reconstructed tree compiles and boots, but it
> did NOT pass the `.port` file's `SHA(libc)` download-integrity check
> (that check binds the exact `src.txz` bytes). Re-running
> `genode/tool/ports/prepare_port libc` once the canonical host is back
> restores the canonical path; delete `genode/contrib/libc-*` first.

> **Incident note — AMENDED (2026-10-19): the same `genode/contrib/`
> re-fetch silently dropped three Sponge kernel patches.** Rows 11, 12
> and 13 above were applied BY HAND to `genode/contrib/sel4-*/` after
> `prepare_port`, with the patch file living only in this repository's
> `docs/patches/`. `genode/contrib/` is git-ignored, so that is a
> purely procedural contract with no enforcement: a re-fetch produced a
> stock seL4 kernel, and every image built from then on was missing the
> reserved-extent export, the device-untyped coverage, and the frame
> console. On real hardware (the LG gram's GOP framebuffer sits above the
> memory map) that means a black screen; on QEMU (GOP at 2 GiB) the boot
> still completed, which is why no QEMU gate caught it.
>
> **Resolution:** all three patches now live in the fork at
> `genode/repos/base-sel4/patches/sel4-*.patch`, where `sel4.port`'s
> `PATCHES := $(sort $(wildcard $(REP_DIR)/patches/*.patch))` picks them
> up and `install.mk` applies each with `patch -p0` on every
> `prepare_port sel4`. The contract is now enforced by the port
> mechanism, not by memory. The port's `sel4.hash` moved from
> `a81460876b917293b45aa18015dc117429f7ffe9` to
> `fcfb8bdd7a988137a47ce210cbec0790ef0a5469` as a result (expected: the
> fingerprint covers the patch set); `genode/tool/ports/update_hash sel4`
> regenerated it. Verification: the three files apply cleanly in the
> port's sorted order against a pristine contrib tree, the prepared tree
> is byte-identical to the intended patched tree, and a from-scratch
> kernel rebuild boots `sponge-desktop-disk-uefi-usb` (PASS),
> `sponge-minimal` (PASS) and `sponge-desktop-disk` (alpha-probe PASS).
> Evidence: `docs/evidence/2026-10-19-sel4-patch-autoapply.md`.
>
> The general lesson, recorded so the next reviewer checks it: **any
> change to `genode/contrib/` that is not expressed as a fork patch file
> is a silent, unenforced manual step. If `prepare_port` does not apply
> it, `prepare_port` will undo it.**

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

Four categories of artifact stay outside the repository on purpose.
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

3. **QEMU and other host packages** (`qemu-system-x86_64` 11.0.3, GNU
   Make, Tcl/expect, cmake, ninja). These come from the operating
   system package manager. Pinning host-package versions is the job of
   the developer's distro, not this repository.

4. **The Mojo SDK** (`pyproject.toml` + `uv.lock`, `uv sync`). Mojo is
   the preferred language for Sponge-owned code
   (`docs/plans/mojo-language-policy.md`): it builds the `tool/*.mojo`
   helpers and the Mojo objects that Genode/seL4 components link
   (`docs/08-development.md` §1.5). The exact release is pinned by
   the committed `uv.lock` (currently 1.0.0b2) and materialized into
   a project-local `.venv`. The pin is load-bearing: KGEN runtime
   symbols are version-sensitive (`docs/evidence/mojo-ffi-probe.md`
   §5), so an SDK bump is not complete until the `nm -u` re-check of
   the emitted objects and a probe re-run pass.

---

