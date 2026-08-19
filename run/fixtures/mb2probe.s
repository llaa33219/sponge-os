# mb2probe.s — minimal multiboot2 kernel (32-bit)
#
# Proves the GRUB2-EFI -> multiboot2 payload handoff works on the
# 17ZD90N, independent of bender. If this prints "MB2-PROBE ALIVE"
# to the VGA text buffer and halts, the firmware+GRUB+multiboot2
# chain is healthy and the freeze is bender-specific. If it also
# freezes with a bare cursor, GRUB's multiboot2 handoff itself is
# broken on this firmware.
#
# Build: as --32 mb2probe.s -o mb2probe.o
#        ld -m elf_i386 -T mb2probe.ld -o mb2probe.elf mb2probe.o

    .set MB2_MAGIC, 0xE85250D6
    .set MB2_ARCH,  0

    .section .mb2hdr, "a"
    .align 8
mb2_header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long (mb2_header_end - mb2_header_start)
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header_start))
    # end tag (type 0, flags 0, size 8)
    .short 0
    .short 0
    .long 8
mb2_header_end:

    .section .text
    .global _start
_start:
    # multiboot2 entry: EAX = 0x36D76289 (magic), EBX = MBI pointer.
    # First: verify the magic and signal the result on screen.
    mov $0xB8000, %edi
    cmp $0x36D76289, %eax
    je 1f
    mov $badmsg, %esi
    jmp 2f
1:  mov $okmsg, %esi
2:  lodsb
    test %al, %al
    jz 3f
    mov %al, (%edi)
    inc %edi
    movb $0x0A, (%edi)        # light-green attribute
    inc %edi
    jmp 2b
3:  cli
4:  hlt
    jmp 4b

    .section .rodata
okmsg:  .asciz "MB2-PROBE ALIVE - grub multiboot2 handoff OK, magic ok"
badmsg: .asciz "MB2-PROBE BAD MAGIC - multiboot2 handoff corrupted"
