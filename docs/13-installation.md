# 13 - Installation and Alpha Quick Start

This guide covers Sponge OS Alpha 0.1.0, codename Archaeocyte. The Alpha media target is seL4 running in QEMU. It is a development release, not a general hardware installer.

> **Phase 8 media model.** As of Phase 8, the Alpha media splits into two
> distinct products (see
> [`docs/14-boot-storage-architecture.md`](14-boot-storage-architecture.md)
> §8 for the design authority):
>
> | Media | What it is | Persistence |
> |---|---|---|
> | `.img` | The **real product** — a 4-partition disk image: BIOS-boot + ESP + GENODE (the Qt6 desktop served from disk via `cached_fs_rom`) + SPONGE-DATA (the writable user-area filesystem). | ✅ Installs survive reboot on this media (Phase 8 P3). |
> | `.iso` | The **live/eval media** — the Phase 7 boot-modules composition on a read-only El Torito image; Tier 2 is a RAM filesystem. | ❌ Nothing persists (it is a read-only optical medium). |
>
> Both media boot the same themed desktop and pass `alpha-probe: PASS`.
> Pick the `.img` to install packages and keep them; pick the `.iso` to
> evaluate Sponge OS without writing anything.

## 1. Prerequisites

Start with a checkout of this repository. The Genode 26.05 source tree is already vendored at `genode/`; no separate Genode checkout is needed.

Install the host toolchain and base packages listed in [`docs/11-environment.md` §7](11-environment.md#7-host-bootstrap). The Alpha media builders additionally require:

```bash
# Debian / Ubuntu
sudo apt install xorriso gptfdisk mtools e2tools dosfstools e2fsprogs coreutils

# Arch / CachyOS
sudo pacman -S libisoburn gptfdisk mtools e2tools dosfstools e2fsprogs
```

The required executable names are `xorriso`, `sgdisk`, `mcopy`, `e2cp`, `e2mkdir`, `mkfs.ext2`, `mkfs.vfat`, `resize2fs`, and `truncate`. The seL4 build also needs the toolchain, CMake, Ninja, QEMU, Tcl, expect, and the Python modules listed in [`docs/11-environment.md` §7](11-environment.md#7-host-bootstrap). Set up the repository-local Mojo environment with:

```bash
uv sync
.venv/bin/mojo --version
```

## 2. Build the Alpha media

### 2.1 One-command build

From the repository root, run:

```bash
./tool/dist
```

The command:

1. Pre-flight checks every host tool the Genode run framework's image plugins AND `tool/mkdata` invoke (`xorriso`, `sgdisk`, `mcopy`, `e2cp`, `e2mkdir`, `mkfs.ext2`, `mkfs.vfat`, `resize2fs`, `truncate`).
2. Builds the **product `.img`** from `run/sponge-desktop-disk.run` with `RUN_OPT='--include image/disk'`. That scenario boots a Tier-0 `image.elf` (≤ 80 MiB) that mounts the GENODE ext2 partition and serves the full Qt6 desktop from `/system` via `cached_fs_rom`, then gates on `alpha-probe: PASS`.
3. Runs `tool/mkdata` on the produced `.img` to add the SPONGE-DATA P4 (`docs/14` §4.3 — `truncate` + `sgdisk` delete/move/new/hybrid + `mkfs.ext2 -E offset`). This is the partition that backs `sponge_pkgd`'s installed-set store, so installs survive reboot.
4. Builds the **live/eval `.iso`** from `run/sponge-alpha.run` with `RUN_OPT='--include image/iso'` — the Phase 7 boot-modules composition on an El Torito image, gated on `alpha-probe: PASS`.
5. Copies the artifacts to `var/dist/`, writes the SHA-256 sidecars, and prints a summary table.

Verify the sidecars with:

```bash
(cd var/dist && sha256sum -c *.sha256)
```

The resulting files are:

```text
var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img   (4 partitions: BIOSBOOT/ESP/GENODE/SPONGE-DATA)
var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso   (live/eval El Torito)
```

Confirm the four partitions are present (the `misleading_success_output` defense — never trust the build exit code alone):

```bash
sgdisk -p var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
# Expect: four partitions, with P4 named SPONGE-DATA.
```

#### Control doors

The automation is the default and a door is always open (AGENTS.md §1.1):

- `./tool/dist --no-data` — produce the `.img` WITHOUT the SPONGE-DATA P4 (a 3-partition image; installs will NOT persist on this media). Useful when iterating on the disk-served desktop alone.
- `./tool/dist --data-size 256` — produce a 256 MiB SPONGE-DATA P4 instead of the default 1024 MiB (handy for fast QEMU boot).
- `./tool/mkdata <img>` — run only the P4 grow/repartition step on an existing 3-partition `.img`. Idempotent.

### 2.2 Manual build

The automation has a documented control path. From the repository root, after the normal build preparation and port setup, the canonical manual sequence is in [`docs/08-development.md` §11.2](08-development.md#112-manual-flow-the-canonical-reference). The short form:

```bash
# Product .img (4-partition). Build the disk-served desktop scenario:
make -C genode/build/x86_64 run/sponge-desktop-disk \
    KERNEL=sel4 BOARD=pc RUN_OPT='--include image/disk'
# Grow SPONGE-DATA P4 onto the produced image:
./tool/mkdata genode/build/x86_64/var/run/sponge-desktop-disk.img
mkdir -p var/dist
cp genode/build/x86_64/var/run/sponge-desktop-disk.img \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.img \
    > sponge-os-0.1.0-alpha-x86_64-sel4.img.sha256)

# Live/eval .iso. Build the alpha boot-modules composition:
rm -rf genode/build/x86_64/var/run/sponge-alpha*
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc RUN_OPT='--include image/iso'
cp genode/build/x86_64/var/run/sponge-alpha.iso \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.iso \
    > sponge-os-0.1.0-alpha-x86_64-sel4.iso.sha256)
(cd var/dist && sha256sum -c *.sha256)
```

## 3. Boot in QEMU

QEMU is the supported Alpha target. Use at least 2 GiB of guest RAM, and pass `-machine q35 -cpu Skylake-Client` — without them QEMU 11.0.3's default machine/CPU makes the seL4 kernel fail early in `boot_sys` (`XSAVE not supported` + `boot_sys failed`; verified 2026-08-07 on QEMU 11.0.3). These commands boot the already-built media directly and keep the guest log in the terminal:

### 3.1 Disk image (the product media — installs persist)

```bash
qemu-system-x86_64 \
    -machine q35 \
    -cpu Skylake-Client \
    -m 2G \
    -drive format=raw,file=var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img \
    -serial stdio \
    -display none \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
```

### 3.2 ISO (live/eval — nothing persists)

```bash
qemu-system-x86_64 \
    -machine q35 \
    -cpu Skylake-Client \
    -m 2G \
    -boot d \
    -cdrom var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso \
    -serial stdio \
    -display none \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
```

The scenario-level media verification uses the same `image/disk` and `image/iso` modes and gates on `alpha-probe: PASS` (for the `.img`, `run/sponge-desktop-disk.run`; for the `.iso`, `run/sponge-alpha.run`). To stop a guest that does not respond to `vct shutdown`, use QEMU's monitor escape, `Ctrl-A`, then `x`, or terminate the QEMU process from the host.

## 4. Write the disk image to USB

The Alpha has no hardware support matrix. Writing an image to USB is provided only as a manual experiment, not as a supported boot target. Physical USB boot is **NOT YET VERIFIED** (Phase 15 target); the Phase-12 USB verification is BIOS-side only on QEMU (`run/sponge-usb-boot.run` proves `BIOS-side USB boot verified` + `alpha-probe: PASS` with the ISO attached via `-device usb-storage`; not a claim about booting the same media on physical hardware). The `dd`-to-USB manual door below remains available as the control escape hatch so a contributor who wishes to test on physical hardware can attempt it without waiting for the Phase-15 documented path.

**WARNING: `dd` permanently overwrites the selected device. Choosing the wrong device can destroy your operating system and all data on it. Confirm the device name twice, unmount its partitions, and keep a separate backup before running this command.**

Replace `/dev/sdX` with the whole USB device, never a partition such as `/dev/sdX1`:

```bash
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
sudo umount /dev/sdX1  # repeat for every mounted partition on the USB device
sudo dd if=var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img \
    of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

## 5. Quick-start tour

The following path is the shortest tour of the delivered Alpha. Each claim names the run scenario that proves it.

1. **Boot the media.** Start the `.img` or `.iso` in QEMU. The seL4 boot chain reaches the Sponge desktop probe marker (`run/sponge-desktop-disk.run` for the `.img`, `run/sponge-alpha.run` for the `.iso`; media boot evidence is recorded in `docs/evidence/task-5-phase7-alpha.log` and `docs/evidence/task-6-phase7-alpha.log` for the ISO flow, and `docs/evidence/p2-desktop-disk.log` for the disk-served product flow).
2. **See the desktop.** The themed panel and launcher appear automatically after boot (same scenarios as step 1).
3. **Open the launcher.** The launcher receives the staged package set from `sponge_pkgd`.
4. **Launch Terminal.** The terminal window, bash prompt, and a keystroke round trip are verified (`run/sponge-terminal.run`).
5. **Launch Files.** The file manager window, navigation, copy, delete, and read-only refusal are verified (`run/sponge-files.run`).
6. **Launch TextEdit.** The Qt6 text editor window renders after install and launch (`run/sponge-textedit.run`). **Falkon's first paint is blocked by a base-sel4 capability-space issue — see §6.**
7. **Inspect system status.** `vct status` reads the live component state (`run/sponge-vct-status.run`).
8. **Inspect packages.** `vct list` reports the installed set, and `vct install` enables a staged package (`run/sponge-pkg-list.run`, `run/sponge-pkg-install.run`).
9. **Read and change configuration.** `vct config theme.active` reads the active theme, and `vct theme apply` exercises the theme command and live theme backend (`run/sponge-config.run`).
10. **Launch explicitly.** `vct launch <package>` starts an installed package through the same backend used by the desktop launcher (`run/sponge-launch.run`).
11. **Open Leitzentrale.** `vct leitzentrale` enables the Leitzentrale subsystem and its viewer marker appears in the desktop (`run/sponge-leitzentrale.run`, `run/sponge-alpha.run`). The viewer is a marker view, not the complete Sculpt UI.
12. **Shut down.** `vct shutdown` requests ACPI poweroff and QEMU exits in the success scenario (`run/sponge-power.run`).

On the **product `.img`**, installs persist across reboots via the SPONGE-DATA partition (`run/sponge-persist-disk.run` proves the two-boot restoration on the same image). On the **`.iso`**, nothing persists — Tier 2 is a RAM filesystem (the read-only optical medium cannot deliver persistence).

## 6. Known limitations

This register is part of the Alpha contract.

- **QEMU-only target → Phase 12 rescope.** Phase 12 expands and records the QEMU-verified hardware matrix; physical-machine boot remains unverified until Phase 15. The five-machine / five-column surface matrix and 16-cell status ledger in `docs/15-hardware-compatibility.md` (4 verified, 1 smoke-only, 11 gap) are the honest advert; no real-hardware row appears. The product `.img` is still verified on seL4 in QEMU (`run/sponge-desktop-disk.run`); the live/eval `.iso` is verified on the same QEMU host (`run/sponge-alpha.run`).
- **Install means enable (`.iso` media only).** On the live/eval `.iso`, all binaries are pre-staged into the boot image and `vct install` enables a package already on the image. It does not deliver a binary at runtime (`run/sponge-pkg-install.run`; decision A1 in `docs/plans/phase7-alpha-decisions.md`). The product `.img` does NOT have this limitation: its package repository lives at `/system/pkg` on the GENODE partition and `vfs_data` on SPONGE-DATA backs `sponge_pkgd`'s store, so installs come from the on-disk repo and persist across reboots (Phase 8 P3, `run/sponge-persist-disk.run`).
- **Installs do not persist on `.iso` media.** The live/eval ISO is read-only by nature; Tier 2 is a RAM filesystem. Use the product `.img` for any install persistence (Phase 8 P3, `run/sponge-persist-disk.run`).
- **QEMU slirp networking only → Phase 12 rescope.** The host network backend remains QEMU user-mode/slirp. Phase 12 verifies the Linux-backed `pc_nic` stack on QEMU e1000, not tap/bridge or physical-network operation. The new `run/sponge-pc-nic.run` proves pc_nic bind + `nic_router: uplink DHCP acquired` on QEMU's user/slirp backend; non-e1000 hardware (`rtl8169`, Wi-Fi, USB-Ethernet) is documented in `docs/15` but remains UNTESTED. The existing iPXE/fetchurl round-trip baseline (`run/sponge-net-probe.run`) is unchanged.
- **Falkon is packaged, boots from disk, AND renders (first paint achieved on the seL4 media).** The Phase 8 disk-served architecture (docs/14 §12.4) is delivered end-to-end: falkon's 509 MiB WebEngine payload boots FROM DISK via `cached_fs_rom` (the binary starts, the dynamic linker resolves the 237 MiB `libQt6WebEngineCore.lib.so` from disk via rom_pkg, and lwIP DHCPs to 10.0.2.15), AND the browser window pixel-renders AND the HTTP fixture page is loaded over the nic stack (`run/sponge-falkon-disk.run` gates on `falkon-probe: PASS` + a host fixture GET; `docs/evidence/c4-logs/sponge-falkon-disk.log` for the latest regression PASS). The boot-module ceiling that blocked Falkon in Phase 7 is fully defeated. The capability-chain blocker that kept first paint unattained through Phase 8 is closed by three Phase 9 workstreams (docs/14 §12.4): C1 size-aware on-demand CNode backing (`93ded092f1`, docs/11 §4 ledger row #6), C2 lazy vm_space growth to 131072 mappings/PD (`19303468ff`, ledger row #7), and C3 the actual last blocker — a missing RM service route in `sponge_pkgd`'s generated parent-provides (`7feaa6510f`; the earlier "cap_quota=2 = exhaustion" reading was wrong — `cap_quota=2` is the FIXED request size for an RM session, `rm_session.h:34`, not a remaining-capacity measure). Both vendored base-sel4 patches are upstreamable (they address the long-standing `platform.cc:108` `XXX`). Honest notes: (1) Falkon is a heavy workload (~1 GiB RAM, slow first paint under softpipe Mesa in QEMU) and the run scenario carries a generous 900 s `run_genode_until` timeout; cold-boot interactive use is not yet measured. (2) Networking is QEMU slirp only (see the next limitation row); the verified page is the host-side HTTP fixture (`net-fixture.txt`), not arbitrary internet content. (3) `run/sponge-falkon.run` (the Phase 7 boot-modules variant) remains out of scope — superseded by the disk-served `sponge-falkon-disk` scenario that defeats the boot-module ceiling.
- **Leitzentrale viewer is limited.** `lz_viewer` shows the Leitzentrale marker and window path, not the full Sculpt UI content (`run/sponge-alpha.run`).
- **No stability guarantee.** This is an Alpha and data loss is possible. Keep anything important outside the guest.
- **No snapshots, backup, or recovery.** The Alpha provides none of these safeguards.
- **Non-interactive manual mode.** `vct install --manual` shows the manual steps, but the prompt is non-interactive because vct has no terminal input channel (`run/sponge-pkg-manual.run`).

For the complete build and environment contract, see [`docs/08-development.md`](08-development.md) and [`docs/11-environment.md`](11-environment.md).
