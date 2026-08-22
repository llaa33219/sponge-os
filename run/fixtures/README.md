# run/fixtures/ — shared scenario fixtures

Host-side data files consumed by run scenarios (staged into boot media
or read during image assembly). Not parsed by any component directly.

## grubx64-full.efi (Phase 15)

A full-featured GRUB 2.12 EFI image (`grub-mkimage -O x86_64-efi`)
with the complete module set embedded (multiboot2, part_gpt, fat,
ext2, gfxterm, efi_gop, echo, ls, terminal, lsefimmap, ...). This is
the bootloader the UEFI product scenarios stage as
`/EFI/BOOT/BOOTX64.EFI`.

**Why not the vendored g2fg `grub2_64.efi`:**
`genode/contrib/grub2-*/boot/grub2/grub2_64.efi` (the alex-ab g2fg
prebuilt) has a limited embedded module set (no `echo`/`terminal`) and
its multiboot2 handoff **freezes on the LG gram 17ZD90N's Insyde H2O
firmware** (15-3 bring-up log, `docs/evidence/phase15-index.md` §13
round 5 — a trivial multiboot2 payload never runs). It only works on
the pinned OVMF 2024.02 used for QEMU. Real-hardware UEFI media must
therefore use this full build.

**Provenance / rebuild:** built on the dev host from Debian's
`grub-efi-amd64-bin` + `grub-common` 2.12-9+deb13u2 `.deb`s (extracted
with `ar`+`tar`, no install), `grub-mkimage -d <moddir> -O x86_64-efi
-p /boot/grub <modules>`. A reproducible host tool
(`tool/mk_grub_efi.mojo`) is a recorded follow-up; treat this binary
like the vendored `genode/tool/boot/bender` (a checked-in binary with
a documented origin) until then.

## grub-diag*.cfg (Phase 15)

Diagnostic GRUB configs used during the 15-3 real-hardware bring-up
(text-mode, visible menu, per-stage markers). `grub-diag.cfg` is the
current one consumed by the `test` bake profile
(`[boot] grub_mode = diagnostic`, see `pkg/bake/README.md`). The
`grub-diag-v5*` variants are the full-GRUB bring-up iterations kept
for reference.

## mb2probe.{s,ld} (Phase 15)

A minimal 32-bit multiboot2 kernel that writes a banner to the VGA
text buffer and halts. Used to prove (or disprove) that a GRUB build's
multiboot2 handoff works on a given firmware, independent of
bender/seL4. Build: `as --32 mb2probe.s -o mb2probe.o && ld -m
elf_i386 -T mb2probe.ld -o mb2probe.elf mb2probe.o`.

## fbprobe2.s / fbprobe3.s (Phase 15)

GOP framebuffer multiboot2 probes. fbprobe2 draws green if the
multiboot2 framebuffer tag (type 8) is present and usable, hangs black
otherwise. fbprobe3 sharpens the negative: GREEN = FB tag present and
usable (addr < 4 GiB, bpp 32); RESET loop = MBI readable but no FB
tag; BLACK hang = MBI unreadable (placed > 4 GiB) or unusable tag
geometry. Loaded directly by GRUB (no bender/seL4), isolating GRUB's
tag emission and MBI placement on real firmware. Same build line with
`fbprobe3.s`; consumed by `grub-diag-v10.cfg` (stage the .elf at the
ESP root).

## files-demo/, net-probe/

Scenario fixture trees (file-manager demo content, network fixture).
See the scenarios that reference them.
