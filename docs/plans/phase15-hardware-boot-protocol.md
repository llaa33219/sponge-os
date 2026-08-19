# Phase 15-3 — Real-Hardware Boot Protocol (User-Executed)

> Status: drafted 2026-08-18, ahead of W4 (UEFI product media). The media
> this protocol boots does not exist until W4 lands
> (`./tool/dist --bake-profile desktop --firmware uefi`); this document is
> the agreed procedure for when it does. Plan reference:
> `docs/plans/phase15-real-hardware-boot.md` (D15.1 target machine, D15.14
> Secure Boot handling, D15.16 early-pull of 15-3).
>
> **Who does what:** the user executes every step on the physical machine
> and every `sudo` command on the host. The agent prepares the media,
> this protocol, and analyzes the captured evidence afterwards. The agent
> never runs `sudo` (AGENTS.md §5.5).

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
