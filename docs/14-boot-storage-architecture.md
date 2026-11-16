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

> **Document structure.** The section bodies moved verbatim into
> `docs/boot-storage/`. The numbered stubs below are the map: each stub
> names the file and section that now holds the content, so every
> `docs/14 §N` reference still resolves.

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

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §2.

### 2.1 What the "~256 MiB ceiling" actually is

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §2.1.

---

## 3. Verified building blocks (Genode 26.05, vendored)

| Mechanism | Path | Status |
|---|---|---|
| GPT partition multiplexer | `genode/repos/os/src/server/part_block/` | in-tree |
| AHCI driver | `genode/repos/os/src/driver/ahci/` (target `ahci`) | in-tree; x86, no kernel restriction |
| NVMe driver | `genode/repos/os/src/driver/nvme/` | in-tree; **seL4-proven** (`genode/repos/os/run/nvme.run:17`) |
| USB mass storage | `genode/repos/os/src/driver/usb_block/` | in-tree |
| ext2 via rump | `genode/repos/dde_rump/src/lib/vfs/rump/vfs_rump.cc` (fs types: ext2fs, ffs, msdos, cd9660, ntfs, udf) | **port prepared** (Phase 8 P1) — `tool/ports/prepare_port dde_rump`, hash recorded in docs/11 §5 (pins rump.git @ 28945d1a); the shipped ext2 path for the storage chain (§5) |
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

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.

### 4.1 Why this is microkernel-shaped

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.1.

### 4.2 Why three partitions and not Sculpt's one

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.2.

### 4.3 Partition layout

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.3.

#### 4.3.1 UEFI partition variant (Phase 15 W4, D15.13)

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.3.1.

### 4.4 Boot flow

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.4.

### 4.5 What stays in boot modules — the explicit rule

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.5.

### 4.6 The ld.lib.so rule (bootstrap-cycle avoidance)

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.6.

### 4.7 The rescue display

Content moved verbatim to [`docs/boot-storage/three-tiers.md`](boot-storage/three-tiers.md) §4.7.

---

## 5. Filesystem decision

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §5.

---

## 6. Persistence model

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §6.

---

## 7. Crash consistency without a journal

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §7.

---

## 8. The ISO question

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §8.

---

## 9. Alternatives considered and rejected

Content moved verbatim to [`docs/boot-storage/design-decisions.md`](boot-storage/design-decisions.md) §9.

---

## 10. Impact map (what changes where)

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §10.

---

## 11. Risks

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §11.

---

## 12. Phase 8 status (P1–P5)

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.

### 12.1 P1 — storage-chain smoke — DELIVERED

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.1.

### 12.2 P2 — desktop from disk — DELIVERED

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.2.

### 12.3 P3 — persistence on SPONGE-DATA — DELIVERED

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.3.

### 12.4 P4 — Falkon from disk — DELIVERED (first paint on seL4)

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.4.

### 12.5 P5 — media + docs — DELIVERED

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.5.

### 12.6 Phase 8 summary table

Content moved verbatim to [`docs/boot-storage/impact-risks-status.md`](boot-storage/impact-risks-status.md) §12.6.
