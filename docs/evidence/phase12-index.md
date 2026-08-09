# Phase 12 evidence index — Hardware support expansion

> Phase 12 of `docs/09-roadmap.md` §10. Work plan:
> `docs/plans/phase12-hardware.md` (W0–W6). All four completion
> criteria GREEN (host QEMU 11.0.3; W0 captured the same QEMU version
> that the W6 regression envelope ran under).
>
> **Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells.**
> The hand-curated matrix is `docs/15-hardware-compatibility.md`; the
> read-only validator is `tool/hw_compat.mojo assert` (reachable from
> `./tool/build verify`).
>
> **Real hardware is a Phase 15 deliverable; not a Phase 12 cell.**
> No cell in `docs/15` carries `target: real-hardware`.

| WS | Deliverable | Evidence | Result |
|----|-------------|----------|--------|
| W0  | Regression baseline (7 scenarios / 8 invocations) | `task-0-phase12-baseline.md` + phase12-net-probe-ref.{start,end} | 7/7 scenarios + 8/8 invocations GREEN (host QEMU 11.0.3) |
| W1  | Explicit q35+Skylake-Client pin in 5 disk-touching scripts + managed `pc` repo + patches verify pre-flight | `task-1-phase12-platform.md` | W0 markers byte-for-byte recovered after the pin |
| W2  | Storage variants: AHCI default + NVMe opt-in + i440fx IDE smoke + multi-disk order check | `task-2-phase12-storage.md` + `phase12-boot-i440fx.log` + `phase12-boot-multidisk.log` + `phase12-desktop-nvme.log` | All 3 new scenarios GREEN; `tool/dist --storage {ahci,nvme}` paths validated |
| W3  | Additive `pc_nic`/e1000 + `nic_router` DHCP scenario | `task-3-phase12-pc-nic.md` + `phase12-pc-nic.log` | `pc_nic: bound device` + `nic_router: uplink DHCP acquired` GREEN |
| W3b | Launch-click flake fix (Phase-11 §11.3 item-1) — usb-tablet absolute recipe | `task-3b-phase12-launch-click.md` + `phase12-w4-w3b-run{1,2,3}.log` | run1 documented flake; runs 2/3 PASS; 2/3 of the W3b consecutive-pair budget met |
| W4  | USB boot smoke + USB keyboard QMP scenario | `task-4-phase12-usb.md` + `phase12-usb-boot.log` + `phase12-usb-kbd.log` | `BIOS-side USB boot verified` + `alpha-probe: PASS` for USB-boot; `usb_hid: KEYBOARD detected` + `KEYBOARD removed` + `sponge-usb-kbd-via-qmp: PASS` for USB-kbd |
| W5  | Hand-curated 5×5 matrix + 16-cell ledger + 6-phase-12-gap list + `tool/hw_compat.mojo assert` validator | `task-5-phase12-hw-compat.md` + `docs/15-hardware-compatibility.md` | 6/6 fixture classes rejected; final exit 0 on the committed matrix; 4 verified / 1 smoke-only / 11 gap |
| W6  | Docs sync + fresh-build check + serialized regression sweep + host verification | `task-6-phase12-regression.md` + `phase12-fresh-build.log` + `phase12-index.md` (this file) + per-scenario envelope logs | Roadmap checkboxes flipped; fresh build confirms `pc` repo present; full sweep runs one at a time; host verification exits 0 |

## Criterion → scenario → PASS marker → evidence → QEMU → time → budget

### Criterion 1 — Boot matrix beyond the current QEMU defaults

| Sub-criterion | Scenario | Exact marker | Evidence | QEMU | boot_time_seconds | budget_seconds | Status |
|---|---|---|---|---|---|---|---|
| q35 + Skylake-Client explicit pin (5 disk-touching scripts) | `run/sponge-boot.run` (default) | `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")` | `docs/evidence/task-0-phase12-baseline.md` + `phase12-envelope-sponge-boot-ahci.log` | 11.0.3 | 13 | 60 | verified |
| AHCI product-media default | `run/sponge-desktop-disk.run` | `alpha-probe: PASS` | `docs/evidence/phase12-envelope-sponge-desktop-disk.log` | 11.0.3 | (W6 envelope) | 900 | verified |
| NVMe product-media opt-in (one namespace) | `run/sponge-desktop-disk-nvme.run` | `partition-check: PASS (Number: 3)` + `alpha-probe: PASS` | `docs/evidence/phase12-desktop-nvme.log` | 11.0.3 | 46 | 900 | verified |
| i440fx PIIX4 IDE smoke (no AHCI, no product image) | `run/sponge-boot-i440fx.run` | `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")` | `docs/evidence/phase12-boot-i440fx.log` | 11.0.3 | 11 | 60 | smoke-only |
| q35/AHCI multi-disk order check | `run/sponge-boot-multidisk.run` | `boot-probe: PASS (22 bytes: "sponge-boot-marker-v1.")` from second disk's P3 | `docs/evidence/phase12-boot-multidisk.log` | 11.0.3 | 16 | 60 | verified (sub-pointer of cell #1) |
| BIOS-side USB boot (QEMU `-device usb-storage`) | `run/sponge-usb-boot.run` | `BIOS-side USB boot verified (SeaBIOS -> GRUB2 -> Bender handoff via usb-storage on q35/Skylake-Client)` + `alpha-probe: PASS` | `docs/evidence/phase12-usb-boot.log` | 11.0.3 | 53 | 900 | verified |

### Criterion 2 — Driver set expanded

| Sub-criterion | Scenario | Exact marker | Evidence | QEMU | boot_time_seconds | budget_seconds | Status |
|---|---|---|---|---|---|---|---|
| `pc_nic` + e1000 + `nic_router` DHCP | `run/sponge-pc-nic.run` | `pc_nic: bound device` + `nic_router: uplink DHCP acquired` + `sponge-pc-nic: PASS` | `docs/evidence/phase12-pc-nic.log` | 11.0.3 | 21 | 300 | verified |
| USB HID keyboard via QMP hotplug | `run/sponge-usb-kbd-via-qmp.run` | `usb_hid: KEYBOARD detected` → `usb_hid: KEYBOARD removed` → `sponge-usb-kbd-via-qmp: PASS` + `Run script execution successful.` | `docs/evidence/phase12-usb-kbd.log` | 11.0.3 | 436 | 900 | verified (primary audit chain); glyph-delta secondary gate is a documented Phase-12 gap |

### Criterion 3 — Hardware compatibility document

| Sub-criterion | Output | Evidence | Notes |
|---|---|---|---|
| Hand-curated 5×5 matrix + 16-cell ledger | `docs/15-hardware-compatibility.md` | `task-5-phase12-hw-compat.md` | 4 verified / 1 smoke-only / 11 gap; no `target: real-hardware` cell |
| `tool/hw_compat.mojo assert` validator | committed in `tool/hw_compat.mojo` + `tool/hw_compat` | `task-5-phase12-hw-compat.md` (6/6 fixture classes rejected; final exit 0) | reachable from `./tool/build verify` |
| 6 Phase-12 specific known gaps | `docs/15-hardware-compatibility.md` §4.1 | — | physical USB boot, multi-namespace NVMe, `virtio_pci_nic`, usb-mouse REL, `i2c_hid`, real-hardware rows |

### Criterion 4 — Run scenarios cover the new configs

| Sub-criterion | Mechanism | Evidence |
|---|---|---|
| Six new focused scenarios (one concern per scenario) | `run/sponge-boot-i440fx.run`, `run/sponge-boot-multidisk.run`, `run/sponge-desktop-disk-nvme.run`, `run/sponge-pc-nic.run`, `run/sponge-usb-boot.run`, `run/sponge-usb-kbd-via-qmp.run` | per-scenario `docs/evidence/phase12-*.log` |
| Bounded markers + durable log pointers | `task-6-phase12-regression.md` | every non-gap matrix cell resolves to a durable log |
| Fresh build proves `pc` repo + patch ledger | `var/scratch/fresh-build-pc-nic.log` + `docs/evidence/phase12-fresh-build.log` | managed block has `repos/pc` exactly once; 9/9 patch ledger rows verify |

## 16-cell cross-product status (4 verified, 1 smoke-only, 11 gap)

| Cell | (Machine, CPU, Storage, NIC, Input) | Status | Description |
|---|---|---|---|
| 1 | (q35, Skylake-Client, AHCI, ipxe/e1000, PS/2+tablet) | verified | `run/sponge-boot.run` + sub-pointers multi-disk + BIOS-USB-media |
| 2 | (q35, Skylake-Client, AHCI, ipxe/e1000, usb-kbd) | verified | `run/sponge-usb-kbd-via-qmp.run` |
| 3 | (q35, Skylake-Client, AHCI, pc_nic/e1000, PS/2+tablet) | verified | `run/sponge-pc-nic.run` |
| 4 | (q35, Skylake-Client, AHCI, pc_nic/e1000, usb-kbd) | gap | disjoint driver chains; never bridged in a single boot |
| 5 | (q35, Skylake-Client, NVMe, ipxe/e1000, PS/2+tablet) | verified | `run/sponge-desktop-disk-nvme.run` |
| 6 | (q35, Skylake-Client, NVMe, ipxe/e1000, usb-kbd) | gap | NVMe + usb-kbd not combined |
| 7 | (q35, Skylake-Client, NVMe, pc_nic/e1000, PS/2+tablet) | gap | NVMe + pc_nic not combined |
| 8 | (q35, Skylake-Client, NVMe, pc_nic/e1000, usb-kbd) | gap | NVMe + pc_nic + usb-kbd not combined |
| 9 | (i440fx, Skylake-Client, AHCI, ipxe/e1000, PS/2+tablet) | smoke-only | `run/sponge-boot-i440fx.run` (PIIX4 IDE; not product-verified) |
| 10 | (i440fx, Skylake-Client, AHCI, ipxe/e1000, usb-kbd) | gap | i440fx + usb-kbd not combined |
| 11 | (i440fx, Skylake-Client, AHCI, pc_nic/e1000, PS/2+tablet) | gap | i440fx + pc_nic not combined |
| 12 | (i440fx, Skylake-Client, AHCI, pc_nic/e1000, usb-kbd) | gap | i440fx + pc_nic + usb-kbd not combined |
| 13 | (i440fx, Skylake-Client, NVMe, ipxe/e1000, PS/2+tablet) | gap | i440fx + NVMe not combined (i440fx lacks NVMe auto-attach) |
| 14 | (i440fx, Skylake-Client, NVMe, ipxe/e1000, usb-kbd) | gap | i440fx + NVMe + usb-kbd not combined |
| 15 | (i440fx, Skylake-Client, NVMe, pc_nic/e1000, PS/2+tablet) | gap | i440fx + NVMe + pc_nic not combined |
| 16 | (i440fx, Skylake-Client, NVMe, pc_nic/e1000, usb-kbd) | gap | i440fx + NVMe + pc_nic + usb-kbd not combined |

## Fresh-build check (W6 step 5)

The plan-mandated fresh-build proof is captured in
`docs/evidence/phase12-fresh-build.log` (the `make run/sponge-pc-nic`
output after `mv genode/build/x86_64 → var/scratch/x86_64-original`
and a fresh `./tool/build prepare`):

- `genode/build/x86_64` was moved aside to `var/scratch/x86_64-original`
  (NOT deleted, per plan).
- `./tool/build prepare` re-created the build directory and applied a
  fresh managed block.
- The fresh managed block contains `REPOSITORIES += .../repos/pc`
  exactly **once** (active entry); the one commented reference at
  line 61 is the upstream template's default comment, not an active
  entry.
- `./tool/patches verify` against the committed ledger exited 0
  (9/9 rows present, all ancestors of `HEAD`, all passing).
- `make -j1 -C genode/build/x86_64 run/sponge-pc-nic KERNEL=sel4 BOARD=pc`
  succeeded in the fresh build, with `pc_nic: bound device` and
  `nic_router: uplink DHCP acquired` markers observed
  (`**Intel(R) PRO/1000 Network Driver**` at line 2098, `**dhcp offer
  from 10.0.2.2, offering 10.0.2.15**` at line 2161, `**dynamic IP
  config: interface 10.0.2.15/24**` at line 2162, `**sponge-pc-nic: PASS**`
  at line 2162, `**Run script execution successful.**` at line 2164).
- The original `genode/build/x86_64` was preserved at
  `var/scratch/x86_64-original` per the plan-mandated "preserve"
  rule. The fresh build is the active build directory.

## `tool/dist --storage` receipts (W6 step 6)

| Mode | Artifact | Hash | Wall time | Notes |
|---|---|---|---|---|
| `--storage ahci` (default) | `var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img` (1,169,506,304 bytes) | `fed23926b3d9cd481dca17784f131a11823d8ca8877c9c0fd3248d43e4ce4220` | 16:38 → 17:28 (~50 min cold build; subsequent runs are incremental) | Default product-media behavior preserved; AHCI snapshot at 17:28. |
| `--storage nvme` (opt-in) | `var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img` (same artifact for Phase 12; NVMe is runtime-routed, not file-routed) | hash above; the host image is the bootloader | 17:32 → 17:46 (full Alpha corroboration) | NVMe chain verified at runtime via `run/sponge-desktop-disk-nvme.run`; the product `.img` is the bootloader media (image/disk auto-attaches it to q35's implicit ICH9), and the NVMe-attached `bin/nvme_disk.img` carries the GENODE partition. |

## W6 sweep — Risk 28 verbatim policy

W6 runs every scenario **one at a time** with `make -j1` and bounded
`run_genode_until` gates. No concurrent `make` in
`genode/build/x86_64` at any point in the sweep. The serialized order
and per-scenario results live in `docs/evidence/task-6-phase12-regression.md`.

## Honest non-claims (grep-ability)

- **No real-hardware cell.** `docs/15-hardware-compatibility.md` has
  no row or cell carrying `target: real-hardware`. The validator
  rejects any such cell with exit 2 and the exact message
  `real hardware is a Phase 15 deliverable; not a Phase 12 cell`.
- **No vendored-tree edit.** `git diff genode/` is empty post-W6 (the
  plan constraint W6 step 11).
- **No `target: real-hardware` in `docs/15` and no durable `.omo/`
  reference in any durable doc.** `.gitignore` excludes `.omo/`; the
  grep contract is `git grep -nF .omo/ docs/ run/ tool/ repos/` →
  empty.
- **No new host dependency.** Phase 12 uses the same QEMU 11.0.3,
  Tcl/expect, GNU Make, Mojo SDK (already pinned by `pyproject.toml` /
  `uv.lock`), and the same six media-creation host tools from §7.3.
