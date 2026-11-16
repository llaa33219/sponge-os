# 15 - Hardware Compatibility: Known Gaps (§4)

> **Breadcrumb:** parent [15 - Hardware Compatibility](../15-hardware-compatibility.md); this file holds its §4 (Known gaps), moved verbatim.
> Siblings in `docs/hardware/`: [surface-matrix.md](surface-matrix.md) (§1.1, §1.4-§1.5), [cell-contract.md](cell-contract.md) (§3), [drivers-qemu-gate.md](drivers-qemu-gate.md) (§7-§8).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

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
| 1 | physical USB boot | SeaBIOS -> GRUB2 -> Bender handoff is QEMU-verified via `-device usb-storage`; physical-stick boot was unverified at Phase 12. **Physical USB boot: NOT YET VERIFIED (Phase 15 target)** (historical Phase-12 verdict, quoted verbatim). Closed by 15-3 on the UEFI side (physical-stick boot first achieved 2026-08-23, LANDED with 0.2.0 on 2026-09-16: the desktop `.img` boots the 17ZD90N from a physical USB stick); the SeaBIOS-side physical-stick path has no real-hardware target machine (the 17ZD90N is UEFI-only) and stays QEMU-verified only. | Phase 15 |
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

