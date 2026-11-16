# 15 - Hardware Compatibility

> Status: hand-curated public hardware contract (Phase 12 base +
> Phase 15 W5 addendum; `docs/plans/phase12-hardware.md` §W5,
> `docs/plans/phase15-real-hardware-boot.md` §"Hardware Matrix Contract").
>
> **Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells.**
> **Phase 15 addendum (D15.11): +1 gap cell (the single 17ZD90N real-
> hardware row, `target: real-hardware` with `qemu-envelope:`).**
> **Phase 15 surface cells (§1.4 + §1.5): +6 verified cells (4 bake
> profile boots + 2 first-boot/reset) + 3 gap cells (UEFI/boot_fb,
> UEFI+NVMe, UEFI+USB-stick).**
> **Combined cross-product (validator-enforced): 4 verified,
> 1 smoke-only, 12 gap = 17 cells.**
>
> This document is **human-curated**. `tool/hw_compat.mojo assert`
> validates it read-only; there is no `generate`, `update`, or
> auto-population path (plan risk 23, plan step 6). Every non-gap cell
> is backed by a scenario under `run/`, an evidence log under
> `docs/evidence/phase12-*.log`, `docs/evidence/phase15-*.log`, or
> `docs/evidence/phase15-index.md`, a measured `boot_time_seconds`, a
> declared `budget_seconds`, and the `QEMU emulator version 11.0.3`
> queried before every run (`docs/evidence/task-0-phase12-baseline.md`
> §"Effective PC board default").
>
> **Real hardware is admitted under D15.11 (Phase 15 W5) ONLY as a
> `gap` row that carries a `qemu-envelope:` link to an existing
> scenario; no other `target: real-hardware` form is accepted.** The
> single real-hardware row (LG gram 17ZD90N-VX7BK, status gap, qemu-
> envelope `run/sponge-desktop-disk-uefi-usb.run`) is the only row of
> this kind; its 15-3 physical-boot evidence started 2026-08-23
> (first boot; interactive USB-mouse confirmation still pending
> then) and LANDED 2026-09-16
> (0.2.0, full interaction on the 17ZD90N); the cell remains `gap`
> per D15.11(b) (the gate admits real-hardware rows only as `gap`)
> and D15.16's documented-gap discipline. The old "QEMU envelope
> boot-blocked" reason is retired: the acpi DMAR misparse is fixed
> (ledger row 16), the W^X/bender-NOP cause is retired by ledger
> rows 10 + 11, and QEMU/OVMF boots reach Genode — the uefi-usb
> envelope is a full `alpha-probe: PASS`.

---

> **Document structure.** The row-by-row matrix detail (§1.1), the
> Phase 15 surface cells (§1.4), and the bake cells (§1.5) moved
> verbatim into `docs/hardware/surface-matrix.md`; §3 moved verbatim
> into `docs/hardware/cell-contract.md`; §4 moved verbatim into
> `docs/hardware/known-gaps.md`; §7-§8 moved verbatim into
> `docs/hardware/drivers-qemu-gate.md`. Each numbered stub below is
> the map: it names the file and section that now holds the content,
> so every `docs/15-hardware-compatibility.md §N` reference still
> resolves. The status summary above, the primary 5x5 matrix, the
> mandated texts (§1.2-§1.3), the complete 17-cell cross-product
> ledger (§2), and §5-§6 plus §9 stay in this file:
> `tool/hw_compat.mojo assert` reads the §2 heading, the ledger
> table, and the summary strings here, and the mandated texts
> promise to appear verbatim in this document.

---

## 1. Primary 5×5 surface matrix

The primary matrix is the user-facing summary. Each non-gap pointer
below the table expands to the cell contract fields (status, scenario,
marker, evidence, QEMU, boot_time_seconds, budget_seconds, target).

| Surface | Current q35/Skylake baseline | Phase-12 variant | Status summary | Scenario/evidence/QEMU/budget |
|---|---|---|---|---|
| Machine | q35 | i440fx IDE smoke | verified + smoke-only | pointers |
| CPU | `Skylake-Client` | no additional CPU | verified + explicit gap | pointers |
| Storage | AHCI | NVMe, multi-disk, BIOS-side USB media | verified/smoke-only/verified | pointers |
| NIC | iPXE/e1000 baseline | `pc_nic`/e1000 | verified + explicit non-e1000 gaps | pointers |
| Input | PS/2 + usb-tablet | usb-kbd | verified + usb-mouse/`i2c_hid` gaps | pointers |

### 1.1 Row-by-row pointers

Content moved verbatim to [`docs/hardware/surface-matrix.md`](hardware/surface-matrix.md) §1.1.

### 1.2 Mandated exact texts (verbatim per plan step 3)

These three texts appear unchanged in this document and in the run
scenarios themselves (the run-scenario comments cite the same plan
source).

> **USB row (risk 1) — Storage / BIOS-side USB media:**
> USB boot = product media bootable as a USB stick on QEMU via
> `-device usb-storage` (BIOS side). Genode-side `usb_block` reads USB
> block devices AFTER `image.elf` is loaded; not a boot-path claim.

> **NIC row (risks 7/17) — NIC / pc_nic/e1000:**
> pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested.

> **Summary row (risk 24):**
> **Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells.**

### 1.3 Mandated honest-gap sentences (verbatim per plan step 3)

> **Physical USB gap (risk 12):**
> Physical USB boot: NOT YET VERIFIED (Phase 15 target).

> **UEFI footgun note (risk 6):**
> UEFI + NIC is a known footgun; the Phase 12 NIC verification is
> on BIOS path only.

---

### 1.4 Phase 15 surface cells (firmware / display / storage / input)

Content moved verbatim to [`docs/hardware/surface-matrix.md`](hardware/surface-matrix.md) §1.4.

### 1.5 Phase 15 bake cells (profile boots + first-boot/reset)

Content moved verbatim to [`docs/hardware/surface-matrix.md`](hardware/surface-matrix.md) §1.5.

---

## 2. 17-cell cross-product ledger

The cross-product is a separate ledger from §1's primary matrix.
Plan step 2 / risk 24: do not imply the cross-product was run when
only a focused scenario proved one concern. The ledger below maps
**2 machines × 1 CPU × 2 storage × 2 NIC × 2 input = 16** Phase 12
cells plus the Phase 15 D15.11 **single real-hardware row** for the
17ZD90N-VX7BK target machine (row 17), totaling **17 cells**:
4 verified, 1 smoke-only, and 12 gap cells. Plan risk 24 requires
this exact 4/1/12 count plus a non-zero verified count.

Tuple dimensions (Phase 12 base):

- **Machine** ∈ {q35, i440fx}
- **CPU** = Skylake-Client (single value)
- **Storage** ∈ {AHCI, NVMe} — `multi-disk` and `BIOS-side USB media`
  are sub-pointers of the AHCI cell, not separate cross-product cells
- **NIC** ∈ {ipxe/e1000, pc_nic/e1000}
- **Input** ∈ {PS/2+usb-tablet, usb-kbd}

Phase 15 D15.11 addition (single row, OUTSIDE the combo):

- **Row 17**: a dedicated cell for the LG gram 17ZD90N-VX7BK target
  machine. The cross-product dimensions are NOT extended; the row
  carries the actual machine name in the **Machine** column and the
  literal `target: real-hardware` in the **Target** column. Status
  is **gap** with a `qemu-envelope: run/sponge-desktop-disk-uefi-usb.run`
  link. D15.11 + R15.7 admit the row only as `gap` (the
  `tool/hw_compat` gate rejects a stronger status outright), and it
  keeps that status: 15-3 LANDED 2026-09-16 (first boot 2026-08-23)
  and the row was not
  flipped to `verified` — the D15.16 documented-gap discipline
  stands.

Each row below is one cell with the full cell contract fields. Empty
cells in the non-gap columns are gap cells (no fabricated scenario,
no fabricated PASS marker). The `Reason` column is non-empty only
for gap cells (per plan step 7 / risk 23 + risk 24). The Phase 15
`qemu-envelope` column is non-empty ONLY for the Phase 15
real-hardware row; for every other row it is empty. The validator
enforces both rules (no fabricated `qemu-envelope`, no
`real-hardware` without `qemu-envelope`).

| # | Machine | CPU | Storage | NIC | Input | Status | Scenario | Marker | Evidence | QEMU | boot_time_seconds | budget_seconds | Target | Reason | qemu-envelope |
|---:|---|---|---|---|---|---|---|---|---|---:|---:|---:|---|---|---|---|
| 1 | q35 | Skylake-Client | AHCI | ipxe/e1000 | PS/2+tablet | verified | run/sponge-boot.run | boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.") | docs/evidence/task-0-phase12-baseline.md | 11.0.3 | 13 | 60 | qemu |  |  |
| 2 | q35 | Skylake-Client | AHCI | ipxe/e1000 | usb-kbd | verified | run/sponge-usb-kbd-via-qmp.run | sponge-usb-kbd-via-qmp: PASS (QMP hotplug audit chain: device_add -> KEYBOARD detected -> device_del -> KEYBOARD removed -> send-key dispatched) | docs/evidence/phase12-usb-kbd.log | 11.0.3 | 436 | 900 | qemu |  |  |
| 3 | q35 | Skylake-Client | AHCI | pc_nic/e1000 | PS/2+tablet | verified | run/sponge-pc-nic.run | sponge-pc-nic: PASS (pc_nic bound e1000, nic_router acquired DHCP 10.0.2.15) | docs/evidence/phase12-pc-nic.log | 11.0.3 | 21 | 300 | qemu |  |  |
| 4 | q35 | Skylake-Client | AHCI | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines pc_nic (driver/nic/pc) with usb-kbd HID input on AHCI storage; the two chains are disjoint capability chains and were never bridged in a single boot |  |
| 5 | q35 | Skylake-Client | NVMe | ipxe/e1000 | PS/2+tablet | verified | run/sponge-desktop-disk-nvme.run | alpha-probe: PASS | docs/evidence/phase12-desktop-nvme.log | 11.0.3 | 46 | 900 | qemu |  |  |
| 6 | q35 | Skylake-Client | NVMe | ipxe/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines NVMe storage with usb-kbd HID input; the W4 input scenario uses a CDROM-boot ISO, not the NVMe driver path |  |
| 7 | q35 | Skylake-Client | NVMe | pc_nic/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines NVMe storage with pc_nic driver stack; the W3 scenario uses a CDROM-boot ISO, not the NVMe driver path |  |
| 8 | q35 | Skylake-Client | NVMe | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines NVMe + pc_nic + usb-kbd; all three Phase-12 driver chains are disjoint and were never bridged in a single boot |  |
| 9 | i440fx | Skylake-Client | AHCI | ipxe/e1000 | PS/2+tablet | smoke-only | run/sponge-boot-i440fx.run | boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.") | docs/evidence/phase12-boot-i440fx.log | 11.0.3 | 11 | 60 | qemu |  |  |
| 10 | i440fx | Skylake-Client | AHCI | ipxe/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines i440fx (PIIX4 IDE) with usb-kbd HID input; the W4 input scenario pins q35/Skylake-Client and the i440fx smoke does not start usb_hid |  |
| 11 | i440fx | Skylake-Client | AHCI | pc_nic/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines i440fx with pc_nic; pc_nic's DDE-Linux platform-driver chain requires q35 enumeration, not PIIX4 IDE |  |
| 12 | i440fx | Skylake-Client | AHCI | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines i440fx + pc_nic + usb-kbd; i440fx + pc_nic alone is already gap and the usb-kbd input adds a second disjoint chain |  |
| 13 | i440fx | Skylake-Client | NVMe | ipxe/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | i440fx does not auto-attach NVMe and Phase 12 has no i440fx + NVMe product path (plan D12.2 / §"Verified Ground Truth") |  |
| 14 | i440fx | Skylake-Client | NVMe | ipxe/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no i440fx + NVMe + usb-kbd combo exists; Phase 12 only verifies i440fx IDE and q35 NVMe as disjoint single-concern smokes |  |
| 15 | i440fx | Skylake-Client | NVMe | pc_nic/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | no i440fx + NVMe + pc_nic combo exists; pc_nic requires q35 enumeration and i440fx lacks NVMe auto-attach |  |
| 16 | i440fx | Skylake-Client | NVMe | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no i440fx + NVMe + pc_nic + usb-kbd combo exists; all three Phase-12 driver chains are disjoint from the i440fx PIIX4 IDE topology |  |
| 17 | LG gram 17ZD90N-VX7BK (i7-1065G7 / Iris Plus G7 / 8 GiB / NVMe SSD / xHCI / Insyde H2O UEFI-only) | Skylake-Client (x86_64) | NVMe (PM981a/PM991 — single-namespace expected) | none functional (no Ethernet; Wi-Fi AX201 CNVio2 = unsupported Genode wifi) | PS/2 keyboard (i8042) + USB HID mouse (15-3 protocol — trackpad dead, USB mouse required; R15.10) | gap |  |  |  |  |  |  | real-hardware | **15-3 first achieved 2026-08-23 (physical boot; interactive USB-mouse confirmation still pending then) and LANDED with 0.2.0 on 2026-09-16: physical boot + full interaction (panel, pointer/click, in-window, mouse movement, native 2560x1600) verified on the 17ZD90N.** The cell stays `gap` per D15.11(b) (the gate admits real-hardware rows only as `gap`) and D15.16's documented-gap discipline. Its QEMU envelope (`run/sponge-desktop-disk-uefi-usb.run`) is no longer boot-blocked: the host-OVMF core-init hang was root-caused to the acpi DMAR misparse (ledger row 16, fixed) and the W^X/bender-NOP cause is retired (`docs/11-environment.md` §4 rows 10 + 11) — QEMU/OVMF boots reach Genode and the envelope is a full `alpha-probe: PASS` (the QEMU-verified UEFI product path). **2026-10-21 (ledger row 27):** a SECOND real-hardware defect on this board is named, fixed and gated in QEMU — the board's xHCI BAR (0x601d140000, 384.9 GiB) was mapped to the wrong physical address (the kernel reset a device untyped's free index to its base, so the frame landed on the GOP framebuffer's address; the driver read zeroes → "Host halt failed, -110" → the storage chain died → the supervisor kill-looped). The QEMU twin of that class is `tool/hwrepro --case midwindow` (the xHCI BAR inside a device untyped): it REPRODUCED the field signature before the fix and is CLEAN after it. The panel itself still needs one reflash-and-eyeball run (the laptop has no serial). | run/sponge-desktop-disk-uefi-usb.run |

**Cell counts:** 4 verified (cells 1, 2, 3, 5) + 1 smoke-only (cell 9) + 12 gap (cells 4, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17) = 17 cells (matches Phase 12 plan step 2 + risk 24 + Phase 15 D15.11). The Phase 15 single real-hardware row is row 17.

### 2.1 Sub-pointer map (BIOS-side USB media + multi-disk)

The cross-product cell #1 (`q35 + Skylake-Client + AHCI + ipxe/e1000 + PS/2+tablet`) is also the parent of two Phase-12 sub-pointers documented in §1's Storage row. These sub-pointers do not get separate cross-product cells (the cross-product dimensions do not include BIOS-handoff or disk-count), but they are recorded as evidence on the parent cell.

- Sub-pointer `multi-disk` → scenario `run/sponge-boot-multidisk.run`, evidence `docs/evidence/phase12-boot-multidisk.log`, status **smoke-only** (parent cell #1 is verified; the smoke-only annotation is per the storage-ordering sub-variant, not the AHCI storage class).
- Sub-pointer `BIOS-side USB media` → scenario `run/sponge-usb-boot.run`, evidence `docs/evidence/phase12-usb-boot.log`, status **verified**. The validator additionally requires this evidence file to contain the literal `BIOS-side USB boot verified` (plan step 7 / risk 1 + risk 12 mitigation).

---

## 3. Cell contract format (validated by `tool/hw_compat.mojo assert`)

Content moved verbatim to [`docs/hardware/cell-contract.md`](hardware/cell-contract.md) §3.

---

## 4. Known gaps (Phase 12 delta — no Phase-11 §11.3 copy-paste)

Content moved verbatim to [`docs/hardware/known-gaps.md`](hardware/known-gaps.md) §4.

---

## 5. Validator contract

`tool/hw_compat.mojo` is the read-only validator for this document.
It exposes exactly two subcommands (plan step 6 / risk 23):

```text
mojo tool/hw_compat.mojo assert         # validate this document; exit 0 on GREEN
mojo tool/hw_compat.mojo help           # usage only
```

There is no `generate`, `update`, or repository-writing subcommand.
The thin launcher `tool/hw_compat` mirrors `tool/patches` and
forwards to `tool/hw_compat.mojo`. The `verify` path of
`tool/build.mojo` runs `./tool/patches verify` then
`./tool/hw_compat assert` sequentially and propagates either
failure.

The validator's exact rules are documented in §3 above and are
exercised by six negative fixtures (one per failure class) recorded
in `docs/evidence/task-5-phase12-hw-compat.md` under `var/`.

---

## 6. Cross-references

- Plan: `docs/plans/phase12-hardware.md` §"W5: Hardware compatibility
  document + assert-only validator" (lines 687-803).
- Prior workstreams: `docs/evidence/task-0-phase12-baseline.md` (W0),
  `docs/evidence/task-1-phase12-platform.md` (W1),
  `docs/evidence/task-2-phase12-storage.md` (W2),
  `docs/evidence/task-3-phase12-pc-nic.md` (W3),
  `docs/evidence/task-3b-phase12-launch-click.md` (W3b),
  `docs/evidence/task-4-phase12-usb.md` (W4).
- W6 (final) workstream: `docs/evidence/task-6-phase12-regression.md`
  + `docs/evidence/phase12-index.md` (the evidence index) +
  `docs/evidence/phase12-fresh-build.log` (the fresh-build proof)
  + `docs/evidence/phase12-envelope-*.log` (the W6 serialized sweep
  per-scenario envelopes; 14 distinct runs covering all six new
  Phase-12 scenarios plus the 3 W3b launch-click runs plus the 8 W0
  envelope scenarios, with one re-run of `sponge-desktop-disk`
  recorded honestly per the W6 plan §"Honest about every attempt").
- Per-scenario run logs: `docs/evidence/phase12-boot-i440fx.log`,
  `docs/evidence/phase12-boot-multidisk.log`,
  `docs/evidence/phase12-desktop-nvme.log`,
  `docs/evidence/phase12-pc-nic.log`, `docs/evidence/phase12-usb-boot.log`,
  `docs/evidence/phase12-usb-kbd.log`.
- Validator evidence: `docs/evidence/task-5-phase12-hw-compat.md`.

---

## 7. QEMU hardware-matrix gate (Phase 15 pre-real-hw hardening, 2026-09-13)

Content moved verbatim to [`docs/hardware/drivers-qemu-gate.md`](hardware/drivers-qemu-gate.md) §7.

---

## 8. Driver packages — per-driver verification state

Content moved verbatim to [`docs/hardware/drivers-qemu-gate.md`](hardware/drivers-qemu-gate.md) §8.

---

## 9. Phase 16 runtime field note (user-attested, 2026-11-07)

A standalone note, deliberately outside the §2 17-cell
cross-product: it is not a cross-product cell, it is not assigned to
the row-17 LG gram entry, and no cell status moves. The counts stay
exactly **4 verified, 1 smoke-only, 12 gap = 17 cells**.

Recorded as a **USER-ATTESTED real-hardware field report**, **not an
independently reproduced agent test**: as of 2026-11-07 the user
reports that, on a physical machine, the system ran, memory-pressure
freezing was prevented, an error was actually displayed on the panel,
and normal operation continued afterward.

Provenance (as attested): parent HEAD `79d70902f5`, Genode pin
`9088c7227c`, the canonical UEFI/USB desktop image
`var/dist/sponge-os-0.2.0-x86_64-sel4.img`, SHA-256
`a907d733f726a7789f6f54944aba3b552579e6ee3c798878e1b4e3636b17fcfa`.
That image independently passed the OVMF/QEMU alpha-probe before
staging.

Honest limits: the report includes no serial log, machine model,
repeat count, timing, or memory measurements, so it supports the
observed degradation/recovery behavior but is not a blanket hardware
certification. Nothing here changes a QEMU matrix row or a documented
partial: the §1 surface cells, the §2 ledger (4/1/12), and the
`docs/evidence/phase16-runtime-sweep.md` §3 sweep matrix all stand
unchanged. Full record: `docs/evidence/phase16-runtime-acceptance.md`
§7 + `docs/evidence/phase16-runtime-sweep.md` §14.
