# 2026-10-19 — seL4 kernel patches are auto-applied; fb console is wrap-around + adaptive scale

Two-part record: the field blocker (rows 11/12/13 lost to a
`genode/contrib/` re-fetch) and the console usability fixes the user
found once the console was visible again.

## 1. The outage

`genode/contrib/` is git-ignored and re-fetchable by design. Ledger
rows 11, 12 and 13 modified the seL4 kernel and were applied **by hand**
to `genode/contrib/sel4-*/` after `prepare_port sel4`, with the durable
record living only as patch files in this repository's `docs/patches/`.
Nothing enforced that hand step.

A `prepare_port sel4` during the 2026-10-18 submodule-split cleanup
re-fetched the port from scratch and none of the three came back:

```
$ grep -rc "highest_map_addr\|SPONGE_DEVICE_UNTYPED_TOP\|fbc_" \
    genode/contrib/sel4-*/src/kernel/sel4/src/kernel/boot.c \
    genode/contrib/sel4-*/src/arch/x86/kernel/boot.c
0   (all markers absent)
```

Every image built since was a stock seL4 kernel. On the LG gram the GOP
framebuffer sits above the memory map, so row 13's absence left the FB
un-mappable and row 11's absence left the `init_freemem` artifacts
unfiltered: a black screen. Under QEMU the GOP framebuffer is at 2 GiB
and the kernel boots either way — which is exactly why no QEMU gate
caught it.

## 2. The fix: the patches moved into the port

`genode/repos/base-sel4/ports/sel4.port` already ends with

```make
PATCHES := $(sort $(wildcard $(REP_DIR)/patches/*.patch))
```

and `genode/tool/ports/mk/install.mk` applies every entry with

```make
PATCH_OPT ?= -p0
...
patch -s $(call _patch_opt,$*) < $$i
```

So a file dropped into `genode/repos/base-sel4/patches/` is applied on
every `prepare_port sel4`, at `-p0`, with no manual step. The three
patches were converted from git format (`a/`…`b/` prefixes,
`diff --git`/`index`/`new file mode` headers) to the port's bare
unified-diff layout and committed to the fork at
`genode/repos/base-sel4/patches/`:

| file | ledger row | touches |
|---|---|---|
| `sel4-reserved-extent-export.patch` | 11 | `src/arch/x86/kernel/boot.c` |
| `sel4-early-fb-console.patch` | 12 | fb_console.c/.h (new), config.cmake, boot_sys.c, pc99 io.c |
| `sel4-device-untyped-coverage.patch` | 13 | `include/kernel/boot.h`, `src/kernel/boot.c` |

**Ordering.** `$(sort)` is a byte-order sort of the file names, so the
three `sel4-*.patch` entries land after `ioapic.patch` and before
`utilization_smp.patch` — which matters, because
`intel_vtx_check.patch` also edits `src/arch/x86/kernel/boot.c`, the
same file as row 11. Verified with the real `$(sort)`:

```
… intel_vtx_check.patch ioapic.patch sel4-device-untyped-coverage.patch
  sel4-early-fb-console.patch sel4-reserved-extent-export.patch
  utilization_smp.patch
```

**Application proof** (fresh `contrib/` fetch, from the port's own log):

```
sel4  apply …/patches/sel4-device-untyped-coverage.patch
sel4  apply …/patches/sel4-early-fb-console.patch
sel4  apply …/patches/sel4-reserved-extent-export.patch
sel4  apply …/patches/utilization_smp.patch
sel4  generate sel4.hash
```

No `Hunk … FAILED`, no `.rej` files. Marker counts in the resulting
tree:

| marker | file | count |
|---|---|---|
| `highest_map_addr` | `src/kernel/boot.c` | 5 |
| `highest_map_addr` | `include/kernel/boot.h` | 1 |
| `SPONGE_DEVICE_UNTYPED_TOP` | `src/kernel/boot.c` | 4 |
| `seL4_X86_mb_mmap_t *)` (synthetic reserved entry) | `src/arch/x86/kernel/boot.c` | 1 |
| `fbc_wrap_to_top` | `src/arch/x86/kernel/fb_console.c` | 4 |
| `FBC_WIDE_PANEL_WIDTH` | `src/arch/x86/kernel/fb_console.c` | 2 |
| `fbc_memmove` | `src/arch/x86/kernel/fb_console.c` | 0 (removed) |

A `diff -r` between the prepared `genode/contrib/sel4-*` tree and the
tree the patches were developed against is empty (only the
`tools/__pycache__` the build tooling creates), so patch and tree agree.

**Round-trip.** Applying the three files to a pristine copy of the
contrib tree and diffing against the intended tree is byte-identical.

**Port hash.** Editing a port's patch set changes its fingerprint;
`prepare_port` refused with `sel4.port is out of date, expected
fcfb8bdd…`. `genode/tool/ports/update_hash sel4` regenerated
`repos/base-sel4/ports/sel4.hash` from `a8146087…` to `fcfb8bdd…`, and
the port then prepares cleanly. This is the normal consequence of
touching a port, not a weakened integrity check.

## 3. Part B: the console fixes

### (a) wrap-around instead of a full-screen scroll

`fbc_scroll_up()` memmoved the entire framebuffer below the cursor up
by one text row, then cleared the last row. GOP writes are uncached
device MMIO without write combining, so the cost per emitted line was
`(height - char_h) * pitch` bytes. On the 17ZD90N's 2560x1600x32 panel
(pitch 10240) that is 16.0 MB of device writes **per log line**; a boot
emitting a few hundred lines crawled to a halt before userland.

It is now `fbc_wrap_to_top()`: the cursor returns to row 0 and that one
row is cleared. Per-line cost drops to `char_h * pitch` — 80 KiB at
scale 1 — a ~200x reduction, and the newest lines stay on screen instead
of the oldest being pushed off the top. `fbc_memmove()` is deleted.
`fbc_paint_clear_and_banner()` still clears and paints the banner
exactly once, so the banner semantics are untouched.

### (b) adaptive font scale

`FBC_SCALE` was a compile-time `2` (introduced in W5 because the 8x8
cell was unreadable on the HiDPI panel) and is now a runtime field:

```c
scale = (width >= FBC_WIDE_PANEL_WIDTH /* 2000 */) ? 1 : 2;
```

The threshold is deliberately keyed on the GOP tag's pixel width
because that is the only cheap, always-available proxy for legibility:
at 2560 px wide an 8x8 cell is ~1.6 mm — the same physical size a
classic VGA text cell had — and a 2x cell would waste two thirds of a
1600-px-tall panel; below 2000 px (a 1024x768 VM display, a 1280x800
panel) the 2x cell is what stays readable at arm's length. The cell
geometry moved from the `FBC_CHAR_W/H` macros into `fbc.char_w` /
`fbc.char_h`, and the "sub-glyph screen" guard is now evaluated against
the cell the console actually picked.

## 4. QEMU verification

Kernel rebuilt from scratch (`rm -rf genode/build/x86_64/var/libcache/
kernel-sel4-pc genode/build/x86_64/kernel`); the built tree reports
`Genode 26.08-23-ge16699624d`, i.e. the new fork pin.

**Console actually prints.** The scenario runs `-nographic`, so the
framebuffer was captured with a temporary QEMU monitor socket and
`screendump` (the run script was restored afterwards — this is a
capture procedure, not a committed change). OVMF hands the kernel
`resolution=2560x1600@32` at physical `0x80000000`, so the adaptive
rule picks scale 1. Decoding the screendump cell-by-cell against the
embedded 8x8 font table:

```
sel4 fb console: ready
Detected 1 boot module(s):
Kernel loaded to: start=0x200000 end=0xe3e000 size=0xc3e000 entry=0x2002a3
ACPI: RSDT paddr=0x7fb7d074
…
Booting all finished, dropped to user space
Warning: init_freemem artifact untyped skipped: 0x200000 size 0x200000
Warning: init_freemem artifact untyped skipped: 0x400000 size 0x400000
Warning: init_freemem artifact untyped skipped: 0x800000 size 0x200000
```

and a later dump holds Genode core's log, showing the console survives
into userland via `SysDebugPutChar`:

```
[init -> sponge_supervisor] sponge_supervisor: phase=1 (initial publish)
[init -> usb] xhci_hcd 00:03.0: xHCI Host Controller
[init -> part_block] GPT Partition 5: LBA 2050048 (2097152 blocks) … 'SPONGE-HOME'
```

Both facts at once are visible in that later dump: it starts
mid-log at `[init -> sponge_supervisor]`, i.e. the cursor **wrapped**,
and every glyph resolves cleanly at an 8x8 cell, i.e. **scale 1** was
chosen on a 2560-px-wide surface.

**Boot time.** `run/sponge-desktop-disk-uefi-usb`, wall clock for the
whole `./tool/build run` (build already warm), two runs each:

| variant | run 1 | run 2 |
|---|---|---|
| old console (memmove scroll, scale 2) | 95.8 s | 93.4 s |
| new console (wrap, adaptive scale) | 99.5 s | 92.7 s |
| `SPONGE_FBCON=off` (console disabled) | 91.7 s | 97.7 s |

All three are within noise of each other: **the console now costs
nothing measurable in QEMU**. That is expected and is the reason the
QEMU number understates the fix — under TCG the framebuffer is host
RAM, so device MMIO costs the same as ordinary memory and the old
scroll was merely slow rather than pathological. The number that
matters for the LG gram is the write accounting in §3(a):
16.0 MB -> 80 KiB per line.

**Regression gates** (all after the from-scratch rebuild, all with
`SPONGE_FBCON=on`):

| scenario | result |
|---|---|
| `run/sponge-desktop-disk-uefi-usb` | PASS (OVMF UEFI boot + usb_block storage chain, DE started) |
| `run/sponge-minimal` | PASS ("Run script execution successful") |
| `run/sponge-desktop-disk` | PASS (`alpha-probe: PASS`) |

The BIOS-boot scenarios are unaffected by the console itself (no
multiboot2 FB tag there), but they do exercise rows 11 and 13, and the
`init_freemem artifact untyped skipped` lines confirm row 11's export +
core-side detection is live again.

## 5. Not done here

No real-hardware (LG gram) re-verification was performed — this machine
has no serial-less HiDPI UEFI target attached. The hardware claim being
restored is rows 11+13 (black screen gone); the console fixes are
proven on QEMU/OVMF with a 2560x1600 framebuffer, which exercises the
same width-dependent code path, but the uncached-MMIO speed-up itself
is a static property of the write count, not a measured number from the
panel.