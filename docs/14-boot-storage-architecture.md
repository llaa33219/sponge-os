# 14 - Boot & Storage Architecture (Proposal)

> Status: **PROPOSAL — awaiting maintainer decision.** This document defines
> how Sponge OS splits boot modules from disk-resident content, which
> filesystems and partition layout it uses, and how user data persists.
> It supersedes the Alpha stop-gap of packing everything into the boot
> image (the `image.elf` single-module model), which hit the seL4 boot
> chain's ~256 MiB boot-module ceiling (see `docs/09-roadmap.md` §9,
> `docs/13-installation.md` §6, and `.omo/evidence/task-16-phase7-alpha.log`).
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
(`.omo/evidence/task-16-phase7-alpha.log` §6):

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
| **rescue display: vesa_fb, minimal nitpicker, console logger** (§4.7) | Leitzentrale subsystem |
| (the init config for the above) | |

Target: Tier 0 ≤ 80 MiB including the rescue display, i.e. 3× headroom
under the ~256 MiB ceiling.

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
| `sponge_configd` | gains the same writable store wiring for config persistence |
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
