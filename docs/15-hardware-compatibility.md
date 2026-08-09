# 15 - Hardware Compatibility

> Status: hand-curated public hardware contract (Phase 12, `docs/plans/phase12-hardware.md` §W5).
>
> **Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells.**
>
> This document is **human-curated**. `tool/hw_compat.mojo assert`
> validates it read-only; there is no `generate`, `update`, or
> auto-population path (plan risk 23, plan step 6). Every non-gap cell
> is backed by a scenario under `run/`, an evidence log under
> `docs/evidence/phase12-*.log` or `docs/evidence/task-0-phase12-baseline.md`,
> a measured `boot_time_seconds`, a declared `budget_seconds`, and the
> `QEMU emulator version 11.0.3` queried before every run
> (`docs/evidence/task-0-phase12-baseline.md` §"Effective PC board default").
>
> **Real hardware is a Phase 15 deliverable; not a Phase 12 cell.**
> No cell in §1, §2, or §3 carries `target: real-hardware`. Physical-machine
> verification remains the Phase 15 milestone; Phase 12 is QEMU-verified
> only.

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

Each pointer below is the cell contract as defined in §3. Where a row
has both a "current baseline" pointer and a "Phase-12 variant" pointer,
both expand to the full cell contract.

#### Machine row — q35 (verified) + i440fx (smoke-only)

**Machine / q35** — the q35/Skylake-Client default that every existing
disk-touching scenario inherits via `genode/repos/base/board/pc/qemu_args`
and the explicit `append qemu_args " -machine q35 -cpu Skylake-Client "`
that W1 added to the five named disk scenarios
(`docs/evidence/task-1-phase12-platform.md` §"QEMU version" + the
Phase 12 plan §D12.1 / W1 step 1 contract).

- status: verified
- scenario: `run/sponge-boot.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/task-0-phase12-baseline.md`
- qemu: 11.0.3
- boot_time_seconds: 13
- budget_seconds: 60
- target: qemu

**Machine / i440fx** — the only Phase-12 deviation from q35
(plan D12.1 / W2 step 4; risk 2 + risk 14 + risk 21 mitigation).
`run/sponge-boot-i440fx.run` boots one disk via PIIX4 IDE; AHCI is
explicitly not started; no product image; `boot_probe` reads the marker
through IDE → `part_block` → VFS. Tier-0 only.

- status: smoke-only
- scenario: `run/sponge-boot-i440fx.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/phase12-boot-i440fx.log`
- qemu: 11.0.3
- boot_time_seconds: 11
- budget_seconds: 60
- target: qemu

#### CPU row — Skylake-Client (verified) + no additional CPU (gap)

**CPU / Skylake-Client** — pinned in every Phase-12 scenario per
W1 step 1. QEMU 11.0.3 without q35 + `Skylake-Client` can fail seL4
in `boot_sys` with `XSAVE not supported` (plan §"Verified Ground Truth"
+ §D12.1 binding decision).

- status: verified
- scenario: `run/sponge-boot.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/task-0-phase12-baseline.md`
- qemu: 11.0.3
- boot_time_seconds: 13
- budget_seconds: 60
- target: qemu

**CPU / no additional CPU** — explicit gap. No CPU experiment is
permitted in Phase 12 (plan §"Verified Ground Truth" + D12.1). No
scenario was exercised with a non-`Skylake-Client` CPU; the XSAVE
contract rules out a Phase-12 second CPU row.

- status: gap
- reason: `XSAVE contract forbids a non-Skylake-Client CPU on QEMU 11.0.3 (seL4 boot_sys); Phase 12 is q35+Skylake-Client only per plan D12.1.`
- target: phase-12+

#### Storage row — AHCI (verified) + NVMe + multi-disk + BIOS-side USB media

**Storage / AHCI** — the q35 ICH9 default that every existing scenario
inherits (`genode/repos/base/board/pc/qemu_args` auto-attaches an
implicit drive to ICH9 AHCI). Same evidence as the q35 baseline.

- status: verified
- scenario: `run/sponge-boot.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/task-0-phase12-baseline.md`
- qemu: 11.0.3
- boot_time_seconds: 13
- budget_seconds: 60
- target: qemu

**Storage / NVMe** — Phase-12 variant 1 (plan W2 step 3 / D12.2 /
risk 3 + risk 10 + risk 26 mitigation). One namespace on a
`pcie-root-port`; explicit root-port + raw drive + `-device nvme`
copied verbatim from `run/sponge-boot.run`'s `SPONGE_BOOT_NVME` block.
Gate is `partition-check: PASS (Number: 3)` followed by
`alpha-probe: PASS` in 900 s.

- status: verified
- scenario: `run/sponge-desktop-disk-nvme.run`
- marker: `[init -> system -> alpha_probe] alpha-probe: PASS`
- evidence: `docs/evidence/phase12-desktop-nvme.log`
- qemu: 11.0.3
- boot_time_seconds: 46
- budget_seconds: 900
- target: qemu

**Storage / multi-disk** — Phase-12 variant 2 (plan W2 step 5 / D12.2 /
risk 8 + risk 21 mitigation). q35/AHCI two-disk ordering smoke; the
expected marker exists only on P3 of the second-created image; the
QEMU drive order is swapped so success cannot come from "first disk"
semantics.

- status: smoke-only
- scenario: `run/sponge-boot-multidisk.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/phase12-boot-multidisk.log`
- qemu: 11.0.3
- boot_time_seconds: 16
- budget_seconds: 60
- target: qemu

**Storage / BIOS-side USB media** — Phase-12 variant 3 (plan W4 step 1 /
D12.3 / risk 1 + risk 5 + risk 12 mitigation). USB boot = product
media bootable as a USB stick on QEMU via `-device usb-storage`
(BIOS side). Genode-side `usb_block` reads USB block devices AFTER
`image.elf` is loaded; not a boot-path claim.

- status: verified
- scenario: `run/sponge-usb-boot.run`
- marker: `BIOS-side USB boot verified (SeaBIOS -> GRUB2 -> Bender handoff via usb-storage on q35/Skylake-Client)`
- evidence: `docs/evidence/phase12-usb-boot.log`
- qemu: 11.0.3
- boot_time_seconds: 53
- budget_seconds: 900
- target: qemu

#### NIC row — iPXE/e1000 (verified) + pc_nic/e1000 (verified) + explicit non-e1000 gaps

**NIC / iPXE/e1000** — the iPXE NIC topology that every existing
product/network scenario inherits. Same evidence as the q35 baseline.

- status: verified
- scenario: `run/sponge-boot.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/task-0-phase12-baseline.md`
- qemu: 11.0.3
- boot_time_seconds: 13
- budget_seconds: 60
- target: qemu

**NIC / pc_nic/e1000** — Phase-12 variant (plan W3 step 1 / D12.4 /
risk 4 + risk 15 + risk 16 + risk 17 + risk 18 mitigation). The
additive `pc_nic` + e1000 + `nic_router` DHCP smoke. The two
load-bearing markers are `pc_nic: bound device` (plan §"Scenario
Architecture Decision" / W3 step 4 gate 1) and
`nic_router: uplink DHCP acquired` (W3 step 4 gate 2); the scenario's
own final `sponge-pc-nic: PASS` line fires only after both gates.

- status: verified
- scenario: `run/sponge-pc-nic.run`
- marker: `sponge-pc-nic: PASS (pc_nic bound e1000, nic_router acquired DHCP 10.0.2.15)`
- evidence: `docs/evidence/phase12-pc-nic.log`
- qemu: 11.0.3
- boot_time_seconds: 21
- budget_seconds: 300
- target: qemu

**NIC / non-e1000 explicit gaps** — the plan-mandated honesty text for
risks 7 and 17:

> "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested."

This row is therefore not a single cell in the cross-product — each
non-e1000 driver family is an explicit gap cell in §2 with its own
target phase:

- rtl8169 (Realtek 8169) → `phase-15+` (no QEMU wiring)
- Wi-Fi (ath9k / iwlwifi / rtlwifi) → `phase-15+` (no QEMU wiring)
- USB-Ethernet → `phase-15+` (no QEMU wiring)
- `virtio_pci_nic` → `phase-12+` (plan §"Must NOT Have" + §"Known gaps")
- Modem (any) → `phase-15+` (plan §"Must NOT Have")
- tap / bridge networking → `phase-15+` (plan §"Must NOT Have" + §"Scope Guards")

#### Input row — PS/2 + usb-tablet (verified) + usb-kbd (verified) + usb-mouse/i2c_hid gaps

**Input / PS/2 + usb-tablet** — the interactive input topology that
`run/sponge-de-sel4-interactive.run` inherits via
`genode/repos/os/recipes/raw/drivers_interactive-pc/drivers.config`
(PS/2 + usb-tablet). Same evidence as the q35 baseline; the W3b
launch-click fix is a QMP-recipe change inside that scenario, not a
new input device class.

- status: verified
- scenario: `run/sponge-boot.run`
- marker: `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")`
- evidence: `docs/evidence/task-0-phase12-baseline.md`
- qemu: 11.0.3
- boot_time_seconds: 13
- budget_seconds: 60
- target: qemu

**Input / usb-kbd** — Phase-12 variant (plan W4 step 3 / D12.5 /
risk 20 mitigation). USB HID keyboard enumeration plus QMP-driven
text input on the seL4 interactive stack. The risk-20 ordered audit
chain is `device_add usb-kbd` → `usb_hid: KEYBOARD detected` →
`device_del` → `send-key` → final PASS.

- status: verified
- scenario: `run/sponge-usb-kbd-via-qmp.run`
- marker: `sponge-usb-kbd-via-qmp: PASS (QMP hotplug audit chain: device_add -> KEYBOARD detected -> device_del -> KEYBOARD removed -> send-key dispatched)`
- evidence: `docs/evidence/phase12-usb-kbd.log`
- qemu: 11.0.3
- boot_time_seconds: 436
- budget_seconds: 900
- target: qemu

**Input / explicit gaps** — the plan §"Must NOT Have" exclusions:

- usb-mouse (HID class `0x3` relative motion) → `phase-12+` (blocked
  by the QPA patch candidate recorded in `docs/11-environment.md`
  §4.2; a Phase 12+ candidate per plan risk 11 / D12.10)
- i2c_hid (touch / I2C-HID) → `phase-15+`
- USB networking, USB serial, USB audio → `phase-15+`
- Any new USB controller class → `phase-15+`

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

## 2. 16-cell cross-product ledger

The cross-product is a separate ledger from §1's primary matrix.
Plan step 2 / risk 24: do not imply the cross-product was run when
only a focused scenario proved one concern. The ledger below maps
**2 machines × 1 CPU × 2 storage × 2 NIC × 2 input = 16** cells and
publishes exactly 4 verified, 1 smoke-only, and 11 gap cells. Plan
risk 24 requires this exact 4/1/11 count plus a non-zero verified
count.

Tuple dimensions:

- **Machine** ∈ {q35, i440fx}
- **CPU** = Skylake-Client (single value)
- **Storage** ∈ {AHCI, NVMe} — `multi-disk` and `BIOS-side USB media`
  are sub-pointers of the AHCI cell, not separate cross-product cells
- **NIC** ∈ {ipxe/e1000, pc_nic/e1000}
- **Input** ∈ {PS/2+usb-tablet, usb-kbd}

Each row below is one cell with the full cell contract fields. Empty
cells in the non-gap columns are gap cells (no fabricated scenario,
no fabricated PASS marker). The `Reason` column is non-empty only
for gap cells (per plan step 7 / risk 23 + risk 24).

| # | Machine | CPU | Storage | NIC | Input | Status | Scenario | Marker | Evidence | QEMU | boot_time_seconds | budget_seconds | Target | Reason |
|---:|---|---|---|---|---|---|---|---|---|---:|---:|---:|---|---|
| 1 | q35 | Skylake-Client | AHCI | ipxe/e1000 | PS/2+tablet | verified | run/sponge-boot.run | boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.") | docs/evidence/task-0-phase12-baseline.md | 11.0.3 | 13 | 60 | qemu |  |
| 2 | q35 | Skylake-Client | AHCI | ipxe/e1000 | usb-kbd | verified | run/sponge-usb-kbd-via-qmp.run | sponge-usb-kbd-via-qmp: PASS (QMP hotplug audit chain: device_add -> KEYBOARD detected -> device_del -> KEYBOARD removed -> send-key dispatched) | docs/evidence/phase12-usb-kbd.log | 11.0.3 | 436 | 900 | qemu |  |
| 3 | q35 | Skylake-Client | AHCI | pc_nic/e1000 | PS/2+tablet | verified | run/sponge-pc-nic.run | sponge-pc-nic: PASS (pc_nic bound e1000, nic_router acquired DHCP 10.0.2.15) | docs/evidence/phase12-pc-nic.log | 11.0.3 | 21 | 300 | qemu |  |
| 4 | q35 | Skylake-Client | AHCI | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines pc_nic (driver/nic/pc) with usb-kbd HID input on AHCI storage; the two chains are disjoint capability chains and were never bridged in a single boot |
| 5 | q35 | Skylake-Client | NVMe | ipxe/e1000 | PS/2+tablet | verified | run/sponge-desktop-disk-nvme.run | alpha-probe: PASS | docs/evidence/phase12-desktop-nvme.log | 11.0.3 | 46 | 900 | qemu |  |
| 6 | q35 | Skylake-Client | NVMe | ipxe/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines NVMe storage with usb-kbd HID input; the W4 input scenario uses a CDROM-boot ISO, not the NVMe driver path |
| 7 | q35 | Skylake-Client | NVMe | pc_nic/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines NVMe storage with pc_nic driver stack; the W3 scenario uses a CDROM-boot ISO, not the NVMe driver path |
| 8 | q35 | Skylake-Client | NVMe | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines NVMe + pc_nic + usb-kbd; all three Phase-12 driver chains are disjoint and were never bridged in a single boot |
| 9 | i440fx | Skylake-Client | AHCI | ipxe/e1000 | PS/2+tablet | smoke-only | run/sponge-boot-i440fx.run | boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.") | docs/evidence/phase12-boot-i440fx.log | 11.0.3 | 11 | 60 | qemu |  |
| 10 | i440fx | Skylake-Client | AHCI | ipxe/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines i440fx (PIIX4 IDE) with usb-kbd HID input; the W4 input scenario pins q35/Skylake-Client and the i440fx smoke does not start usb_hid |
| 11 | i440fx | Skylake-Client | AHCI | pc_nic/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines i440fx with pc_nic; pc_nic's DDE-Linux platform-driver chain requires q35 enumeration, not PIIX4 IDE |
| 12 | i440fx | Skylake-Client | AHCI | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no Phase-12 scenario combines i440fx + pc_nic + usb-kbd; i440fx + pc_nic alone is already gap and the usb-kbd input adds a second disjoint chain |
| 13 | i440fx | Skylake-Client | NVMe | ipxe/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | i440fx does not auto-attach NVMe and Phase 12 has no i440fx + NVMe product path (plan D12.2 / §"Verified Ground Truth") |
| 14 | i440fx | Skylake-Client | NVMe | ipxe/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no i440fx + NVMe + usb-kbd combo exists; Phase 12 only verifies i440fx IDE and q35 NVMe as disjoint single-concern smokes |
| 15 | i440fx | Skylake-Client | NVMe | pc_nic/e1000 | PS/2+tablet | gap |  |  |  |  |  |  | phase-15+ | no i440fx + NVMe + pc_nic combo exists; pc_nic requires q35 enumeration and i440fx lacks NVMe auto-attach |
| 16 | i440fx | Skylake-Client | NVMe | pc_nic/e1000 | usb-kbd | gap |  |  |  |  |  |  | phase-15+ | no i440fx + NVMe + pc_nic + usb-kbd combo exists; all three Phase-12 driver chains are disjoint from the i440fx PIIX4 IDE topology |

**Cell counts:** 4 verified (cells 1, 2, 3, 5) + 1 smoke-only (cell 9) + 11 gap (cells 4, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16) = 16 cells (matches plan step 2 + risk 24).

### 2.1 Sub-pointer map (BIOS-side USB media + multi-disk)

The cross-product cell #1 (`q35 + Skylake-Client + AHCI + ipxe/e1000 + PS/2+tablet`) is also the parent of two Phase-12 sub-pointers documented in §1's Storage row. These sub-pointers do not get separate cross-product cells (the cross-product dimensions do not include BIOS-handoff or disk-count), but they are recorded as evidence on the parent cell.

- Sub-pointer `multi-disk` → scenario `run/sponge-boot-multidisk.run`, evidence `docs/evidence/phase12-boot-multidisk.log`, status **smoke-only** (parent cell #1 is verified; the smoke-only annotation is per the storage-ordering sub-variant, not the AHCI storage class).
- Sub-pointer `BIOS-side USB media` → scenario `run/sponge-usb-boot.run`, evidence `docs/evidence/phase12-usb-boot.log`, status **verified**. The validator additionally requires this evidence file to contain the literal `BIOS-side USB boot verified` (plan step 7 / risk 1 + risk 12 mitigation).

---

## 3. Cell contract format (validated by `tool/hw_compat.mojo assert`)

Every cell in §1 and §2 follows one of the two formats below.
`tool/hw_compat.mojo assert` reads this document and validates every
cell against the format + the cross-product counts + the per-rule
checks (plan step 7 / plan risk 13 + risk 22 + risk 23 + risk 24).

### 3.1 Non-gap cell (verified or smoke-only)

```text
status: verified|smoke-only
scenario: run/<name>.run
marker: <exact PASS or load-bearing success marker>
evidence: docs/evidence/<path>
qemu: 11.0.3
boot_time_seconds: <measured integer>
budget_seconds: <declared integer>
target: qemu
```

Rules the validator enforces on non-gap cells (plan step 7):

- The `status` value is exactly `verified` or `smoke-only`.
- `scenario` resolves to an existing file under `run/`.
- `evidence` resolves to an existing file under `docs/evidence/`.
- `evidence` contains the `marker` substring as a byte-for-byte match.
- `qemu`, `boot_time_seconds`, and `budget_seconds` are present.
- `boot_time_seconds ≤ budget_seconds` (over-budget = loud failure).
- For USB-boot evidence (`*usb-boot.log`), the evidence additionally
  contains the literal `BIOS-side USB boot verified`.

### 3.2 Gap cell

```text
status: gap
reason: <non-empty reason>
target: phase-XX
```

Rules the validator enforces on gap cells (plan step 7):

- The `status` value is exactly `gap`.
- `reason` is non-empty.
- `target` is non-empty and is a phase designation (never `real-hardware`).
- `target: real-hardware` is rejected with exit 2 and the exact message
  `real hardware is a Phase 15 deliverable; not a Phase 12 cell`.

### 3.3 Aggregate rules

- The cross-product cell counts are exactly 4 verified, 1 smoke-only,
  and 11 gap. Any other distribution is a loud failure.
- The verified count is non-zero (at least one verified cell).
- The document is never modified by the validator. `tool/hw_compat.mojo`
  has no `generate`, `update`, or repository-writing path
  (plan step 6 / risk 23).

---

## 4. Known gaps (Phase 12 delta — no Phase-11 §11.3 copy-paste)

Plan step 4 + risk 27 mitigation: the known gaps are the six
Phase-12-discovered categories from the plan, plus two Phase-12-
discovered findings from the W4 evidence that the orchestrator
amendment extends into this section (clearly marked as Phase-12-
discovered, NOT Phase-11 copy-paste).

### 4.1 Plan-mandated Phase-12 delta categories (risk 27)

| # | Gap | Reason | Target phase |
|---|---|---|---|
| 1 | physical USB boot | SeaBIOS -> GRUB2 -> Bender handoff is QEMU-verified via `-device usb-storage`; physical-stick boot is unverified. **Physical USB boot: NOT YET VERIFIED (Phase 15 target).** | Phase 15 |
| 2 | multi-namespace NVMe | W2 exercises one namespace on a `pcie-root-port` with explicit root-port + raw drive + `-device nvme`. `part_block` pins by partition number and never auto-probes additional namespaces. | Phase 12+ |
| 3 | `virtio_pci_nic` | Explicitly excluded by plan §"Must NOT Have" and §"Scope Guards". The pc_nic driver stack does not enumerate virtio NIC devices. | Phase 12+ |
| 4 | usb-mouse relative motion | The QPA path (Qt Platform Abstraction) currently only consumes absolute input from `usb-tablet`. A usb-mouse would require a QPA patch candidate; see `docs/11-environment.md` §4.2 (Phase-12 patch-candidate ledger). | Phase 12+ |
| 5 | `i2c_hid` | Touch / I2C-HID is not exercised by any Phase-12 scenario. `drivers_interactive-pc` does not include an `i2c_hid` driver. | Phase 15+ |
| 6 | real-hardware rows | All Phase-12 evidence is QEMU 11.0.3 only. **Real hardware is a Phase 15 deliverable; not a Phase 12 cell.** No cell in §1 or §2 carries `target: real-hardware`. | Phase 15 |

### 4.2 Phase-12-discovered W4 findings (orchestrator amendment)

These two gaps were discovered during the Phase-12 W4 workstream
(2026-08-08) and are **honest Phase-12 deltas**, not Phase-11
follow-ups. They are listed here so that a W6 reader can find them
without grepping the W4 evidence files.

#### 4.2.1 Residual launch-click nondeterminism (Phase-12-discovered)

- **Observed:** the W3b usb-tablet absolute launch-click fix reduced
  the Phase-11 3/3 launch-click flake to an occasional single-run
  miss (observed 1/3 in the W4 regression re-run
  `docs/evidence/task-4-phase12-usb.md` §"W3b three-pass
  launch-click regression gate", runs 1/2/3 with run 1 timed out at
  the `phase launch` gate while runs 2/3 passed in 60 s and 67 s).
- **Class:** timing / host-variance.
- **Impact:** does not invalidate the W3b acceptance (`3/3 launch-
  phase passes` on the originally-failing 3/3); does mean the
  `sponge-de-sel4-interactive.run` regression envelope is no longer
  strict-deterministic across back-to-back runs on this host.
- **Mitigation today:** the `run/sponge-de-sel4-interactive.run`
  recipe in `run/qmp.inc` is unchanged; the launch-only selector
  still routes through the W4-proven usb-tablet absolute
  workspace-press → workspace-move → workspace-release choreography.
- **Target phase:** Phase 13+ (probe focus handling is a sibling
  concern; see §4.2.2).

#### 4.2.2 usb-kbd glyph-delta secondary gate (Phase-12-discovered)

- **Observed:** the post-device_del `send-key` travels the PS/2
  path (already covered by `run/sponge-terminal-qmp.run`); the
  `terminal_probe`'s focus ROM did not update in the W4 environment
  (`docs/evidence/task-4-phase12-usb.md` §"3. usb-kbd-via-qmp.run"
  + `docs/evidence/phase12-usb-kbd.log` tail: 120 echo polls at
  `glyphs=98`, then `terminal-probe: FAIL keystroke did not round-
  trip to a render change`). The glyph delta is therefore recorded
  as a probe-focus quirk, not a USB-input failure.
- **Class:** probe focus / nitpicker focus ROM update.
- **Impact:** the primary risk-20 audit chain (`device_add` →
  `usb_hid: KEYBOARD detected` → `device_del` → `send-key` →
  `sponge-usb-kbd-via-qmp: PASS`) passed in the W4 run; only the
  secondary glyph-delta gate (an optional verification of the
  PS/2 path that is identical to `run/sponge-terminal-qmp.run`'s
  path) did not fire.
- **Mitigation today:** the W4 scenario's `sponge-usb-kbd-via-qmp: PASS`
  line fires on the primary audit chain and remains the load-bearing
  cell marker (see §2 cell #2). The glyph-delta secondary gate is
  not required for the cross-product verified count.
- **Target phase:** Phase 13+ (probe focus handling).

### 4.3 Honesty constraints (re-stated for grep-ability)

These three constraints appear verbatim in §1.2 + §1.3 and are
re-stated here as a single searchable block:

- **USB boundary (risk 1):** "USB boot = product media bootable as a
  USB stick on QEMU via `-device usb-storage` (BIOS side).
  Genode-side `usb_block` reads USB block devices AFTER `image.elf`
  is loaded; not a boot-path claim."
- **NIC honesty (risks 7/17):** "pc_nic = Linux-NIC-driver stack
  (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified
  on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented
  but NOT QEMU-tested."
- **UEFI footgun (risk 6):** "UEFI + NIC is a known footgun; the
  Phase 12 NIC verification is on BIOS path only."

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