/* mb2probe.ld — link the minimal multiboot2 probe at the canonical
 * 1 MiB load address, header section first (GRUB scans the first
 * 32 KiB of the file for the multiboot2 header). */
ENTRY(_start)
SECTIONS
{
  . = 0x100000;
  .mb2hdr : { *(.mb2hdr) }
  .text   : { *(.text) }
  .rodata : { *(.rodata) }
}
