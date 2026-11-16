# Boot & storage architecture: design decisions

> **Breadcrumb:** parent [14 - Boot & Storage Architecture](../14-boot-storage-architecture.md); this file holds its §2 (Why the Alpha model must go, with §2.1) and §5-§9 (filesystem decision, persistence model, crash consistency, ISO question, alternatives), moved verbatim.
> Siblings in `docs/boot-storage/`: [three-tiers.md](three-tiers.md) (§4), [impact-risks-status.md](impact-risks-status.md) (§10-§12).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

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
