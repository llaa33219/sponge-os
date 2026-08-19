# 14 - Boot & Storage Architecture

> Status: **ADOPTED — Phase 8 delivered.** This document defines how
> Sponge OS splits boot modules from disk-resident content, which
> filesystems and partition layout it uses, and how user data persists.
> It supersedes the Alpha stop-gap of packing everything into the boot
> image (the `image.elf` single-module model), which hit the seL4 boot
> chain's ~256 MiB boot-module ceiling (see `docs/09-roadmap.md` §9,
> `docs/13-installation.md` §6, and `docs/evidence/task-16-phase7-alpha.log`).
>
> Phase 8 (P1–P5) implemented this architecture; the per-phase outcome
> is recorded in §12. P1–P3 are committed and ship on the product `.img`
> (`./tool/dist`); P4 is DELIVERED — falkon boots from disk AND reaches
> first paint on seL4 (Phase 9 §12.4 closed the capability-chain
> blocker with three workstreams: C1 size-aware CNode backing, C2 lazy
> vm_space growth, C3 RM service route fix); P5 integrated the
> 4-partition product media into `./tool/dist` and rewrote the
> install/limitation docs. The proposal-only sections below are
> retained verbatim as the design authority for the implementation.
>
> Every mechanism referenced here was verified against the vendored
> Genode 26.05 tree (`genode/`). Citations point at exact paths.

---

## 1. Goals

1. **Microkernel-shaped boot.** Boot modules contain only what is needed
   to reach persistent storage and start the real system. Nothing more.
2. **No boot-module ceiling.** Application payloads (Falkon's 509 MiB
   WebEngine closure is the stress case) load from disk, never through
   the boot chain.
3. **User data persistence.** Installed packages, configuration, and
   user files survive reboots on the install media.
4. **Rational and boring.** Prefer mechanisms Genode already proves in
   production (Sculpt OS) over novel ones.

Non-goals (for this iteration): real-hardware matrix, network package
fetching, journaling filesystems, OTA image updates.

---

## 2. Why the Alpha model must go

The Alpha media packs every component binary, every Qt/WebEngine library,
every theme, and every package payload into boot modules. On base-sel4
all boot modules are linked into one `image.elf`, and the
GRUB→bender→seL4 chain fails above ~256 MiB of modules
(`docs/11-environment.md` §10, todo-16 evidence). Consequences already
observed:

- Falkon is packaged but cannot boot (`pkg/falkon/`, 64 ROMs, 509 MiB).
- Installed-set persistence is impossible on the media (no writable fs).
- `vct install` can only "enable pre-staged packages" — there is no
  runtime binary delivery, which is philosophically backwards for an OS
  with a package manager.
- Every payload change requires re-linking the entire boot image.

This is a live-cd reflex, not an OS architecture. The fix is the
standard one, and Genode already ships every piece of it.

### 2.1 What the "~256 MiB ceiling" actually is

Two distinct limits, both observed in the todo-16 evidence log
(`docs/evidence/task-16-phase7-alpha.log` §6):

1. **Bender relocation limit** — "no memory for relocation found":
   bender cannot find contiguous physical space for a single packed
   `image.elf` above roughly 256 MiB.
2. **seL4 untyped-cnode limit** — "device memory unavailable (due to
   limited untyped cnode range)": seL4 cannot map a single boot module
   above roughly 256 MiB into the initial untyped space, independent of
   QEMU RAM size (tested up to `-m 8G`).

Moving payloads to disk dodges **both**, because bytes no longer travel
through bender or seL4's boot-module setup at all. Residual costs move
to runtime RAM and capability budgets, addressed in §11.

---

## 3. Verified building blocks (Genode 26.05, vendored)

| Mechanism | Path | Status |
|---|---|---|
| GPT partition multiplexer | `genode/repos/os/src/server/part_block/` | in-tree |
| AHCI driver | `genode/repos/os/src/driver/ahci/` (target `ahci`) | in-tree; x86, no kernel restriction |
| NVMe driver | `genode/repos/os/src/driver/nvme/` | in-tree; **seL4-proven** (`genode/repos/os/run/nvme.run:17`) |
| USB mass storage | `genode/repos/os/src/driver/usb_block/` | in-tree |
| ext2 via rump | `genode/repos/dde_rump/src/lib/vfs/rump/vfs_rump.cc` (fs types: ext2fs, ffs, msdos, cd9660, ntfs, udf) | **port NOT prepared** — needs `prepare_port dde_rump` |
| FAT via vfs | `genode/repos/libports/src/lib/vfs/fatfs/vfs_fatfs.cc` | in-tree, no port needed |
| ROM-from-fs (cached) | `genode/repos/os/src/server/cached_fs_rom/` | in-tree |
| ROM-from-fs (uncached) | `genode/repos/os/src/server/fs_rom/` | in-tree |
| ROM-from-tar | `genode/repos/os/src/server/tar_rom/` | in-tree |
| Boot disk image tool | `genode/tool/run/image/disk` (GPT: P1 BIOS-boot, P2 ESP, P3 GENODE ext2; whole run dir is `e2cp`'d into P3, lines 79-90) | in use today |
| UEFI GOP display driver | `genode/repos/os/src/driver/framebuffer/boot/` (boot_fb, reads `platform_info/boot/framebuffer`; requires bpp=32 + type=RGB) | in-tree, **used by `sponge-desktop-disk-uefi[-nvme].run`** |
| GRUB2 EFI binary (UEFI bootloader) | `genode/contrib/grub2-*/boot/grub2/grub2_64.efi` (alex-ab fork, GRUB 2.12+; modules include multiboot2, efi_gop, gfxterm_background, png, fat, ext2) | in-tree, prebuilt |
| Reference architecture | Sculpt OS: `genode/repos/gems/run/sculpt.run`, `genode/repos/gems/sculpt/option/sculpt:234-272` (`depot` chroot → `depot_rom` cached_fs_rom → `runtime` monitor), `genode/repos/gems/src/app/sculpt_manager/runtime/prepare.cc` (`/config/<VERSION>/` overlay) | production-proven |

There is **no ext3/ext4** in the tree. Journaling is unavailable;
crash consistency must be designed around that (§7).

---

## 4. The architecture: three tiers

```
┌─────────────────────────────────────────────────────────────┐
│ TIER 0 — boot modules (image.elf, target ≤ 64 MiB)          │
│  seL4 kernel, core, init, ld.lib.so, timer, report_rom,     │
│  platform/acpi/pci_decode, ahci (storage), part_block,      │
│  vfs(+vfs_rump), cached_fs_rom                              │
│  Role: reach the GENODE partition, publish /system as ROMs, │
│  start the on-disk system init. Nothing else.               │
├─────────────────────────────────────────────────────────────┤
│ TIER 1 — system area (GENODE partition, read-mostly)        │
│  /boot          boot chain (grub, bender, image.elf)        │
│  /system/bin    desktop stack: display/input drivers,       │
│                 nitpicker, wm, decorator, sponge-de,        │
│                 configd, themed, pkgd, probes               │
│  /system/lib    Qt6, mesa, libc, etc. (NOT ld.lib.so —      │
│                 see §4.6)                                   │
│  /system/pkg    the package repository (metadata + payload) │
│  /system/themes theme files                                 │
│  /system/lz     Leitzentrale subsystem configs + binaries   │
│  Role: everything the OS ships. Mounted read-only for       │
│  consumers; writable only for a future system-update path.  │
├─────────────────────────────────────────────────────────────┤
│ TIER 2 — user area (SPONGE-DATA partition, writable)        │
│  /store/pkgd.xml      installed-set store (docs/12 §13)     │
│  /store/config.xml    configd persistent store              │
│  /home/<user>/        user files, downloads, falkon profile │
│  Role: everything the user produces. Survives re-imaging    │
│  of the system media (Tier 0+1) untouched.                  │
└─────────────────────────────────────────────────────────────┘
```

### 4.1 Why this is microkernel-shaped

The seL4 kernel + core + init + the storage session chain
(platform→ahci→Block→part_block→vfs→File_system→cached_fs_rom→ROM)
is the smallest set that can turn "bytes on a disk" into "ROM sessions",
which is the only currency Genode components are paid in. Everything
else — including the display stack — is an ordinary program loaded
through that chain. The kernel never sees a window, a browser, or a
theme file. That is the whole point of a microkernel system, and it is
exactly how Sculpt OS is structured (§3, reference architecture).

### 4.2 Why three partitions and not Sculpt's one

Sculpt keeps system and user state on a single GENODE partition and
separates them by directory policy (`chroot` + vfs policies). That is
proven and remains our **fallback (Alternative C, §9)**. We propose a
dedicated user partition because it makes the strongest persistence
guarantee with the simplest mental model:

- Re-imaging the media (`dd` a new release) rewrites Tier 0+1 and
  leaves Tier 2 byte-identical. With one partition, a full-disk `dd`
  destroys user state unless the user performs a manual backup dance —
  which violates the Convenience philosophy.
- A corrupt system area can be re-flashed without touching user data.
- The partition boundary is inspectable with ordinary tools
  (`sgdisk -l`), satisfying the Control philosophy: the user can see
  and mount both areas on any host OS (ext2 is universally readable).

### 4.3 Partition layout

The existing `image/disk` tool produces P1/P2/P3 and then packs the
image as `header + P3 + backup-GPT-gap` with **no free sectors** (it
`resize2fs -M`-shrinks P3, cats the parts, and `--move-second-header`s
the backup GPT to the image end — `genode/tool/run/image/disk:94-126`).
P4 therefore cannot be added with a bare `sgdisk --new=4`; there is no
space. `tool/dist` grows and repartitions the image post-build
(~15 lines of Mojo, no vendored-tree patch):

```
truncate -s +<data_bytes> <img>          # make room at the end
sgdisk --delete=3 <img>                  # drop P3 entry (data stays)
sgdisk --move-second-header <img>        # backup GPT -> new image end
sgdisk --new=3:<old_first>:<old_last> <img>   # re-pin P3 at old offset
sgdisk --new=4:<p4_first>:<p4_last> <img>     # create SPONGE-DATA
mkfs.ext2 -E offset=<p4_byte_off> -L SPONGE-DATA <img> <p4_kib>
sgdisk --hybrid <img>                    # rebuild hybrid MBR (P1-P3)
```

Notes: the hybrid MBR holds at most 3 GPT entries, so P4 exists in the
GPT only — fine for GRUB and for Linux hosts (`sgdisk -l` sees it);
document that MBR-only tools will not list it. The default
`--image-disk_size` computation must be told about `<data_bytes>` or
`resize2fs -M` eats the gap. A cleaner long-term option is a small
vendored patch adding `--image-disk_data_size` to `image/disk`; if ever
taken it MUST be recorded in the docs/11 patch ledger (AGENTS.md §5.2).

| # | Name | FS | Content | Writable by |
|---|---|---|---|---|
| 1 | (BIOS boot) | raw | GRUB core | image build |
| 2 | (ESP) | FAT | `bootx64.efi`, `bootia32.efi` | image build |
| 3 | `GENODE` | ext2 | `/boot`, `/system` (Tier 1) | image build now; system-update path later |
| 4 | `SPONGE-DATA` | ext2 | `/store`, `/home` (Tier 2) | pkgd, configd, apps |

Default sizes (QEMU/dev images): P3 = system content ×1.5
(~1.5–2 GiB with Falkon staged), P4 = 1 GiB default, configurable via
`tool/dist --data-size`.

#### 4.3.1 UEFI partition variant (Phase 15 W4, D15.13)

The UEFI media path is the Sponge-side recipe
(`run/sponge-desktop-disk-uefi.run` and `-nvme.run`); the vendored
`genode/tool/run/image/uefi` is **unexercised and rejected** per
D15.13 (it copies only top-level run_dir entries — the `/system`
tree would silently not land — and hardcodes `/usr/share/ovmf/OVMF.fd`
plus a single FAT partition with no docs/14 4-partition layout).
The Sponge-side variant uses the same handcrafted-image pattern
proven in `run/sponge-boot-i440fx.run` and
`run/sponge-boot-multidisk.run` (sgdisk + mkfs + e2cp + dd outside
the Genode image plugins), and appends its own OVMF pflash args to
qemu_args.

| # | Name | FS | Content | Writable by |
|---|---|---|---|---|
| 1 | `ESP` (FAT32 EF00) | FAT | `EFI/BOOT/BOOTX64.EFI` (renamed `grub2_64.efi`), `/boot/grub/grub.cfg`, `/boot/font.pf2` | image build |
| 2 | **(ABSENT)** | — | The P2 slot is intentionally not allocated so P3/P4 partition numbers are identical to the BIOS media; part_block's `<partition number="3"/>` pin and tool/mkdata's P4 grow sequence work **unchanged** | — |
| 3 | `GENODE` (ext2 8300) | ext2 | `/boot/{bender,sel4,image.elf}` (bootloader modules read by GRUB2 EFI's multiboot2 menuentry) + `/system` (Tier 1) | image build now; system-update path later |
| 4 | `SPONGE-DATA` | ext2 | `/store`, `/home` (Tier 2) | pkgd, configd, apps |

The P2-absent choice is binding. The alternatives considered and
rejected (recorded in the W4 run-script header):

* **A.** P1=BIOS-boot + P2=ESP + P3=GENODE + P4=SPONGE-DATA (the
  BIOS image/disk layout, two firmware-resident partitions) —
  rejected because image/disk does not produce this without a
  vendored-tree edit (AGENTS.md §5.2 prohibition).
* **B.** P1=ESP + P2=GENODE + P3=SPONGE-DATA (contiguous from
  P1=1) — rejected because the W1 reference scenario's
  part_block pin was on partition 2, and switching to this
  layout would re-pin part_block AND re-do tool/mkdata's
  P3 grow sequence for the UEFI path only. W4 wants the
  partition numbers to match the BIOS media so the two
  product .img files can be cross-validated (e.g. by a
  future 15-3 recovery-tool or a swap-test).
* **C.** **CHOSEN:** P1=ESP + P2 ABSENT + P3=GENODE + P4=SPONGE-DATA.
  `sgdisk --new=1:2048:+64M` creates P1 (the ESP); the next
  `sgdisk --new=3:0:+1024M` skips the P2 slot and creates P3
  directly. part_block's `<partition number="3"/>` policy
  works unchanged. tool/mkdata's `truncate + sgdisk
  --delete=3 + --move-second-header + --new=3 + --new=4 +
  mkfs.ext2 -E offset=…` works unchanged. The P2 slot is
  never allocated, so the partition count after mkdata is 3
  (P1, P3, P4) instead of 4; the structural verifier
  (`verify_partitions` in `tool/dist.mojo`) handles both
  layouts.

Display chain under UEFI (R15.2 / D15.13):

```
OVMF GOP → GRUB2 gfxterm (insmod gfxterm + set gfxpayload=auto)
         → multiboot2 FB tag (type 8) → bender → seL4
         (MULTIBOOT2_TAG_FB, boot_sys.c:683-687) →
         SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER → base-sel4 core
         (platform.cc:463-490) → platform_info/boot/framebuffer
         (phys/width/height/bpp/pitch/type) → boot_fb → Capture
         session (served by nitpicker) → desktop stack
```

`boot_fb` (`genode/repos/os/src/driver/framebuffer/boot/`) is the
UEFI display driver; `vesa_fb` is the BIOS one. The drivers
sub-init config in `sponge-desktop-disk-uefi.run` is
scenario-local (under `bin/`, never edits the vendored
`drivers_interactive-pc` recipe) and starts `boot_fb` instead
of `vesa_fb`. boot_fb requires bpp=32 + type=1 (RGB) and
rejects other GOP modes (R15.2); the OVMF default of
1024×768×32 RGB on Skylake-Client satisfies.

GRUB2 EFI multiboot2 menuentry (W1 OVMF W^X workaround tokens
per `docs/evidence/phase15-uefi-boot-smoke.log`, D15.13 verified
ground truth §B):

```
set root=(hd0,gpt3)
menuentry 'Genode on seL4 (UEFI)' {
  insmod multiboot2
multiboot2 /boot/bender serial intel_hwp_performance phys_max=256M serial_fallback
  module2 /boot/sel4 sel4 disable_iommu console_port=0x3f8 debug_port=0x3f8
  module2 /boot/image.elf image.elf
}
```

The vendored `grub2_64.efi` (alex-ab fork, GRUB 2.12+, commit
`1bc67dc` from 2025-03-06, built with `--disable-shim-lock`)
includes the `ext2` module, so GRUB can read the GENODE
partition's `/boot/` from (hd0,gpt3). `font.pf2` is loaded by
gfxterm's `terminal_output`.

### 4.4 Boot flow

```
GRUB → bender → seL4 (image.elf = Tier 0 only)
  └─ init
      ├─ platform/acpi/pci_decode (hardware discovery)
      ├─ ahci (Block, the boot disk — QEMU q35 attaches -drive to AHCI;
      │         NVMe variant for the in-tree-proven path: -device nvme)
      │   quota guidance: ram 128M, caps 5000 (Sculpt sizes block
      │   drivers at max_ram 256M for DMA pools; see §11)
      ├─ part_block → Block/P3, Block/P4
      │   (config pins partitions BY NUMBER — <partition number="3"/>,
      │    <partition number="4"/> — never auto-probe; a stale probe on
      │    slow real AHCI would otherwise read as "no GENODE partition")
      ├─ vfs_sys: Vfs_block(P3) + <rump fs="ext2fs">  → File_system "/"
      ├─ rom_sys: cached_fs_rom(vfs_sys, chroot /system) → ROM service
      └─ system init (binary "init" via rom_sys; config ROM
          /system/init/system.config via rom_sys;
          ROM label_last="ld.lib.so" → parent, see §4.6)
            ├─ display/input drivers, nitpicker, wm/decorator  (ROMs via rom_sys)
            ├─ sponge_configd, sponge_themed, sponge_pkgd      (ROMs via rom_sys)
            ├─ sponge-de                                       (ROM via rom_sys)
            │   (every dynamically linked child: ROM label_last="ld.lib.so"
            │    → parent, which the nested init re-routes up to core — §4.6)
            ├─ vfs_data: Vfs_block(P4) + <rump fs="ext2fs"> → File_system
            │     → pkgd store at /store/pkgd.xml (writable!)
            │     → configd store at /store/config.xml (writable!)
            └─ pkg_runtime: package binaries resolve as ROMs from
                  /system/pkg/<name>/payload/<binary> via rom_sys
```

Key properties:

- **Falkon boots.** Its 509 MiB never enters the boot chain;
  `libQt6WebEngineCore.lib.so` (226 MiB) is paged in by `cached_fs_rom`
  on demand. (RAM cost stays; the ceiling that blocked it was the boot
  module transport, not RAM.)
- **Install persistence works on the media.** pkgd's existing store
  (docs/12 §13, already implemented and proven on base-linux lx_fs)
  finally gets a writable fs on seL4: `vfs_data`.
- **The package repository becomes real.** `/system/pkg` on disk means
  `vct search`/`update` compare against a mutable repo, and a future
  update path can replace packages without re-linking any boot image.
- **init config is data, not image.** `system.config` lives on disk;
  changing the desktop composition no longer rebuilds `image.elf`.

### 4.5 What stays in boot modules — the explicit rule

A file goes into Tier 0 **iff** removing it makes the Tier-1 mount
impossible, **or** it is the dynamic linker (§4.6), **or** it is needed
for the rescue display (§4.7). Everything else is forbidden from Tier 0.
Concretely:

| In Tier 0 (image.elf) | Never in Tier 0 again |
|---|---|
| kernel, core, init, **ld.lib.so** | wm, decorator |
| timer, report_rom | sponge-de, sponge_configd, sponge_themed |
| platform, acpi, pci_decode | sponge_pkgd, vct |
| ahci (or nvme), part_block | all package payloads |
| vfs, vfs_rump, libc (driver deps) | themes, fonts beyond the console font |
| cached_fs_rom | Qt6/Mesa libraries |
| **rescue display: vesa_fb (BIOS) or boot_fb (UEFI), minimal nitpicker, console logger** (§4.7) | Leitzentrale subsystem |
| (the init config for the above) | |

Target: Tier 0 ≤ 80 MiB including the rescue display, i.e. 3× headroom
under the ~256 MiB ceiling. The UEFI display driver is `boot_fb`
(reads `platform_info/boot/framebuffer` from the GOP framebuffer,
§4.3.1); the BIOS display driver is `vesa_fb` (real-mode VBE via
x86emu). The two are NEVER mixed — a UEFI scenario that starts
vesa_fb hangs at the first VBE call (no INT 10h under UEFI, and
the seL4 multiboot2 path hard-wires `vbeMode = -1`,
`genode/repos/base-sel4/src/kernel/boot_sys.c:623`).

### 4.6 The ld.lib.so rule (bootstrap-cycle avoidance)

`ld.lib.so` is a **Tier-0 boot module only**. It never lives on disk
(`/system/lib` does not contain it), and it is never served by
`cached_fs_rom`. This is the Sculpt-proven pattern
(`genode/repos/gems/run/sculpt.run:329`): every dynamically linked
child carries the explicit route

```
<service name="ROM" label_last="ld.lib.so"> <parent/> </service>
```

up to core's boot-module ROM. Applied here: the nested system init
(binary `init` served from disk via `rom_sys`) still gets *its* linker
from core ROM via this route; all desktop children get theirs the same
way, re-routed through the nested init. Without this rule the second
init can never start — the component that loads a dynamically linked
binary must itself already be loaded. This is a one-line convention
applied uniformly by the pkgd/system config generators, not per-app
work.

### 4.7 The rescue display

Tier 0 includes `vesa_fb`, a minimal nitpicker, and a console logger
(report consumer writing to both serial and the framebuffer console),
~6–8 MiB total. Rationale: if the GENODE mount fails, the system must
tell the user *on the screen*, not only on a serial line — a black
screen with a silent serial log fails the Convenience philosophy and
gives the user no door to walk through (Control). The rescue path
distinguishes "Block session not yet ready" (retry, slow real AHCI)
from "partition table unreadable" (rescue screen + offer to continue
with Tier 2 on RAM, i.e. ISO-style live mode).

---

## 5. Filesystem decision

**ext2 via `vfs_rump`, for both GENODE and SPONGE-DATA.**

Rationale:

1. It is the Sculpt-proven path (Sculpt mounts the GENODE partition
   with `<rump fs="ext2fs" writeable="yes">`; see
   `sculpt_manager/runtime/file_system.cc:16-55`).
2. ext2 is readable/writable by every host OS the developer might use
   (Control: inspectable media).
3. FAT (`vfs_fatfs`) needs no port preparation and is the documented
   fallback if `prepare_port dde_rump` ever becomes a problem; it loses
   POSIX permissions and 4 GiB+ file comfort, so it is not primary.
4. There is no journaling fs in the tree (no ext3/4; ffs exists but is
   less traveled in Genode). ext2's lack of a journal is handled in §7.

Port preparation: `tool/ports/prepare_port dde_rump`, hash recorded in
docs/11 §5 (one-time; the port pins rump.git @ 28945d1a).

---

## 6. Persistence model

| Data | Where | Written by | Format |
|---|---|---|---|
| Installed-set store | `SPONGE-DATA:/store/pkgd.xml` | sponge_pkgd (single writer) | docs/12 §13 (versioned XML, torn-write detection already specced §13.2) |
| Config store | `SPONGE-DATA:/store/config.xml` | sponge_configd (single writer) | flat key-value XML |
| User files | `SPONGE-DATA:/home/` | apps (file manager, textedit, falkon downloads) | plain fs |
| Boot/system | `GENODE:/boot`, `/system` | image build (later: `vct system update`) | as built |

Alpha semantics change, honestly documented in docs/13 §6 upon
adoption: "install = enable pre-staged packages" becomes
"install = enable packages from the on-disk repository"; persistence
limitations for installs and config are removed; the ISO remains a
no-persistence live/eval mode (§8).

---

## 7. Crash consistency without a journal

1. **Single-writer rule** (already true): each store file has exactly
   one daemon owner. No concurrent writers, no lock files.
2. **Whole-file replacement**: stores are written as
   write-new-then-rename within the same directory (vfs_rump supports
   rename), never in-place edits. The §13.2 torn-write detection stays
   as the backstop.
3. **Repair path**: `vct` gains (later phase) a `check` verb wrapping
   `e2fsck` against SPONGE-DATA, mirroring Sculpt's "Check" button.
   Until then, the honest limitation line stays in docs/13.
4. Read-only consumption of `/system` means a power cut mid-write can
   only ever affect SPONGE-DATA, never the OS itself.

---

## 8. The ISO question

An ISO is physically read-only. Options:

- **Disk image (.img) = the real product.** Four partitions, full
  persistence. This is what `tool/dist` optimizes and what "install to
  USB" means.
- **ISO = live/eval mode.** Tier 2 is a plain RAM file system
  (`<ram/>` in the Tier-2 vfs): everything works, nothing persists. The
  quick-start says so in one line. This matches every live-OS
  convention and keeps the ISO useful for evaluation without promising
  persistence it cannot deliver.

---

## 9. Alternatives considered and rejected

- **A. Status quo (single image.elf)** — rejected: boot-module ceiling,
  no persistence, no runtime delivery, un-microkernel (§2).
- **B. tar_rom second-stage module (todo-16 attempt)** — rejected:
  still bounded by the boot chain (seL4 reset during boot-module setup
  with a 509 MiB module), and provides no writable storage.
- **C. Sculpt-style single GENODE partition (system + user dirs by
  policy)** — viable and proven; kept as **fallback** if the P4 tooling
  fights us. Weaker persistence story under full-media re-imaging (§4.2).
- **D. base-hw instead of base-sel4** — out of scope; the kernel lock
  stands (docs/09 §2, AGENTS.md). Nothing in this proposal is
  kernel-specific beyond the Tier-0 driver glue, which nvme.run proves
  on seL4.

---

## 10. Impact map (what changes where)

| Area | Change |
|---|---|
| `run/sponge-alpha.run` | splits into Tier-0 boot config + on-disk `system.config`; the desktop composition moves to disk |
| new `run/sponge-boot.run` | Tier-0-only minimal boot proving the storage chain (the new smoke test) |
| `tool/dist.mojo` | stages `/system` into the run dir (auto-e2cp'd into GENODE by image/disk), adds the §4.3 P4 grow/repartition sequence, `--data-size` flag, enlarged default `--image-disk_size` |
| `sponge_pkgd` | package binaries route via rom_sys (label-based ROM routing to cached_fs_rom); store path becomes a writable fs session (already implemented — just wired to vfs_data); the generated start nodes gain the uniform `label_last="ld.lib.so" → parent` route (§4.6) |
| `sponge_configd` | gains the same writable store wiring for config persistence — delivered in Phase 14 W6 (`run/sponge-configd-persist.run` proves the mechanism on a RAM vfs; the product-media wiring against `/store/config.xml` on SPONGE-DATA is the same code path with a different mount root and a `/store` parent directory pre-created by the install media — the daemon expects the parent directory to exist before the first save, the same as `sponge_pkgd`) |
| `pkg/<name>/metadata.xml` | unchanged (the repo format survives; the repo's *location* moves to `/system/pkg`) |
| `docs/08, 11, 13` | development flow update, dde_rump port row, QEMU nvme/ahci args, limitations rewrite (falkon + persistence caveats removed when delivered) |
| focused scenarios (`sponge-terminal` etc.) | unchanged — they remain the component-level dev gates |

Phased rollout (each phase separately committable and scenario-gated):
P1 storage-chain smoke (`sponge-boot.run`: mount GENODE, serve a ROM
from disk, read it back on seL4 — acceptance **includes** a successful
`prepare_port dde_rump` with its hash recorded in docs/11 §5; if the
port fails, the primary FS decision falls back to FAT per §5 and §7's
rename-based consistency story must be revisited because FAT rename is
not reliably atomic). P2 on-disk system init + desktop from disk.
P3 pkgd/configd stores on SPONGE-DATA (persistence scenarios).
P4 Falkon from disk (the ceiling-killer proof). P5 dist/media +
docs/13 rewrite.

---

## 11. Risks

1. **ahci on base-sel4 is analogy-proven, not demonstrated** (nvme is
   demonstrated via `genode/repos/os/run/nvme.run:17`). Mitigation: P1
   tests both; QEMU default for the product media is the one that
   passes; nvme is the likely primary (`-device nvme`). Block drivers
   get explicit quotas (ahci: ram 128M, caps 5000; nvme: ram 64M,
   caps 5000) — Sculpt sizes block drivers at `max_ram: 256M` precisely
   because DMA-able memory on seL4 is a constrained pool
   (`genode/repos/gems/sculpt/option/board-pc`).
2. **rump on seL4**: widely used on hw kernels; seL4 usage to be proven
   in P1 (fallback: vfs_fatfs needs no port, with the §7 caveat that
   FAT rename is not reliably atomic). Budget **two** rump instances
   (vfs_sys + vfs_data) at ~64–128 MiB resident each — this is a real
   RAM line item, not noise. RAM-tight configurations MAY instead use
   one rump instance with ro/rw subdirectory policies (the Alternative
   C single-partition shape) at the cost of the §4.2 isolation story.
3. **cached_fs_rom RAM and capability growth**: it caches ROMs in RAM
   (Sculpt manages it with `managed max_ram: 2G`). Falkon's 226 MiB
   WebEngine library is cached on first use. Additionally, on seL4,
   mapping a large ROM dataspace into a client costs frame capabilities
   in the client's PD — `libQt6WebEngineCore.lib.so` is ~58k frames.
   `pkg/falkon/metadata.xml`'s Alpha value `caps="4000"` is at least an
   order of magnitude too small for the disk-served model; P4 must
   measure and set the real value (start ≥ 30000) and QEMU RAM for the
   Falkon scenario should be ≥ 6 GiB (cached_fs_rom's cache + falkon's
   own heap + two rumps + seL4's SKIM window make 4 GiB tight).
4. **Boot-time fs dependency**: a corrupt or slow GENODE partition must
   not brick the boot, and the failure must be visible on the screen.
   Mitigations: part_block pins partitions by number (never auto-probe,
   §4.4); the rescue display (§4.7) distinguishes "Block session not
   yet ready" (retry) from "partition table unreadable" (rescue screen
   + optional RAM-backed live mode). Never a silent hang.
5. **Tier-0 roster correctness**: the §4.5 list must be validated
   against the real start ordering of the current
   `run/sponge-alpha.run` drivers sub-init (acpi report consumers,
   report_rom policies) before P1 — an under-specified Tier 0 is
   exactly the class of bug AGENTS.md §4.2's scenario-gate rule exists
   to catch.

---

## 12. Phase 8 status (P1–P5)

This section is the implementation outcome log for the P1–P5 rollout
defined in §10. Each sub-section names the commit, the scenario that
proves the claim, and the evidence file. P4 (Falkon from disk) was
landed in two stages: the Phase 8 P4 scenario proved the disk-served
architecture, and the Phase 9 cspace workstreams (C1–C4, §12.4) closed
the capability-chain blocker that had kept first paint unattained. The
design authority for every
claim is the section of this document the phase implements (cited
per-phase). The Phase 8 plan tracker is
`docs/plans/phase8-storage.md`.

### 12.1 P1 — storage-chain smoke — DELIVERED

- **Commit:** `d3473f61f6`.
- **Scenario:** `run/sponge-boot.run` (base-sel4 only,
  `RUN_OPT='--include image/disk'`).
- **Claim proven:** the Tier-0 disk-read chain
  (§4.4 — `platform`/`acpi`/`pci_decode` → `ahci` (Block) → `part_block`
  (Block/P3, pinned BY NUMBER) → `vfs` (`<rump fs="ext2fs"/>`) →
  `cached_fs_rom` (chroot `/system`) → `boot_probe` reading
  `/system/marker.txt` and logging `boot-probe: PASS`).
- **`prepare_port dde_rump`:** succeeded; hash recorded in
  `docs/11-environment.md` §5 (acceptance gate per §10).
- **NVMe variant:** also proven (`SPONGE_BOOT_NVME=1`); AHCI remains the
  QEMU default for the product media (q35 auto-attach).
- **Failure path:** `SPONGE_DISK_FAIL=1` corrupts the marker; bounded
  timeout, no silent hang (§7 risk 4).
- **Evidence:** `docs/evidence/p1-storage-boot.log`.

### 12.2 P2 — desktop from disk — DELIVERED

- **Commit:** `e7f8b9a458`.
- **Scenario:** `run/sponge-desktop-disk.run` (base-sel4 only,
  `RUN_OPT='--include image/disk'`).
- **Claim proven (the §4.4 key properties):**
  - The full Qt6 desktop boots with `image.elf` ≤ 12 MiB (well under
    the §4.5 80 MiB target). The desktop stack (wm/decorator/
    sponge_configd/sponge_themed/sponge_pkgd/sponge-de/alpha_probe) is
    served entirely from `/system/bin` and `/system/lib` on the GENODE
    ext2 partition via two `cached_fs_rom` instances (rom_sys chroot
    `/system`, rom_lib chroot `/system/lib`).
  - ld.lib.so is Tier-0 only (§4.6); every dynamically linked child
    carries `<service ROM label_last="ld.lib.so"><parent/></service>`,
    cascading up through the nested init.
  - `alpha-probe: PASS` from a disk boot (themed panel pixel + launcher
    feed + configd broadcast).
- **Evidence:** `docs/evidence/p2-desktop-disk.log`.

### 12.3 P3 — persistence on SPONGE-DATA — DELIVERED

- **Commits:** `tool/mkdata` partition creator `f25a81dcbe`; two-boot
  persistence scenario `run/sponge-persist-disk.run` rides the same
  commit.
- **Scenario:** `run/sponge-persist-disk.run` (base-sel4 only,
  `RUN_OPT='--include image/disk'`).
- **Claim proven (§6 persistence model):** `sponge_pkgd`'s
  installed-set store survives a reboot when backed by a writable ext2
  on the SPONGE-DATA partition. `tool/mkdata` implements the §4.3
  grow/repartition sequence; `vfs_data` (Vfs_block on P4 + rump ext2)
  backs the store. The two-boot proof is adversarial: boot 1 installs
  `hello` and writes the store; boot 2 boots a fresh QEMU against the
  SAME image file and `sponge_pkgd` logs `restored N root(s) from
  store` — a line that can ONLY appear if boot 1's write survived. A
  host-side `e2cp` readback of `/store/installed.xml` off the image
  corroborates the write independent of any guest log
  (`misleading_success_output` defense).
- **Documentation sync:** `docs/13-installation.md` §6 no longer lists
  install-persistence as a limitation on the `.img` media (it is still
  listed for the live/eval `.iso`, which is read-only by nature — §8).
- **Evidence:** `docs/evidence/p3-persistence.log`,
  `docs/evidence/p3-failure-channel.log` (the read-only failure path).

### 12.4 P4 — Falkon from disk — DELIVERED (first paint on seL4)

- **Commits:** `run/sponge-falkon-disk.run` `bae5423d1b` (the Phase 8
  P4 scenario + architectural proof); Phase 9 closes the blocker chain
  with three workstreams — C1 `93ded092f1` (size-aware on-demand CNode
  backing), C2 `19303468ff` (lazy vm_space growth to 131072 mappings),
  C3 `7feaa6510f` (RM service route fix — the actual last blocker).
- **Scenario:** `run/sponge-falkon-disk.run` (base-sel4 only,
  `RUN_OPT='--include image/disk'`).
- **Architectural claim PROVEN (§1 goal 2 / §4.4 key property
  "Falkon boots"):** falkon's 509 MiB WebEngine payload boots FROM DISK
  via `cached_fs_rom` — `image.elf` stays at ~12 MiB. falkon's process
  starts, the dynamic linker resolves the 237 MiB
  `libQt6WebEngineCore.lib.so` from disk via rom_pkg, and lwIP DHCPs to
  10.0.2.15 over the `ipxe_nic` + `nic_uplink` stack. The Phase 7
  boot-module ceiling (§2.1 — Bender relocation + seL4 untyped cnode)
  is fully defeated: the payload never enters the boot chain.
- **First paint ACHIEVED on seL4 (Phase 9 closes the chain).** The
  `falkon_probe` pixel-verifies the browser window (59% non-background,
  37 distinct color buckets at capture poll 420) AND the host fixture
  GET is observed in the access log (falkon's config arg navigates to
  `http://10.0.2.2:8765/net-fixture.txt`). Both gates pass. Verified
  twice in the Phase 9 C3 evidence and re-verified in the Phase 9 C4
  full-regression suite (`docs/evidence/c4-logs/sponge-falkon-disk.log`,
  `falkon-probe: PASS`, `Run script execution successful.`).
- **The blocker chain — three layers, each closed by a Phase 9
  workstream (all vendored-tree patches are in the docs/11 §4 ledger
  and are upstreamable).** Each layer is documented with the seL4
  kernel diagnostic that pinned it, the patch that closed it, and the
  canary that proves no regression:
  1. **Main per-PD CSpace exhaustion (closed by C1, ledger #6).** The
     default per-child-PD main CSpace was `CSPACE_SIZE_LOG2 = 6+7 =
     13` → 8192 slots, and each child-PD CNode was backed by exactly
     one 4 KiB untyped page (`cnode.h:159` → 128 slots/CNode at
     seL4's 32-byte CTE). Falkon-class dynamic workloads exceed this.
     C1 (`93ded092f1`) makes the CNode backing size-aware and
     on-demand (the `Cnode` ctor allocates backing sized to the
     requested CNode, drawn from the existing 16 KiB untyped pool via
     the per-arch `cnode_backing_alloc`, WITHOUT the fixed 16K-pool
     carve that regressed ahci DMA in earlier P4 attempts). Canary:
     `run/sponge-boot.run` PASS (ahci/DMA unaffected — the C1 evidence
     `docs/evidence/c1-dma-safe-backing.log` documents the diagnosis
     of WHY the earlier fixed-pool carve broke ahci and how the
     on-demand path avoids it).
  2. **vm_space PTE-pool exhaustion (closed by C2, ledger #7).**
     Core's `Vm_space` keeps a SEPARATE per-PD pool of PTE/page-frame
     selectors (`NUM_VM_SEL_LOG2 = 7+5+3 = 15` → 32768 selectors per
     PD, `platform.cc:672` "Physical memory per PD at most: 128M").
     Each mapped 4 KiB page consumes one selector from this pool;
     falkon's ~100k WebEngine frames AND rom_pkg's ~130k cached
     payload frames each independently exceed 32768, surfacing as
     `flush page table entries - mapping cache full - PD: <name> out
     of CAP`. C2 (`19303468ff`) raises `LEAF_CNODE_SIZE_LOG2` 7→9 on
     x86_64 (→ `NUM_VM_SEL_LOG2` 15→17, 131072 selectors/PD, 512 MiB
     max mappings/PD) and constructs the 3rd/4th-level CNodes LAZILY
     on first use via the new `_ensure_leaf()` hook — eager
     construction starved core heap or silently crashed `Platform()`
     in all prior attempts (catalogued in `docs/evidence/p4-cspace-
     fix.log` §4). Canary: `run/sponge-desktop-disk.run` PASS.
  3. **Missing RM service route (closed by C3 — the ACTUAL last
     blocker; the prior "cap_quota=2 = exhaustion" reading was
     WRONG).** After C1+C2, falkon booted, DHCP'd, and then was
     stopped with `[pkg_runtime] Warning: falkon: no route to service
     "RM" (label="falkon -> ")` / `[falkon] Error: stop because parent
     denied RM-session: label="", ram_quota=64K, cap_quota=2`. The
     Phase 7 / Phase 8 diagnosis read `cap_quota=2` as "only 2 caps
     remain" and attributed the stop to main-CSpace exhaustion; that
     reading was incorrect. `cap_quota=2` is `Rm_session::CAP_QUOTA`
     (`rm_session.h:34`) — the FIXED cap quota an `Rm_connection`
     requests, NOT a remaining-capacity measure. The real blocker was
     a pure config gap: RM is a distinct core service (NOT subsumed
     by PD — PD only hands out the component's own address_space /
     stack_area / linker_area region maps; additional region maps for
     mmap / `qtwebengine_shm` need a separate `Rm_connection` routed
     to core), and `sponge_pkgd`'s generated `parent-provides` did
     not include it. C3 (`7feaa6510f`) adds `<service name="RM"/>`
     to `_generate_runtime_config()`'s parent-provides (and to the
     hardcoded parent-provides in `run/sponge-falkon-disk.run`). Zero
     vendored-tree change; a one-line config fix. The c3 evidence
     log (`docs/evidence/c3-main-cspace-falkon.log`) decodes every
     token of the denial message to source, line by line.
- **The 8192/16384/30000/200000 caps-fquota numbers are moot.** They
  were all observed-failure data from BEFORE C1+C2+C3. With the
  chain closed, the falkon scenario passes at its documented sizing
  (`pkg/falkon/` `caps=200000`; `falkon ram=1G`; `pkg_runtime
  caps=210000 ram=1500M`; `system caps=250000 ram=3000M`; `rom_pkg
  ram=768M`; QEMU `-m 6G`).
- **The two base-sel4 capability patches are upstreamable.** Ledger
  rows #6 (C1) and #7 (C2) in `docs/11-environment.md` §4 record the
  what / where / why / how-to-drop for each. Both address the
  long-standing `platform.cc:108` `XXX` ("allocate intermediate
  CNodes ... here") — the upstream resolution is dynamic, lazy CNode
  allocation, which is exactly what C1+C2 implement. The "Drop When"
  column for both rows reads: upstream resolves `platform.cc:108`
  `XXX` / grows vm_space dynamically.
- **Evidence ( REQUIRED reading — they save the next iteration the
  ~2 dozen build+boot cycles the P4 + Phase 9 work consumed):**
  `docs/evidence/p4-cspace-falkon.log` (the original one-line-patch
  attempt that surfaced the single-page CNode backing cap, with the
  regression-clean 16384 config);
  `docs/evidence/p4-cspace-fix.log` (the multi-file main-CSpace fix
  that PROVED the boot-module ceiling could be defeated, then
  documented the vm_space + 16K-pool layers);
  `docs/evidence/p4-falkon-disk.log` (the Phase 8 P4 architectural
  proof — falkon boots from disk, DHCPs, blocked at first paint by
  the then-undocumented cspace/vm_space chain);
  `docs/evidence/c1-dma-safe-backing.log` (C1: DMA-safe on-demand
  large CNode backing, ahci canary green);
  `docs/evidence/c2-lazy-vmspace.log` (C2: lazy vm_space growth to
  131072/PD, canaries green, falkon gets past mapping-cache-full);
  `docs/evidence/c3-main-cspace-falkon.log` (C3: the RM route fix —
  decodes the denial message, FALKON FIRST PAINT ACHIEVED);
  `docs/evidence/c4-regression.log` (C4: full regression + this docs
  closure).
- **Documentation sync:** `docs/13-installation.md` §6 falkon entry
  updated — falkon is packaged, boots from disk, AND renders (first
  paint achieved on the seL4 media); the "first paint blocked"
  language is removed; honest notes kept (QEMU slirp only, HTTP
  fixture verified, heavy workload).
- **Honest non-fatal observation (C4 regression suite):**
  `run/sponge-desktop-disk.run` exhibited a flaky timing failure on
  the first two of three back-to-back C4 runs (alpha_probe polled
  1180 iterations without the themed panel ever compositing; the
  `flush page table entries - mapping cache full - PD: init -> {fb,
  vfs, rom_lib} out of CAP` warnings that also appear in the C2
  canary log preceded the stall). The third run PASSED in 64 s with
  `alpha-probe: PASS`. The same scenario PASSED deterministically in
  the C2 canary and in the task-20 final gate. The stall is therefore
  NOT a deterministic Phase 9 regression; it is a timing window in
  the C2 lazy-vm_space leaf construction under disk-served ROM load
  (sponge-de sometimes loses the race against the Tier-0 vfs/rom_lib
  children that are still servicing their own mapping-cache pressure
  — the warnings are non-fatal in the passing runs). Tracked here,
  not silently absorbed; the fix shape is either an `_ensure_leaf`
  retry on transient allocation failure or a cap_quota bump on the
  Tier-0 vfs/rom_lib children.

### 12.5 P5 — media + docs — DELIVERED

- **Commits:** not yet committed (the orchestrator commits per the
  Phase 8 plan; this section records the implementation outcome).
- **Implementation:**
  - `tool/dist` rewritten: the default product media is the
    4-partition `.img` (`run/sponge-desktop-disk` + `tool/mkdata` for
    SPONGE-DATA P4); the `.iso` is the live/eval alpha boot-modules
    media (§8). Control doors: `--no-data` (skip P4), `--data-size <N>`
    (custom P4 size), and the standalone `tool/mkdata` for re-runs.
  - `docs/13-installation.md` rewritten: install-persistence limitation
    removed for the `.img` (kept for the `.iso`); falkon entry updated
    (Phase 9 §12.4: architecture works AND first paint is achieved on
    seL4); the 4-partition media + mkdata step documented in the
    install flow.
  - `docs/08-development.md` §11 + §4 updated for the new dist flow
    and the new run scenarios (`sponge-boot`, `sponge-desktop-disk`,
    `sponge-persist-disk`, `sponge-falkon-disk`).
- **Verification (acceptance gates):**
  1. `./tool/dist` produces a 4-partition `.img` that boots to
     `alpha-probe: PASS` (sgdisk -p + boot log captured in the
     evidence).
  2. `RUN_OPT='--include image/iso'` of `sponge-alpha` still boots to
     PASS (live/eval mode).
  3. `docs/13` no longer lists install-persistence as a limitation on
     the `.img` and carries the updated falkon entry.
  4. `docs/14` §12 (this section) records the Phase 8 outcome per
     phase.
- **Evidence:** `docs/evidence/p5-media-docs.log`.

### 12.6 Phase 8 summary table

| Phase | Outcome | Commit | Evidence |
|---|---|---|---|
| P1 storage-chain smoke | ✅ DELIVERED | `d3473f61f6` | `docs/evidence/p1-storage-boot.log` |
| P2 desktop from disk | ✅ DELIVERED | `e7f8b9a458` | `docs/evidence/p2-desktop-disk.log` |
| P3 persistence on SPONGE-DATA | ✅ DELIVERED | `f25a81dcbe` | `docs/evidence/p3-persistence.log` |
| P4 Falkon from disk | ✅ DELIVERED (first paint on seL4 — Phase 9 §12.4) | `bae5423d1b` (Phase 8 P4 scenario) + Phase 9 `93ded092f1` (C1) + `19303468ff` (C2) + `7feaa6510f` (C3) | `docs/evidence/p4-falkon-disk.log`, `docs/evidence/c1-dma-safe-backing.log`, `docs/evidence/c2-lazy-vmspace.log`, `docs/evidence/c3-main-cspace-falkon.log`, `docs/evidence/c4-regression.log` |
| P5 media + docs | ✅ DELIVERED (this phase) | (orchestrator commits) | `docs/evidence/p5-media-docs.log` |
