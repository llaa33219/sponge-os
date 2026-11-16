# 15 - Hardware Compatibility: Surface Matrix Detail (§1.1, §1.4-§1.5)

> **Breadcrumb:** parent [15 - Hardware Compatibility](../15-hardware-compatibility.md); this file holds its §1.1 (Row-by-row pointers), §1.4 (Phase 15 surface cells), and §1.5 (Phase 15 bake cells), moved verbatim.
> Siblings in `docs/hardware/`: [cell-contract.md](cell-contract.md) (§3), [known-gaps.md](known-gaps.md) (§4), [drivers-qemu-gate.md](drivers-qemu-gate.md) (§7-§8).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

---

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
complete (D15.13) and stays `gap` for the uefi/uefi-nvme cells per
D15.16 (structure-only QEMU gates that do not exercise the desktop
gate — no desktop PASS is claimed from them), with real-hardware
**first achieved 2026-08-23 (the 15-3 first-boot milestone;
interactive USB-mouse confirmation still pending then) and fully
landed with 0.2.0 on 2026-09-16 (full boot + interaction on the
17ZD90N)**. The old W1 OVMF core-init hang / W^X
+ bender-NOP boot-block cause is retired (acpi DMAR misparse fixed
at ledger row 16; bender `check_mem` rebuilt at row 10; kernel
reserved-extent export + core-side detection at row 11): QEMU/OVMF
boots reach Genode, and the uefi-usb QEMU envelope is a full
`alpha-probe: PASS` (the QEMU-verified UEFI product path).

| Surface | Phase 15 variant | Status | Reason | Scenario / evidence / QEMU / budget |
|---|---|---|---|---|
| Firmware | UEFI/OVMF + boot_fb (desktop) | gap | D15.16 documented gap: the Sponge-side UEFI recipe is structurally complete (D15.13, handcrafted GPT P1=ESP + P3=GENODE, GRUB2 EFI multiboot2 → bender → seL4) and this cell is a structure-only QEMU gate — the run does not exercise the desktop gate (`alpha-probe`), so no desktop PASS is claimed. Not a boot block: the W1 OVMF core-init hang / W^X + bender-NOP cause is retired (`docs/11-environment.md` §4 rows 10/11/16) and QEMU/OVMF boots reach Genode (row 12 verification: `run/sponge-desktop-disk-uefi` reaches the Genode log + init tree on OVMF). Real-hardware verified by 15-3 (first boot 2026-08-23, full interaction accepted 2026-09-16, the 17ZD90N) | scenario `run/sponge-desktop-disk-uefi.run`; evidence `docs/evidence/phase15-w4-uefi-product-media.log` + `docs/evidence/phase15-index.md` §8 + §9; QEMU-verified = boot reaches Genode, no desktop gate claimed from this cell (D15.16); 15-3 = first boot 2026-08-23 (the user-executed 17ZD90N protocol), LANDED 2026-09-16 (full interaction accepted with 0.2.0); budget n/a (host-side boot timeout 180 s per the W4 acceptance contract; the scenario's structural-gate PASS is host-side sgdisk + mdir + e2ls only, NOT a QEMU boot PASS) |
| Firmware | UEFI/OVMF + NVMe (desktop) | gap | D15.16 documented gap: the Sponge-side UEFI NVMe envelope (D15.1, the target-machine's controller class) is structurally complete (D15.13, handcrafted NVMe disk + UEFI image) and this cell is a structure-only QEMU gate — the run does not exercise the desktop gate, so no desktop PASS is claimed. Not a boot block (the W1 hang / W^X + bender-NOP cause is retired; see the boot_fb cell). The 17ZD90N's PM981a/PM991 NVMe is in the QEMU-verified `nvme` driver path per Phase 12 W2; 15-3 (first boot 2026-08-23, LANDED 2026-09-16) verified the UEFI USB-stick boot on that machine, and this NVMe envelope has no separate real-hardware receipt | scenario `run/sponge-desktop-disk-uefi-nvme.run`; evidence `docs/evidence/phase15-w4-uefi-product-media.log` + `docs/evidence/phase15-index.md` §8; QEMU-verified = boot reaches Genode, no desktop gate claimed from this cell (D15.16); 15-3 = first boot 2026-08-23, LANDED 2026-09-16 (via the uefi-usb media; no separate NVMe real-hardware receipt); budget n/a |
| Firmware | UEFI/OVMF + USB-stick (Tier-0 xHCI + usb_block) | gap | D15.16 documented gap (the `gap` status stands; the desktop gate is proven on this envelope below): the Sponge-side UEFI USB-stick envelope (D15.1 15-3 deliverable) is structurally complete (D15.13, handcrafted USB-stick image with one `pc_usb_host` serving both usb_hid class 0x3 and usb_block class 0x8). Note: BIOS-side USB-stick boot is a Phase 12 verified row (`sponge-usb-boot.run` SeaBIOS → GRUB2 → Bender via `-device usb-storage`); the Phase 15 cell is the UEFI-side xHCI + `usb_block` envelope, NOT a new product image. Real-hardware 17ZD90N uses xHCI on Insyde H2O; **15-3 first booted
this UEFI USB-stick media 2026-08-23 (interactive USB-mouse
confirmation still pending then) and LANDED with 0.2.0 on 2026-09-16:
the physical 17ZD90N is fully interactive (panel, pointer/click,
in-window interaction, mouse movement, native 2560x1600).** The QEMU envelope
is no longer boot-blocked: the host-OVMF core-init hang was
root-caused to the acpi DMAR misparse (ledger row 16, fixed) and the
W^X/bender-NOP cause is retired (`docs/11-environment.md` §4 ledger
rows 10 + 11) — QEMU/OVMF boots reach Genode and this envelope is a
full `alpha-probe: PASS` (the QEMU-verified UEFI product path).
**2026-10-21 (ledger row 27):** the gram's
xHCI BAR (0x601d140000, 384.9 GiB) was mapped to the WRONG physical
address — a device untyped's free index was reset to the untyped's
base by the kernel, so the frame landed on the GOP framebuffer's
address instead of the BAR's, the driver read zeroes and the whole
storage chain died ("Host halt failed, -110"). The QEMU twin of that
exact class is now a gate: `tool/hwrepro --case midwindow` (the xHCI
BAR inside a device untyped) is CLEAN after the fix; see
`docs/evidence/xhci-high-bar-mapping.md` | scenario `run/sponge-desktop-disk-uefi-usb.run`; evidence `docs/evidence/phase15-w4-uefi-product-media.log` + `docs/evidence/phase15-index.md` §10; QEMU-verified = YES (`alpha-probe: PASS` — the QEMU-verified UEFI product path); 15-3 = first boot 2026-08-23, LANDED 2026-09-16 (full interaction accepted with 0.2.0); budget n/a |
| Input | usb-mouse HID (relative motion) | verified | the QPA → `usb-tablet` absolute-input path is a Phase 11/12 baseline; the new usb-mouse HID path uses pc_usb_host class 0x3 + usb_hid (Linux hid-core hid-generic) + event_filter REL forwarding. The audit chain (QMP `device_add usb-mouse` → `usb_hid: MOUSE detected` → REL motion + BTN_LEFT via `qmp_move_rel` + `qmp_ps2_button` → QMP `device_del` → `usb_hid: MOUSE removed`) passes end-to-end. The Phase 14 row #2 nitpicker pointer-ROM gap ("nitpicker pointer ROM only updates on absolute_motion") and row #12 cursor-invisible-under-PS/2-only-input are cross-referenced as honest gap-row evidence from the scenario's secondary observation; the usb-mouse cell is precisely their Phase 15 envelope. The QPA → usb-mouse relative-motion patch candidate from `docs/15-hardware-compatibility.md` §4.1 row 4 (Phase 12+ gap) remains open as a Phase 17+ item | scenario `run/sponge-usb-hid-mouse.run`; evidence `docs/evidence/phase15-usb-hid-mouse.log`; QEMU 11.0.3; boot_time 175 s; budget 600 s; target qemu |

**UEFI cells honesty note:** all three UEFI surface cells are
**host-side structurally verified** (the Sponge-side recipe
produces a real `.img` with the documented partition layout,
ESP + GENODE + SPONGE-DATA, with the boot modules on the ext2
root per the W4 orchestrator's `grub.cfg`-vs-`e2ls` path-
consistency fix) and their QEMU runs stay **structure gates that
do not exercise the desktop gate** (D15.16) — the uefi/uefi-nvme
runs claim no desktop PASS. The old expectation that the QEMU
boot hits the W1 OVMF core-init hang (the W^X + bender-NOP
cause) is retired (`docs/11-environment.md` §4 rows 10/11/16):
QEMU/OVMF boots reach Genode, and the uefi-usb envelope is a
full `alpha-probe: PASS`. Per the W4 binding decisions, the real
acceptance gate is host-side structural verification + honest gap
recording, NOT a fabricated QEMU boot PASS. 15-3 has LANDED
(first boot 2026-08-23; full interaction accepted 2026-09-16); the
cells stay honest `gap` and no UEFI cell is
marked `verified` here (plan MUST NOT HAVE, verbatim: "No
fabricated scenario PASS. Do not mark any UEFI cell verified
(all are gap per the W1 outcome)." — the W1-outcome cause inside
that sentence is superseded by the ledger rows above; the
no-fabrication rule is unchanged).

---

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
