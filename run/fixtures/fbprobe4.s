# fbprobe4.s — multiboot2 FB-tag timing-ladder reporter (32-bit)
#
# Phase 15 17ZD90N display-path bisect (v11). Loaded DIRECTLY by GRUB's
# multiboot2 (no bender/seL4). v10's fbprobe3 answered "green vs not",
# but its black-hang covered too many cases. v4 replaces the black
# hangs with a RESET at a case-specific delay — time from Enter to the
# reboot (screen drops to the LG logo / firmware) identifies the case:
#
#   ~3 s   : EAX magic wrong (multiboot2 handoff anomaly)
#   ~6 s   : MBI total_size implausible (MBI unreadable — EBX
#            truncated, i.e. GRUB placed the MBI above 4 GiB)
#   ~12 s  : MBI walked to the end tag — NO framebuffer tag present
#            (hypothesis b: Insyde GRUB does not emit the tag)
#   ~24 s  : FB tag present but framebuffer address >= 4 GiB
#            (hypothesis: high GOP framebuffer — 32-bit payload cannot
#            draw, and the kernel's early identity map cannot reach it)
#   never (permanent black) : FB tag present but bpp != 32
#   GREEN screen            : FB tag present and usable — tag is fine,
#                             the chain's problem is downstream
#
# Delays are a busy loop (~0.75 s/unit at ~3 GHz, buckets 4/8/16/32
# units — 2x spacing, wall-clock distinguishable). The reset pulses the
# 8042 reset line (falls back to a triple fault), so each case reboots
# back to the GRUB menu for the next entry.
#
# 32-bit, no paging (multiboot2 entry state), direct physical access.
# Build: as --32 fbprobe4.s -o fbprobe4.o
#        ld -m elf_i386 -T mb2probe.ld -o fbprobe4.elf fbprobe4.o

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
    jne .bad_magic
    mov %ebx, %esi              # MBI addr
    # MBI sanity: total_size must be plausible. Without paging a
    # garbage EBX (MBI above 4 GiB truncating) reads garbage rather
    # than faulting — catch it here.
    mov (%esi), %ecx            # total_size
    cmp $16, %ecx
    jb .bad_mbi
    cmp $0x1000000, %ecx        # 16 MiB upper bound
    ja .bad_mbi
    add $8, %esi                # first tag
.tagloop:
    mov (%esi), %eax            # tag type
    test %eax, %eax
    jz .no_fb                   # end tag (type 0) without finding FB
    cmp $8, %eax                # framebuffer tag?
    je .found_fb
    mov 4(%esi), %edx           # tag size
    add %edx, %esi
    add $7, %esi
    and $0xfffffff8, %esi       # align up to 8
    jmp .tagloop

.found_fb:
    # tag layout: +8 fb_addr(u64) +16 pitch(u32) +20 width(u32)
    #             +24 height(u32) +28 bpp(u8) +29 type(u8)
    mov 12(%esi), %edx          # fb_addr high 32
    test %edx, %edx
    jnz .fb_high                # FB >= 4 GiB — cannot draw (32-bit)
    cmpb $32, 28(%esi)          # bpp must be 32
    jne .hang                   # bpp != 32: permanent black
    mov 8(%esi), %edi           # fb_addr low 32
    mov 20(%esi), %ecx          # width
    mov 24(%esi), %edx          # height
    imul %edx, %ecx             # width*height pixels
    mov $0x0000FF00, %eax       # green (XRGB), proven in fbprobe2/3
.fillg:
    mov %eax, (%edi)
    add $4, %edi
    dec %ecx
    jnz .fillg
    jmp .hang                   # green stays on screen

.bad_magic: mov $4,  %ebx
    jmp .delay_reset
.bad_mbi:   mov $8,  %ebx
    jmp .delay_reset
.no_fb:     mov $16, %ebx
    jmp .delay_reset
.fb_high:   mov $32, %ebx
    # fall through

.delay_reset:                   # ebx = outer loop count (units ~0.75 s)
    mov %ebx, %edx
.outer:
    mov $1000000000, %ecx
.inner:
    dec %ecx
    jnz .inner
    dec %edx
    jnz .outer
    # pulse the 8042 reset line
.rw:
    in $0x64, %al
    test $2, %al                # wait for 8042 input buffer empty
    jnz .rw
    mov $0xFE, %al
    out %al, $0x64
    # if the reset did not take, force a triple fault
    lidt .zero_idt
    int $3
.hang:
    cli
.h: hlt
    jmp .h

    .align 8
.zero_idt:
    .word 0
    .long 0
