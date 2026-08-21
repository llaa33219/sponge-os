# fbprobe2.s — GOP framebuffer self-diagnosing multiboot2 kernel (32-bit)
#
# Phase 15 real-hardware display-path discriminator (17ZD90N).
# Walks the multiboot2 info for the framebuffer tag (type 8):
#   - FB tag found  -> fill the screen GREEN  (GOP draw path works)
#   - FB tag absent -> fill the screen RED    (no framebuffer handed to us)
#   - (the boot hangs before us / black = the chain died before the
#     payload, i.e. bender is fine but earlier stage failed — but note:
#     this probe REPLACES the OS chain, so if we run at all, GRUB's
#     multiboot2 handoff worked)
#
# 32-bit, no paging (multiboot2 entry state), direct physical access.
# Build: as --32 fbprobe2.s -o fbprobe2.o
#        ld -m elf_i386 -T mb2probe.ld -o fbprobe2.elf fbprobe2.o

    .set MB2_MAGIC, 0xE85250D6

    .section .mb2hdr, "a"
    .align 8
mb2_header_start:
    .long MB2_MAGIC
    .long 0                                # architecture i386
    .long (mb2_header_end - mb2_header_start)
    .long -(MB2_MAGIC + 0 + (mb2_header_end - mb2_header_start))
    .short 0
    .short 0
    .long 8
mb2_header_end:

    .section .text
    .global _start
_start:
    # EAX = mb2 magic (0x36D76289), EBX = MBI physical address
    cmp $0x36D76289, %eax
    jne .no_fb
    mov %ebx, %esi              # MBI addr
    # MBI: u32 total_size, u32 reserved, then tags at +8
    mov (%esi), %ecx            # total_size (not strictly needed)
    add $8, %esi                # first tag
.tagloop:
    mov (%esi), %eax            # tag type
    test %eax, %eax
    jz .no_fb                   # end tag (type 0) without finding FB
    cmp $8, %eax                # framebuffer tag?
    je .found_fb
    # next tag: advance by tag size (u32 at +4), 8-byte aligned
    mov 4(%esi), %edx           # tag size
    add %edx, %esi
    add $7, %esi
    and $0xfffffff8, %esi       # align up to 8
    jmp .tagloop

.found_fb:
    # tag layout: +8 fb_addr(u64) +16 pitch(u32) +20 width(u32)
    #             +24 height(u32) +28 bpp(u8) +29 type(u8)
    mov 8(%esi), %edi           # fb_addr low 32 (assume < 4GB)
    mov 20(%esi), %ecx          # width
    mov 24(%esi), %edx          # height
    mov 16(%esi), %ebx          # pitch (bytes per row)
    # pixel count approach: fill width*height pixels, 4 bytes each
    imul %edx, %ecx             # width*height
    # green pixel value 0x0000FF00 (XRGB) / works for 32bpp
    mov $0x0000FF00, %eax
.fillg:
    mov %eax, (%edi)
    add $4, %edi
    dec %ecx
    jnz .fillg
    jmp .halt

.no_fb:
    # no FB tag: try to write red to the default UEFI GOP area is
    # unsafe without an address; instead just spin with interrupts
    # off (the screen keeps whatever GRUB showed). To make "no FB"
    # distinguishable, write to the legacy VGA text buffer too — under
    # GOP it is invisible, but it is the best blind signal we have.
    mov $0xB8000, %edi
    mov $0x4F46, (%edi)         # red 'F' attribute (invisible under GOP)
.halt:
    cli
.h: hlt
    jmp .h
