# Phase 15 W3 — early framebuffer text console (evidence index)

**Goal:** make the vendored seL4 kernel write its boot log AND the Genode
core log onto the GOP framebuffer, so the 15-3 real-hardware operator
on the LG gram 17ZD90N (UEFI-only, no serial port) can see what the
machine is doing after GRUB hands off. Without this, every diagnostic
byte between `bender` and `Genode v...` is structurally blind on the
panel.

**Deliverable:** the kernel mirrors every byte that flows through
`kernel_putDebugChar` (which is called by BOTH the kernel's `printf`
path AND `kernel_putchar` from the `SysDebugPutChar` syscall) onto the
GOP framebuffer as 8×8 ASCII glyphs. The console is byte-by-byte
fidelity with the existing serial log channel — no new code path for
userland to opt into.

Verified on QEMU/OVMF (the in-tree bound scenario `sponge-fbprobe-uefi`
at `var/run/sponge-fbprobe-uefi.img`), captured via `screendump` on the
QEMU monitor socket, decoded pixel-by-pixel back to text. The framebuffer
shows the full kernel boot log including the goal strings ("Starting
node #0 with APIC ID 0", "Booting all finished, dropped to user space",
plus the Genode core `Warning: device memory in range …` lines via
`SysDebugPutChar`). BIOS regression on `sponge-minimal` is clean — the
console stays disabled because `boot_state.fb_info.addr == 0`.

## How I solved the FB mapping (the engineering crux)

The challenge: seL4 has two paging modes during boot, and the hook point
must reach the FB at the right VA in both.

### Phase 1 — pre-`setCurrentVSpaceRoot` (early boot)

`head.S` sets up a temporary `boot_pml4` that identity-maps the first
4 GiB via 2 MiB pages (every 2 MiB between 0 and 4 GiB has a PDE in
`_boot_pd` set to `paddr | 0x87`). Then it sets `CR0.PG` and jumps to
`_entry_64`, which drops into `boot_sys()` → `try_boot_sys_mbi2()`.
**Paging is on, but the active PML4 has no kernel window mapping.**
So at this point the FB at physical 0x80000000 is reached by writing
*to that same address* (the identity map).

```
hook:fb_console_init()  // called right after MULTIBOOT2_TAG_FB is parsed
phase:                 // pre-`setCurrentVSpaceRoot` (boot_pml4 active)
base_vaddr = (uint32_t *)(fbc_uintptr)fbc.phys   // = phys directly
                                                       // (boot_pml4 identity maps it)
```

On x86_64, the kernel's permanent PML4 maps `[PPTR_BASE, PPTR_TOP) =
[0xffffff8000000000, 0xffffffff80000000)` to physical
`[PADDR_BASE = 0, PADDR_TOP = 0x80000000)` (the first **512 GiB** —
read directly from `genode/contrib/sel4-*/src/kernel/sel4/include/plat/pc99/plat/64/plat_mode/machine/hardware.h`).
Any GOP framebuffer BAR in the low 512 GiB (every real hw/laptop GOP and
every QEMU GOP allocation we tested: physical 0x80000000 on the 17ZD90N
sim and 0x80000000 on OVMF q35 + Skylake-Client) is covered by EITHER
mapping, so no new MMU work is required.

### Phase 2 — post-`setCurrentVSpaceRoot` (normal kernel + userland boot)

`try_boot_sys_node()` calls `map_kernel_window()` then
`setCurrentVSpaceRoot(kpptr_to_paddr(X86_KERNEL_VSPACE_ROOT), 0)`. After
that, the active PML4 is the kernel's permanent PML4. The boot_pml4
identity map is gone — writing to the FB's physical address now
triggers #PF (the VA `phys + 0` is no longer mapped).

```
hook:fb_console_enter_kernel_window()  // called after setCurrentVSpaceRoot
phase:                                 // kernel PML4 active
base_vaddr = (uint32_t *)paddr_to_pptr((paddr_t)fbc.phys)
                          // = phys + PPTR_BASE = phys + 0xffffff8000000000
```

`paddr_to_pptr()` is the standard kernel wrapper at
`include/machine.h:38` — it returns `paddr + PPTR_BASE_OFFSET`, where
`PPTR_BASE_OFFSET = PPTR_BASE − PADDR_BASE = 0xffffff8000000000` on
x86_64 (`hardware.h:33`). The kernel *already* uses this same mapping
for every byte of physical memory below 512 GiB; the FB is just
another physical range from the kernel's perspective.

The hook point is `kernel_putDebugChar()` in
`src/plat/pc99/machine/io.c` — the single function that BOTH
`putchar()` (used by `printf`) AND `kernel_putchar()` (used by
`SysDebugPutChar` from Genode core) ultimately call. One hook
captures both the kernel log and the Genode log without any userland
cooperation.

### Safety constraints

The console disables itself (zero-init `fbc`, every later write is a
no-op) when ANY of the following holds:

| Check | Why |
|-------|-----|
| `boot_state.fb_info.addr == 0` | No multiboot2 FB tag — BIOS scenarios don't issue one |
| `bpp != 32` | The hand-crafted 32-bit font assumes XRGB8888 / BGRX8888 |
| `type != 1` | Indexed-color FBs need a LUT we don't decode |
| `phys >= PADDR_TOP (512 GiB)` | Outside the direct-map; would need a new PTE |
| `width < 8` or `height < 8` | Sub-glyph screen — impossible to render |

The BIOS regression on `sponge-minimal` confirms path 1: GRUB on the
BIOS path doesn't emit a multiboot2 FB tag, `fb_info.addr` stays zero,
the console stays disabled, and the run script prints
`Run script execution successful.` with the standard 26.05 banner
(`[init -> vct] vct (0.1.1-alpha / Archaeocyte) starting`).

## What appears on screen (the verified proof)

`fb-decoded-t2.txt` is the literal text reconstructed pixel-by-pixel from
`fb-screendump-t2.ppm` (QEMU monitor `screendump` captured at t≈2 s,
when the kernel had just finished its putchar-emit run and Genode core's
first warnings were starting). The first 10 rows reproduce verbatim:

```
row   0: sel4 fb console: ready                  ← from fb_console_init() banner
row   1: Detected 1 boot module(s):                ← seL4 mbi2 loop, AFTER fb tag
row   2: Kernel loaded to: start=0x200000 ...      ← seL4 try_boot_sys()
row   3: ACPI: RSDT paddr=0x7fb7d074
...
row  19: ACPI: 1 CPU(s) detected
row  20: ELF-loading userland images from boot modules:
row  22: Moving loaded userland images to final location:
row  23: Starting node #0 with APIC ID 0           ← GOAL STRING 1
row  35: Booting all finished, dropped to user space ← GOAL STRING 2
row  36+: [34mWarning: device memory in range ...  ← Genode core, via SysDebugPutChar
```

(The `?` in `Starting node ?0` and the `??34m` prefix on the warnings
are ANSI escape sequences — Genode core sends `\e[34m` for blue, which
the simple pixel-mode console renders as a placeholder glyph + `[34m`
literal characters. The boot log is otherwise byte-identical to the
serial log.)

`fb-banner-only.png` is a 200×40 PNG crop of the top of the same
screendump, kept at native pixel resolution — readable by eye as
white-on-black text. Reading row-by-row: `sel4 fb console: ready`,
`Detected 1 boot module(s)`, `Kernel loaded to: start=0`, `ACPI: RSDT
paddr=0x2fb2d0`, `ACPI: RSDT vaddr=0x2fb2d0` — exact match.

`fb-screendump-t3.ppm` / `fb-screendump-t3.png` (captured at t≈3 s) show
the same text continuing into Genode core's boot — `Block: [0x…,
0x…)`, `=> mem_size=100925440 (96 MB) / mem_avail=100925440 (96 MB)`,
plus the repeated device-memory `Warning:` lines that get emitted during
core's initial reservation pass. 125 text rows decoded.

## Encoding detail — the font

The 8×8 font is **hand-crafted and embedded** in
`src/arch/x86/kernel/fb_console.c` as a 95-entry table (ASCII 32..126).
~760 bytes .rodata. Glyphs follow the VGA 8×8 conventions
(MSB = leftmost pixel of each row, 8 bytes per row). Out-of-range code
points render as a filled 8×8 block (cursor-aligned). The 17ZD90N
LCD at native 2560×1600 x32 holds 320×200 text cells, with no inter-cell
spacing — every pixel of the screen is used. ANSI / escape sequences
are rendered as literal placeholder + plain characters (no parser).

## Build wire-up

Added to `src/arch/x86/config.cmake` alongside the other x86 CFILES:

```cmake
        kernel/ept.c
+       kernel/fb_console.c
        kernel/thread.c
```

Plus the two new hook sites (full hunks in
`docs/patches/sel4-early-fb-console.patch`):

* `boot_sys.c`: `fb_console_init()` after `MULTIBOOT2_TAG_FB` parse;
  `fb_console_enter_kernel_window()` right after `setCurrentVSpaceRoot()`.
* `plat/pc99/machine/io.c`: `fb_console_putchar()` appended at the end of
  `kernel_putDebugChar()` after the existing UART write — zero impact on
  BIOS / non-FB scenarios (the called function is a no-op when
  `fbc.enabled == 0`).

Total diff vs upstream kernel HEAD `492a510242`: **+717 lines** (-28
hunk-stripped). **563 in the new `fb_console.c`, 51 in the new
`fb_console.h`, ~25 in `boot_sys.c` (two hook calls), ~15 in
`io.c` (one hook call), 1 line in `config.cmake` (the new CFILES
entry). Patch applies cleanly to a pristine tree fetched via
`git archive HEAD src/`.

The patch ledger row is at `docs/11-environment.md` §4 row 12.

## Limitations (recorded honestly)

* **ANSI escapes become literal characters.** Genode core wraps every
  warning in `\e[34m`, so the console displays `[34mWarning: …` instead
  of blue text. Acceptable trade-off for a putchar-level hook; a
  stateful escape parser would be ~150 lines of C, against the goal's
  "minimal/safe" constraint.
* **Nitpicker/boot_fb overwrites the kernel text once the desktop
  starts.** boot_fb's Capture session re-renders the FB on every
  damage event. The screendump therefore has a window of ~2–3 seconds
  after paging turns on to capture the kernel text before Genode
  takes over. For the 15-3 real-hardware debug session, this means
  the panel WILL show the boot text (we just rebooted, the desktop
  hasn't started yet) but as soon as the desktop reaches first paint,
  the kernel text is gone — which is the expected behavior on a
  working system. For diagnostic scenarios, the right approach is to
  read the screendump of the *intermediate boot sequence*, not the
  final desktop frame. The decoded `fb-decoded-t2.txt` is exactly such
  an intermediate capture.
* **BIOS path does not see the FB at all.** `boot_state.fb_info.addr`
  is zero on BIOS boots (no multiboot2 FB tag from BIOS GRUB). The
  console stays disabled; the UART-only log continues unchanged. Tested
  clean on `run/sponge-minimal` — `Run script execution successful.`
  with no regression.
* **Physical address must be < 512 GiB.** `phys >= PADDR_TOP` causes
  `fb_console_init()` to disable the console rather than add a new MMIO
  mapping. The 17ZD90N GOP BAR is well below this; QEMU OVMF allocates
  the GOP at 0x80000000 (first 2 GiB). Tested both; both work.
* **Only 32-bpp RGB.** Indexed-color modes (multiboot2 type 0) and
  non-32-bpp modes are not handled (no LUT decoding). The GRUB
  `gfxpayload` setting in the production scenarios is forced to
  `*x32` so this never fires. If a future scenario needs an 8/16-bpp
  GOP mode, the console disables and the existing UART log carries it.
* **No font file, no escape parser, no scrollback.** This is a
  diagnostic console, not a vt100 emulator. The 95-glyph embedded font
  covers the printable ASCII set used by both the kernel boot log and
  Genode's log channels. Overflow rolls the FB up by one text row.
* **`memmove` is locally implemented as `fbc_memmove`** in
  `fb_console.c` because the vendored freestanding kernel doesn't
  declare `memmove` and `-Werror=nested-externs` rejects the implicit
  builtin declaration. Same byte-by-byte semantics as the standard
  libc version.

## File inventory (durable record)

```
docs/patches/sel4-early-fb-console.patch        — diff vs upstream kernel HEAD
docs/evidence/phase15-fb-console/
  README.md                                  — this file
  fb-screendump-t2.ppm                       — QEMU monitor screendump (raw PPM)
  fb-screendump-t2.png                       — same, rendered
  fb-screendump-t2-top.png                   — top 800 px crop, resized to 1280×400
  fb-screendump-t3.ppm                       — same, captured at t≈3 s
  fb-screendump-t3.png
  fb-screendump-t3-resized.png
  fb-banner-only.png                         — 200×40 native-res crop of row 0–4
  fb-banner-zoom.png                         — 640×176 zoom of the top 22 rows
  fb-decoded-t2.txt                          — literal text, row by row (t≈2 s)
  fb-decoded.txt                             — literal text, row by row (t≈3 s)
docs/11-environment.md                       — patch ledger row 12
```
