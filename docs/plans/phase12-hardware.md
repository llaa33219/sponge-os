# Phase 12 — Hardware Support Expansion (Work Plan)

> Status: awaiting execution. Created 2026-08-08.
> Roadmap reference: `docs/09-roadmap.md` §10 *Phase 12* (lines 530–548).
> Pre-planning consultation: `docs/plans/phase12-hardware-support.md` (Metis consultation, 2026-08-07).
> Prior phase plan: `docs/plans/phase11-de-customization.md`.

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

## Binding Decisions (from Metis, do not re-litigate)

| # | Decision | Phase 12 outcome |
|---|---|---|
| D12.1 | Machine matrix is explicitly pinned q35 + `Skylake-Client`, plus one i440fx smoke | Every disk-touching existing run script pins `-machine q35 -cpu Skylake-Client`; `run/sponge-boot-i440fx.run` alone uses `-machine pc`, IDE, no product image, and no Sponge DE. |
| D12.2 | Storage variants are AHCI-default product media, opt-in NVMe product media, and one AHCI multi-disk smoke | `tool/dist --storage {ahci,nvme}` defaults to AHCI; NVMe gets explicit root-port/device plumbing; multi-disk positively checks the partition-number pin; ATAPI remains deferred. |
| D12.3 | USB boot means BIOS-side boot of the existing ISO as a QEMU USB stick | `run/sponge-usb-boot.run` attaches the ISO through `usb-storage`; this is not a claim that Genode-side `usb_block` loads `image.elf`. |
| D12.4 | The real-NIC-driver path is `pc_nic` with QEMU `e1000` | Add a stand-alone `run/sponge-pc-nic.run` with the Linux-backed driver stack and DHCP gate; keep all product and existing scenario `ipxe_nic` topologies unchanged and keep QEMU slirp as the host backend. |
| D12.5 | Input beyond PS/2 + usb-tablet is USB keyboard HID | Add `run/sponge-usb-kbd-via-qmp.run` using `pc_usb_host` → `usb_hid` → `event_filter`, QMP hotplug, and a glyph-delta gate; usb-mouse and `i2c_hid` remain deferred. |
| D12.6 | The compatibility contract is `docs/15-hardware-compatibility.md` plus a read-only validator | Ship a hand-curated 5×5 matrix with scenario/evidence/QEMU/budget traceability and an assert-only `tool/hw_compat.mojo`. |
| D12.7 | Include only the Phase-11 §11.3 item-1 launch-click fix | Land the usb-tablet absolute launch-click recipe before the new Phase-12 input scenarios; defer §11.2 item 1 and §11.3 items 2–4. |
| D12.8 | `image/uefi` is out of scope | Phase 12 remains BIOS/GRUB2; the UEFI + NIC footgun is documented as a gap for Phase 15. |
| D12.9 | W0 is the seven-scenario regression baseline | Capture both `sponge-boot` variants plus six other named scenarios and their exact load-bearing markers before any Phase-12 implementation change. |
| D12.10 | No new vendored-tree patches | Keep all work outside `genode/`; an unavoidable patch need becomes at most one docs/11 §4.2 candidate with evidence, not a Phase-12 patch. |

## Scope Guards

- **No edits to the vendored `genode/` tree.** Use only in-tree-proven
  Genode paths. A newly discovered patch need may be recorded in
  `docs/11-environment.md` §4.2 but is not committed in Phase 12.
- **No product-media NIC topology change.** The product image and
  `run/sponge-falkon-disk.run` keep `ipxe_nic`; `pc_nic` is additive in
  a new stand-alone scenario. Existing scenarios' start nodes are not
  substituted.
- **No real-hardware claims or matrix cells.** Phase 12 is QEMU-verified;
  physical-machine verification is Phase 15.
- **No machine types beyond q35 and i440fx.** No ARM `virt`, `microvm`,
  alternative CPU model, or additional board target.
- **No `image/uefi`.** Keep BIOS/GRUB2 and record UEFI as a known gap.
- **No new USB controller classes or USB feature areas.** The input claim
  is HID class `0x3` keyboard; no USB mouse, networking, serial, or audio.
  USB storage is BIOS-side boot media only.
- **No new Phase-11 follow-up patches.** Only §11.3 item 1 is included;
  §11.2 item 1 and §11.3 items 2–4 remain deferred.
- **No automatic compatibility-document generation.** Humans curate
  `docs/15`; `tool/hw_compat.mojo assert` validates and never writes it.
- **No `git subtree pull`.** Genode stays pinned at 26.05.
- **No new host packages.** Use the existing QEMU 11.0.3, Tcl/Expect,
  build tools, and Mojo 1.0.0b2 environment.
- **No TCC, OVMF, or EDK2 dependency.** UEFI is not introduced.
- **No new `sponge_pkgd` start nodes.** The NIC and input changes remain
  per-scenario hardware-driver concerns.
- **Explicitly deferred/rejected:** `virtio_pci_nic`, usb-mouse,
  `i2c_hid`, ATAPI, tap/bridge networking, and all additional machine
  types.

## Verified Ground Truth (consultation-validated — trust, do not re-derive)

- **Effective QEMU default:** `genode/repos/base/board/pc/qemu_args`
  supplies `-machine q35 -cpu Skylake-Client` and e1000/slirp. Existing
  `.run` scripts rely on this implicit default; W1 makes the product
  storage scripts explicit while preserving the board default as a
  safety net.
- **XSAVE contract:** QEMU 11.0.3 without q35 + `Skylake-Client` can fail
  seL4 in `boot_sys` with `XSAVE not supported`; no CPU experiment is
  permitted in this phase.
- **Storage auto-attach:** q35 auto-attaches an implicit drive to ICH9
  AHCI; i440fx auto-attaches to PIIX4 IDE. The i440fx smoke therefore
  does not start AHCI and does not claim product-media support.
- **NVMe plumbing:** q35 does not auto-attach NVMe. The existing
  `SPONGE_BOOT_NVME=1` path in `run/sponge-boot.run` is the canonical
  `pcie-root-port` + `-device nvme` pattern to copy.
- **Partition identity:** `part_block` pins P3/P4 by partition number;
  it never auto-probes. One QEMU NVMe namespace is in scope;
  multi-namespace is a documented gap. The multi-disk scenario must
  prove the selected marker comes from the second disk.
- **`pc_nic` source:** the source, package recipe, and upstream example
  are under `genode/repos/pc/`. The DDE-Linux path reuses the already
  pinned Linux 6.18.19 sources. The managed repository set must include
  `pc` for a fresh build.
- **`pc_nic` sizing:** upstream uses about 140 caps / 16 MiB for its
  simple run; Phase 12 deliberately uses `caps: 1000 | ram: 32M` to
  avoid silent seL4 cap exhaustion and gates on bind plus DHCP.
- **NIC topology stays additive:** `run/sponge-net-probe.run` remains the
  iPXE/fetchurl round-trip baseline. `run/sponge-pc-nic.run` is a new
  DHCP-focused driver-stack smoke and does not rewire existing product
  scenarios.
- **USB input chain:** `drivers_interactive-pc` already routes HID class
  `0x3` through `pc_usb_host` → `usb_hid` → `event_filter`. A QEMU
  `usb-kbd` hotplug is sufficient to expose a KEYBOARD report.
- **USB boot boundary:** the ISO format is suitable for an ISO or USB
  stick. BIOS/GRUB2 loads `image.elf`; Genode-side `usb_block` exists
  only after that load and is not the boot-path claim.
- **QMP foundation:** `run/qmp.inc` already supplies bounded QMP connect,
  input, tablet-selection, `send-key`, and marker-dispatch helpers. The
  Phase-11 launch flake is a recipe problem, not a new input component.
- **Patch state:** the committed patch ledger has nine rows; §4.2 has
  one current candidate. Phase 12 may add at most one candidate and no
  patch.
- **Host tools:** `tool/build.mojo`, `tool/dist.mojo`, and
  `tool/patches.mojo` are the extension points. The new compatibility
  tool mirrors the read-only structure of `tool/patches.mojo`.

## Metis AI Failure Points (Risk Register — encoded as task guardrails)

| # | Trap | Mitigation |
|---|---|---|
| 1 | Claiming USB boot when only BIOS-side USB-storage was tested, while prose implies Genode `usb_block` boot | The docs/15 row and scenario comment must say exactly: "USB boot = product media bootable as a USB stick on QEMU via `-device usb-storage` (BIOS side). Genode-side `usb_block` reads USB block devices AFTER `image.elf` is loaded; not a boot-path claim." The evidence must contain `BIOS-side USB boot verified`. |
| 2 | i440fx silently changes the controller to IDE and AHCI probes the wrong or no device | `sponge-boot-i440fx.run` does not start AHCI, explicitly uses the IDE path, reuses `boot_probe`, and is labeled `smoke-only`, not product verified. |
| 3 | NVMe namespace semantics invalidate the P3 pin | Exercise one namespace only; require `alpha-probe: PASS` plus a `part_block` P3 report/byte check; list multi-namespace NVMe as a gap. |
| 4 | `pc_nic` silently hangs from inadequate caps/RAM | Use `pc_nic | caps: 1000 | ram: 32M`; require `pc_nic: bound device` and `nic_router: uplink DHCP acquired` within 300 seconds. |
| 5 | New sequential scenarios make the suite impractical | Keep i440fx, multi-disk, USB-boot handoff, and the NVMe Tier-0 check near 60 seconds where realistic; record every `boot_time_seconds`; acknowledge 600s+ full seL4 desktop gates and enforce per-cell budgets. |
| 6 | `image/uefi` injects `-net none` and silently kills a future NIC combo | Keep `image/uefi` out of scope and record "UEFI + NIC is a known footgun; the Phase 12 NIC verification is on BIOS path only." |
| 7 | "Real NIC" is misread as real Intel hardware tested | Use the exact docs row: "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested." |
| 8 | The partition-number pin accidentally means "first disk" | In the two-disk smoke, swap drive order and require `boot-probe: PASS` from a marker stored only on P3 of the second disk. |
| 9 | Fresh `tool/build prepare` lacks the `pc` repository | Put `pc` in the managed `REPOSITORIES` block for all seL4 builds and prove the first `sponge-pc-nic.run` compile in a freshly prepared build directory. |
| 10 | NVMe is attached without its required q35 root port | Copy the existing `SPONGE_BOOT_NVME` QEMU root-port/drive/device sequence verbatim and gate the product scenario on NVMe-backed `alpha-probe: PASS`. |
| 11 | Patch-ledger candidates grow without actionable drop contracts | Add at most one new §4.2 candidate; only when required, with evidence, Where, Why, and Drop When. Acceptance is candidate count `≤ current + 1`. |
| 12 | QEMU USB boot is generalized to physical USB boot | State "USB boot is QEMU-verified; physical USB boot is Phase 15" and keep `Physical USB boot: NOT YET VERIFIED (Phase 15 target)` in known gaps. |
| 13 | docs/15 references deleted or renamed scenarios/evidence | `tool/hw_compat.mojo assert` rejects missing scenario/evidence paths and remains read-only; docs are hand-curated. |
| 14 | AHCI fails on an empty i440fx topology | Do not start AHCI; explicitly exercise PIIX4 IDE and require the same marker read through IDE → `part_block` → VFS. |
| 15 | `pc_nic` uses an unproven `nic_router` policy | Copy upstream `pc_nic.run` policy `label_prefix: pc_nic | domain: uplink`; Phase-12's mandatory new gate stops at `nic_router: uplink DHCP acquired`, while the unchanged `sponge-net-probe.run` remains the IP round-trip baseline. |
| 16 | DDE-Linux preparation doubles build time | After a clean build, compare first `sponge-pc-nic` build with `sponge-net-probe`; the second path must complete within 2× the first/reference budget and reuse prepared Linux sources. |
| 17 | The NIC claim expands to modem or Wi-Fi support | Apply the same exact honesty text as risk 7; non-e1000 hardware remains a Phase-15 gap. |
| 18 | Existing product or Falkon scenarios are rewired from iPXE to `pc_nic` | Do not modify any existing scenario's start nodes for the new NIC proof; `sponge-pc-nic.run` is additive. |
| 19 | Patch-ledger rows 6/7 are lost even though `pc_nic` sizing relies on them | Run `./tool/patches verify` as W1's first gate and from the build verification pre-flight; require all nine ledger rows and no subtree update. |
| 20 | QEMU keyboard input passes through PS/2 while the scenario claims USB | Use ordered QMP hotplug: boot, `device_add usb-kbd`, observe `usb_hid: KEYBOARD detected`, `device_del`, then issue `send-key`; the required chain is KEYBOARD detection followed by glyph delta, with ordered log evidence. |
| 21 | i440fx's zero/one-disk boundary makes the partition pin ambiguous | Carry exactly one disk in the i440fx smoke and require `boot-probe: PASS`; keep the two-disk order test separate. |
| 22 | A doc-sync pass silently adds real-hardware cells | `tool/hw_compat.mojo assert` rejects any `target: real-hardware` with exit 2 and `real hardware is a Phase 15 deliverable; not a Phase 12 cell`. |
| 23 | `tool/hw_compat.mojo` becomes a generator | The only operation is `assert`; no `generate`, `update`, or repository-writing path exists. |
| 24 | The document claims all 16 cells verified when only four are | Use statuses `verified`, `smoke-only`, and `gap`; publish exactly 4 verified, 1 smoke-only, and 11 gap cells, and make the validator require a non-zero verified count. |
| 25 | The launch-click fix merely re-adds a usb-tablet device instead of changing choreography | Change the QMP recipe to the W4-proven usb-tablet absolute workspace-press → workspace-move → workspace-release path; require three consecutive passes of the formerly 3/3-failing launch phase before new input scenarios. |
| 26 | NVMe hits `Quota exceeded` under DMA pressure | Size `nvme | caps: 5000 | ram: 64M`; require one bounded 300-second Tier-0 gate and the full desktop `alpha-probe: PASS`; do not retry with ad hoc cap bumps. |
| 27 | Known gaps are copied from Phase 11 instead of recording Phase-12 deltas | List exactly: physical USB boot; multi-namespace NVMe; `virtio_pci_nic`; usb-mouse REL/QPA candidate; `i2c_hid`; and real-hardware rows. Phase-11 §11.3 follow-ups are not compatibility gaps. |
| 28 | Scenario serialization is treated as local advice and concurrent builds corrupt the shared build dir | The entire W6 sweep runs one scenario at a time with no concurrent `make` in `genode/build/x86_64`; use `make -j1` for image/build steps and bounded `run_genode_until` gates. |

## Matrix Cell Contract (target state)

Every non-gap cell in `docs/15-hardware-compatibility.md` uses the same
machine-readable fields, kept in prose-friendly Markdown:

```text
status: verified|smoke-only
scenario: run/<name>.run
marker: <exact PASS or load-bearing success marker>
evidence: docs/evidence/phase12-<name>.log
qemu: 11.0.3
boot_time_seconds: <measured integer>
budget_seconds: <declared integer>
target: qemu
```

A `gap` cell carries `status: gap`, a precise reason, and a target phase;
it does not fabricate a scenario or PASS marker. `tool/hw_compat.mojo assert`
validates every referenced path and marker, rejects any other status, and
never mutates the document.

## Scenario Architecture Decision

**Decision: focused scenarios, one hardware concern per scenario, plus
one shared QMP recipe fix. Existing product scenarios remain unchanged
except for the explicit q35 + CPU pin.**

| Scenario / tool | Criterion | Action | Kernel | Distinct gate | Budget contract |
|---|---|---|---|---|---|
| `run/sponge-boot.run` | 1, 4 | Existing AHCI + `SPONGE_BOOT_NVME=1` baseline; add explicit q35/CPU pin only | base-sel4 | `boot-probe: PASS ... sponge-boot-marker-v1` | Capture both variants in W0; Tier-0 target near 60 s |
| `run/sponge-boot-i440fx.run` | 1, 4 | New storage-only i440fx/PIIX4 IDE smoke; one disk, no AHCI, no DE/product image | base-sel4 | `boot-probe: PASS` from IDE-backed marker | ≤60 s target; `smoke-only` |
| `run/sponge-boot-multidisk.run` | 1, 4 | New q35/AHCI two-disk ordering and P3/P4 pin check | base-sel4 | `boot-probe: PASS` from second disk's P3 marker | ≤60 s target |
| `run/sponge-desktop-disk-nvme.run` | 1, 4 | New product-media NVMe variant using one namespace and explicit root port | base-sel4 | P3 number/byte check + `alpha-probe: PASS` | Tier-0 check near 60 s; full desktop 600 s+ reality, bounded at 900 s |
| `run/sponge-pc-nic.run` | 2, 4 | New additive `pc_nic` + e1000 + `nic_router` DHCP smoke | base-sel4 | `pc_nic: bound device` + `nic_router: uplink DHCP acquired` | 300 s cold DDE-Linux gate; not a desktop scenario |
| `run/sponge-usb-boot.run` | 1, 4 | New BIOS-side USB-storage attachment of the existing ISO | base-sel4 | `BIOS-side USB boot verified` + `alpha-probe: PASS` | BIOS handoff target ≤60 s; full Alpha corroboration is 600 s+ |
| `run/sponge-usb-kbd-via-qmp.run` | 2, 4 | New interactive stack with USB keyboard hotplug/remove and QMP `send-key` | base-sel4 + QMP | `usb_hid: KEYBOARD detected` → glyph delta → scenario PASS | 600 s+ seL4 desktop reality; bounded at 900 s |
| `run/qmp.inc` | 2 regression prerequisite | Change launch-entry click to the proven usb-tablet absolute choreography | host/QMP helper | `sponge-de-probe: phase launch PASS` in 3 consecutive runs | Existing 600 s launch budget per run |
| `tool/hw_compat.mojo` | 3, 4 | New assert-only compatibility validator | host | exit 0 on the committed matrix and evidence | Host check, target <5 s |

**Why not one mega-scenario:** storage, NIC, USB boot, and USB input fail
at different capability-chain stages. Focused scenarios preserve a
single distinct marker, keep short Tier-0 checks short, and prevent a
600s+ desktop boot from masking a controller or DHCP failure.

## Task Dependency Graph

| Task | Depends On | Reason |
|---|---|---|
| W0: TDD-green baseline | None | Freezes the seven existing regression contracts, both boot variants, QEMU version, effective machine/CPU, and timing before edits. |
| W1: Explicit platform pin + build pre-flight | W0 | All later scenarios require reproducible q35/CPU arguments, managed `pc`, and verified patch-ledger state. |
| W2: Storage variants | W1 | Reuses the explicit platform pin and canonical NVMe wiring. |
| W3: `pc_nic` scenario | W1 | Requires the managed `pc` repository and patch verification gate; file set is disjoint from W2. |
| W3b: Launch-click flake fix | W1 | Shared QMP prerequisite; disjoint from W2 storage and W3 NIC files and must land before W4 input work. |
| W4: USB boot + USB keyboard scenarios | W2, W3, W3b | Starts only after the three parallel branches join; input scenario consumes the corrected QMP recipe. |
| W5: Compatibility document + validator | W4 | Matrix statuses and pointers can be authored only after every new scenario has an evidence contract. |
| W6: Docs sync + evidence + regression/fresh-build sweep | W2, W3, W3b, W4, W5 | Roadmap closure requires all scenarios, compatibility checks, fresh-build proof, and serialized regression evidence. |

## Parallel Execution Graph

```text
Wave 1 (start immediately):
└── W0: seven-scenario TDD-green baseline (8 invocations: boot AHCI + NVMe)

Wave 2 (after W0):
└── W1: explicit q35+Skylake-Client pins + managed pc repo + patches verify

Wave 3 (after W1 — fire three IN PARALLEL; disjoint file sets):
├── W2: storage variants
│       (tool/dist.mojo, new i440fx/multidisk/NVMe-desktop run scripts)
├── W3: pc_nic scenario
│       (new run/sponge-pc-nic.run only; W1 already owns tool/build.mojo)
└── W3b: launch-click flake fix
        (run/qmp.inc + run/sponge-de-sel4-interactive.run recipe call site)

Wave 4 (after all Wave-3 branches):
└── W4: USB scenarios
        (run/sponge-usb-boot.run + run/sponge-usb-kbd-via-qmp.run)

Wave 5 (after W4):
└── W5: docs/15 5×5 matrix + tool/hw_compat.mojo assert validator

Wave 6 (after W5):
└── W6: roadmap/docs sync + evidence index + fresh-build check + full sweep

Critical Path: W0 → W1 → W3 → W4 → W5 → W6
(the W3 pc_nic cold-build/calibration branch is expected to be the longest
Wave-3 branch; W4 waits for W2, W3, and W3b regardless).
```

**Note:** scenario runs must serialize. Code edits in Wave 3 may proceed
in parallel because their file sets are disjoint, but there must be no
concurrent `make` in `genode/build/x86_64`. All boot and final regression
runs execute one scenario at a time, with `make -j1` for the shared build
directory.

## Tasks

### W0: TDD-green baseline capture

**EXPECTED OUTCOME**: Before any Phase-12 implementation change, the seven
D12.9 scenarios are run one at a time (eight invocations because
`sponge-boot.run` has AHCI and NVMe variants). Their exact current
load-bearing markers, QEMU version, effective q35 + `Skylake-Client`
arguments, elapsed seconds, command lines, and result are recorded in
`docs/evidence/task-0-phase12-baseline.md`. No source or run-script file
changes in this task.

1. Create `docs/evidence/task-0-phase12-baseline.md` with columns:
   scenario, variant, kernel/board, exact command, effective machine/CPU,
   `QEMU_VERSION`, exact marker, `boot_time_seconds`, and result.
2. Run the following **sequentially**; never start the next command until
   the prior QEMU/make process exits:

   > Invocation rule (corrected after the first W0 attempt): a
   > command-line `RUN_OPT=` override discards build.conf's accumulated
   > `RUN_OPT += ...` lines (`power_on/qemu`, `log/qemu`,
   > `boot_dir/sel4`). `sponge-desktop-disk.run`,
   > `sponge-persist-disk.run`, `sponge-falkon-disk.run`, and
   > `sponge-alpha.run` self-heal via their `ensure_plugin_loaded`
   > guards, but `sponge-boot.run` has no such guard, so every
   > RUN_OPT-overriding invocation below passes the FULL plugin list
   > verbatim: `RUN_OPT="--include power_on/qemu --include log/qemu
   > --include boot_dir/sel4 --include image/disk"` (proven by
   > `docs/evidence/p1-storage-boot.log`).

   | # | Scenario invocation | Exact load-bearing marker to capture |
   |---|---|---|
   | 1a | `make -j1 -C genode/build/x86_64 run/sponge-boot KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `boot-probe: PASS ... sponge-boot-marker-v1` |
   | 1b | `SPONGE_BOOT_NVME=1 make -j1 -C genode/build/x86_64 run/sponge-boot KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `boot-probe: PASS ... sponge-boot-marker-v1` |
   | 2 | `make -j1 -C genode/build/x86_64 run/sponge-desktop-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `alpha-probe: PASS` |
   | 3 | `make -j1 -C genode/build/x86_64 run/sponge-persist-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `pkg-seq-probe: PASS`, then `sponge_pkgd: restored ... root(s) from store`, then `Test succeeded: installed set restored from SPONGE-DATA (P4) after reboot` |
   | 4 | `make -j1 -C genode/build/x86_64 run/sponge-falkon-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include power_on/qemu --include log/qemu --include boot_dir/sel4 --include image/disk"` | `falkon-probe: PASS` and `sponge-falkon-disk: ALL CHECKS PASSED ...` |
   | 5 | `make -j1 -C genode/build/x86_64 run/sponge-net-probe KERNEL=sel4 BOARD=pc` | exact fixture first line + `child "fetchurl" exited with exit value 0` + `Run script execution successful.`; do not invent a new PASS literal in W0 |
   | 6 | `make -j1 -C genode/build/x86_64 run/sponge-de-sel4-interactive KERNEL=sel4 BOARD=pc` | `sponge-de-probe: phase input PASS`, `phase panel PASS`, `phase launch PASS`, final `sponge-de-probe: PASS` |
   | 7 | `make -j1 -C genode/build/x86_64 run/sponge-launch KERNEL=sel4 BOARD=pc` | `launch-probe: PASS` |
3. Record the exact QEMU version emitted or queried for each invocation.
   Record that W0 used the effective board default
   q35 + `Skylake-Client`; W1 will make it explicit in the scripts.
4. Record elapsed seconds without changing existing timeouts. Flag the
   known 600s+ desktop scenarios rather than pretending they are
   short-form.
5. If any load-bearing marker is absent, stop Phase-12 execution and
   diagnose the pre-existing regression. Do not weaken a marker or
   proceed with an untrusted baseline.

**Files**: create `docs/evidence/task-0-phase12-baseline.md`; no code or
run-script edits.

- **Category**: `quick`
- **Skills**: [`debugging`] (only if a supposedly green baseline is red;
  no implementation changes belong in W0)
- **Depends On**: None
- **Kernel tags**: sel4 / pc for all eight invocations
- **Acceptance Criteria**:
  - All seven D12.9 scenario entries are green; `sponge-boot.run` is
    green in both AHCI and NVMe modes, for eight serialized invocations.
  - The evidence file contains each exact marker, command,
    `QEMU_VERSION`, effective q35 + `Skylake-Client`, and
    `boot_time_seconds`.
  - Risk 5 mitigation is explicit: per-scenario timing is captured;
    short Tier-0 and 600s+ desktop reality are not conflated.
  - Risk 28 mitigation is verbatim operational policy: scenarios run
    **ONE AT A TIME**, with no concurrent `make` in
    `genode/build/x86_64` and `make -j1` for the shared build directory.

### W1: Explicit platform pin + build pre-flight

**EXPECTED OUTCOME**: Every existing disk-touching scenario states its
q35 + `Skylake-Client` contract directly before the memory/headless QEMU
arguments; `tool/build.mojo` manages the `pc` repository for a fresh
seL4 build and performs the read-only patch-ledger verify gate. Existing
scenario behavior and markers remain unchanged.

1. In each of these files, insert exactly
   `append qemu_args " -machine q35 -cpu Skylake-Client "` immediately
   before its existing `append qemu_args " -nographic -m ... "` line:
   - `run/sponge-boot.run`
   - `run/sponge-desktop-disk.run`
   - `run/sponge-persist-disk.run`
   - `run/sponge-falkon-disk.run`
   - `run/sponge-alpha.run`
2. Preserve every existing storage/device argument and timeout. Do not
   change the CPU model, the board default file, or any existing scenario
   start node. The W2 i440fx script is the only Phase-12 deviation.
3. In `tool/build.mojo`, ensure the marker-delimited managed
   `REPOSITORIES` block contains
   `REPOSITORIES += $(GENODE_DIR)/repos/pc` for every prepared build,
   positioned with the other PC driver repositories. The operation must
   remain idempotent for both a new and an already-managed `build.conf`.
4. Add an explicit read-only patch pre-flight in `tool/build.mojo` that
   invokes the existing `./tool/patches verify` contract before a build
   scenario proceeds. Propagate a non-zero exit and print a direct
   message naming `docs/11-environment.md` §4. Do not make the build tool
   repair, export, drop, or modify patches.
5. Run `./tool/patches verify` as this workstream's first gate and record
   its nine-row receipt in `docs/evidence/task-1-phase12-platform.md`.
6. Run the W0 storage scenarios once after the pin change and compare
   their exact markers. The full fresh-directory proof is deliberately
   deferred to W6, after `sponge-pc-nic.run` exists.

**Files**: edit `run/sponge-boot.run`,
`run/sponge-desktop-disk.run`, `run/sponge-persist-disk.run`,
`run/sponge-falkon-disk.run`, `run/sponge-alpha.run`,
`tool/build.mojo`; create `docs/evidence/task-1-phase12-platform.md`.
The existing `tool/patches` / `tool/patches.mojo` implementation is
invoked, not rewritten.

- **Category**: `deep`
- **Skills**: [`mojo-syntax`] (`tool/build.mojo` is modified)
- **Depends On**: W0
- **Kernel tags**: host (build pre-flight); sel4 / pc (scenario checks)
- **Acceptance Criteria**:
  - All five named disk-touching scripts carry one explicit
    `-machine q35 -cpu Skylake-Client` before `-nographic -m`; no
    alternative CPU or machine is introduced.
  - Risk 9 mitigation is present: the managed `REPOSITORIES` block
    includes `pc` for **ALL seL4 builds**, is idempotent, and W6 is
    assigned the first compile in a freshly prepared build directory.
  - Risk 19 mitigation is present: `./tool/patches verify` is W1's
    first gate, exits 0 only when all nine ledger rows are present, and
    the build pre-flight fails loudly rather than dropping a patch.
  - The W0 marker set remains byte-for-byte equivalent; no existing
    scenario start node or product NIC topology changes.
  - No file under `genode/` is edited.

### W2: Storage variants and product-media selector

**EXPECTED OUTCOME**: `./tool/dist --storage ahci` preserves the current
product-media default, while `--storage nvme` builds the one-namespace
NVMe product path. Three focused scenarios cover i440fx IDE,
q35/AHCI multi-disk ordering, and q35/NVMe desktop-from-disk. Partition
identity remains by number, all failures are bounded, and no ATAPI or
additional machine type enters scope.

1. Extend `tool/dist.mojo` argument parsing and help with
   `--storage {ahci,nvme}`:
   - default `ahci` keeps current behavior and artifact naming;
   - reject any other value before a build starts with a concise English
     error and usage line;
   - `nvme` selects `run/sponge-desktop-disk-nvme.run` for the product
     `.img`; the ISO/live media path remains unchanged;
   - keep `tool/dist` as the existing thin argument-forwarding launcher.
2. In the generated Tier-0 configuration selected by the storage mode,
   use exactly one of `<start name="ahci">` or `<start name="nvme">`.
   The QEMU branch must be equally explicit: AHCI mode uses the q35 ICH9
   AHCI attachment (`-device ahci` in the D12.2 mode contract, without
   double-attaching the board's implicit drive), while NVMe mode copies
   the root-port + raw drive + `-device nvme` sequence from
   `run/sponge-boot.run`'s `SPONGE_BOOT_NVME` block and retains one
   namespace.
3. Create `run/sponge-desktop-disk-nvme.run` from the existing
   `run/sponge-desktop-disk.run` topology, changing only the storage
   driver and QEMU attachment:
   - explicit q35 + `Skylake-Client`;
   - `nvme | caps: 5000 | ram: 64M`;
   - one namespace on a `pcie-root-port`;
   - retain `<partition number="3"/>` semantics;
   - add a P3 report/byte assertion (`Number: 3`) before the existing
     `alpha-probe: PASS` gate;
   - record an early Tier-0 timing target near 60 seconds and the honest
     600s+ full-desktop reality (bounded at 900 seconds).
4. Create `run/sponge-boot-i440fx.run` as a focused derivative of
   `run/sponge-boot.run`:
   - explicit `-machine pc -cpu Skylake-Client`;
   - exactly one boot disk and explicit PIIX4 IDE path;
   - do not build or start AHCI;
   - do not carry the product `.img`, Sponge DE, or an NVMe toggle;
   - reuse `boot_probe` and require `boot-probe: PASS` through
     IDE → `part_block` → VFS;
   - label its compatibility cell `smoke-only`; target ≤60 seconds.
5. Create `run/sponge-boot-multidisk.run` as a q35/AHCI Tier-0 smoke:
   - create two images; put the expected marker only on P3 of the second
     disk;
   - deliberately swap QEMU drive order so success cannot come from
     "first disk" semantics;
   - retain the partition-number contract and assert the marker from
     disk 1/second disk;
   - require `boot-probe: PASS`; target ≤60 seconds.
6. Run each new scenario red before final wiring, then green one at a
   time. Capture evidence as
   `docs/evidence/phase12-boot-i440fx.log`,
   `docs/evidence/phase12-boot-multidisk.log`, and
   `docs/evidence/phase12-desktop-nvme.log`, each with QEMU version,
   explicit machine/CPU, marker, and `boot_time_seconds`.
7. Exercise `./tool/dist --storage ahci` and
   `./tool/dist --storage nvme`; record command receipts and artifact
   hashes in `docs/evidence/task-2-phase12-storage.md` without claiming
   physical-media verification.

**Files**: edit `tool/dist.mojo`; create
`run/sponge-desktop-disk-nvme.run`, `run/sponge-boot-i440fx.run`,
`run/sponge-boot-multidisk.run`,
`docs/evidence/task-2-phase12-storage.md`, and the three named evidence
logs. The thin `tool/dist` launcher remains unchanged unless argument
forwarding is proven broken.

- **Category**: `deep`
- **Skills**: [`mojo-syntax`, `debugging`] (Mojo selector plus storage
  scenario/probe calibration)
- **Depends On**: W1
- **Kernel tags**: host (`tool/dist`); sel4 / pc (all scenarios)
- **Acceptance Criteria**:
  - `--storage ahci` is the default and preserves current behavior;
    `--storage nvme` selects exactly one NVMe driver and explicit q35
    root-port/device chain; invalid values fail before build.
  - Risk 2 mitigation: i440fx explicitly disables AHCI, reuses the same
    `boot_probe`, and is `smoke-only`, not product verified.
  - Risk 3 mitigation: the NVMe row says "1 namespace verified;
    multi-namespace is a GAPS entry" and requires `alpha-probe: PASS`
    plus the P3 `Number: 3` report/byte check.
  - Risk 5 mitigation: i440fx and multi-disk target ≤60 seconds; the
    NVMe Tier-0 check targets near 60 seconds while the full seL4
    desktop explicitly records its 600s+ reality and bounded 900-second
    gate. Every log contains `boot_time_seconds`.
  - Risk 8 mitigation: AHCI binds two disks and `boot-probe: PASS` reads
    a marker that exists only on P3 of the **SECOND disk** after order
    is swapped.
  - Risk 10 mitigation: the QEMU root-port/drive/NVMe device sequence is
    copied from `SPONGE_BOOT_NVME`; failure is heard at the probe, not
    mistaken for image-build success.
  - Risk 14 mitigation: i440fx does not start AHCI and exercises
    PIIX4 IDE → `part_block` → VFS.
  - Risk 21 mitigation: i440fx carries **EXACTLY ONE disk**; the second
    disk exists only in the separate multi-disk scenario.
  - Risk 26 mitigation: NVMe is `caps: 5000 | ram: 64M`; no quota-failure
    retry or ad hoc cap bump is permitted.
  - No ATAPI, `image/uefi`, additional machine type, alternative CPU,
    or vendored-tree edit appears.

### W3: Additive `pc_nic` scenario

**EXPECTED OUTCOME**: A new `run/sponge-pc-nic.run` proves that the
Linux-backed `pc_nic` stack binds QEMU e1000 and obtains DHCP through the
upstream-proven `nic_router` policy on base-sel4. The existing iPXE
product/network scenarios remain byte-for-byte untouched by this task.
The claim is driver-stack-on-QEMU, never non-e1000 real hardware.

1. Create `run/sponge-pc-nic.run` using these two grounded sources:
   - driver/platform/capability and `nic_router` policy from
     `genode/repos/pc/run/pc_nic.run`;
   - bounded-run and evidence style from `run/sponge-net-probe.run`.
2. Build `pc_nic` from `genode/repos/pc/src/driver/nic/pc/` with QEMU
   `-device e1000`; use the W1-managed `pc` repository. Do not add a
   local patch or duplicate the DDE-Linux source tree.
3. Configure the driver exactly as `pc_nic | caps: 1000 | ram: 32M`.
   Use the upstream `nic_router` policy shape
   `label_prefix: pc_nic | domain: uplink` and preserve QEMU's user/slirp
   netdev. Do not add tap/bridge networking.
4. Gate in this order, each bounded:
   - `pc_nic: bound device`;
   - `nic_router: uplink DHCP acquired`;
   - final scenario success.
   Set the cold DDE-Linux run timeout to 300 seconds. The unchanged
   `run/sponge-net-probe.run` remains the HTTP round-trip proof; do not
   expand the new driver smoke into a product topology rewrite.
5. Put the exact honesty comment from risk 7 at the top of the scenario.
   It must identify e1000 as the only QEMU-verified model.
6. Measure build preparation:
   - from a clean build, time the first/reference
     `sponge-net-probe.run` build;
   - time the `sponge-pc-nic.run` build after the shared Linux sources
     are prepared;
   - require the latter to stay within 2× the reference budget and
     record both values.
7. Capture QEMU version, q35 + `Skylake-Client`, both mandatory markers,
   `boot_time_seconds`, build timings, and the unchanged-scenario diff
   receipt in `docs/evidence/phase12-pc-nic.log` and
   `docs/evidence/task-3-phase12-pc-nic.md`.

**Files**: create `run/sponge-pc-nic.run`,
`docs/evidence/phase12-pc-nic.log`, and
`docs/evidence/task-3-phase12-pc-nic.md`. Do not edit
`run/sponge-net-probe.run`, `run/sponge-falkon-disk.run`,
`run/sponge-alpha.run`, or other existing scenario files in W3.

- **Category**: `deep`
- **Skills**: [`debugging`] (DDE-Linux bind/DHCP and quota calibration)
- **Depends On**: W1
- **Kernel tags**: sel4 / pc; QEMU e1000 + user/slirp backend
- **Acceptance Criteria**:
  - Risk 4 mitigation is exact: `pc_nic | caps: 1000 | ram: 32M` and
    both `pc_nic: bound device` and `nic_router: uplink DHCP acquired`
    appear within 300 seconds; a silent hang is a loud timeout failure.
  - Risks 7 and 17 use the exact claim:
    "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested."
  - Risk 15 mitigation: policy is
    `label_prefix: pc_nic | domain: uplink`, copied from upstream; the
    Phase-12 mandatory gate stops at DHCP acquisition and the existing
    iPXE/fetchurl scenario remains the round-trip baseline.
  - Risk 16 mitigation: the timed build completes within 2× the
    reference and demonstrates shared Linux-source preparation rather
    than a second independent download/build path.
  - Risk 18 mitigation: the new scenario builds and reaches DHCP
    **WITHOUT modifying any existing scenario's start nodes**.
  - The risk-20 sibling concern is explicit: the NIC proof cannot pass
    on an old iPXE device because the bind marker names `pc_nic` and the
    scenario builds/starts no `ipxe_nic` child.
  - No tap/bridge, Wi-Fi/modem claim, product-media topology change,
    vendored patch, or real-hardware status is introduced.

### W3b: Launch-click flake fix (Phase-11 §11.3 item 1)

**EXPECTED OUTCOME**: The formerly 3/3-failing launch-entry click in
`run/sponge-de-sel4-interactive.run` uses the already-proven usb-tablet
absolute path rather than the drifting PS/2 relative recipe. Three
consecutive complete launch-phase passes establish the deterministic
prerequisite before W4 writes any new input scenario.

1. In `run/qmp.inc`, add the smallest launch-only selector needed for
   `qmp_exec_target` to route a click marker through the existing
   `qmp_tablet_index` → `mouse_set` → absolute motion → press/release
   helper. Preserve PS/2-relative dispatch as the default so Phase-10
   input and panel phases still exercise their original path.
2. In `run/sponge-de-sel4-interactive.run`, change only the launcher
   entry's QMP dispatch call to select that tablet-absolute recipe. The
   launch popup's S-toggle and unrelated scenarios retain their current
   recipes unless the existing marker contract already identifies the
   entry click separately.
3. Preserve the W4-proven choreography as workspace press → workspace
   move → workspace release with bounded pacing. Merely adding
   `-device usb-tablet` is not a fix; that device already exists.
4. Run `sponge-de-sel4-interactive.run` three consecutive times,
   sequentially. Every run must reach `pkg_gui_demo: window shown`,
   `sponge-de-probe: phase launch PASS`, final
   `sponge-de-probe: PASS`, and `Run script execution successful.`
5. Record all three commands, markers, QEMU version, click target, and
   timing in `docs/evidence/task-3b-phase12-launch-click.md`; retain the
   Phase-11 flake evidence link for before/after traceability.

**Files**: edit `run/qmp.inc` and
`run/sponge-de-sel4-interactive.run`; create
`docs/evidence/task-3b-phase12-launch-click.md`.

- **Category**: `deep`
- **Skills**: [`debugging`] (empirical QMP pointer recipe calibration)
- **Depends On**: W1
- **Kernel tags**: sel4 / pc + QMP
- **Acceptance Criteria**:
  - Risk 25 mitigation is enforced: this is the usb-tablet absolute
    workspace-press → workspace-move → workspace-release choreography,
    not "switch to QEMU usb-tablet" and not a new device declaration.
  - The affected launch phase passes 3/3 back-to-back on the host that
    previously recorded 3/3 failures; all four named final markers are
    captured per run.
  - Input/panel PS/2 coverage remains green; no QPA patch, vendored-tree
    edit, or Phase-11 §11.3 item 2–4 work is introduced.
  - W4 is blocked until this task is green.

### W4: USB boot and USB keyboard scenarios

**EXPECTED OUTCOME**: Two new focused scenarios prove (a) BIOS-side boot
of the product ISO when QEMU exposes it as USB storage and (b) USB HID
keyboard enumeration plus QMP-driven text input on the seL4 interactive
stack. Both consume the W3b QMP fix, carry precise non-overclaiming
comments, and use distinct markers and evidence logs.

1. Create `run/sponge-usb-boot.run` from the existing ISO media path:
   - build/stage the same BIOS/GRUB2 ISO composition used by
     `run/sponge-alpha.run`;
   - explicitly pin q35 + `Skylake-Client`;
   - attach it with
     `-boot menu=on -device usb-ehci -device usb-storage,drive=stick -drive id=stick,format=raw,file=<iso>,if=none`;
   - emit/gate the literal marker `BIOS-side USB boot verified` when the
     bootloader/media handoff succeeds;
   - require the existing `alpha-probe: PASS` as the final end-to-end
     corroboration, without changing the claim to Genode `usb_block`;
   - record a ≤60-second target for the BIOS-side handoff and the honest
     600s+ full-Alpha gate, bounded at 900 seconds.
2. Put this exact comment in `run/sponge-usb-boot.run`:
   "USB boot = product media bootable as a USB stick on QEMU via
   `-device usb-storage` (BIOS side). Genode-side `usb_block` reads USB
   block devices AFTER `image.elf` is loaded; not a boot-path claim."
3. Create `run/sponge-usb-kbd-via-qmp.run` by reusing the
   `run/sponge-de-sel4-interactive.run` driver stack and QMP wiring:
   - explicit q35 + `Skylake-Client`;
   - `pc_usb_host`, `usb_hid`, and `event_filter` unchanged;
   - do not add usb-mouse or `i2c_hid`;
   - boot without relying on a static usb-kbd pass condition;
   - QMP `device_add` a named `usb-kbd` device;
   - require the `usb_hid` devices report to add a KEYBOARD entry and
     emit `usb_hid: KEYBOARD detected`;
   - QMP `device_del` the named keyboard and capture the removal event;
   - only then dispatch the prescribed `send-key` command and require a
     terminal/textedit glyph delta, preserving the exact risk-20 audit
     order;
   - require a final distinct scenario PASS marker.
4. Use `run/qmp.inc`'s QEMU-11 qcode object form for `send-key`; do not
   use the rejected string form. All QMP waits and glyph probes are
   bounded. The full interactive scenario has a 600s+ reality and a
   900-second upper gate.
5. Run both scenarios red, then green, one at a time. Capture
   `docs/evidence/phase12-usb-boot.log`,
   `docs/evidence/phase12-usb-kbd.log`, and
   `docs/evidence/task-4-phase12-usb.md`, including QEMU version,
   explicit platform pin, markers, ordered QMP transcript, and
   `boot_time_seconds`.
6. Re-run the W3b three-pass launch-click gate once after the new USB
   input scenario lands; the shared helper must not regress.

**Files**: create `run/sponge-usb-boot.run`,
`run/sponge-usb-kbd-via-qmp.run`,
`docs/evidence/phase12-usb-boot.log`,
`docs/evidence/phase12-usb-kbd.log`, and
`docs/evidence/task-4-phase12-usb.md`. Edit `run/qmp.inc` only if the
new hotplug command needs a generic bounded helper; do not duplicate QMP
socket logic in the new scenario.

- **Category**: `deep`
- **Skills**: [`debugging`] (USB device/report and QMP input calibration)
- **Depends On**: W2, W3, W3b; W3b is a hard prerequisite
- **Kernel tags**: sel4 / pc; QMP for USB keyboard
- **Acceptance Criteria**:
  - Risk 1 mitigation appears verbatim in both the USB-boot scenario
    comment and later docs/15 row; the evidence contains the literal
    `BIOS-side USB boot verified`, never "Genode USB block boot".
  - Risk 5 mitigation: BIOS handoff has a ≤60-second target where
    realistic; a retained full Alpha or USB-keyboard desktop gate is
    labeled 600s+ and bounded at 900 seconds; both evidence logs carry
    `boot_time_seconds`.
  - Risk 12 mitigation is exact: "USB boot is QEMU-verified; physical
    USB boot is Phase 15"; the scenario makes no physical-stick claim.
  - Risk 20 ordered chain is audible:
    `device_add usb-kbd` → `usb_hid: KEYBOARD detected` → `device_del`
    → `send-key` → glyph delta → final PASS. A PS/2-only pass cannot
    satisfy the KEYBOARD detection gate.
  - Risk 25 remains green through the W3b three-pass launch regression.
  - No Genode-side `usb_block` boot claim, usb-mouse, `i2c_hid`, UEFI,
    new USB class, new component, or vendored patch appears.

### W5: Hardware compatibility document + assert-only validator

**EXPECTED OUTCOME**: `docs/15-hardware-compatibility.md` is the
hand-curated public hardware contract: a 5-row × 5-column surface matrix,
a 16-cell tuple-status ledger, evidence/QEMU/budget traceability, and
Phase-12-specific known gaps. `tool/hw_compat.mojo assert` validates the
contract read-only and is reachable from `./tool/build verify`.

1. Create `docs/15-hardware-compatibility.md` with this exact primary
   5×5 skeleton (five data rows, five columns):

   | Surface | Current q35/Skylake baseline | Phase-12 variant | Status summary | Scenario/evidence/QEMU/budget |
   |---|---|---|---|---|
   | Machine | q35 | i440fx IDE smoke | verified + smoke-only | pointers |
   | CPU | `Skylake-Client` | no additional CPU | verified + explicit gap | pointers |
   | Storage | AHCI | NVMe, multi-disk, BIOS-side USB media | verified/smoke-only/gap | pointers |
   | NIC | iPXE/e1000 baseline | `pc_nic`/e1000 | verified + explicit non-e1000 gaps | pointers |
   | Input | PS/2 + usb-tablet | usb-kbd | verified + usb-mouse/`i2c_hid` gaps | pointers |

   Each non-gap pointer expands to the Matrix Cell Contract fields above.
2. Add a separate 16-cell cross-product ledger for
   2 machines × 1 CPU × 2 storage × 2 NIC × 2 input. Publish exactly
   four `verified`, one `smoke-only`, and eleven `gap` cells. Do not
   imply the cross-product was run when only a focused scenario proved
   one concern.
3. Use these exact mandated texts:
   - USB row (risk 1):
     "USB boot = product media bootable as a USB stick on QEMU via
     `-device usb-storage` (BIOS side). Genode-side `usb_block` reads USB
     block devices AFTER `image.elf` is loaded; not a boot-path claim."
   - NIC row (risks 7/17):
     "pc_nic = Linux-NIC-driver stack (e1000e/rtl8169/ath9k/iwlwifi/rtlwifi/USB-Ethernet). QEMU-verified on `-device e1000` only; rtl8169/Wi-Fi/-USB-Ethernet documented but NOT QEMU-tested."
   - Summary row (risk 24):
     "Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells."
   - Physical USB gap (risk 12):
     "Physical USB boot: NOT YET VERIFIED (Phase 15 target)."
   - UEFI note (risk 6):
     "UEFI + NIC is a known footgun; the Phase 12 NIC verification is
     on BIOS path only."
4. Add a **Known gaps** section containing exactly these Phase-12 delta
   categories from risk 27, with no Phase-11 §11.3 copy-paste:
   - physical USB boot (Phase 15);
   - multi-namespace NVMe (Phase 12+);
   - `virtio_pci_nic` (Phase 12+);
   - usb-mouse relative motion, blocked by the QPA patch candidate
     (Phase 12+);
   - `i2c_hid` (Phase 15+);
   - real-hardware rows (Phase 15).
5. Do not create a real-hardware row. A known-gap sentence may name
   Phase 15, but no cell may contain `target: real-hardware`.
6. Create `tool/hw_compat.mojo`, mirroring the parse/assert/read-only
   shape of `tool/patches.mojo`, with only these commands:
   - `assert` — validate and exit;
   - `help` — usage only.
   There is no `generate`, `update`, write, or auto-population path.
7. `assert` must:
   - parse only recognized statuses: `verified`, `smoke-only`, `gap`;
   - for every `verified` or `smoke-only` cell, require the scenario file
     and evidence log to exist and require the evidence to contain the
     cell's exact marker;
   - require QEMU version, `boot_time_seconds`, and `budget_seconds` and
     fail if measured time exceeds budget;
   - require the USB evidence to contain `BIOS-side USB boot verified`;
   - reject any `target: real-hardware` with exit 2 and exact message
     `real hardware is a Phase 15 deliverable; not a Phase 12 cell`;
   - require exactly 4 verified, 1 smoke-only, and 11 gap cells and at
     least one verified cell;
   - for a gap cell, require a non-empty reason and target phase rather
     than a fabricated scenario/PASS marker;
   - never modify a repository file.
8. Add the thin executable launcher `tool/hw_compat` using the existing
   Mojo-launcher pattern. Extend `tool/build.mojo`'s `verify` path so it
   sequentially runs `./tool/patches verify` and
   `./tool/hw_compat assert`, propagating either failure.
9. Run `./tool/hw_compat assert` against a small temporary invalid matrix
   fixture for each failure class (missing scenario, missing evidence,
   absent PASS marker, over-budget timing, invalid status,
   `target: real-hardware`) and then against the committed docs/15.
   Fixtures live under `var/` or the tool's test scratch and are not
   durable docs.
10. Record validator cases and the final exit-0 receipt in
    `docs/evidence/task-5-phase12-hw-compat.md`.

**Files**: create `docs/15-hardware-compatibility.md`,
`tool/hw_compat.mojo`, `tool/hw_compat`, and
`docs/evidence/task-5-phase12-hw-compat.md`; edit `tool/build.mojo` to
wire the verification command.

- **Category**: `deep`
- **Skills**: [`mojo-syntax`] (`tool/hw_compat.mojo` and
  `tool/build.mojo` are modified)
- **Depends On**: W4
- **Kernel tags**: host (validator); matrix references sel4 / pc QEMU
  evidence
- **Acceptance Criteria**:
  - Risks 1 and 7 exact row texts appear unchanged; every non-gap cell
    points to an existing scenario, evidence log, exact marker, QEMU
    version, and timing budget.
  - Risk 5 mitigation: every non-gap evidence log contains
    `boot_time_seconds`; the validator rejects an over-budget cell and
    preserves ≤60-ish short gates versus 600s+ desktop gates.
  - Risk 6 mitigation: no UEFI scenario is added and the BIOS-only
    UEFI/NIC footgun sentence is present.
  - Risk 13 mitigation: missing/renamed scenario or evidence pointers
    fail loudly; the document stays hand-curated.
  - Risk 22 mitigation: any `target: real-hardware` exits 2 with
    `real hardware is a Phase 15 deliverable; not a Phase 12 cell`.
  - Risk 23 mitigation: the tool's only operation is `assert`; there is
    no `generate` or `update` subcommand and no repo-writing call path.
  - Risk 24 mitigation: status vocabulary is exactly
    `verified`/`smoke-only`/`gap`, counts are exactly 4/1/11, and the
    verified count is non-zero.
  - Risk 27 mitigation: known gaps are exactly physical USB boot,
    multi-namespace NVMe, `virtio_pci_nic`, usb-mouse REL,
    `i2c_hid`, and real-hardware rows; Phase-11 follow-ups are absent.
  - `./tool/hw_compat assert` and `./tool/build verify` exit 0 against
    the committed matrix without modifying it.

### W6: Docs sync + evidence index + fresh-build/full regression

**EXPECTED OUTCOME**: All four Phase-12 roadmap checkboxes are closed
only after every new and baseline scenario is green; QEMU-only and
slirp-only limitations are honestly narrowed rather than deleted; the
patch-candidate count rule is enforced; the complete Phase-12 evidence
index is durable; a freshly prepared build proves the managed `pc`
repository; and the entire regression sweep runs one scenario at a time.

1. **Roadmap and run documentation**:
   - `docs/09-roadmap.md` §10 Phase 12: flip all four checkboxes only
     after the corresponding scenario/evidence/tool gate is green.
     Add criterion → scenario → exact marker → evidence traceability.
   - `run/README.md`: document all five new scenario files, the NVMe
     product-media selector, exact claims, kernels, markers, and timing
     classes. Do not advertise real hardware.
   - `tool/README.md`: document `./tool/dist --storage`,
     `./tool/hw_compat assert`, and `./tool/build verify`.
   - `docs/08-development.md`: document the automated and manual
     equivalents for the storage selector, patch pre-flight, matrix
     assertion, and serialized Phase-12 regression commands (control
     escape hatch required by AGENTS.md §3.5).
2. **Patch-candidate discipline** (`docs/11-environment.md` §4.2):
   - record the current candidate count before edits;
   - add no row when all criteria use in-tree paths;
   - if and only if a criterion cannot be met without a vendored patch,
     add at most one candidate with Phase-12 evidence link, Where, Why,
     and Drop When, and leave the affected compatibility cell `gap`;
   - acceptance is candidate count `≤ current-count + 1`; no candidate
     is promoted and no `genode/` edit is made.
3. **Installation limitations** (`docs/13-installation.md` §6):
   - rescope, do not remove, the QEMU-only limitation to:
     "Phase 12 expands and records the QEMU-verified hardware matrix;
     physical-machine boot remains unverified until Phase 15."
   - rescope, do not remove, the slirp-only limitation to:
     "The host network backend remains QEMU user-mode/slirp. Phase 12
     verifies the Linux-backed `pc_nic` stack on QEMU e1000, not
     tap/bridge or physical-network operation."
   - keep the dd-to-USB manual experiment/control door and state that
     physical USB boot remains unverified.
4. **Evidence index**: create `docs/evidence/phase12-index.md` in the
   Phase-11 index style. Map each roadmap criterion and each 4/1/11
   matrix status to scenario, exact marker, evidence path, QEMU version,
   measured time, and budget. Include W0–W6 and W3b artifacts. Do not
   cite `.omo/`.
5. **Fresh-build-directory check** (risk 9's deferred W7 check, folded
   into W6):
   - preserve or remove the old `genode/build/x86_64` only within the
     repository's allowed scratch workflow;
   - run `./tool/build prepare` against a genuinely absent build
     directory;
   - verify the generated managed block contains `repos/pc` exactly
     once;
   - run `./tool/patches verify` before compile;
   - compile and run `sponge-pc-nic.run` as the first Phase-12
     hardware scenario in that fresh build;
   - require both bind and DHCP markers; record the full receipt in
     `docs/evidence/phase12-fresh-build.log`.
6. Run `./tool/dist --storage ahci` and
   `./tool/dist --storage nvme` after the fresh build. Record successful
   artifact hashes, scenario markers, and timing; do not call an
   artifact physically verified.
7. Run the full suite **one scenario at a time** with `make -j1`. New
   Phase-12 gates:

   | Scenario | Kernel | Required marker(s) |
   |---|---|---|
   | `sponge-boot-i440fx.run` | sel4 | `boot-probe: PASS` from IDE marker; `smoke-only` |
   | `sponge-boot-multidisk.run` | sel4 | `boot-probe: PASS` from second disk's P3 marker |
   | `sponge-desktop-disk-nvme.run` | sel4 | P3 `Number: 3` byte check + `alpha-probe: PASS` |
   | `sponge-pc-nic.run` | sel4 | `pc_nic: bound device` + `nic_router: uplink DHCP acquired` |
   | `sponge-usb-boot.run` | sel4 | `BIOS-side USB boot verified` + `alpha-probe: PASS` |
   | `sponge-usb-kbd-via-qmp.run` | sel4 | `usb_hid: KEYBOARD detected` → glyph delta → final PASS |
   | `sponge-de-sel4-interactive.run` ×3 | sel4 | `phase launch PASS` + final `sponge-de-probe: PASS` in all three |

8. Re-run the W0 regression envelope **unchanged in marker semantics**:

   | Scenario | Variant | Required W0 marker |
   |---|---|---|
   | `sponge-boot.run` | AHCI | `boot-probe: PASS ... sponge-boot-marker-v1` |
   | `sponge-boot.run` | NVMe | `boot-probe: PASS ... sponge-boot-marker-v1` |
   | `sponge-desktop-disk.run` | AHCI | `alpha-probe: PASS` |
   | `sponge-persist-disk.run` | AHCI/two boot | probe PASS + restored-store final success line |
   | `sponge-falkon-disk.run` | AHCI | `falkon-probe: PASS` + `ALL CHECKS PASSED` |
   | `sponge-net-probe.run` | iPXE/e1000 | exact fixture bytes + fetchurl exit 0 |
   | `sponge-de-sel4-interactive.run` | QMP input/panel/launch | all three phase PASS markers + final PASS |
   | `sponge-launch.run` | click + vct path | `launch-probe: PASS` |

9. Run host verification last:
   - `./tool/patches verify`;
   - `./tool/hw_compat assert`;
   - `./tool/build verify`.
   Stop on the first failure; do not flip roadmap checkboxes around a
   red matrix or regression cell.
10. Record the serialized sweep in
    `docs/evidence/task-6-phase12-regression.md` and per-scenario
    `docs/evidence/phase12-*.log` files. Every non-gap matrix cell must
    resolve to one of these durable logs.
11. Confirm the final repository has no `genode/` diff, no
    `target: real-hardware` cell, no new host dependency, and no durable
    `.omo/` reference.

**Files**: edit `docs/09-roadmap.md`, `docs/11-environment.md` only if the
candidate rule requires a row/note, `docs/13-installation.md`,
`docs/08-development.md`, `run/README.md`, `tool/README.md`, and the W5
`docs/15-hardware-compatibility.md` if evidence pointers/timings need
final values; create `docs/evidence/phase12-index.md`,
`docs/evidence/phase12-fresh-build.log`,
`docs/evidence/task-6-phase12-regression.md`, and the final
`docs/evidence/phase12-*.log` set.

- **Category**: `unspecified-high`
- **Skills**: [`debugging`] (fresh-build and serialized regression
  diagnosis; no scenario is waived)
- **Depends On**: W2, W3, W3b, W4, W5
- **Kernel tags**: host; sel4 / pc; QEMU 11.0.3
- **Acceptance Criteria**:
  - All new Phase-12 gates and every W0 regression marker pass one at a
    time; no concurrent `make` runs in `genode/build/x86_64`.
  - Risk 9 fresh-build mitigation is complete: a newly prepared build
    includes `pc` exactly once and compiles/runs `sponge-pc-nic` first.
  - Risk 11 mitigation is exact: docs/11 §4.2 candidate count is
    `≤ current-count + 1`; a row exists only when required and includes
    evidence, Where, Why, and Drop When; no patch ships.
  - Risk 12 remains honest: physical USB is Phase 15, and the manual
    dd-to-USB experiment is not removed.
  - Risk 13 is closed by `./tool/hw_compat assert` exit 0 against every
    durable pointer and PASS marker.
  - Risk 19 is rechecked: all nine committed patch-ledger rows verify
    before fresh compile and at final host verification.
  - Risks 22 and 24 are rechecked: no `target: real-hardware`; matrix
    counts remain exactly 4 verified, 1 smoke-only, 11 gaps.
  - Risk 27's six Phase-12 gaps remain the only compatibility-gap list;
    Phase-11 deferred work is not relabeled as hardware evidence.
  - Risk 28 is explicit in the evidence header: the full sweep runs
    scenarios **ONE AT A TIME** with `make -j1` and bounded
    `run_genode_until` gates.
  - Roadmap checkboxes link to exact markers and evidence; QEMU-only and
    slirp-only lines are rescoped, not deleted; no `.omo/` references,
    vendored-tree edits, or unrun verification claims exist.

## Must NOT Have (Metis exclusions, binding)

The following are explicitly excluded from Phase 12:

- Any edit or patch under `genode/`, including promotion of a §4.2
  candidate or a `git subtree pull`.
- Any real-hardware matrix cell, verified physical USB/SSD claim, or
  wording that makes QEMU evidence sound physical.
- `image/uefi`, OVMF/EDK2/TCC, or a UEFI + NIC scenario.
- `virtio_pci_nic`, tap/bridge networking, Wi-Fi/modem/USB-Ethernet
  verification, or replacement of existing `ipxe_nic` product paths.
- usb-mouse, `i2c_hid`, USB networking/serial/audio, or a new USB
  controller class.
- A Genode-side `usb_block` boot-path claim. The Phase-12 USB claim is
  BIOS-side only.
- ATAPI or another storage protocol beyond AHCI, one-namespace NVMe,
  i440fx IDE smoke, multi-disk AHCI, and BIOS USB media.
- Machine types beyond q35 and i440fx, or CPU models other than
  `Skylake-Client`.
- A hardware compatibility generator, auto-populator, or writer.
  `tool/hw_compat.mojo` is assert-only.
- New host packages or mutations outside the repository.
- New `sponge_pkgd` start nodes or new OS components.
- Phase-11 §11.2 item 1 or §11.3 items 2–4. Only the launch-click flake
  is included.
- Concurrent scenario builds in the shared Genode build directory.
- Acceptance by visual/manual confirmation or by a prose-only grep.
  Every verified/smoke cell requires a program gate and durable evidence.

Any unavoidable criterion failure is represented honestly as a `gap`
and, only if appropriate, at most one patch-ledger candidate. It is not
papered over with a vendored patch or broader claim.

## Commit Strategy

One logical change per commit, in dependency order, with conventional
English commit messages:

1. `test(evidence): capture phase 12 hardware regression baseline`
2. `build(tool): pin q35 platform and verify managed pc repository`
3. `feat(tool): add storage selector for ahci and nvme media`
4. `feat(run): add i440fx and multidisk storage smokes`
5. `feat(run): add nvme desktop-from-disk scenario`
6. `feat(run): add pc_nic e1000 dhcp scenario`
7. `fix(qmp): use tablet absolute recipe for launcher entry click`
8. `feat(run): add bios-side usb boot smoke`
9. `feat(run): add usb keyboard qmp scenario`
10. `feat(tool): add assert-only hardware compatibility validator`
11. `docs(hardware): add phase 12 compatibility matrix and known gaps`
12. `docs(roadmap): close phase 12 with serialized regression evidence`

TDD discipline per workstream: baseline first; for each new scenario,
observe the missing/failing gate, make the smallest scoped change, then
run to its bounded green marker. Do not merge roadmap closure before the
fresh-build and full serialized sweep are green.

## Success Criteria

1. **C1 — boot matrix:** explicit q35 + `Skylake-Client` pins are in all
   existing disk-touching scripts; i440fx IDE is `smoke-only`; AHCI,
   one-namespace NVMe, multi-disk partition order, and BIOS-side USB
   media have distinct scenario evidence.
2. **C2 — expanded drivers:** `run/sponge-pc-nic.run` proves e1000 bind
   plus DHCP with `caps: 1000 | ram: 32M`; the new USB keyboard scenario
   proves HID KEYBOARD enumeration and glyph delta; product iPXE paths
   remain unchanged.
3. **C3 — explicit compatibility contract:** the hand-curated 5×5
   surface matrix and 16-cell ledger ship in
   `docs/15-hardware-compatibility.md`; statuses are exactly 4 verified,
   1 smoke-only, 11 gaps; no real-hardware cell exists.
4. **C4 — loud regression coverage:** every new scenario has a distinct
   bounded marker and durable `docs/evidence/phase12-*.log` pointer;
   `./tool/hw_compat assert` and `./tool/build verify` exit 0.
5. **Baseline preservation:** all seven W0 scenarios, including both
   `sponge-boot` variants, retain their exact load-bearing markers; the
   launch-click phase passes three consecutive runs.
6. **Reproducibility:** a freshly prepared build contains the managed
   `pc` repository, verifies all nine patch-ledger rows, and compiles/runs
   `sponge-pc-nic` without relying on the author's old build directory.
7. **Timing honesty:** short Tier-0 gates state ≤60-ish targets where
   realistic; full seL4 desktop paths state their 600s+ reality; every
   evidence log records `boot_time_seconds` and is within its declared
   budget.
8. **Documentation honesty:** Phase-12 roadmap checkboxes have scenario
   traceability; docs/13 QEMU-only and slirp-only limitations are
   rescoped, not removed; physical hardware stays a Phase-15 gap.
9. **No scope violation:** no vendored-tree edit, UEFI, additional
   machine/CPU, ATAPI, `virtio_pci_nic`, tap/bridge, usb-mouse,
   `i2c_hid`, new component, new host package, or generated matrix.
10. **Safe execution:** the full suite is run one scenario at a time;
    durable docs contain no `.omo/` reference and make no claim about a
    check that was not actually run.
