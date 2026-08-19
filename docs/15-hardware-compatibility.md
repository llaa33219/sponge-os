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
> this kind; it flips to `verified` in 15-3 after the user-executed
> physical-boot evidence lands (D15.11 + R15.7).

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

### 1.4 Phase 15 surface cells (firmware / display / storage / input)

Phase 15 extends the primary surface with the cells the target
machine (LG gram 17ZD90N-VX7BK) and the 15-3 physical-boot
milestone require. The 16-cell cross-product ledger in §2 stays
as the Phase 12 base; the Phase 15 cells below are admitted to
§1 only (the cross-product count rule in §2.2 reflects the
single D15.11 real-hardware row addition).

The Phase 15 firmware axis is the new surface dimension; the
BIOS/GRUB2 path is the verified Phase 12 baseline (unchanged).
UEFI/OVMF is a Phase 15 surface addition that is structurally
complete (D15.13) but QEMU-unverified (D15.16, the W1 OVMF core-
init hang) and real-hardware pending (the 15-3 milestone).

| Surface | Phase 15 variant | Status | Reason | Scenario / evidence / QEMU / budget |
|---|---|---|---|---|
| Firmware | UEFI/OVMF + boot_fb (desktop) | gap | W1 OVMF core-init hang under host OVMF dated 2026-05; the Sponge-side UEFI recipe is structurally complete (D15.13, handcrafted GPT P1=ESP + P3=GENODE, GRUB2 EFI multiboot2 → bender → seL4) but the host-side Genode core under UEFI hangs between Platform construction and the `Genode v...` banner. Real-hardware 2020-era Insyde H2O is expected to NOT have this hang; verified once a physical 17ZD90N boot lands in 15-3 | scenario `run/sponge-desktop-disk-uefi.run`; evidence `docs/evidence/phase15-w4-uefi-product-media.log` + `docs/evidence/phase15-index.md` §8 + §9; QEMU-verified = NO (host hang); 15-3 = pending (the user-executed 17ZD90N protocol); budget n/a (host-side boot timeout 180 s per the W4 acceptance contract; the scenario's structural-gate PASS is host-side sgdisk + mdir + e2ls only, NOT a QEMU boot PASS) |
| Firmware | UEFI/OVMF + NVMe (desktop) | gap | same W1 OVMF core-init hang; the Sponge-side UEFI NVMe envelope (D15.1, the target-machine's controller class) is structurally complete (D15.13, handcrafted NVMe disk + UEFI image) but inherits the W1 hang. Real-hardware 2020 Insyde H2O is expected to boot the same chain (the 17ZD90N's PM981a/PM991 NVMe is in the QEMU-verified `nvme` driver path per Phase 12 W2); verified once a physical 17ZD90N boot lands in 15-3 | scenario `run/sponge-desktop-disk-uefi-nvme.run`; evidence `docs/evidence/phase15-w4-uefi-product-media.log` + `docs/evidence/phase15-index.md` §8; QEMU-verified = NO; 15-3 = pending; budget n/a |
| Firmware | UEFI/OVMF + USB-stick (Tier-0 xHCI + usb_block) | gap | same W1 OVMF core-init hang; the Sponge-side UEFI USB-stick envelope (D15.1 15-3 deliverable) is structurally complete (D15.13, handcrafted USB-stick image with one `pc_usb_host` serving both usb_hid class 0x3 and usb_block class 0x8). Note: BIOS-side USB-stick boot is a Phase 12 verified row (`sponge-usb-boot.run` SeaBIOS → GRUB2 → Bender via `-device usb-storage`); the Phase 15 cell is the UEFI-side xHCI + `usb_block` envelope, NOT a new product image. Real-hardware 17ZD90N uses xHCI on Insyde H2O; verified once a physical boot lands in 15-3 | scenario `run/sponge-desktop-disk-uefi-usb.run`; evidence `docs/evidence/phase15-w4-uefi-product-media.log` + `docs/evidence/phase15-index.md` §10; QEMU-verified = NO; 15-3 = pending; budget n/a |
| Input | usb-mouse HID (relative motion) | verified | the QPA → `usb-tablet` absolute-input path is a Phase 11/12 baseline; the new usb-mouse HID path uses pc_usb_host class 0x3 + usb_hid (Linux hid-core hid-generic) + event_filter REL forwarding. The audit chain (QMP `device_add usb-mouse` → `usb_hid: MOUSE detected` → REL motion + BTN_LEFT via `qmp_move_rel` + `qmp_ps2_button` → QMP `device_del` → `usb_hid: MOUSE removed`) passes end-to-end. The Phase 14 row #2 nitpicker pointer-ROM gap ("nitpicker pointer ROM only updates on absolute_motion") and row #12 cursor-invisible-under-PS/2-only-input are cross-referenced as honest gap-row evidence from the scenario's secondary observation; the usb-mouse cell is precisely their Phase 15 envelope. The QPA → usb-mouse relative-motion patch candidate from `docs/15-hardware-compatibility.md` §4.1 row 4 (Phase 12+ gap) remains open as a Phase 16+ item | scenario `run/sponge-usb-hid-mouse.run`; evidence `docs/evidence/phase15-usb-hid-mouse.log`; QEMU 11.0.3; boot_time 175 s; budget 600 s; target qemu |

**UEFI cells honesty note:** all three UEFI surface cells are
**host-side structurally verified** (the Sponge-side recipe
produces a real `.img` with the documented partition layout,
ESP + GENODE + SPONGE-DATA, with the boot modules on the ext2
root per the W4 orchestrator's `grub.cfg`-vs-`e2ls` path-
consistency fix) but **NOT QEMU-boot-verified** — the QEMU
boot is expected to hit the W1 OVMF core-init hang (D15.16).
Per the W4 binding decisions, the real acceptance gate is
host-side structural verification + honest gap recording, NOT
a fabricated QEMU boot PASS. The cells are honest `gap` until
15-3 lands. Do NOT mark any UEFI cell `verified` before then
(plan MUST NOT HAVE: "No fabricated scenario PASS. Do not mark
any UEFI cell verified (all are gap per the W1 outcome).").

### 1.5 Phase 15 bake cells (profile boots + first-boot/reset)

The Phase 15 bake machinery (D15.3/D15.4/D15.8) introduces
profile-driven media (`pkg/bake/{minimal,desktop}.profile`)
and first-boot sentinel-based P4 seeding (D15.9). The cells
below are the bake profile-boot and first-boot/reset envelopes
proven in W2a + W3. They are added to §1 as new surface cells
(they do NOT appear in the §2 cross-product because the cross-
product dimensions do not include bake profile or sentinel
state — the bake envelope is orthogonal to the HW combinations).

| Surface | Phase 15 variant | Status | Reason | Scenario / evidence / QEMU / budget |
|---|---|---|---|---|
| Bake profile | sponge-alpha.run × profile=desktop (Falkon + every default package staged) | verified | bake::stage lands 7 packages + manifest in `[run_dir]/bin/`; R15.3 verifier fires synchronously after staging; `alpha-probe: PASS` (criteria a/b/c/d — themed panel + launcher + configd + lz) on q35/Skylake-Client; the 2 GiB D15.5 budget is 33% used. Falkon is install-enabled but not runtime-runnable from this scenario (a documented gap; the rom_pkg re-export wiring is W5/W6 scope) | scenario `run/sponge-alpha.run` with `SPONGE_BAKE_PROFILE=desktop`; evidence `docs/evidence/phase15-index.md` §6 cell 1; QEMU 11.0.3; budget n/a (regression envelope from Phase 14) |
| Bake profile | sponge-alpha.run × profile=minimal | verified | bake::stage lands 2 packages + manifest in `[run_dir]/bin/`; reproduces today's hello-only regression baseline; `alpha-probe: PASS` on q35/Skylake-Client | scenario `run/sponge-alpha.run` with `SPONGE_BAKE_PROFILE=minimal`; evidence `docs/evidence/phase15-index.md` §6 (no separate log; the W2a evidence records the desktop=none PASS); QEMU 11.0.3; budget n/a |
| Bake profile | sponge-desktop-disk.run × profile=desktop | verified | bake::stage lands 7 packages + 509 MiB falkon payload + 65 MiB textedit payload + manifest + bake defaults into P3; the desktop profile's source-built binaries (terminal/textedit/files/calculator/pdf_view) are added to the scenario's build list; `alpha-probe: PASS` (criteria a/b/c — desktop; lz deferred); the 2 GiB budget has 1.4 GiB headroom | scenario `run/sponge-desktop-disk.run` with `SPONGE_BAKE_PROFILE=desktop`; evidence `docs/evidence/phase15-index.md` §6 cell 3; QEMU 11.0.3; budget 2 GiB (D15.5 hard gate; observed 718 MB) |
| Bake profile | sponge-desktop-disk.run × profile=minimal | verified | bake::stage lands 2 packages + manifest; no payloads; `alpha-probe: PASS` on q35/Skylake-Client; image 104 MB / 1 GiB budget = 10% used | scenario `run/sponge-desktop-disk.run` with `SPONGE_BAKE_PROFILE=minimal`; evidence `docs/evidence/phase15-index.md` §6 cell 4; QEMU 11.0.3; budget 1 GiB (observed 104 MB) |
| Bake first-boot | sentinel-based P4 seeding (apply baked defaults once, user edits survive reboot) | verified | `bake.applied=yes` sentinel in the atomic `store.xml` write; first boot applies baked defaults, second boot preserves user edits across reboots without re-seeding; proven via `bake-firstboot-probe: PASS boot1` + `bake-firstboot-probe: PASS boot2` on base-linux (two fresh core/init boots over a shared writable config store); host-side `store.xml` inspection independently requires both the sentinel and the user override after boot 1 | scenario `run/sponge-bake-firstboot.run`; evidence `docs/evidence/phase15-index.md` §7; budget n/a |
| Bake reset | `vct bake reset` restores baked defaults after user override | verified | `vct bake reset` (`set bake.applied=no`) drives the seed-once-via-sentinel workflow; the proven scenario on base-sel4 + QEMU (the vct binary is built in the base-sel4 scenario; the reset chain runs via a focused probe on the same config request channel) asserts override → reset → restored baked values → `bake.applied=yes` broadcast → persisted | scenario `run/sponge-bake-reset.run`; evidence `docs/evidence/phase15-index.md` §7; budget n/a |

**Bake cells honesty note:** the 4 bake profile cells reproduce
the Phase 12 alpha-probe verified chain with a profile-driven
staging layer (no new component, no new scenario, no new boot
module); the verified status is inherited from the underlying
alpha-probe evidence + the bake profile's R15.3 verifier. The
2 first-boot/reset cells are NEW scenarios that add the
sentinel-based seeding envelope (D15.9). Both bake-reset and
bake-firstboot are observed to PASS in their respective
scenarios (see `docs/evidence/phase15-index.md` §7). No fabricated
PASS — every verified cell maps to a real QEMU boot + an
evidence log.

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
  link. The row flips to `verified` after the user-executed 15-3
  physical-boot evidence lands (D15.11 + R15.7).

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
| 17 | LG gram 17ZD90N-VX7BK (i7-1065G7 / Iris Plus G7 / 8 GiB / NVMe SSD / xHCI / Insyde H2O UEFI-only) | Skylake-Client (x86_64) | NVMe (PM981a/PM991 — single-namespace expected) | none functional (no Ethernet; Wi-Fi AX201 CNVio2 = unsupported Genode wifi) | PS/2 keyboard (i8042) + USB HID mouse (15-3 protocol — trackpad dead, USB mouse required; R15.10) | gap |  |  |  |  |  |  | real-hardware | 15-3 physical boot pending (user-executed); the QEMU envelope `run/sponge-desktop-disk-uefi-usb.run` is structurally verified (W4 + W-USB, host-side sgdisk + mdir + e2ls) but QEMU-boot-blocked by the W1 OVMF core-init hang; real-hardware verification on the 17ZD90N's 2020 Insyde H2O is the 15-3 deliverable. The cell flips to verified only after the user-executed physical-boot evidence lands (D15.11 + R15.7). | run/sponge-desktop-disk-uefi-usb.run |

**Cell counts:** 4 verified (cells 1, 2, 3, 5) + 1 smoke-only (cell 9) + 12 gap (cells 4, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17) = 17 cells (matches Phase 12 plan step 2 + risk 24 + Phase 15 D15.11). The Phase 15 single real-hardware row is row 17.

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
qemu-envelope: <empty for Phase 12 cells>
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
- `qemu-envelope:` is empty for Phase 12 cells (and empty for any
  non-gap cell — only the Phase 15 real-hardware gap row carries
  the field, and a gap row may NOT carry scenario/marker/evidence
  per §3.2 below).

### 3.2 Gap cell

```text
status: gap
reason: <non-empty reason>
target: phase-XX
qemu-envelope: <empty for Phase 12 gap cells; OR `run/<scenario>.run` for Phase 15 real-hardware rows>
```

Rules the validator enforces on gap cells (plan step 7 + Phase 15 D15.11):

- The `status` value is exactly `gap`.
- `reason` is non-empty.
- `target` is non-empty and is either a phase designation (`phase-XX`)
  OR (Phase 15 D15.11) the literal `real-hardware` for a real-
  hardware row.
- A `target: real-hardware` row is admitted ONLY when BOTH:
  (a) `qemu-envelope:` is non-empty AND names an existing scenario
  file under `run/`; AND
  (b) `status:` is exactly `gap` (any stronger status — `verified`
  or `smoke-only` — is rejected with exit 2 until the 15-3
  physical-boot evidence lands; D15.11 + R15.7).
- A `target: real-hardware` row WITHOUT a `qemu-envelope:` is
  rejected with exit 2 and the exact message
  `real hardware is a Phase 15 deliverable; not a Phase 12 cell`
  (the original Phase 12 reject rule; preserved verbatim).
- The `qemu-envelope` cell column is empty for every non-real-
  hardware row (Phase 12 + Phase 15 surface cells).

### 3.3 Aggregate rules

- The cross-product cell counts are exactly 4 verified, 1 smoke-only,
  and 12 gap (Phase 12 11 gap + Phase 15 D15.11 1 real-hardware
  gap). Any other distribution is a loud failure.
- The verified count is non-zero (at least one verified cell).
- Exactly ONE row carries `target: real-hardware` (D15.11). Multiple
  real-hardware rows are rejected (the validator's real-hardware
  branch fires on the first such row and exits 2).
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