> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 12.

### Phase 12: Hardware Support Expansion

#### Goal

The system boots on a wider range of (virtual and physical) platform
configurations, and the supported-hardware surface is explicit rather
than accidental. This phase builds the enablement infrastructure;
Phase 15 is the concrete real-hardware boot milestone.

#### Completion Criteria

- [x] **Boot matrix beyond the current QEMU defaults**: additional
  machine types, AHCI and NVMe storage variants, USB boot media.
  **Traceability (criterion → scenario → exact marker → evidence,
  all on QEMU 11.0.3 host):**
  - *q35 + Skylake-Client explicit pin on every disk-touching script:*
    `run/sponge-boot.run`, `run/sponge-desktop-disk.run`,
    `run/sponge-persist-disk.run`, `run/sponge-falkon-disk.run`,
    `run/sponge-alpha.run` all carry
    `append qemu_args " -machine q35 -cpu Skylake-Client "` before
    `-nographic -m` (added in W1). Evidence:
    `docs/evidence/task-1-phase12-platform.md` (W1 pin receipt) +
    `docs/evidence/task-0-phase12-baseline.md` (W0 baseline
    `boot/probe: PASS ... sponge-boot-marker-v1` recovered byte-for-byte
    after the pin).
  - *AHCI product-media default:* `run/sponge-desktop-disk.run` →
    `alpha-probe: PASS` (W0 baseline + W6 envelope; evidence
    `docs/evidence/phase12-envelope-sponge-desktop-disk.log`).
  - *NVMe product-media opt-in:* `run/sponge-desktop-disk-nvme.run` →
    `Number: 3` P3 byte check + `alpha-probe: PASS` (one namespace,
    NVMe `caps: 5000 | ram: 64M`). Evidence
    `docs/evidence/phase12-desktop-nvme.log`.
  - *i440fx IDE smoke (no AHCI, no product image):*
    `run/sponge-boot-i440fx.run` → `boot-probe: PASS` from the
    PIIX4-IDE-backed marker. Evidence
    `docs/evidence/phase12-boot-i440fx.log`.
  - *q35/AHCI multi-disk order check (P3 of second disk):*
    `run/sponge-boot-multidisk.run` → `boot-probe: PASS` from the
    second disk's P3 marker. Evidence
    `docs/evidence/phase12-boot-multidisk.log`.
  - *BIOS-side USB boot (QEMU `-device usb-storage`, BIOS side only):*
    `run/sponge-usb-boot.run` → `BIOS-side USB boot verified` +
    `alpha-probe: PASS`. Evidence `docs/evidence/phase12-usb-boot.log`.
- [x] **Driver set expanded**: networking beyond QEMU slirp (at least
  one real NIC driver path), input beyond PS/2 + usb-tablet.
  **Traceability:**
  - *Linux-backed `pc_nic` + QEMU e1000 + `nic_router` DHCP:*
    `run/sponge-pc-nic.run` → `pc_nic: bound device` +
    `nic_router: uplink DHCP acquired` (caps: 1000 | ram: 32M,
    300 s cold-DDE-Linux gate). Evidence
    `docs/evidence/phase12-pc-nic.log`. The change is additive —
    every existing product/iPXE scenario is byte-for-byte untouched.
    Honest claim text (per docs/15 matrix row):
    "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested."
  - *USB HID keyboard via QMP hotplug (probe-focus ROM quirk absent;
    PS/2 send-key path still covered by `sponge-terminal-qmp.run`):*
    `run/sponge-usb-kbd-via-qmp.run` → ordered chain
    `usb_hid: KEYBOARD detected` → `usb_hid: KEYBOARD removed` →
    `sponge-usb-kbd-via-qmp: PASS` + `Run script execution
    successful.`. Evidence `docs/evidence/phase12-usb-kbd.log`.
    The missing glyph-delta secondary gate is recorded as a documented
    Phase-12 gap in `docs/15-hardware-compatibility.md` (probe-focus
    ROM quirk; see also `docs/evidence/task-4-phase12-usb.md` §3/§5
    deviation #5).
- [x] **A hardware compatibility document** listing tested
  configurations and known gaps. **Traceability:**
  `docs/15-hardware-compatibility.md` is the hand-curated 5×5 surface
  matrix + 16-cell ledger (4 verified, 1 smoke-only, 11 gap). The
  readability and honesty rules are enforced by
  `tool/hw_compat.mojo assert` (additive, no `generate` / `update` /
  repo write); the `assert` validator is reachable from
  `./tool/build verify` and from the W6 final host verification
  (exits 0; receipts in `docs/evidence/task-5-phase12-hw-compat.md`).
  No `target: real-hardware` cell exists; this is a Phase-15 cell, not
  a Phase-12 cell.
- [x] **Run scenarios cover the new configurations so regressions
  fail loudly.** **Traceability:** the six new Phase-12 scenarios
  (`sponge-boot-i440fx.run`, `sponge-boot-multidisk.run`,
  `sponge-desktop-disk-nvme.run`, `sponge-pc-nic.run`,
  `sponge-usb-boot.run`, `sponge-usb-kbd-via-qmp.run`) plus the
  augmented `run/qmp.inc` (the W3b launch-click fix) carry distinct
  bounded markers and durable `docs/evidence/phase12-*.log` pointers.
  The full serialized sweep (one scenario at a time, `make -j1`,
  no concurrent `make` in `genode/build/x86_64`) is captured in
  `docs/evidence/task-6-phase12-regression.md` (W6) +
  `docs/evidence/phase12-index.md` (the evidence index). The fresh
  build (`docs/evidence/phase12-fresh-build.log`) proves the managed
  `pc` repository, the nine-row patch ledger, and a freshly prepared
  build directory that compiles `sponge-pc-nic.run` first.

## Status note (moved from README)

PC and display drivers as **installable packages** — relocated from the
README `Current Status` section; canonical detail: `docs/15-hardware-compatibility.md` §8, receipts `docs/evidence/driver-packages.md`
and `docs/evidence/fb-driver-packages.md`:

- `vct install pc_nic` brings the Ethernet driver up through the stock
  package flow in a dedicated `driver_runtime` sandbox — a second
  nested `init` inside `system`, sibling of `pkg_runtime`, whose config
  `sponge_pkgd` also owns. The split is the minimum-privilege boundary:
  hardware capabilities (`Platform`, `Dma`, `Irq`, `Io_mem`, `Io_port`)
  are granted by a generated `<parent-provides>` list that no
  application package can reach.
- `pc_nic` is verified end to end in QEMU (installed into
  `driver_runtime`, binds the e1000, leases `10.0.2.15`); `usb_net`,
  `pc_wifi`, `intel_gpu` and `pc_intel_fb` build and install but are
  deliberately **not** autostarted — no QEMU model exists for their
  hardware, so starting them silently would be a claim we cannot back.
- **The display driver is a package too:** `fb_vesa` (BIOS/VESA) and
  `fb_boot` (UEFI/GOP) are ordinary driver packages that the media's
  bake manifest seeds, so the boot display arrives through
  `driver_runtime` instead of a privileged Tier-0 node, and switching
  display drivers is `vct remove <old> && vct install <new>` — verified
  at runtime, with the composited screen coming back on the other
  driver (`SPONGE_FB_SWITCH=on`). A second fb driver running at the
  same time is refused by `sponge_pkgd` as `exclusive-conflict`.
- Gates: `run/sponge-pkg-driver.run` (install + exclusivity),
  `run/sponge-desktop-disk.run` and `run/sponge-desktop-disk-uefi.run`
  (the display, and the runtime switch with `SPONGE_FB_SWITCH=on`).
