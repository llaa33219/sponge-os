> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 15.

### Phase 15: Real-Hardware Boot

> Work plan: `docs/plans/phase15-real-hardware-boot.md` (2026-08-17).
> User-directed decomposition: 15-1 bake-customizable + UEFI media,
> 15-2 QEMU hardware-diversity matrix, 15-3 physical boot by the user.
> Target machine (D15.1): LG gram 17 (2020) 17ZD90N-VX7BK.
>
> **Progress (2026-08-21):** 15-1 and 15-2 are done. 15-1 shipped the
> bake system (`pkg/bake/{minimal,desktop,test}.profile`, `run/bake.inc`,
> `tool/bake`, first-boot seeding in `sponge_configd`, `vct bake
> list/show/reset`), the UEFI product media
> (`run/sponge-desktop-disk-uefi{,-nvme,-usb}.run`, `boot_fb` display
> path, `tool/dist --firmware uefi`), and the 15-3 desktop USB-boot
> artifact (`./tool/dist --bake-profile desktop --firmware uefi
> --storage usb`). 15-2 shipped the usb-mouse HID envelope
> (`run/sponge-usb-hid-mouse.run`), the D15.11 `tool/hw_compat`
> real-hardware admission policy, and the updated
> `docs/15-hardware-compatibility.md` (4 verified / 1 smoke-only /
> 12 gap incl. the single 17ZD90N real-hardware gap row); a 9-scenario
> serial regression sweep is all-PASS.
>
> **15-3 (desktop booted on hardware, 2026-08-23):** the full desktop
> boots from USB on the physical 17ZD90N — the on-panel boot log ends
> with `child "alpha_probe" exited with exit value 0`, the same
> compositor-verified state as QEMU (themed-panel pixel check,
> launcher feed, configd broadcast through the real compositor; the
> GPT partitions printed over the real xHCI + USB stick before that,
> proving the storage data path). The earlier "GRUB cannot produce a
> working payload framebuffer" reading was wrong — GRUB emits the FB
> tag fine; the truth was a four-defect stack below the application,
> each layer found from the kernel early-FB console's on-panel boot
> log (ledger row 12): (1) seL4-kernel device-untyped coverage did
> not reach the high MMIO aperture (row 13); (2) Genode core's flat
> phys CNode capped at 8 GiB and its v1 replacement was unallocatable
> on the fragmented Insyde map (row 14 v2: sequential-slot CNode,
> measured GOP FB at 256 GiB, xHCI BAR at ~384.5 GiB); (3) seL4
> retype is watermark-only — mid-chunk BARs got wrong frames until
> the fast-forward fix; (4) storage-serving PDs exhausted their
> mapping-registry caps on slow real-USB I/O (row 6 addendum: 65536
> slots/PD + caps quota). Full bring-up log:
> `docs/evidence/phase15-index.md` §13–§14 +
> `docs/plans/phase15-hardware-boot-protocol.md`.
>
> **Phase 15 CLOSED / 0.2.0 released (2026-09-16):** the final
> interactive-input confirmation landed — on the physical 17ZD90N the
> panel appears and pointer/click, in-window element interaction, and
> mouse movement all work smoothly at the panel's native 2560x1600
> (correct EDID/mode detection). The blocking defect was the acpi
> driver's DMAR misparse (ledger row 16): real Intel firmware ships a
> DMAR (DRHD+RMRR) that QEMU's synthetic tables omit; the misparse
> threw `Mmio::Range_violation`, killing the acpi child and stalling
> pci_decode/platform/usb (storage+input dead, Tier-0 alive) on both
> stack generations. Fixed by a spec-corrected, bounded entry walk
> plus skipping DMAR report generation; regression gate
> `./tool/hwrepro --case acpi-dmar`. The pre-real-hw QEMU harness
> also shipped: `run/sponge-hw-matrix.run` + `tool/hwtest` (20-variant
> emulated-hardware matrix) and `tool/hwrepro` (boundary / in-window /
> acpi-dmar cases), docs/08 §16 + docs/11 §10.6 + docs/15 §7.

#### Goal

Sponge OS boots successfully on at least one physical machine — the
QEMU-only limitation is retired. **Completing this phase releases
0.2.0-alpha.**

#### Completion Criteria

- [x] Boot verified on physical hardware from USB or SSD media
  (target machine recorded in the compatibility document).
- [x] Input, display, and storage functional on that machine; the
  desktop reaches the same verified state as the QEMU scenarios.
- [x] `docs/13-installation.md` updated: real-hardware install path
  documented, QEMU-only limitation removed or rescoped.
- [x] Known hardware-specific issues recorded with reproduction
  notes.

## Status note (moved from README)

Facts relocated from the README `Current Status` section during the
README trim (substance kept as-is, nothing invented):

- **Pre-registered hardware gaps:** trackpad and Wi-Fi remain
  pre-registered gaps (the 2026-09-16 0.2.0 interaction acceptance
  covers pointer/click, in-window interaction, and mouse movement
  only).
- **Verified vct boot output (base-sel4 on QEMU)** — the 0.2.0 release
  receipt, which had no roadmap home before this note:

  ```
  Genode 26.08
  699 MiB RAM and 523288 caps assigned to init
  [init -> vct] vct (0.2.0 / Archaeocyte) starting
  [init -> vct] vct — Very Convenient Tool
  [init -> vct] version: 0.2.0 (Archaeocyte)
  Run script execution successful.
  ```

  (The Phase 1 boot receipt in `phase1-minimum-boot.md` is the older
  Genode 26.05 / vct 0.0.1-pre-alpha block; the block above is the
  current one.)
- **Vendoring:** Genode 26.08 is vendored at `genode/`, pinned to
  upstream commit `d18ee9e917`; one `git clone sponge-os` brings the
  whole build (canonical record: `docs/11-environment.md`).
