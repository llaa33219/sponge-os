# fbprobe3.s — multiboot2 FB-tag reporter (32-bit, no paging)
#
# Phase 15 17ZD90N display-path bisect (v10). Loaded DIRECTLY by GRUB's
# multiboot2 (no bender/seL4), so it isolates GRUB's tag emission and
# MBI placement from everything downstream. fbprobe2's blind spot: "no
# FB tag" and "MBI unreadable" were both a black hang. v3 signals:
#
#   GREEN screen  : FB tag present, addr < 4 GiB, bpp=32 — tag usable
#   RESET loop    : MBI readable (magic + sane size + walked to the end
#                   tag) but NO FB tag — GRUB did not emit one
#                   (hypothesis b: no tag on Insyde even with gfxterm)
#   BLACK hang    : EAX magic wrong, MBI implausible (EBX truncated —
#                   MBI placed above 4 GiB), FB addr >= 4 GiB, or
#                   bpp != 32 — a placement/geometry problem
#
# 32-bit, no paging (multiboot2 entry state), direct physical access.
# Build: as --32 fbprobe3.s -o fbprobe3.o
#        ld -m elf_i386 -T mb2probe.ld -o fbprobe3.elf fbprobe3.o

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
    jne .hang
    mov %ebx, %esi              # MBI addr
    # MBI sanity: total_size must be plausible. Without paging a
    # garbage EBX (MBI above 4 GiB truncating) reads garbage rather
    # than faulting — catch it here.
    mov (%esi), %ecx            # total_size
    cmp $16, %ecx
    jb .hang
    cmp $0x1000000, %ecx        # 16 MiB upper bound
    ja .hang
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
    jnz .hang                   # FB above 4 GiB — cannot draw (32-bit)
    cmpb $32, 28(%esi)          # bpp must be 32
    jne .hang
    mov 8(%esi), %edi           # fb_addr low 32
    mov 20(%esi), %ecx          # width
    mov 24(%esi), %edx          # height
    imul %edx, %ecx             # width*height pixels
    mov $0x0000FF00, %eax       # green (XRGB), proven in fbprobe2
.fillg:
    mov %eax, (%edi)
    add $4, %edi
    dec %ecx
    jnz .fillg
    jmp .hang

.no_fb:
    # MBI was fully walked and carries no FB tag. Signal by pulsing the
    # 8042 reset line — the machine reboots (visible: LG logo / GRUB
    # menu returns), distinct from both green and the black hang.
.reset_wait:
    in $0x64, %al
    test $2, %al                # wait for 8042 input buffer empty
    jnz .reset_wait
    mov $0xFE, %al              # pulse CPU reset line
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
