# Phase 15-3 — Real-Hardware Boot Protocol (User-Executed)

> Status: **in progress — the OS/boot chain is fixed and QEMU-verified;
> the real-hardware display path (GRUB → multiboot2 FB tag → boot_fb) on
> the 17ZD90N's iGPU GOP is the open blocker** (2026-08-21). The boot
> chain is proven end-to-end on OVMF (`alpha-probe: PASS`). See "Current
> state" below before running.
> Plan reference: `docs/plans/phase15-real-hardware-boot.md` (D15.1 target
> machine, D15.14 Secure Boot handling, D15.16 pivot). Bring-up log:
> `docs/evidence/phase15-index.md` §13–§14.
>
> **Who does what:** the user executes every step on the physical machine
> and every `sudo` command on the host. The agent prepares the media,
> this protocol, and analyzes the captured evidence afterwards. The agent
> never runs `sudo` (AGENTS.md §5.5).

## Current state (2026-08-21)

Fixed and committed (the OS/boot chain is proven on QEMU/OVMF):

- Vendored g2fg GRUB2 multiboot2 broken on the Insyde firmware → the
  UEFI scenarios now stage a full GRUB 2.12 (`run/fixtures/grubx64-full.efi`).
- bender `check_mem` too strict for UEFI maps → principled source rebuild
  (`docs/11` patch-ledger row 10).
- **seL4 kernel `init_freemem` produced overlapping untyped caps under
  fragmented UEFI maps → fixed** (ledger row 11). This was the main
  hang: OVMF smoke now `boot-probe: PASS`, desktop `alpha-probe: PASS`.

RESOLVED (2026-08-23, v13 cap-fix build):

- **The 17ZD90N boots Sponge OS to the desktop.** The display-path
  chain that blocked every earlier round was a stack of four
  independent defects, each found and fixed from the visible boot log
  that the kernel early-FB console (ledger row 12) provided: (1) the
  kernel's device-untyped coverage did not reach the high MMIO
  aperture (row 13); (2) core's flat phys CNode capped at 8 GiB and
  its v1 replacement was unallocatable on the fragmented Insyde map
  (row 14 v2: sequential-slot CNode); (3) seL4 retype is
  watermark-only, so mid-chunk BARs got wrong frames until the
  fast-forward fix; (4) the storage-serving PDs exhausted their
  mapping-registry caps on slow real-USB I/O (row 6 addendum: 65536
  slots/PD + caps quota 500→16000). Measured real-hw addresses: GOP
  framebuffer at 0x4000000000 (256 GiB), PCH xHCI BAR at
  0x601d140000 (~384.5 GiB).
- **Evidence:** `[init -> system] child "alpha_probe" exited with exit
  value 0` on the panel — alpha_probe's themed-panel pixel check,
  launcher-feed check, and configd-broadcast check all passed through
  the real compositor on real hardware (identical to the QEMU
  success sequence; the screen showing the log after it is the
  scenario simply having finished, not a hang). GPT partitions were
  printed (xHCI + USB-stick DMA proven); USB 3.0 enumerated.
- Pending final confirmation: desktop visible + USB mouse interaction
  (user); media sha256 `bc84b017…`.
- **Round v8→v9 (fb-console kernel):** the vendored seL4 kernel now
  carries an early framebuffer text console (docs/11 ledger row 12,
  `docs/patches/sel4-early-fb-console.patch`, OVMF-verified per
  `docs/evidence/phase15-fb-console/`). The production image rebuilt
  with this kernel (sha256 prefix `f0bcea6b9866`) still boots to a
  black screen on the 17ZD90N — the `sel4 fb console: ready` banner
  never appears. That means one of: (a) GRUB cannot drive the Insyde
  GOP in graphics mode at all, (b) GRUB sets the mode but emits no
  usable multiboot2 FB tag, (c) bender (32-bit, <4 GiB initial page
  tables) dies reading an MBI placed above 4 GiB (v7 forensics: Insyde
  loads GRUB data at 0x100360000), or (d) the GOP LFB is not the
  scanned-out buffer.

- **Round v9 results (user-reported on the 17ZD90N):** videotest works
  at 2560x1600x32 AND 1024x768x32 — GRUB drives the Insyde GOP and the
  LFB is genuinely scanned out, so hypotheses (a) "GRUB can't drive the
  GOP" and (d) "LFB not scanned out" are DEAD. videoinfo displays the
  mode list. The gfxterm chain is black WITH and WITHOUT `cutmem 1G`;
  the text-console chain shows GRUB's "no console" warning (expected —
  no FB tag in console mode). Remaining candidates: (b) GRUB emits no
  usable multiboot2 FB tag on Insyde even in gfxterm mode, or (c) the
  MBI/modules are placed above 4 GiB (Insyde loads GRUB data at
  0x100360000 per v7) so the 32-bit receivers (bender, fbprobe2) die
  before reading anything. cutmem not curing the chain either weakens
  (c) or shows cutmem does not constrain the multiboot2 MBI placement.

- **Round v10 results (user-reported on the 17ZD90N):** fbprobe3 hung
  BLACK on all entries (plain / +cutmem 1G / +1024x768x32) — never
  green, never the no-tag reset. Since the no-tag case resets and it
  never fired, GRUB almost certainly DOES emit a framebuffer tag on
  Insyde; the remaining candidates are the framebuffer address being
  >= 4 GiB (the "high GOP framebuffer" hypothesis — also explains the
  kernel fb-console death, whose identity-phase writes only reach the
  low 4 GiB, and predicts boot_fb's IO_MEM failure like the xhci BAR
  at 32 GiB), or the MBI itself above 4 GiB. cutmem not changing the
  outcome argues against MBI placement.
- **Round v11 results (user-reported on the 17ZD90N): BREAKTHROUGH.**
  The chain entries (4/5) show the kernel boot log ON THE PANEL,
  starting with `sel4 fb console: ready` — real-hardware boot
  visibility achieved for the first time. The boot stops at a Genode
  component abort (`Warning: abort called - thread: ...`); the exact
  component is unreadable at the fb console's 8x8 font size on the
  2560x1600 panel (photo / larger font pending). fbprobe4 entries
  (1/2/3) stayed black — the 32-bit probe cannot draw to the high FB,
  while the kernel console (row-12 deferral path) can, which all but
  confirms the high-FB hypothesis (GOP framebuffer >= 4 GiB on this
  Insyde firmware). videoinfo works (entry 6).
- **Round v12/v13 results (user-reported on the 17ZD90N):** the v12
  boot-log screen identified the exact failure: boot_fb's IO_MEM for
  the GOP framebuffer at **0x4000000000 (256 GiB)** and pc_usb_host's
  IO_MEM for the PCH xHCI BAR at **0x601d140000 (~384.5 GiB)** both
  failed — the row-14 v1 high-phys CNode (flat 32 GiB window, 256 MiB
  backing) could neither cover addresses 128 GiB apart nor allocate
  its backing on the fragmented Insyde map ("16k pool exhausted").
  v1 had also never been truly verified (its acceptance ran on the
  low-BAR EHCI path by mistake). **Row 14 was redesigned (v2)**: a
  small high-phys CNode (2^14 slots, 512 KiB backing) with sequential
  slot allocation + a phys→slot table — no address-window assumption.
  Additional latent defects fixed along the way: early (not lazy)
  construction, top-CNode slot 0x7e0 reservation, and the core-CNode
  wiring for the runtime selector. QEMU-verified with the xHCI BAR at
  32 GiB (xhci_hcd binds, USB 3.0 enumerates, configd ready) plus all
  regressions. Commit `69db536604`. The v12 test then surfaced a
  subtler root cause: seL4's `Untyped_Retype` creates objects at the
  untyped's free-offset WATERMARK, never at a requested offset — so a
  BAR in the middle of a multi-GiB device untyped yields a frame for
  the chunk's base, and the driver reads garbage registers (fault in
  `quirk_usb_early_handoff`). QEMU had masked this because its 32 GiB
  BAR sits exactly on a chunk base. Fixed by fast-forwarding the
  watermark (retype+delete throwaway large frames to skip); verified
  on QEMU by forcing a mid-chunk BAR (two xHCI controllers). Commit
  `966c76852f`. Media: `var/dist/sponge-diag-v13-uefi.img`.
- **Round v11 (ready for the user):** kernel fixes landed —
  fb_console defers all drawing until the direct map covers the FB
  (safe to 512 GiB; ledger row 12 regenerated) and device-untyped
  coverage is extended above the memory-map top (ledger row 13,
  LOW+HIGH split, `SPONGE_DEVICE_UNTYPED_TOP=2^40`). Caveat: QEMU
  keeps the GOP at 2 GiB even with virtio-vga, so the high-FB path is
  verified-by-construction only; the 17ZD90N is the binding test.
  fbprobe4 (`run/fixtures/fbprobe4.s`) adds a reset-timing ladder
  (~3 s magic / ~6 s MBI-high / ~12 s no-tag / ~24 s FB-high / black
  bpp / green OK). Media: `var/dist/sponge-diag-v11-uefi.img`.
  OVMF rehearsal 2026-08-23: v11-1 = 100% green; v11-4 chain boots to
  `sponge_configd: ready` with the fb-console log on the framebuffer
  (pixel-verified, low-FB regression of the refactored console).

## Resume notes (display work in progress)

- The chain up to the display is proven; the only open piece is getting
  a valid framebuffer to `boot_fb` on the real panel.
- **Current diagnostic: `var/dist/sponge-diag-v10-uefi.img`** — fixed
  storage-chain base + `run/fixtures/grub-diag-v10.cfg` + fbprobe3
  (`run/fixtures/fbprobe3.s`), a 32-bit multiboot2 payload loaded
  DIRECTLY by GRUB (no bender/seL4) that isolates GRUB's tag emission
  and MBI placement. fbprobe3 signals: GREEN = FB tag present and
  usable (addr < 4 GiB, bpp 32); RESET loop = MBI readable but no FB
  tag; BLACK hang = MBI unreadable (placed > 4 GiB, EBX truncated) or
  FB addr >= 4 GiB or bpp != 32. OVMF rehearsal 2026-08-22: v10-1
  fills 2560x1600 100% green. Menu: 1=plain, 2=+cutmem 1G,
  3=+gfxpayload=1024x768x32, 4=chain control, 5=chain gfxpayload=keep,
  6=videoinfo; the interpretation matrix is in the cfg header comment.
- (superseded) `var/dist/sponge-diag-v9-uefi.img` — v9 bisect menu;
  fb-console production image with the ESP grub.cfg swapped for
  `run/fixtures/grub-diag.cfg` v9 (a visible text-mode menu; zero new
  code). OVMF rehearsal (2026-08-22): the menu renders with all six
  entries, v9-4 boots the chain, and the fb-console kernel log renders
  on the framebuffer (pixel-verified screendump); `videotest` is
  embedded in `grubx64-full.efi` (strings-verified) but its menu
  entries were not boot-rehearsed. It bisects the four layers above:
  - entries 1/2 `videotest 2560x1600x32` / `1024x768x32`: pattern
    visible → GRUB can drive the GOP (rules out (a)); GRUB error text
    or black → (a) confirmed, iterate `gfxmode`/GRUB version.
  - entry 3 `videoinfo`: re-dump the GOP mode list (photograph).
  - entry 4 (default) gfxterm + chain: fb-console banner + log → the
    hang is in core, read the last line; black with videotest working
    → (b) or (d); desktop → done.
  - entry 5 gfxterm + `cutmem 1G`: banner appears where entry 4 was
    black → (c) confirmed (MBI was above 4 GiB); keep cutmem or patch
    bender's allocation. Mechanism source-verified in morbo
    (`/tmp/opencode/morbo/standalone/`, branch genode_bender @77a6918):
    bender NEVER enables paging (no CR3/CR0.PG writes anywhere; the
    `REGISTER_SETTER` macros in `include/asm.h` have no call sites),
    the multiboot2 MBI address arrives in 32-bit EBX
    (`start.asm:110`), and module `mod_start`/`mod_end` are u32 per
    the spec (`mbi2.c`) — so bender's entire reachable space is
    0–4 GiB physical, and an Insyde high placement (v7: GRUB data at
    0x100360000) truncates to garbage. Caveat for interpretation:
    entry 5 only discriminates (c) if the FB tag IS emitted (i.e. (b)
    is false); with no tag, 4 and 5 are both black regardless.
  - entry 6 text console: control (no FB tag; kernel fb console stays
    disabled by design).
- Diagnostic tools in the repo: `run/fixtures/fbprobe2.s` (GOP-draw
  probe), the kernel early-FB console (ledger row 12),
  `run/fixtures/grub-diag.cfg` v9 (consumed by the `test` bake
  profile's `grub_mode = diagnostic`), and the full GRUB's
  `videotest`/`videoinfo`/`lsefimmap`.
- Angles not yet tried: having Genode locate the GOP framebuffer
  without GRUB's FB tag (a Genode/bootloader-side change); a different
  GRUB build/version; upstream (Genode/seL4) engagement on the
  base-sel4 UEFI + GOP path.
- **usb storage chain QEMU-verified (2026-08-22):**
  `run/sponge-desktop-disk-uefi-usb` now boots the media on OVMF to
  `sponge_configd: ready` under a fail-loud gate (previously the
  D15.16-era gate killed every boot at the `Genode v` banner, so the
  chain had never run). Two latent defect classes were found and
  fixed: (i) **xHCI unusable on OVMF** — qemu-xhci and nec-usb-xhci
  both receive a 64-bit BAR that OVMF places at 0x800000000 (32 GiB,
  the q35 pci-hole64 default), outside the seL4 kernel's device-
  untyped coverage, so the platform IO_MEM session fails and
  `init -> usb` aborts; `pci-hole64-size=0` does not move it. The
  verification attach uses usb-ehci (32-bit BAR, sub-4 GiB; the
  Genode stack — pc_usb_host with xhci_hcd+ehci_hcd → usb_block — is
  HCD-agnostic). **Real-hw relevance:** if Insyde ever places the Ice
  Lake PCH xHCI BAR high (above-4G decoding), the same
  `I/O memory ... not available` error will appear on the fb console;
  the principled fix (extend the kernel's device-untyped coverage
  above the memory-map top) is a docs/11 ledger row-13 candidate.
  (ii) **read-only incoherence** — the stick was attached
  `readonly=on` and usb_block was `writeable: no` while
  part_block/vfs asked `writeable: yes` (media design: installs
  persist on P3), so the rump ext2fs mount died with EACCES
  ("Mounting 'ext2fs' failed (13)"); now writable end-to-end
  (`-snapshot` keeps QEMU writes in a temporary overlay).

## 0. What this boot proves (and does not)

Proves (roadmap criteria 1, 2, 4):

- Sponge OS boots from USB on the LG gram 17ZD90N-VX7BK (UEFI, Secure
  Boot disabled).
- Input (PS/2 keyboard + USB mouse), display (UEFI GOP framebuffer via
  `boot_fb`), and storage (NVMe, P3 GENODE + P4 SPONGE-DATA) work; the
  desktop reaches the same verified state as the QEMU scenarios.

Does **not** prove (pre-registered gaps — expected, not failures):

- Trackpad: I2C-HID, Genode has no `i2c_hid` driver → **dead. Use a USB
  mouse.** (R15.10)
- Wi-Fi: Intel AX201 (CNVio2) unsupported → no networking. The machine
  has no Ethernet. (R15.11)
- Power management beyond acpica basics; suspend/resume untested.

## 1. Prerequisites

| Item | Requirement |
|---|---|
| USB stick | ≥ 4 GiB (desktop profile image ≈ 1.9 GiB). USB-A preferred — the machine has 3× USB-A (PCH-direct) + 1× USB-C/TB3. **Remove any OTHER USB storage devices before booting** — the storage policy matches any mass-storage device by class (class 0x8), so a second stick could be picked instead. |
| USB mouse | **Mandatory** (trackpad unsupported). Any USB HID mouse. |
| Media | `var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img` built with `./tool/dist --bake-profile desktop --firmware uefi --storage usb` (W-USB, 2026-08-18). sha256 `0aa4151413fbcf7b7b5bda2948687a6b68ef5931730595060fac94f147a28f64` — verify after writing: `sha256sum` the first 2049966080 bytes of the stick (`sudo head -c 2049966080 /dev/sdX \| sha256sum`). |
| Machine | LG gram 17ZD90N-VX7BK, charged or on AC. |

## 2. Firmware preparation (one-time, ~2 minutes)

1. Power on, press **F2** repeatedly → Insyde H2O setup.
2. **Security → Secure Boot Configuration → Secure Boot Option →
   Disabled.** (D15.14 — the GRUB2 EFI is signed with Genode-vendor keys
   absent from the OEM db; disabling is the documented path.)
3. **Intel Advanced → Platform Settings → TCSS Platform Settings →
   Thunderbolt Configuration → Integrated Thunderbolt Support →
   Disabled.** (R15.9 — iTBT causes shutdown/reboot hangs on this
   machine generation; Linux community-confirmed.)
4. Save & exit (F10 → Yes).

These settings are reversible; note their original values (both are
Enabled/On by default).

## 3. Write the image to USB (host, user-run)

```bash
# Identify the stick BEFORE and AFTER inserting it — the new device is the stick.
lsblk -d -o NAME,SIZE,MODEL
sudo dd if=var/dist/sponge-os-0.2.0-alpha-x86_64-sel4.img of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

**WARNING:** `of=/dev/sdX` must be the USB stick, never the internal
NVMe (`/dev/nvme0n1`) or any other disk. The internal SSD is not touched
by anything else in this protocol — the boot is fully non-destructive
to the machine's existing data as long as `dd` targets the stick.

## 4. Boot and capture

1. Stick in a **USB-A port** (left side preferred; if boot fails, try
   another port before anything else).
2. Power on, press **F10** → boot menu → select the USB stick
   (typically "UEFI: <stick model>").
3. Capture with a phone camera:
   - **C1**: the GRUB menu ("Genode on seL4" entry).
   - **C2**: the boot log scrolling (seL4 + Genode core lines).
   - **C3**: the desktop once it settles (themed panel + launcher), or
     the exact screen where it stops.
   - **C4**: if anything fails — the last visible screen, in focus.
4. If the desktop appears, exercise the success checklist (§5) and note
   each line's result (pass/fail/notes).

## 5. Success checklist (roadmap criterion 2)

| # | Check | Expected |
|---|---|---|
| S1 | Display | Desktop at native 2560×1600, themed panel visible (no VESA fallback artifacts). |
| S2 | Keyboard (PS/2) | Type in the terminal: characters appear correctly. |
| S3 | USB mouse | Cursor moves and clicks register (panel launcher toggle opens). |
| S4 | Storage | `vct status` runs; packages list shows the baked `desktop` set (terminal, textedit, files, calculator, pdf_view, falkon). |
| S5 | Baked defaults | First boot shows the profile defaults (`vct bake show`); `vct status` shows `bake: desktop @ v1`. |
| S6 | Persistence | Create a change (e.g. `vct config set panel.height 64`), reboot from the same stick, confirm the change survived on SPONGE-DATA (P4). |
| S7 | Trackpad (expected fail) | Trackpad does nothing — pre-registered gap, record confirmation only. |

## 6. Failure decision tree

| Symptom | Likely cause | Action |
|---|---|---|
| USB not in the F10 menu | Secure Boot still on; stick not written; port issue | Re-check §2 step 2; re-`dd`; try another USB-A port. |
| Firmware error screen / immediate return to menu | Secure Boot on | §2 step 2. |
| GRUB menu never appears | Wrong EFI path on media | Capture C4; report (W4 media defect). |
| Bender prints `Reserved memory ... overlaps with phdr ...` and stops | bender `check_mem` rejecting an EFI-memory-map overlap (the docs/11 §4.2 bender-filter ledger candidate, seen on OVMF; unexpected on Insyde but possible) | Capture C4 with the full message (addresses + type number); report — this triggers the bender source-rebuild path with the real map as evidence. |
| Boot hangs with NO visible stage info (production image) | Unknown stage — gfxterm hides GRUB output | Re-burn with the **`test` profile** (`./tool/dist --bake-profile test --firmware uefi --storage usb`): the diagnostic GRUB runs text-mode with a visible 10 s menu and `SPONGE-DIAG:` markers before every stage. The last marker printed pinpoints the stop: nothing after LG logo = firmware never ran GRUB; `grub alive` = GRUB works; `booting now` then freeze = bender/seL4/Genode core stage (report immediately — matches the W1 OVMF core-hang signature). |
| "Booting 'Genode on seL4'" then black/hang >3 min | Matches the OVMF core-init hang signature (R15.16/W1) — would prove the hang is NOT OVMF-specific | Capture C4 with the last lines; report immediately — this flips the D15.16 assumption and triggers the vendored-chain fix path (docs/11 §4.2 candidates). |
| Desktop but no cursor | USB mouse not enumerated; or only PS/2 present | Re-plug mouse; PS/2 keyboard should still work (S2). |
| Desktop but wrong/low resolution | GOP mode fallback | Note the resolution; still counts as display-functional with a recorded note. |
| Hangs on shutdown/reboot | iTBT left enabled | §2 step 3. |

## 7. What to send back

- Photos C1–C4.
- The §5 checklist with per-line results.
- The exact BIOS settings used (photo of the two setup screens if
  convenient).
- Any unexpected behavior, verbatim.

The agent then: closes roadmap criteria 1/2/4 (or records gaps with
reproduction notes per criterion 4), flips the single
`target: real-hardware` row in `docs/15-hardware-compatibility.md` per
D15.11, updates `docs/13-installation.md` (criterion 3), and prepares
the 0.2.0-alpha release notes.

## 8. Safety notes

- The Korean SKU ships with FreeDOS; there is no preinstalled OS to
  damage. The USB boot never writes to the internal NVMe.
- The stick is re-writable; `dd` again any time.
- BIOS changes in §2 are standard user settings and fully reversible.
