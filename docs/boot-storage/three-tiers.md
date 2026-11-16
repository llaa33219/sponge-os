# Boot & storage architecture: the three tiers

> **Breadcrumb:** parent [14 - Boot & Storage Architecture](../14-boot-storage-architecture.md); this file holds its §4 (The architecture: three tiers, with §4.1-§4.7 and §4.3.1), moved verbatim.
> Siblings in `docs/boot-storage/`: [design-decisions.md](design-decisions.md) (§2, §5-§9), [impact-risks-status.md](impact-risks-status.md) (§10-§12).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

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

**Naming authority (binding).** The data partitions are partition P3
`GENODE`, partition P4 `SPONGE-VAR`, and partition P5 `SPONGE-HOME`,
plus the firmware partitions; there is **no P6+**, no renames, and no
re-purposing (D16R.2). These three names are THE naming authority
(D16R.3) for every document, tool, and scenario. `SPONGE-DATA` is the
**historical** name of the pre-P3.2 writable partition: it survives
only as history (below and in the Phase-8 receipts) and is never
current wording.

| # | Name | FS | Content | Writable by | Served by |
|---|---|---|---|---|---|
| 3 | `GENODE` | ext2 | `/boot`, `/system` (Tier 1) | image build now; system-update path later | `vfs_sys` (read-only at policy AND mount) |
| 4 | `SPONGE-VAR` (64M) | ext2 | `/pkgd` (package-store region), `/log` (log-sink region) | pkgd, the log sink | **two** providers: `vfs_pkg` + `vfs_log` |
| 5 | `SPONGE-HOME` (1024M) | ext2 | `/user` (Tier 2) | apps (file manager, textedit, falkon downloads) | `vfs_home` |

The firmware partitions precede them: BIOS media carries P1 BIOS-boot
+ P2 ESP; the UEFI media carries P1 ESP with P2 intentionally absent
(§4.3.1). Re-imaging the media rewrites Tier 0+1 (and the firmware
partitions) and leaves P4/P5 byte-identical — the Tier-2 persistence
guarantee of §4.2 now spans the two data partitions.

P4 deliberately backs **two** File_system providers — the three data
partitions are three storage *roles*, not three mounts: `vfs_pkg` and
`vfs_log` are independent vfs+rump instances on the same ext2
partition, each through its own `part_block` (upstream `part_block`
allows one Block session per partition — `partition 4 already in use`,
`os/src/server/part_block/main.cc:286`), and their two independent
rump caches carry a coherence caveat while both can write. The full
provider story — the `vfs_pkg`/`vfs_log` split rationale, the
`part_block_log` instance, and the caveat — is `docs/17` §2a.

Default sizes on the shipped media: P4 = 64M, P5 = 1024M; P3 is
content-sized by the image build (measured: the browser-staged
new-media build lands a 701.0 MiB P3 with ≈15.5 MiB margin over the
staged content — `docs/evidence/phase16-p26-browser-staging.md` §5;
new media may grow P3 for the measured WebEngine closure, existing
deployed media is never resized in place, D16R.2). The hybrid MBR
holds at most 3 GPT entries, so P4/P5 exist in the GPT only — fine for
GRUB and for Linux hosts (`sgdisk -l` sees them); document that
MBR-only tools will not list them.

**Naming history (preserved, not current).** The Phase-8 P3
persistence work (§12.3) shipped the writable area as a single P4
named `SPONGE-DATA` (`/store` + `/home`), added post-build by
`tool/dist`/`tool/mkdata` because `image/disk` packs
`header + P3 + backup-GPT-gap` with **no free sectors** (it
`resize2fs -M`-shrinks P3, cats the parts, and `--move-second-header`s
the backup GPT to the image end — `genode/tool/run/image/disk:94-126`),
so a bare `sgdisk --new=4` has no space. The grow/repartition sequence
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

The three-area layout (user directive 2026-10-05) split that one
partition's two roles: system-mutable state (`/pkgd`, `/log`) went to
P4 `SPONGE-VAR` and the user area (`/user`) to P5 `SPONGE-HOME`. The
shipped product media provisions both partitions in the scenario
itself; `tool/mkdata` recognizes the three-area label set (`P4 already
exists as SPONGE-VAR — three-area scenario layout, data areas already
provisioned (P4/P5); idempotent no-op`) and `tool/dist`'s partition
verifier accepts either label set. Legacy artifacts still carrying the
two-area shape (the `-legacy` BIOS path through `tool/mkdata`, whose
default writable size is 1 GiB via `tool/dist --data-size`, and any
deployed pre-P3.2 medium) are never migrated or resized in place
(D16R.2). Older Phase-8-era text in this document (the §4 tier
diagram, §5–§7, §12) and older scenario headers/logs still spell
`SPONGE-DATA`; read those as the historical name. The
`--image-disk_size` note of that era stands: the default computation
must be told about `<data_bytes>` or `resize2fs -M` eats the gap; a
cleaner long-term option is a small vendored patch adding
`--image-disk_data_size` to `image/disk`, which if ever taken MUST be
recorded in the docs/11 patch ledger (AGENTS.md §5.2).

#### 4.3.1 UEFI partition variant (Phase 15 W4, D15.13)

The UEFI media path is the Sponge-side recipe
(`run/sponge-desktop-disk-uefi*.run` family); the vendored
`genode/tool/run/image/uefi` is **unexercised and rejected** per
D15.13 (it copies only top-level run_dir entries — the `/system`
tree would silently not land — and hardcodes `/usr/share/ovmf/OVMF.fd`
plus a single FAT partition with no docs/14 partition layout).
The Sponge-side variant uses the same handcrafted-image pattern
proven in `run/sponge-boot-i440fx.run` and
`run/sponge-boot-multidisk.run` (sgdisk + mkfs + e2cp + dd outside
the Genode image plugins), and appends its own OVMF pflash args to
qemu_args.

| # | Name | FS | Content | Writable by |
|---|---|---|---|---|
| 1 | `ESP` (FAT32 EF00) | FAT | `EFI/BOOT/BOOTX64.EFI` (renamed `grub2_64.efi`), `/boot/grub/grub.cfg`, `/boot/font.pf2` | image build |
| 2 | **(ABSENT)** | — | The P2 slot is intentionally not allocated so the data-partition numbers (P3/P4/P5) are identical across the firmware variants; part_block's `<partition number="3"/>` pin and tool/mkdata's P4 grow sequence work **unchanged** | — |
| 3 | `GENODE` (ext2 8300) | ext2 | `/boot/{bender,sel4,image.elf}` (bootloader modules read by GRUB2 EFI's multiboot2 menuentry) + `/system` (Tier 1) | image build now; system-update path later |
| 4 | `SPONGE-VAR` (ext2 8300) | ext2 | `/pkgd`, `/log` (system-mutable) | pkgd, the log sink |
| 5 | `SPONGE-HOME` (ext2 8300) | ext2 | `/user` (Tier 2) | apps |

Rows 3–5 are the §4.3 naming authority. The canonical product media
(`sponge-desktop-disk-uefi-usb.run`) provisions P4 `SPONGE-VAR` (64M)
and P5 `SPONGE-HOME` (1024M) itself; the Phase-15 `uefi`/`uefi-nvme`
scenarios and the `-legacy` BIOS artifacts still grow the historical
single `SPONGE-DATA` P4 through `tool/mkdata` (§4.3 naming history —
never migrated in place, D16R.2).

The P2-absent choice is binding. The alternatives considered and
rejected (recorded in the W4 run-script header; this is a Phase-15
decision record and spells the writable partition `SPONGE-DATA`, its
name at decision time — the §4.3 naming authority supersedes the
naming only, not the decision):

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
  layouts (and accepts the three-area `SPONGE-VAR`/`SPONGE-HOME`
  label set in place of `SPONGE-DATA`).

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
