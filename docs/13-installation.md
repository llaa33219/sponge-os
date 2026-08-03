# 13 - Installation and Alpha Quick Start

This guide covers Sponge OS Alpha 0.1.0, codename Archaeocyte. The Alpha media target is seL4 running in QEMU. It is a development release, not a general hardware installer.

## 1. Prerequisites

Start with a checkout of this repository. The Genode 26.05 source tree is already vendored at `genode/`; no separate Genode checkout is needed.

Install the host toolchain and base packages listed in [`docs/11-environment.md` §7](11-environment.md#7-host-bootstrap). The Alpha media builders additionally require:

```bash
# Debian / Ubuntu
sudo apt install xorriso gptfdisk mtools e2tools dosfstools e2fsprogs

# Arch / CachyOS
sudo pacman -S libisoburn gptfdisk mtools e2tools dosfstools e2fsprogs
```

The required executable names are `xorriso`, `sgdisk`, `mcopy`, `e2cp`, `e2mkdir`, `mkfs.ext2`, `mkfs.vfat`, and `resize2fs`. The seL4 build also needs the toolchain, CMake, Ninja, QEMU, Tcl, expect, and the Python modules listed in [`docs/11-environment.md` §7](11-environment.md#7-host-bootstrap). Set up the repository-local Mojo environment with:

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

The command checks the media tools, builds both `image/disk` and `image/iso` from `run/sponge-alpha.run`, copies the release artifacts into `var/dist/`, and writes SHA-256 sidecars. Verify the sidecars with:

```bash
(cd var/dist && sha256sum -c *.sha256)
```

The resulting files are:

```text
var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
```

### 2.2 Manual build

The automation has a documented control path. From the repository root, after the normal build preparation and port setup, run the equivalent commands from [`docs/08-development.md` §11.2](08-development.md#112-manual-flow-the-canonical-reference):

```bash
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/disk'
mkdir -p var/dist
cp genode/build/x86_64/var/run/sponge-alpha.img \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.img \
    > sponge-os-0.1.0-alpha-x86_64-sel4.img.sha256)

rm -rf genode/build/x86_64/var/run/sponge-alpha*
make -C genode/build/x86_64 run/sponge-alpha \
    KERNEL=sel4 BOARD=pc \
    RUN_OPT='--include image/iso'
cp genode/build/x86_64/var/run/sponge-alpha.iso \
   var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
(cd var/dist && sha256sum sponge-os-0.1.0-alpha-x86_64-sel4.iso \
    > sponge-os-0.1.0-alpha-x86_64-sel4.iso.sha256)
(cd var/dist && sha256sum -c *.sha256)
```

## 3. Boot in QEMU

QEMU is the supported Alpha target. Use at least 2 GiB of guest RAM. These commands boot the already-built media directly and keep the guest log in the terminal:

### 3.1 Disk image

```bash
qemu-system-x86_64 \
    -m 2G \
    -drive format=raw,file=var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img \
    -serial stdio \
    -display none \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
```

### 3.2 ISO

```bash
qemu-system-x86_64 \
    -m 2G \
    -boot d \
    -cdrom var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso \
    -serial stdio \
    -display none \
    -netdev user,id=net0 \
    -device e1000,netdev=net0
```

The scenario-level media verification uses the same `image/disk` and `image/iso` modes and gates on `alpha-probe: PASS` (`run/sponge-alpha.run`). To stop a guest that does not respond to `vct shutdown`, use QEMU's monitor escape, `Ctrl-A`, then `x`, or terminate the QEMU process from the host.

## 4. Write the disk image to USB

The Alpha has no hardware support matrix. Writing an image to USB is provided only as a manual experiment, not as a supported boot target.

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

1. **Boot the media.** Start the `.img` or `.iso` in QEMU. The seL4 boot chain reaches the Sponge desktop probe marker (`run/sponge-alpha.run`; media boot evidence is recorded in `.omo/evidence/task-5-phase7-alpha.log` and `.omo/evidence/task-6-phase7-alpha.log`).
2. **See the desktop.** The themed panel and launcher appear automatically after boot (`run/sponge-alpha.run`).
3. **Open the launcher.** The launcher receives the pre-staged package set from `sponge_pkgd` (`run/sponge-alpha.run`).
4. **Launch Terminal.** The terminal window, bash prompt, and a keystroke round trip are verified (`run/sponge-terminal.run`).
5. **Launch Files.** The file manager window, navigation, copy, delete, and read-only refusal are verified (`run/sponge-files.run`).
6. **Launch TextEdit.** The Qt6 text editor window renders after install and launch (`run/sponge-textedit.run`). **Falkon is not bootable on this Alpha media; do not expect a browser window here.** Its package is staged and documented in the limitations below (`.omo/evidence/task-16-phase7-alpha.log`).
7. **Inspect system status.** `vct status` reads the live component state (`run/sponge-vct-status.run`).
8. **Inspect packages.** `vct list` reports the installed set, and `vct install` enables a pre-staged package (`run/sponge-pkg-list.run`, `run/sponge-pkg-install.run`).
9. **Read and change configuration.** `vct config theme.active` reads the active theme, and `vct theme apply` exercises the theme command and live theme backend (`run/sponge-config.run`).
10. **Launch explicitly.** `vct launch <package>` starts an installed package through the same backend used by the desktop launcher (`run/sponge-launch.run`).
11. **Open Leitzentrale.** `vct leitzentrale` enables the Leitzentrale subsystem and its viewer marker appears in the desktop (`run/sponge-leitzentrale.run`, `run/sponge-alpha.run`). The viewer is a marker view, not the complete Sculpt UI.
12. **Shut down.** `vct shutdown` requests ACPI poweroff and QEMU exits in the success scenario (`run/sponge-power.run`).

Installs are not persistent on the seL4 media. After a reboot, repeat the `vct install` step for any package you want to enable (`run/sponge-pkg-persist.run` proves persistence only in the separate base-linux development flow, not on Alpha media).

## 6. Known limitations

This register is part of the Alpha contract.

- **QEMU-only target.** There is no hardware support matrix for this release. The verified target is seL4 in QEMU (`run/sponge-alpha.run`).
- **Install means enable.** All Alpha binaries are pre-staged into the seL4 boot image. `vct install` enables a package already on the image. It does not deliver a binary at runtime (`run/sponge-pkg-install.run`; decision A1 in `docs/plans/phase7-alpha-decisions.md`).
- **No install persistence on seL4 media.** Installed package state is lost when the Alpha media reboots. The base-linux `lx_fs` persistence scenario is a development proof only (`run/sponge-pkg-persist.run`; decision A6).
- **QEMU slirp networking only.** Networking is limited to QEMU user-mode networking and the slirp path. Wi-Fi, real-hardware networking, and a network configuration UI are out of scope (`run/sponge-net-probe.run`).
- **Falkon is packaged but not bootable on seL4 media.** The boot chain has an approximately 256 MB boot-module ceiling, while Falkon's WebEngine payload is approximately 509 MB. The package is therefore not a working browser in this Alpha. The planned fix is disk-based payload staging after Alpha (`.omo/evidence/task-16-phase7-alpha.log`, decision D5 in `docs/plans/phase7-alpha-decisions.md`).
- **Leitzentrale viewer is limited.** `lz_viewer` shows the Leitzentrale marker and window path, not the full Sculpt UI content (`run/sponge-alpha.run`).
- **No stability guarantee.** This is an Alpha and data loss is possible. Keep anything important outside the guest.
- **No snapshots, backup, or recovery.** The Alpha provides none of these safeguards.
- **Non-interactive manual mode.** `vct install --manual` shows the manual steps, but the prompt is non-interactive because vct has no terminal input channel (`run/sponge-pkg-manual.run`).

For the complete build and environment contract, see [`docs/08-development.md`](08-development.md) and [`docs/11-environment.md`](11-environment.md).
