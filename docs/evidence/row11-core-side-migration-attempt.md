# Row 11 core-side migration attempt — BIOS-proven, UEFI-blocked (2026-09-12)

Attempt to replace the seL4 kernel patch (ledger row 11,
`sel4-uefi-untyped-overlap.patch`) with a core-side detection that
skips "artifact" RAM untypeds — the leak fragments of the kernel's
`init_freemem` subtraction bug — keeping the seL4 kernel pristine
(AGENTS.md §5.2 microkernel-purity direction).

## What was proven (BIOS / sponge-minimal)

Two implementations were tried; the second (detection inside
`Initial_untyped_pool` with a lazy one-shot scan, skipping flagged
ranges in both `turn_into_untyped_object` and `alloc()`) works on the
BIOS/QEMU path:

- With the kernel patch REMOVED, the leak exists even on plain
  BIOS/QEMU boots (it was always masked by the kernel patch):
  a 9-warning series of image-tail fragments
  ([0x2f81000,0x2f82000) .. power-of-2 series up to 256 KiB,
  ~0.5 MiB total) was detected and skipped.
- `run/sponge-minimal` PASSES with the kernel patch removed.
- Detection classes: (1) mutual overlap among RAM untypeds — freemem
  regions are disjoint by construction, any overlap is a subtraction
  artifact; (2) overlap with core's own image, physical extent from
  the ELF program headers (`p_paddr`) of the mapped core image,
  laundered via `asm volatile ("" : "+r" (img))` to defeat GCC 14
  `-Warray-bounds` provenance tracking; (3) overlap with non-usable
  regions of the raw multiboot mmap (X86_MBMMAP bootinfo chunk,
  variable-stride entries).

## What blocked UEFI (sponge-desktop-disk-uefi-usb, OVMF)

The boot still crash-looped (~29 reboots in the 300 s gate) with the
core-side detection active. Hard data from the serial log:

- Exactly 5 unique artifacts flagged per boot (145 warnings = 5 × 29):
  - `[0x800000,0xa00000)` 2 MiB, `[0x80c000,0x810000)` 16 KiB,
    `[0x900000,0xa00000)` 1 MiB — the ledger row 11 example triple,
    correctly detected (mutual-overlap class).
  - `[0x2000000,0x4000000)` 32 MiB, `[0x4000000,0x8000000)` 64 MiB —
    at least the 64 MiB one is suspected a false positive (source
    check unidentified: not explained by the parsed image extent).
- `module #0: start=0x1780000 end=0x2780410` — the userland image
  (core ELF + boot modules) is physically at [0x1780000, 0x2780410)
  under OVMF. Core's own ELF phdrs cover only part of that; the
  module-data tail beyond core's last phdr is reserved by the kernel
  but invisible to a phdr-derived extent — fragments overlapping only
  that tail escape detection and still crash the retype path.
- First-attempt lessons that still apply: the 16K-pool path
  (`_init_core_page_table_registry`) runs BEFORE `_init_allocators`,
  so detection must live at the pool level (attempt 2 did); and an
  identity assumption (virt == phys) for `_prog_img_beg` is false
  under OVMF — the image is NOT loaded at its link address.

## Disposition

Reverted cleanly: tree back to the 8/9-sweep HEAD, kernel patch
re-applied to contrib, kernel+core rebuilt, and
`run/sponge-desktop-disk-uefi-usb` re-verified PASS (structural gates
3/3 + OVMF usb_block storage chain). No ledger change.

## Next-session plan

1. One instrumented OVMF run dumping the FULL bootinfo untypedList +
   the raw mmap + the module region, to design detection from real
   data (the current gap analysis is inference from warnings).
2. Explain the `[0x4000000,0x8000000)` 64 MiB flag (false-positive
   source: which of the three checks fired).
3. Find a core-visible signal for the full reserved image extent
   (core phdrs + module tail). Options: the untyped-free gap below
   the first RAM untyped (the image + kernel window is the hole
   between low fragments and the first big untyped), or an upstream
   bootinfo extension request (Genode issue) carrying ui_reg.
4. Only after UEFI passes both with the kernel patch removed: drop
   row 11 (patch file + ledger row), add the core-side row.
