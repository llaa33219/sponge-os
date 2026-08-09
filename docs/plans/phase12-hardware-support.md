# Phase 12 — Hardware Support Expansion (Pre-Planning Consultation)

> Status: pre-planning (Metis consultation). Created 2026-08-07.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 12* (lines 530–548).
> Prior phase plan: `docs/plans/phase11-de-customization.md`.
> This document is the **input** to Prometheus; it is not itself the
> plan. The numbered **Binding Decisions** are not re-litigated in the
> plan. The **Metis AI Failure Points** are the plan's risk register.

## Goal Restatement (docs/09-roadmap.md §10, Phase 12, verbatim)

The system boots on a wider range of (virtual and physical) platform
configurations, and the supported-hardware surface is **explicit** rather
than accidental. This phase builds the enablement infrastructure;
**Phase 15 is the concrete real-hardware boot milestone.**

Four completion criteria (verbatim):

1. **Boot matrix beyond the current QEMU defaults**: additional machine
   types, AHCI and NVMe storage variants, USB boot media.
2. **Driver set expanded**: networking beyond QEMU slirp (at least one
   **real NIC driver path**), input beyond PS/2 + usb-tablet.
3. **A hardware compatibility document** listing tested configurations
   and known gaps.
4. **Run scenarios cover the new configurations** so regressions fail
   loudly.

Implicit constraint (from the goal text): the matrix is **QEMU-verified**
in Phase 12. Real-hardware row is Phase 15's deliverable, not Phase 12's.
Phase 12's hardware compatibility document is therefore a Phase-12-tested
QEMU matrix with "known gaps" — the real-hardware row is *one* such gap
until Phase 15 advertises it as a row, not a row today.

## Intent Classification

**Type**: Mid-sized (with two architectural forks — storage Tier-0 and
NIC driver stack).
**Confidence**: High.
**Rationale**: the deliverable is bounded (one matrix document, N new
scenarios, one or two patches to vendor where the in-tree-proven path is
missing) but two forks materially change the outcome: (a) whether
**product-media** NVMe replaces AHCI as the default storage, and (b)
whether the "real NIC driver path" claim is **pc_nic** (Linux-backed,
real-hardware-relevant) or **virtio_pci_nic** (modern, virtualized).
Both forks are resolved below as binding decisions.

Pre-analysis ran without escalation because the question set is concrete
and the policy environment (AGENTS.md, three-philosophy constraint, the
9-row patch ledger, the docs/14 §4.4 NVMe note, the doscs/13 §3 XSAVE
pin) is already crisp; no exploration agent was needed.

## Binding Decisions (from Metis, do not re-litigate)

| # | Decision | Phase 12 outcome |
|---|---|---|
| D12.1 | **Machine-type matrix = current q35+Skylake-Client pinned EXPLICIT in every run script, plus ONE additional i440fx (`-machine pc`) variant on the storage-smoke path only.** | Every disk-touching run script (`sponge-boot.run`, `sponge-desktop-disk.run`, `sponge-persist-disk.run`, `sponge-falkon-disk.run`, `sponge-alpha.run`) gets `append qemu_args " -machine q35 -cpu Skylake-Client "` before the existing `-nographic -m NG` line. The implicit `genode/repos/base/board/pc/qemu_args` default is **kept** as a safety net, but the explicit pin catches a board-default regression loudly. The single i440fx variant lives in a new `sponge-boot-i440fx.run` (storage-only smoke; mirrors `sponge-boot.run`'s `SPONGE_BOOT_NVME=1` toggle discipline, switches to `SPONGE_BOOT_PC=1`). It does NOT carry the product `.img` (the product stays on q35) and does NOT carry Sponge DE (QEMU's i440fx emulates a different ICH/PIIX4 IDE chain; AHCI is q35's ICH9 auto-attach, while on i440fx the default storage is IDE — `sponge-boot-i440fx.run` explicitly disables the AHCI driver and bounds the smoke to "the Tier-0 chain survives an i440fx topology", not "the product media boots on i440fx"). Different CPU (qemu64/host/core2duo) is **rejected** — docs/13 §3 names the XSAVE failure on QEMU 11.0.2's default; touching the CPU model is a regression direction with no product value. |
| D12.2 | **Storage variants = (a) NVMe replaces AHCI as the Tier-0 product-media storage driver on a per-image flag, (b) one multi-disk AHCI smoke variant on the boot-smoke path, (c) ATAPI on AHCI is DEFERRED.** | `tool/dist` gains `--storage {ahci,nvme}` (default `ahci` to keep current behavior). The flag toggles `<start name="ahci">` vs `<start name="nvme">` in the Tier-0 init config and the corresponding QEMU `-device ahci` / `-device nvme` + `pcie-root-port` plumbing. `sponge-boot.run` already proves the NVMe path as `SPONGE_BOOT_NVME=1`; productionizing it means running `sponge-desktop-disk.run` end-to-end with NVMe (a new `sponge-desktop-disk-nvme.run`, or a `SPONGE_DESKTOP_NVME=1` toggle mirroring the boot smoke) and asserting `alpha-probe: PASS` + the partition pin `<partition number="3"/>` still works on NVMe namespace 1. The multi-disk variant is `sponge-boot-multidisk.run`: a second `(hd1)` image is `dd`'d in the run script, ahci binds both, and `part_block` is asserted to mount P3/P4 from disk 1's pin (positive regression check that the partition-number pin is not silently relying on "first disk"). ATAPI on AHCI is rejected because the Genode ATAPI/cdrom path is not exercised by any current Sponge scenario and adding it is scope creep. |
| D12.3 | **"USB boot media" = the existing `.iso` written to a USB stick and bootable on QEMU via `-device usb-storage` (the (i) interpretation). The Genode-side `usb_block` driver path (the (ii) interpretation) is DEFERRED.** | The `.iso` is the USB-stick image by design (`genode/tool/run/image/iso` docstring: "boot from ISO and USB stick"). A new `sponge-usb-boot.run` smoke wraps the existing `sponge-alpha.run` ISO with `-boot menu=on -device usb-ehci -device usb-storage,drive=stick -drive id=stick,format=raw,file=<iso>,if=none` and asserts `alpha-probe: PASS` from a USB-attached boot. The (ii) interpretation — a Tier-0 chain where **Genode's** `usb_block` driver reads the boot media before `image.elf` is loaded — is **structurally impossible** in the current architecture: `image.elf` is what brings `usb_block` into existence, and image.elf is what USB boot would have to load. That requires the bootloader (GRUB2+bender) to read USB, which is upstream Genode / GRUB2 territory and is firmly out of scope for Phase 12. The Phase 12 "USB boot" claim is therefore precise: the product media is bootable from a USB stick on QEMU (and the dd-to-USB control door in docs/13 §4 becomes a supported bootstrap path, not a "manual experiment"). |
| D12.4 | **Real NIC driver path = `pc_nic` (Linux-NIC-driver stack) with `-device e1000`, replacing `ipxe_nic` on the boot path.** | A new `sponge-pc-nic.run` (seL4, e1000 model, pc_nic driver, nic_router uplink to the same `lwIP` + `fetchurl` configuration that `sponge-net-probe.run` already carries) proves the criterion-2 "beyond QEMU slirp" claim. The honest claim is "Linux-backed NIC driver stack that also drives rtl8169 / ath9k / iiwlwifi / rtlwifi / USB-Ethernet on real hardware" — NOT "we tested those real chips on QEMU" (QEMU only emulates `e1000`, not rtl8169 or any Wi-Fi). The scenario asserts that `pc_nic` binds the QEMU `e1000` device, DHCPs via `nic_router`, and reaches the same `fetchurl` round-trip as `sponge-net-probe.run`. The criterion-2 VERIFIED scope is "the pc_nic driver stack boots and round-trips on QEMU"; the "real hardware" line goes into docs/15 as "tested-elsewhere, not QEMU-verified for non-e1000 models". The NIC topology reshuffle is contained: pc_nic substitutes for ipxe_nic in the existing `sponge-falkon-disk.run` (where the NIC currently comes from ipxe_nic for the falkon HTTP fixture) BEFORE the new scenario is built, so the regression gate is the kept scenario. The netdev backend stays QEMU slirp — AGENTS.md §5.5 forbids the `tap`/`bridge` host-netdev path on privilege grounds. virtio_pci_nic is **rejected** as the primary claim: it is a real driver, but its transport is virtualized; the criterion reads "real NIC driver path", and pc_nic with `-device e1000` is the real-Intel-NIC path. virtio_pci_nic may be added as a tertiary smoke in a later phase if the matrix grows. |
| D12.5 | **Input beyond PS/2 + usb-tablet = `usb-kbd` via `usb_hid` (the keyboard class of HID 0x3), driven by QMP `send-key` end-to-end. usb-mouse and i2c_hid are DEFERRED.** | A new `sponge-usb-kbd-via-qmp.run` (seL4, base-sel4, the same `drivers_interactive-pc` driver stack as `sponge-de-sel4-interactive.run` PLUS `-device usb-kbd` instead of `-device usb-tablet`) sends a known key sequence via QMP → qemu usb-kbd → pc_usb_host → usb_hid → event_filter → sponge-de → terminal/textedit probe, and asserts the glyph delta. The existing `sponge-terminal-qmp.run` and `sponge-textedit-qmp.run` already round-trip QMP keys to focused windows, but they target QEMU's PS/2-emulated keyboard (the implicit `qemu_args` keyboard controller), not the USB path; the new scenario is the first scenario that explicitly dissociates the keyboard from PS/2. usb-mouse (REL motion) is rejected because the Genode QPA's REL→ABS conversion path is broken (the very issue flagged in roadmap §11.2 item 1) — testing usb-mouse now would fail without proving anything new and would risk being misread as a regression. i2c_hid is rejected because QEMU does not emulate the Intel Tiger/Alder Lake I2C HID chipsets that the Genode `pc_i2c_hid` driver targets. The criterion-2 VERIFIED scope is therefore "HID class 0x3 keyboards over USB" — the documented tip of the HID iceberg, with mouse / digitizer / touch screens remaining in the Phase 12+ GAPS column of docs/15. |
| D12.6 | **Hardware compatibility document = `docs/15-hardware-compatibility.md`, a matrix of (machine × CPU × storage × NIC × input) tuples, each cell pointing at a scenario PASS marker + evidence log + QEMU version. Maintained by a new `tool/hw_compat.mojo` host tool.** | The matrix is the public face of Phase 12's "explicit rather than accidental" goal. Every row has at minimum: status (`verified` / `smoke-only` / `gap`) + a scenario file path + an evidence log path under `docs/evidence/phase12-*.log` + the QEMU version the cell was verified against (the `QEMU_VERSION` env var the scenario captured, or the docs/11 §3 host pin). The shipped Phase 12 matrix is 5 rows × 5 columns and grows leftward (new inputs) and downward (new tiers) without breaking the schema. `tool/hw_compat.mojo assert` walks every cell and fails loudly if any cell's evidence pointer is missing OR if any referenced scenario file does not exist. The tool is read-only against the repo (no auto-population); cells are hand-curated. The "real hardware" row is logged as a Phase 15 deliverable, not a Phase 12 row. |
| D12.7 | **Phase 11 follow-ups: INCLUDE §11.3 item 1 (launch-click flake → switch to usb-tablet absolute path); DEFER §11.2 item 1, §11.3 items 2, 3, 4.** | The launch-click flake fix is included because the new `sponge-pc-nic.run` and `sponge-usb-kbd-via-qmp.run` scenarios both verify pointer/click side-effects (a launch click on the launcher uses the same underlying QMP path the flake hit). Including the fix removes the flake-as-mask risk: a Phase 12 regression that breaks the click-to-launch chain would then fail loudly on a deterministic path, not on a 3/3-flake. The other items are deferred: §11.2 item 1 (QPA tablet absolute-motion upstream patch) is a patch-ledger candidate — it would be progressed in a Phase 12 workstream IF the Phase 12 §11.3.1 trace identifies the QPA path as the root cause, but only by adding it to the patch ledger, not by orchestrating the upstream contribution; §11.3 item 2 (themed_decorator live asset re-skin) is a Phase 12+ open question unrelated to the boot matrix; §11.3 item 3 (`panel.position` live move) is a design question, not a Phase 12 bootstrap question; §11.3 item 4 (drag delta hardening) is timing-recipe, NOT blocker. |
| D12.8 | **`image/uefi` is OUT OF SCOPE for Phase 12.** | UEFI is a real-hardware boot path; Phase 15 is its target. Phase 12 stays on BIOS/GRUB2 (the current default). The hybrid MBR/GPT + embedded EFI binaries on the product `.img` already make the media BIOS-bootable today, which is sufficient for the Phase 12 boot matrix. The `power_on/qemu` `-net none` quirk on `image/uefi` is recorded in docs/15 §"Known gaps" as "UEFI + nic is a known footgun; defer to Phase 15 when the UEFI path is exercised for real" so the omission is honest but unblinking. |
| D12.9 | **W0 baseline = the seven current-passing scenarios Phase 12 might regress, captured as a TDD-red gate before any Phase 12 code change.** | `sponge-boot.run` (AHCI NVMe variants), `sponge-desktop-disk.run`, `sponge-persist-disk.run`, `sponge-falkon-disk.run`, `sponge-net-probe.run`, `sponge-de-sel4-interactive.run`, `sponge-launch.run` (the §11.3.1 click target). One scenario per affected Phase 12 criterion, captured to `docs/evidence/task-0-phase12-baseline.md` with the exact final-line marker. The regression envelope is the seven existing PASS markers; the new Phase 12 scenarios gain their own baseline in their respective plan tasks. The W0 capture also asserts the documented QEMU version (`QEMU_VERSION` env var) plus the docs/13 §3 q35+Skylake-Client pin — both of these are also implicit gates on the new scenarios. |
| D12.10 | **No new vendored-tree patches in Phase 12.** | The 9-row ledger is the contract; the in-tree-proven paths (NVMe driver, pc_nic driver, usb_hid, pc_usb_host, usb_block) are sufficient for the Phase 12 matrix. The existing patch-ledger candidates (themed_decorator live asset reload; QPA tablet absolute-motion) STAY candidates and are NOT promoted to committed patches in Phase 12. If a Phase 12 workstream discovers that a Phase 12 criterion CANNOT be met without a vendored-tree patch (e.g. partition pin on NVMe namespace > 1), the patch is RECORDED as a NEW ledger candidate in docs/11 §4.2 with the Phase 12 evidence log link, and the workstream SHIPS without the criterion satisfied — Phase 12's "explicit rather than accidental" goal is met by GAPS, not by patches. |

## Scope Guards

- **No edits to vendored `genode/` tree.** Phase 12 is fully built on
  the in-tree-proven paths. Any patch-ledger candidate from Phase 12
  workstreams is recorded in docs/11 §4.2 but not committed in Phase 12.
- **No product-media NIC topology change.** The product `.img` keeps
  `ipxe_nic` for the falkon HTTP fixture (`sponge-falkon-disk.run`).
  `pc_nic` is a NEW scenario, not a substitution into the product.
- **No real-hardware claims in docs/15.** The matrix is QEMU-verified
  only. The "real hardware" row is a Phase 15 row, not a Phase 12 row.
- **No new machine types beyond i440fx and q35.** virt (QEMU's `-machine virt`
  for ARM/aarch64) is Phase 15+ (kernel-locked to seL4; no aarch64
  board target today). microvm is out of scope. The Phase 12 matrix is
  2 machine types × 1 CPU × 2 storage × 2 NIC × 2 input = 16 cells, the
  intersection of which is the Phase 12 criterion scope.
- **No image/uefi.** D12.8.
- **No new USB controller classes.** The Phase 12 USB input claim is
  keyboards (HID class 0x3 sub-class keyboard). USB mass storage is
  out-of-scope for input (D12.3 covers the storage side as BIOS-side
  boot only). USB networking / USB serial / USB audio are Phase 12+
  gaps.
- **No new patches to the Phase 11 §11.3 follow-up list.** §11.3 item 1
  is INCLUDED (D12.7); the others are DEFERRED, not deprioritized.
- **No automatic doc generation.** docs/15 cells are hand-curated.
  `tool/hw_compat.mojo assert` is the loud-fail check; the matrix is
  not auto-generated from scenario run output (some rows are
  `smoke-only` precisely because the run output is a partial gate).
- **No `git subtree pull` in Phase 12.** The Genode pin stays at 26.05.
  The Phase 12 work does not touch the vendored tree except via the
  patch-ledger candidate mechanism (D12.10).
- **No new host packages.** The Phase 12 toolset uses the existing host
  pins (docs/11 §3) — `qemu-system-x86_64` 11.0.2, `tcl`/`expect`,
  `cmake`/`ninja`, `Mojo` 1.0.0b2 via `uv`. The new `tool/hw_compat.mojo`
  is pure Mojo (no new host package).
- **No TCC / OVMF / EDK2 host dependency.** D12.8.
- **No new `sponge_pkgd` start nodes.** `pc_nic` is a NIC driver, not a
  package runtime; the NIC topology change is per-scenario, not a
  package-level change.

## Verified Ground Truth (two-codebase-inventory-validated — trust, do not re-derive)

- **QEMU args default**:
  `genode/repos/base/board/pc/qemu_args` is exactly 3 lines:
  `-machine q35 -cpu Skylake-Client -net nic,model=e1000,netdev=net0 -netdev user,id=net0`.
  No `.run` script overrides `-machine` or `-cpu`. `append qemu_args` is
  used only for `-nographic`, `-m NG`, `-device nec-usb-xhci,id=xhci
  -device usb-tablet`, and the QMP socket. (`grep -l 'machine q35' run/`
  returns 0 hits in scripts; the default is lossy.)
- **XSAVE failure mode**: docs/13 §3 — without `q35+Skylake-Client`,
  QEMU 11.0.2's default emulates a CPU without XSAVE, and seL4 fails
  early in `boot_sys` with `XSAVE not supported` + `boot_sys failed`.
  Verified 2026-08-07 on QEMU 11.0.2. The pin is part of the
  reproducibility contract.
- **Storage auto-attach by machine type**: q35 auto-attaches the implicit
  `-drive` to ICH9 AHCI. i440fx auto-attaches the implicit `-drive` to
  PIIX4 IDE. This is the reason D12.1 binds the i440fx variant to the
  storage-smoke path only and explicitly disables the AHCI driver in
  `sponge-boot-i440fx.run` (the run script asserts the implicit IDE
  path or an explicit `-device ahci` re-attach — this is the cubic
  microns of the variant's value).
- **NVMe plumbing on q35**: q35 does NOT auto-attach NVMe. The existing
  `SPONGE_BOOT_NVME=1` variant in `sponge-boot.run` uses a
  `pcie-root-port` + `-device nvme` + the `driver/nvme` Genode binary.
  This is the same pattern Phase 12 lifts to the product `.img`.
- **`part_block` partition pin**: docs/14 §4.4 — the config is
  `<partition number="3"/>` and `<partition number="4"/>`, BY NUMBER,
  never auto-probe. The pin is namespace-agnostic on AHCI (only one
  NVMe namespace is typically emulated by QEMU; multi-namespace is a
  Phase 12+ gap). The multi-disk smoke (D12.2) verifies the pin still
  resolves when ahci binds two disks.
- **pc_nic source and deps**:
  `genode/repos/pc/src/driver/nic/pc/` (the source),
  `genode/repos/pc/recipes/pkg/pc_nic/` (the recipe), and the upstream
  `genode/repos/pc/run/pc_nic.run` example. The `pc` repo is NOT in
  `tool/build.mojo`'s managed `REPOSITORIES` block today (it is
  appended ad-hoc for the `pc_nic.run` and other pc-repo runs). Phase 12
  commits `pc` to the managed block. The DDE-Linux build behind pc_nic
  is the same Linux 6.18.19 kernel already pinned for the
  usb_hid/pc_usb_host build (patch-free path).
- **pc_nic capability cost**: the upstream `pc_nic.run` example sizes
  `pc_nic` at `caps: 140 | ram: 16M`. The existing `ipxe_nic` runs in
  `sponge-net-probe.run` at the same order of magnitude (no explicit
  cap-quota cost). The Phase 9 capspace work (ledgers #6, #7) means
  the `caps: 140` is comfortable on the Phase-9 base; a regression
  here would be diagnostic.
- **NIC topology reshuffle**:
  `sponge-falkon-disk.run` currently uses `ipxe_nic` + `nic_uplink`
  for the falkon HTTP fixture. The Phase 12 reshuffle moves the
  product-image NIC to ipxe_nic (unchanged) and adds the NEW scenario
  `sponge-pc-nic.run` as a stand-alone smoke (does NOT modify
  `sponge-falkon-disk.run`). The substitution is "additive, not
  subtractive".
- **Input chain beyond PS/2 + usb-tablet**:
  the existing `drivers_interactive-pc` config set
  (`genode/repos/os/recipes/raw/drivers_interactive-pc/drivers.config`)
  wires `usb_hid` to bind ALL HID class 0x3 devices QEMU exposes
  (`<device class="0x3"/>`). usb_hid already routes both keyboards and
  mice through the same single binary — the existing Phase 10/11
  scenarios only QEMU-emulate `usb-tablet`, so usb_hid only ever sees
  a POINTER. Adding `-device usb-kbd` is one QEMU arg; the Phase 12
  scenario adds the device and asserts usb_hid's `devices` report
  carries a KEYBOARD entry.
- **USB mass storage plumbing**:
  `genode/repos/os/src/driver/usb_block/` is the in-tree Genode driver.
  `genode/repos/os/run/usb_block.run` uses `-M pc` + `-device usb-ehci`
  + `-device usb-storage` + `bios_handoff: no`. The Genode-side block
  driver is fully proven in-tree; the Phase 12 gap is the
  **bootloader-side** USB read (D12.3).
- **Patch-ledger candidates (Phase 12+)**: docs/11 §4.2 has exactly one
  row today (themed_decorator live asset reload). Roadmap §11.3 item 1
  adds a SECOND implicit candidate (the QPA tablet absolute-motion fix).
  Phase 12 may add NEW candidates (e.g. partition pin on NVMe namespace
  > 1, if the criterion cannot be met without it), but does NOT
  promote any candidate to a committed patch.
- **Phase 11 follow-ups flagged for Phase 12**:
  - §11.2 item 1 (QPA tablet absolute-motion) — patch-ledger candidate
    territory; D12.7.
  - §11.3 item 1 (launch-click flake) — INCLUDED in Phase 12 (D12.7).
  - §11.3 item 2 (themed_decorator live asset re-skin) — already a
    §4.2 candidate; deferred.
  - §11.3 item 3 (`panel.position` live move) — design question;
    deferred.
  - §11.3 item 4 (drag delta hardening) — timing-recipe; deferred.
- **Existing `tool/` host tools**: `tool/build.mojo` (managed
  `REPOSITORIES` block), `tool/dist.mojo` (4-partition image builder),
  `tool/mkdata.mojo` (P4 grow/repartition), `tool/test_theme_payload_size.mojo`
  (Phase 11 host gate), `tool/decor_assets.mojo` (Phase 11 theme tar),
  `tool/patches.mojo` (patch ledger read-only helper). The new
  `tool/hw_compat.mojo` mirrors `tool/patches.mojo`'s read-only shape.

## Metis AI Failure Points (Risk Register — encoded as task guardrails)

| # | Trap | Mitigation |
|---|---|---|
| 1 | Claiming "USB boot works" when only the BIOS-side USB-storage path was tested (the (i) interpretation) but the docs claim is misread as "Genode USB block driver boots from USB" (the (ii) interpretation). | Phase 12 claim is hard-coded in `docs/15-hardware-compatibility.md` row text: "USB boot = product media bootable as a USB stick on QEMU via `-device usb-storage` (BIOS side). Genode-side `usb_block` reads USB block devices AFTER `image.elf` is loaded; not a boot-path claim." The `sponge-usb-boot.run` scenario's scenario comment carries the same language verbatim. `tool/hw_compat.mojo assert` checks every USB-boot row's evidence log contains the literal phrase `BIOS-side USB boot verified` (not "Genode USB block boot"). |
| 2 | i440fx silently changing the block controller (IDE auto-attach on `-M pc`) so the AHCI driver probe fails or, worse, silently binds to a different PCI device ID. | `sponge-boot-i440fx.run` explicitly disables the AHCI driver in the boot init config and bounds the smoke to "the Tier-0 chain survives an i440fx topology" — it does NOT attempt to boot the product image. The scenario's W2 acceptance criterion is the boot probe's `boot-probe: PASS` log, which is the same probe `sponge-boot.run` already uses (AVOIDS-AMBUGUITY: same probe code, different topology). The new cell in docs/15 is `smoke-only` not `verified`. |
| 3 | NVMe namespace/partition pinning differences — NVMe uses namespaces; the docs/14 §4.4 `<partition number="3"/>` pin may need namespace-id annotation on multi-namespace NVMe devices. | `sponge-boot-nvme.run` (already exists as `SPONGE_BOOT_NVME=1`) and the new `sponge-desktop-disk-nvme.run` both use the `nvme` QEMU model, which exposes exactly 1 namespace. The Phase 12 docs/15 row for NVMe is "1 namespace verified; multi-namespace is a GAPS entry". Multi-namespace is NOT a Phase 12 goal. The scenario's W3 acceptance criterion is `alpha-probe: PASS` + the partition pin's `Number: 3` byte read (a new check: the probe reads `part_block`'s `Block/P3` report and asserts the partition number field). |
| 4 | pc_nic needing much larger caps/RAM than ipxe_nic — Phase 9 unblocked capspace, but pc_nic's full Linux kernel resume may still push the per-driver cap request into the silent-out-of-caps hang class (the §11.1.1 root cause). | `sponge-pc-nic.run` adopts the Phase 9 sizing from the leaked §11.1 lesson: `pc_nic | caps: 1000 | ram: 32M` (over-provisioned by 7× the upstream example; the upstream `caps: 140` is calibrated for the simpler `pc_nic.run` use case, not the full Linux-driver path). The scenario's W2 acceptance criterion is `pc_nic: bound device` + `nic_router: uplink DHCP acquired` logs — both must appear within the `run_genode_until` timeout, with the timeout raised to 300s to absorb the DDE-Linux cold boot. A silent hang fails the timeout loudly. |
| 5 | Boot time explosions making the regression suite impractical — adding 4+ new scenarios to a sequential-make pipeline (Phase 10 baseline = 600s+ per seL4 desktop scenario). | The Phase 12 plan caps the new seL4 scenarios at 2 (`sponge-pc-nic.run`, `sponge-usb-kbd-via-qmp.run`); the rest (`sponge-boot-i440fx.run`, `sponge-boot-multidisk.run`, `sponge-desktop-disk-nvme.run`, `sponge-usb-boot.run`) are SHORT-FORM scenarios tuned to complete in <60s on base-sel4 (Tier-0 only, no Sponge DE). W0 captures the per-scenario boot time baseline; W6 sweep enforces a per-criterion cell's boot-time budget in docs/15 (e.g. "USB boot smoke ≤ 60s on QEMU 11.0.2"). `tool/hw_compat.mojo assert` ALSO checks every cell's evidence log carries a `boot_time_seconds: <N>` line (parsed from the run log) and fails loudly if any cell exceeds its budget. |
| 6 | The `power_on/qemu` `-net none` quirk on `image/uefi` silently killing the NIC in a hypothetical uefi+nic combo scenario. | D12.8 — `image/uefi` is out of scope. The risk is moot in Phase 12. The quirk is recorded in docs/15 §"Known gaps" as a forward-looking note ("UEFI + NIC is a known footgun; the Phase 12 NIC verification is on BIOS path only"). |
| 7 | "Real NIC driver path" being interpreted as "real-Intel-NIC-on-real-hardware" rather than "real-driver-stack-beyond-iPXE-on-QEMU". | The Phase 12 docs/15 row text is hard-coded: "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested." The `sponge-pc-nic.run` scenario's scenario comment carries the same language. The "real hardware" column for those non-e1000 devices is the Phase 15 gap. |
| 8 | Part Block partition pin BY NUMBER silently relying on "first disk" semantics — when the boot disk changes (multi-disk smoke), the partition pin could miss. | `sponge-boot-multidisk.run` is the explicit positive regression check: ahci binds TWO disks, the same `<partition number="3"/>` pin (the docs/14 §4.4 contract) is asserted to mount from disk 1 (not disk 0 — the order is swapped by the run script to catch "first disk" assumptions). The scenario's W3 acceptance criterion is `boot-probe: PASS` from disk 1 — the probe reads the marker.txt from P3 of the SECOND disk, a check that is ONLY audible if the pin is bound to the second disk. |
| 9 | The `pc` repo not being in the managed `REPOSITORIES` block — `tool/build.mojo` appends `sponge/libports/gems/pc/dde_linux/dde_ipxe` for the existing scenarios, but the new `sponge-pc-nic.run` adds `pc_nic` to the build, and a contributor who runs `tool/build prepare` for the first time after the Phase 12 commit will fail with "no rule to build pc_nic" unless the REPOSITORIES block is updated. | `tool/build.mojo`'s `prepare` step's `REPOSITORIES` block addition is explicit (the `pc` repo is appended for ALL seL4 builds, not just the interactive scenarios). The W1 acceptance criterion for the new scenario is "the scenario's first compile succeeds in a freshly prepared build directory" — not in the build directory the Phase 12 author used. The fresh-build test is documented as the W7 regression check. |
| 10 | NVMe on q35 requires an explicit `pcie-root-port` + `-device nvme` (q35 does NOT auto-attach NVMe like it auto-attaches AHCI drives) — the existing `SPONGE_BOOT_NVME=1` variant in `sponge-boot.run` already does this; the new `sponge-desktop-disk-nvme.run` must mirror the same pattern. | The QEMU args for the new scenario are copy-pasted from the existing `sponge-boot.run` NVMe variant (verified by `grep -A3 SPONGE_BOOT_NVME run/sponge-boot.run` and pasted verbatim). The W3 acceptance criterion is `alpha-probe: PASS` from the NVMe-bound product image. A regression on the NVMe plumbing fails loudly on the boot probe, not at image-build time. |
| 11 | Patch ledger quality — adding row-after-row of candidates without committing any patches pollutes the ledger and dilutes the "Drop When" contract. | The Phase 12 workstream's discovery process adds AT MOST ONE new patch-ledger candidate (and the new candidate is recorded in docs/11 §4.2 with the Phase 12 evidence log link, the "Where" path, the "Why", and the "Drop When" column — the same shape as the existing candidate). If the workstream discovers that no patch is needed (the criterion is met with the in-tree-proven path), NO new candidate is added. The W6 acceptance criterion is "docs/11 §4.2 candidate count ≤ 1 + current-count" — never `current + N`. |
| 12 | The QEMU-claimed "USB boot" being invariably false on a real USB stick — QEMU's USB stack may not bit-for-bit emulate the BIOS USB handoff that real hardware uses. | Phase 12 claim is precise: "USB boot is QEMU-verified; physical USB boot is Phase 15." The docs/15 §"Known gaps" row is "Physical USB boot: NOT YET VERIFIED (Phase 15 target)". The dd-to-USB manual control door in docs/13 §4 is NOT removed (it remains a "manual experiment"); the new `sponge-usb-boot.run` is the QEMU-verifiable proof. |
| 13 | docs/15-hardware-compatibility.md drift vs scenarios — the matrix cells reference scenario files that move, get renamed, or get deleted. | `tool/hw_compat.mojo assert` checks every cell's `scenario:` and `evidence:` fields reference existing files (FAIL LOUDLY otherwise). The W6 acceptance criterion is the tool exits 0; the matrix is regenerated by hand-curation only (no auto-population). The tool is read-only against the repo. |
| 14 | The `ahci` driver not gracefully handling empty-port enumeration on a QEMU i440fx topology — the `sponge-boot-i440fx.run` smoke might fail not because the topology is fundamentally broken, but because the AHCI driver probe fails on "no AHCI controller found". | The i440fx smoke does NOT start the AHCI driver; the run script asserts the explicit IDE path (`-device piix4-ide`) instead. The smoke is "the Tier-0 chain survives i440fx" — the storage path is the IDE flavour, not the AHCI flavour. The W2 acceptance criterion is `boot-probe: PASS` from the IDE-backed runner (probe reads marker.txt via the IDE→part_block→vfs chain). |
| 15 | The `pc_nic` driver needing a specific `nic_router` policy that the existing `sponge-net-probe.run` does NOT carry (because ipxe_nic's NIC session probe shape differs from pc_nic's). | `sponge-pc-nic.run` adopts the upstream `genode/repos/pc/run/pc_nic.run` nic_router config verbatim (the policy `label_prefix: pc_nic | domain: uplink` is the proven shape). The W3 acceptance criterion is `nic_router: uplink DHCP acquired` — a known Phase 9 bootable shape. The scenario does NOT carry the Sponge-side `vfs_lwip` + `fetchurl` configuration from `sponge-net-probe.run` (Phase 12's pc_nic smoke stops at DHCP acquisition; the IP-level round-trip is a Phase 12+ goal). |
| 16 | The DDE-Linux build behind pc_nic doubling the Phase 12 build time — pc_nic and usb_hid both build the Linux kernel (~140 MB each port), and the existing build.sh dedup may not catch the second kernel if the two `.port` files differ. | The Phase 12 W1 acceptance criterion is "the second build of `sponge-pc-nic.run` after a clean `rm -rf genode/build` completes in ≤ 2× the time of the first build of `sponge-net-probe.run`" — i.e. the build system dedups the Linux kernel source preparation step. A regression here fails loudly on the W1 wall-clock budget, not on a silent hang. |
| 17 | The "real NIC driver path" claim being misread as "we have a real-hardware modem or Wi-Fi" — pc_nic supports ath9k/iwlwifi/rtlwifi but these are NOT QEMU-emulated. The honest claim is "Linux-NIC-driver stack" — not "real hardware". | (Same as #7; the docs/15 row text and the scenario comment carry the precise language.) |
| 18 | The Sponge DE `sponge-falkon-disk.run` and `sponge-alpha.run` already use ipxe_nic via nic_router/nic_uplink. Switching them to pc_nic would mean re-wiring the entire NIC chain (nic_router policies, lwIP VFS, etc.) — the risk is scope creep. | D12.4 forbids the substitution: the product image keeps ipxe_nic; pc_nic is a NEW scenario, not a substitution. The W2 acceptance criterion for the new scenario is "the pc_nic scenario builds and round-trips WITHOUT modifying any existing scenario's start nodes". |
| 19 | The patch-ledger entries #6, #7 (C1, C2) — the Phase 9 capspace work — being reverted by future tooling if the build system decides the patches are no longer needed. The Phase 12 size budget on `pc_nic` (cap_quota=1000) RELIES on those patches. | `tool/patches.mojo verify` is W1's first gate (exits 0 only if all 9 ledger rows are present). The new pc_nic scenario is also explicitly gated on the patches NOT being dropped by `git subtree pull` (the `tool/build` pre-flight check on `etc/build.conf` includes a `patches --verify` step). The W7 regression check enforces this. |
| 20 | The usbkbd QEMU device silently being plumbed via PS/2 (QEMU's default keyboard controller) on some QEMU versions — the scenario would pass without exercising the USB path. | The scenario's QMP recipe explicitly disambiguates: it BOOTS, sends a QMP `device_add` for `usb-kbd`, asserts the usb_hid `devices` report adds a KEYBOARD entry, REMOVES the device via `device_del`, and ONLY THEN sends the `send-key` QMP command. The W3 acceptance criterion chain is: `usb_hid: KEYBOARD detected` → `send-key: glyph delta` — the order matters. A PS/2-only regression would fail the KEYBOARD detection step. |
| 21 | The `i440fx` machine type making the part_block's partition pin ambiguous on a NO-disk-vs-one-disk boundary — QEMU's auto-attach semantics differ. | The i440fx smoke carries EXACTLY ONE disk (the iso image), and the boot probe's W2 acceptance criterion is `boot-probe: PASS` — if the partition pin is ambiguous, the probe fails. The multi-disk smoke (D12.2) is the SECOND disk, not the first — the QEMU `-drive` ordering is the regression that catches "first disk" assumptions (risk #8). |
| 22 | The "real hardware" row in docs/15 being silently populated by AI during a doc-syncing pass — Phase 12's matrix is QEMU-only. | `tool/hw_compat.mojo assert` has a hard-coded rule: any cell with `target: real-hardware` is REJECTED (the tool exits 2 with the message "real hardware is a Phase 15 deliverable; not a Phase 12 cell"). The W6 acceptance criterion is the tool runs clean. |
| 23 | The new `tool/hw_compat.mojo` being scaffolded as a doc-generator rather than a doc-validator — the matrix is hand-curated, NOT auto-populated from scenario runs. | The tool's only operation is `assert` (validate all cells reference existing scenarios + existing evidence logs + have non-empty status). There is no `generate` or `update` subcommand. The tool's source mirrors the `tool/patches.mojo` read-only shape (per docs/11 §4.1). |
| 24 | The Phase 12 workstream claiming the boot-matrix criterion is met by listing ALL 16 matrix cells when only 4 are verified. | docs/15's cell-status is one of three values: `verified`, `smoke-only`, `gap`. The criterion-1 boot-matrix row is a SUMMARY row, not a cell — the summary's "verified" count is the count of `verified` cells under it. The W6 acceptance criterion is: the 4 verified cells are populated; the remaining 12 cells are `smoke-only` (1) or `gap` (11), explicitly. `tool/hw_compat.mojo assert` checks that the verified count is non-zero. |
| 25 | The Phase 12 §11.3 item 1 fix (launch-click flake → usb-tablet absolute path) being mis-implemented as "switch to QEMU usb-tablet which already exists" rather than "change the QMP recipe to use the workspace-press→workspace-move→workspace-release choreography that the W4 drag proof validated". | The fix is a 1-line QMP recipe change in the affected scenarios (the `qmp_launch_*` Tcl helpers in `run/qmp.inc`). The W3 acceptance criterion is the ONE-back-to-back-pass-on-the-affected-3-runs-as-host-timing-variance test (the Phase 11 FLAKE log shows 3/3 fails on the host; Phase 12 success is 3/3 passes). The fix is committed BEFORE the new Phase 12 scenarios are written, so the new scenarios already use the corrected recipe. |
| 26 | The `nvme` driver's `Quota exceeded` log on boot under high DMA pressure — the docs/14 §11 risk #1 warns "ahci on base-sel4 is analogy-proven, not demonstrated" and the parallel NVMe-risk is the same. | `sponge-desktop-disk-nvme.run` adopts the docs/14 §11 sizing: `nvme | caps: 5000 | ram: 64M` (the explicit dossier number). The W3 acceptance criterion is `alpha-probe: PASS` within the `run_genode_until` timeout (300s — the Phase 9 generous gate). A failure is loud; the plan does NOT paper over a quota-too-low with a cap-bump retry. |
| 27 | The docs/15 §"Known gaps" being a copy-paste of the Phase 11 §11.3 follow-ups, with no Phase 12-specific gaps. | The gaps section is the explicit DELTA: items that Phase 12 TRIED to verify and could NOT on QEMU. The Phase 12 gaps are: (a) physical USB boot (Phase 15); (b) multi-namespace NVMe (Phase 12+); (c) virtio_pci_nic (Phase 12+); (d) usb-mouse REL (Phase 12+ — blocked by the QPA patch candidate); (e) i2c_hid (Phase 15+); (f) real-hardware rows (Phase 15). The Phase 11 §11.3 follow-ups are NOT gaps — they are deferred work items, not unverified configurations. |
| 28 | The scenario serialization constraint (`make -j1` in `genode/build/x86_64` per the Phase 11 W4 note) being treated as a per-task constraint rather than a per-regression-sweep constraint. | The Phase 12 regression sweep explicitly runs scenarios ONE AT A TIME (the `genode/build/x86_64` make is single-threaded against the shared build dir). The W6 sweep's documentation states this constraint upfront; the plan's `tool/run_phase12_regression.s` (or whatever the host-side runner is) uses `make -j1` for the `build_image` step and `run_genode_until` for the boot step. |

## Directives for Prometheus

### Core Directives

- MUST: Follow the binding decisions table verbatim — the 10 numbered
  decisions are not re-litigated in the plan.
- MUST: Treat the Phase 12 output as a **QEMU-verified matrix** with
  explicit gaps (D12.10 / risk #22). Real-hardware is a Phase 15 row.
- MUST: Capture the Phase 12 W0 baseline (D12.9) BEFORE any code change.
- MUST: Keep the existing `genode/` vendor tree untouched (D12.10).
- MUST: Document the 28 risk mitigations in the plan's task-level
  `Acceptance Criteria` blocks — they are FORBIDDEN from being
  silently absorbed.
- MUST NOT: Substitute the product media's NIC topology (D12.4) — pc_nic
  is additive, not subtractive.
- MUST NOT: Claim "USB boot works" without the docs/15 row text and
  scenario comment both carrying the precise "BIOS-side USB boot"
  language (risk #1).
- MUST NOT: Introduce image/uefi (D12.8).
- MUST NOT: Promote any patch-ledger candidate to a committed patch in
  Phase 12 (D12.10).
- MUST NOT: Auto-generate the docs/15 matrix (risk #23).
- PATTERN: Mirror the Phase 11 plan's "5-row scenario architecture
  decision" — one concern per scenario, one criterion per scenario.
- TOOL: Use `tool/hw_compat.mojo assert` (new) for the docs/15
  drift-detection check. Mirror `tool/patches.mojo`'s read-only shape.

### QA / Acceptance Criteria Directives (MANDATORY)

- MUST: Every Phase 12 scenario has a single distinct `run_genode_until`
  match marker (e.g. `boot-probe: PASS`, `alpha-probe: PASS`,
  `pc_nic: bound device`, `nic_router: uplink DHCP acquired`,
  `usb_hid: KEYBOARD detected`, `wm-probe: PASS` after the click
  re-choreography). The marker is the criterion-1..4 cell in docs/15.
- MUST: Every new scenario's evidence log carries the `QEMU_VERSION`
  env var captured at run time and the docs/13 §3
  `q35+Skylake-Client` pin (or the i440fx deviation for the i440fx
  variant).
- MUST: Every docs/15 cell has a `verified` / `smoke-only` / `gap`
  status that is auditable from the cell's evidence log.
- MUST: `tool/hw_compat.mojo assert` exits 0 against the W6-committed
  docs/15. The tool is run in CI via `tool/build verify` (the
  pre-flight check).
- MUST: Boot-time budgets are documented per cell (risk #5) and
  enforced by the evidence log's `boot_time_seconds` line.
- MUST: The §11.3 item 1 fix (D12.7) is committed BEFORE the new Phase
  12 scenarios that would otherwise depend on the flake path.
- MUST NOT: Use "user visually confirms" or "user manually tests" as
  any acceptance criterion.
- MUST NOT: Use `grep "phrase"` as a QA criterion for a PROSE
  deliverable (the docs/15 cells are auditable from the evidence
  log, not from a string match in the cell text).
- MUST: For the hardware-compatibility document, the QA is a
  program-and-machine assertion: `tool/hw_compat.mojo assert` exits
  0 AND the per-cell evidence log exists at the referenced path AND
  the evidence log contains the relevant scenario's PASS marker. The
  cell text is hand-curated; the doc's behaviour is its structural
  validity, not its wording.

## Recommended Approach

Phase 12 is a **scope-tight, matrix-building phase** with one real
architectural fork (the NIC driver choice) and three run-script-only
tweaks (the machine-type pin, the storage variant flag, the USB boot
smoke). The plan should: (1) W0 capture the seven-baseline, (2) W1
master the `q35+Skylake-Client` pin and add the multi-disk / NVMe /
USB-boot smokes, (3) W2 build the pc_nic scenario on the existing
DDE-Linux kernel with over-provisioned caps, (4) W3 build the
usb-kbd-via-QMP scenario, (5) W4 fix the §11.3 item 1 launch-click flake
on the corrected QMP recipe, (6) W5 author docs/15 and
`tool/hw_compat.mojo`, (7) W6 sweep the regression and write the
evidence. The 10 binding decisions above are the contract; the 28
risk mitigations are the plan's audit trail.
