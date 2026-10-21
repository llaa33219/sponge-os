# The xHCI BAR was mapped to the wrong physical address (a device untyped's free index was reset)

**Date:** 2026-10-21
**Subject:** the LG gram 17ZD90N field failure (`xhci_hcd 00:03.0: Host
halt failed, -110` → the storage chain dies → no `/system` → the
supervisor kill-loops) is reproduced in QEMU, root-caused, fixed and
gated.

---

## 1. The field symptom

On the gram (17ZD90N, Ice Lake, xHCI BAR at `0x601d140000` = 384.9 GiB)
the current media's kernel console shows

```
[init -> usb] pci 00:03.0: xHCI HW did not halt within 32000 usec status = 0x0
[init -> usb] xhci_hcd 00:03.0: Host halt failed, -110
[init -> usb] xhci_hcd 00:03.0: can't setup: -110
[init -> usb] xhci_hcd 00:03.0: init 00:03.0 fail, -110
[init -> usb] xhci_hcd 00:03.0: probe with driver xhci_hcd failed with error -110
[init -> usb] Will sleep forever...
```

`status = 0x0` is the tell: the driver read **zeroes** out of the
controller's MMIO. The xHCI is a Tier-0 component on the product
media, so its death removes the whole storage chain (`usb_block` →
`part_block` → `vfs`): `/system` is never served, `system` never
starts, and the supervisor kill-loops the subtree
(`state_seen=0`, `kill=N`).

This is the same signature the harness has documented for the OLD
media (`26.05 + seL4 13.0.0`): "the mapping silently reads zeroes; the
xHCI probe times out and the DDE driver deadlocks" — the field report
and the old media share one mechanism.

## 2. The QEMU reproduction (a new harness case)

`tool/hwrepro` had a `boundary` case (the BAR at 54 TiB — out of
range, expected to stall) and an `inwindow` case (the BAR at
`0x800000000` = 32 GiB — expected to boot clean). The in-window case
never exercised the interesting path, because **32 GiB is exactly the
base of the device untyped that covers it** — the mapping worked by
luck. Every QEMU-verified address so far has been on a untyped
boundary:

| device untyped (QEMU/OVMF, 4 GiB RAM) | base | size |
|---|---|---|
| `untyped[49]` | `0x180000000` (6 GiB) | 2 GiB |
| `untyped[50]` | `0x200000000` (8 GiB) | 8 GiB |
| `untyped[51]` | `0x400000000` (16 GiB) | 16 GiB |
| `untyped[52]` | `0x800000000` (32 GiB) | 32 GiB |
| `untyped[53]` | `0x1000000000` (64 GiB) | 64 GiB |
| … | doubling ladder up to 1 TiB | |

The probe scenario (`run/sponge-high-phys-probe.run`) asks for IO_MEM at
16 GiB and 2 TiB — 16 GiB is `untyped[51]`'s base (lucky), 2 TiB is
beyond the ceiling (expected refusal). The gram's BAR at 384.9 GiB is
**inside** `untyped[55]`/`[56]`, 128.9 GiB past its base — the class
the harness never covered.

**The new `midwindow` case** puts the xHCI BAR *inside* a device
untyped: one extra 64-bit-BAR device (an NVMe controller, no drive,
no driver) makes OVMF hand the xHCI `0x800004000` — 32 GiB **+ 16 KiB**
— instead of `0x800000000`:

```
Bus  0, device   3, function 0:
    USB controller: PCI device 1033:0194
      BAR0: 64 bit memory at 0x800004000 [0x800007fff]
Bus  0, device   4, function 0:
    Class 0264: PCI device 1b36:0010
      BAR0: 64 bit memory at 0x800000000 [0x800003fff]
```

The QEMU twin of the gram, down to the "inside a device untyped, not
at its base" shape. **Before the fix it reproduces the field
signature exactly** (`var/hwrepro/sponge-os-0.2.0-x86_64-sel4-midwindow.log`):

```
[init -> usb] pci 00:03.0: xHCI HW did not halt within 32000 usec status = 0x0
[init -> usb] xhci_hcd 00:03.0: Host halt failed, -110
[init -> usb] xhci_hcd 00:03.0: probe with driver xhci_hcd failed with error -110
[init -> usb] Will sleep forever...
[init -> sponge_supervisor] sponge_supervisor: FAULT max_skipped=0 exited=0 starting=0 state_seen=0 ...
[init -> sponge_supervisor] sponge_supervisor: published <system> kill=2 ...
```

— the storage chain dies, `/system` never appears, the supervisor
kill-loops. The same lines the gram's console shows.

## 3. The mechanism

**Where the frame is created.** Core's high-phys path
(`genode/repos/base-sel4/src/core/include/untyped_memory.h`,
ledger row 14) creates a page frame for a device address by retyping
it from the device untyped that covers it. seL4 does **not** create
the object at a requested address: `Untyped_Retype` creates it at the
untyped's **free index** — a single monotonic watermark that advances
with every retype. To reach a BAR in the *middle* of a device untyped,
core fast-forwards that watermark by retyping throwaway frames
(`Core::high_phys_skip_to()`) and then retypes the real frame.

**What broke the fast-forward.** seL4's retype path
(`src/kernel/sel4/src/object/untyped.c`) starts with

```c
status = ensureNoChildren(slot);
if (status != EXCEPTION_NONE) {          /* the untyped HAS children */
    freeIndex = cap_untyped_cap_get_capFreeIndex(cap);
    reset = false;
} else {                                  /* the untyped has NO children */
    freeIndex = 0;                        /* ← RESET to the untyped's base */
    reset = true;
}
```

The throwaway frames are deleted again immediately, so the device
untyped is **childless at every retype** → the free index is reset to
the untyped's base at every step → the "fast-forward" consumed
nothing → **the real frame was created at the untyped's base, not at
the requested address**.

On the gram the covering untyped is `[256 GiB, 512 GiB)` (the GOP
framebuffer's untyped): the xHCI BAR's frame landed at **256 GiB** —
the framebuffer's address — and the driver read zeroes. The GOP
framebuffer itself (256 GiB) is the untyped's base, so it mapped
correctly *by luck*; that is why the desktop came up while the storage
chain did not.

The same mechanism in QEMU: the xHCI BAR at `0x800004000` landed on
`0x800000000` — the NVMe controller's BAR — and the xHCI read zeroes
from there.

**Why no test caught it.** Every verified address so far sat exactly
on a device untyped's base (16 GiB, 32 GiB, the QEMU GOP at 2 GiB),
where the buggy path and the correct path coincide. The gram's BAR is
the first real device address in the middle of a untyped.

## 4. The fix (ledger row 27, kernel patch)

`genode/repos/base-sel4/patches/sel4-device-untyped-ladder.patch`
(auto-applied by `prepare_port sel4`):

1. **`src/kernel/sel4/src/object/untyped.c`** — a **device** untyped
   keeps its free index when its last child is deleted. The reset is
   correct for RAM (the memory goes back) and fatal for device
   memory (the watermark is the whole point). A *fresh* untyped still
   takes the reset path — it carries the `MAX_FREE_INDEX` marker,
   which the patch checks explicitly (getting that wrong makes every
   device untyped look exhausted: "Untyped Retype: Insufficient memory
   (1 * 4096 bytes needed, 0 bytes available)").

2. **`src/kernel/sel4/src/kernel/boot.c`** — one free index per
   untyped means two device windows inside the *same* untyped can only
   be placed in increasing address order. The gram's GOP framebuffer
   (256 GiB) and its xHCI BAR (384.9 GiB) are 128.9 GiB apart, and
   the xHCI (Tier-0) is mapped long before the display driver
   (`driver_runtime`) asks for the framebuffer — so the second one
   would be refused. The HIGH device region is therefore covered by a
   **uniform 64 GiB grid** of device untypeds (the doubling ladder
   only fills the gap between the memory map and the first grid
   point): the two gram windows land in independent untypeds, so their
   order no longer matters. 1 TiB / 64 GiB = 16 grid untypeds, which
   fits the bootinfo budget (`CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS`
   = 160; the measured list is 114 entries).

Core needed no change: the watermark bookkeeping in
`untyped_memory.h` was written for this model and was correct — the
kernel's reset was the defect.

## 5. Before / after (harness verdicts, `var/dist/sponge-os-0.2.0-x86_64-sel4.img`)

| case | xHCI BAR0 | before | after |
|---|---|---|---|
| `midwindow` (the gram's class) | `0x800004000` | **FAIL** — `Host halt failed, -110`, `Will sleep forever...`, supervisor kill-loop | **CLEAN** — usb chain + system init alive (4 of 5 runs; the fifth hit the unrelated `Region_conflict` flake, §5b) |
| `inwindow` (control) | `0x800000000` | CLEAN | CLEAN |
| `boundary` (control, 54 TiB) | `0x380000000000` | REPRODUCED (signature A: MMIO refusal) | REPRODUCED (signature A) |

The `midwindow` boot log after the fix:

```
[init -> usb] xhci_hcd 00:03.0: xHCI Host Controller
[init -> usb] xhci_hcd 00:03.0: hcc params 0x00087001 hci version 0x100 quirks 0x0000000000000014
[init -> usb] xhci_hcd 00:03.0: Host supports USB 3.0 SuperSpeed
[init -> usb] usb 1-1: new high-speed USB device number 2 using xhci_hcd
[init -> usb_hid] Connected device: input0 (QEMU QEMU USB Mouse at usb-usbbus-0/input0) MOUSE
[init -> system -> sponge_configd] sponge_configd: ready
```

Regressions: `run/sponge-minimal` PASS, `run/sponge-desktop-disk`
PASS, `run/sponge-desktop-disk-uefi-usb` PASS,
`run/sponge-high-phys-probe` PASS (16 GiB in-coverage, 2 TiB
out-of-coverage).

## 5a. Regression or long-standing? — long-standing, and the project's
own "fix" for it was a no-op

The mechanism is **not** a regression of the recent seL4 bump: the
retype path is byte-identical in 13.0.0 and 16.0.0 in this respect
(checked against the upstream sources, `cd6d3b8c25d4` = 13.0.0 vs the
pinned 16.0.0 — the only differences in the file are the
`current_extra_caps` refactor):

```c
    status = ensureNoChildren(slot);
    if (status != EXCEPTION_NONE) {
        freeIndex = cap_untyped_cap_get_capFreeIndex(cap);
        reset = false;
    } else {
        freeIndex = 0;          /* ← the reset, in both releases */
        reset = true;
    }
```

More importantly, **the project had already found this mechanism in
August 2026** — `docs/plans/phase15-hardware-boot-protocol.md`, the
v12/v13 round:

> "The v12 test then surfaced a subtler root cause: seL4's
> `Untyped_Retype` creates objects at the untyped's free-offset
> WATERMARK, never at a requested offset — so a BAR in the middle of a
> multi-GiB device untyped yields a frame for the chunk's base, and
> the driver reads garbage registers. QEMU had masked this because its
> 32 GiB BAR sits exactly on a chunk base. **Fixed by fast-forwarding
> the watermark** (retype+delete throwaway large frames to skip);
> verified on QEMU by forcing a mid-chunk BAR (two xHCI controllers).
> Commit `966c76852f`."

That diagnosis was right; the fix was a no-op — the fast-forward
consumes nothing, because the kernel resets the index at every step
(this file, §3). The QEMU verification could not see the difference:
with two xHCI controllers the FIRST one (on a chunk base) still
carries the chain, so the boot looks healthy.

So the defect dates from the row-13/14 high-phys path itself
(2026-08) and the field report that brought it in is its first hard,
transcribed evidence — not a regression of anything since.

**One record does not fit, and it is flagged rather than explained
away.** The 15-3 / 0.2.0 milestones (2026-08-23, 2026-09-16) report
the gram booting this UEFI USB-stick media "fully interactive", and
docs/13 states that "GPT partitions printed over the real xHCI + USB
stick". The repository holds **no guest-side xHCI log** from those
rounds (the gram has no serial; the panel log was transcribed for the
display chain, not for the usb chain), so that claim rests on the
milestone's own summary. Either those rounds ran a different storage
chain (the gram also has an NVMe SSD — row 17) or the guest's xHCI
was reached differently then. This is registered as an open
question, not as a settled contradiction: the field's current report
(with the `-110` signature transcribed from the panel) is the
evidence this work is based on.

## 5b. A flake seen once, disclosed

Of the five `midwindow` runs (one before the BAR-address reporting was
added, four after), four are CLEAN and one failed with a different
signature — `report_rom: Uncaught exception of type
'Genode::Attached_dataspace::Region_conflict'`, before the usb chain
started at all. That is a **known, not-root-caused** failure mode of
this image (`docs/evidence/sponge-debug-firstpaint.md` §5:
"`Region_conflict` … comes from somewhere else in `report_rom`'s attach
path and needs its own hypothesis-driven run"), registered as an open
item in `docs/17-runtime-component-topology.md` §5. It is unrelated
to the MMIO mapping (the xHCI never appeared in that log) and it did
not recur in the other runs.

## 6. What the gram should now do

The gram's xHCI BAR at `0x601d140000` is 128.9 GiB into the device
untyped `[256 GiB, 512 GiB)`; with the 64 GiB grid it is placed in the
untyped `[384 GiB, 448 GiB)` — an untyped of its own, independent of
the GOP framebuffer's `[256 GiB, 320 GiB)`. The frame is created at
the BAR's address (the watermark fast-forward now actually advances),
the xHCI reads its capability registers (`hci version 0x100`), the USB
storage stick and the HID mouse enumerate, `usb_block` serves the
stick, `/system` boots, and the supervisor has a subtree to supervise
instead of a kill-loop.

**Not yet proven on the panel.** The QEMU proof covers the mechanism
and the class; the gram itself still needs one reflash-and-eyeball
run (the panel has no serial — the on-panel boot log is the only
witness, `docs/evidence/phase15-fb-console/README.md`). What to look
for: no `Host halt failed, -110`, `sponge_configd: ready` in the boot
log, and a desktop that reaches the panel.

**Known residual limitation (documented, not fixed here).** Within one
device untyped the watermark is monotonic, so the *same* device window
can be materialised only once per untyped; a driver that closes and
re-opens the same BAR gets a clean refusal ("I/O memory … not
available") instead of a second frame. The 64 GiB grid keeps that
window small, and the device windows the product media uses (the GOP
framebuffer, the xHCI BAR) are in different untypeds.

## 6a. How the kernel patch was brought into the prepared port (disclosure)

`prepare_port sel4` could **not** be run end to end on this host: the
port's own gate `check_python_module,future` fails (the host python
has no `future` module) and the port would have to re-clone seL4 from
GitHub. The patch was therefore applied in place to the already
prepared tree (`genode/contrib/sel4-<old-hash>/`), the port's
fingerprint was re-stamped (`tool/ports/update_hash sel4` →
`28c4a43907d4ac453c195d93b65885948d731921`) and the prepared
directory was renamed to the new fingerprint — the same content
`prepare_port` would produce for that fingerprint.

That equivalence is **proved, not assumed**: all sixteen patch files
of `repos/base-sel4/patches/` (the port's `PATCHES := $(sort
$(wildcard $(REP_DIR)/patches/*.patch))` order) were reverse-applied
to a copy of the prepared tree (reaching the pristine upstream
source — the `SPONGE_DEVICE_UNTYPED_TOP` marker gone) and then
re-applied in the port's order; the result is **byte-identical** to
the live prepared tree. So a fresh `prepare_port sel4` on a host with
the python modules installed reproduces exactly this tree.

## 7. Files

| what | where |
|---|---|
| the kernel patch (durable record) | `genode/repos/base-sel4/patches/sel4-device-untyped-ladder.patch` (fork) |
| forwarding notice | `docs/patches/sel4-device-untyped-ladder.patch` |
| the new harness case | `tool/hwrepro.mojo` (`--case midwindow`) |
| ledger | `docs/11-environment.md` §4 row 27 |
| hardware contract | `docs/15-hardware-compatibility.md` (the 17ZD90N row) |
| runtime topology | `docs/17-runtime-component-topology.md` §5 |
| logs | `var/hwrepro/sponge-os-0.2.0-x86_64-sel4-{midwindow,inwindow,boundary}.log` |
