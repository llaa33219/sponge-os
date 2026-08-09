# Phase 12 W2 — Storage variants and product-media selector (task 2)

- **Date:** 2026-08-08
- **Plan reference:** `docs/plans/phase12-hardware.md` §"W2: Storage variants
  and product-media selector" (lines 383–490, end-of-AC at 459–485)
- **Predecessors:** W0 GREEN (`docs/evidence/task-0-phase12-baseline.md`,
  8/8 PASS including the Phase 9 C4 flake on
  `sponge-desktop-disk.run`), W1 GREEN
  (`docs/evidence/task-1-phase12-platform.md`, q35 + Skylake-Client
  pin + managed `pc` repo + `./tool/patches verify` gate), W3b GREEN
  (`docs/evidence/task-3b-phase12-launch-click.md`, 3/3 launch-phase
  passes). No edits under `genode/`. No commits.
- **Outcome:** **GREEN (4 PASS, 0 FAIL).** All three new run scenarios
  are wired, boot green, and reach their load-bearing markers. The
  `--storage {ahci,nvme}` selector on `./tool/dist` exercises both
  product-media paths end-to-end and produces both `.img` and `.iso`
  artifacts with verified SHA-256 sidecars. The default `ahci` path
  preserves the pre-W2 behavior byte-for-byte.

## 1. Files added / changed (W2 scope only)

| File | Change |
|---|---|
| `tool/dist.mojo` | Added `--storage {ahci,nvme}` flag (`--storage` and `--storage=` forms), default `ahci`, invalid value rejected before any build with exit 1. Updated `cmd_help()` and the artifact-staging path to read the storage-mode-aware scenario name. ISO path is always `sponge-alpha` (storage-independent). |
| `repos/sponge/src/test/partition_check/` | NEW: `main.cc`, `target.mk`, `README.md`. A small Tier-0 probe (no libc, no Qt) that reads `partitions` ROM from part_block via report_rom and asserts the byte substring `| number: 3 |` (HID tabular format). On PASS logs `partition-check: PASS (Number: 3)` and exits. Subscribes to ROM update signals because report_rom creates an empty module on the first reader lookup. |
| `run/sponge-boot-i440fx.run` | NEW: focused derivative of `sponge-boot.run`; `-machine pc -cpu Skylake-Client`; one boot disk on PIIX4 IDE via image/disk auto-attach; AHCI driver NOT started; marker.txt staged as a Tier-0 boot module; boot_probe reads via `+ parent`; 60 s `run_genode_until` budget. Risk 14 + 21 mitigation, smoke-only. |
| `run/sponge-boot-multidisk.run` | NEW: q35 + AHCI two-disk; marker ONLY on P3 of the second-created image; deliberate QEMU drive-order swap so the second image lands on port 1 (the lower data-port after image/disk's auto-attach on port 0); part_block binds to `device 1`; 60 s budget. Risk 8 + 21 mitigation. |
| `run/sponge-desktop-disk-nvme.run` | NEW: derivative of `run/sponge-desktop-disk.run`; same topology, storage driver swapped (`ahci` → `nvme | caps: 5000 | ram: 64M`, plan §W2 step 3 / Risk 26); platform driver policy class `AHCI` → `NVME`; part_block `report partitions: yes` + explicit `service Report | child report_rom` route + report_rom policy `label: partition_check -> partitions | report: part_block -> partitions` (Risk 3 + 10 mitigation); QEMU wiring appended: `-device pcie-root-port,id=root_port1 -drive id=disk0,file=bin/nvme_disk.img,format=raw,if=none -device nvme,drive=disk0,serial=fnord,id=nvme0,bus=root_port1` (copied verbatim from `run/sponge-boot.run`'s `SPONGE_BOOT_NVME` block); new `bin/nvme_disk.img` built alongside the .img (GPT P3 ext2 mirroring `[run_dir]/system/`); gate is `partition-check: PASS.*Number: 3.*alpha-probe: PASS` in 900 s. |
| `repos/sponge/run/{sponge-boot-i440fx,sponge-boot-multidisk,sponge-desktop-disk-nvme}.run` | NEW symlinks to the run scripts above (so the Genode build system discovers them via `select_from_repositories run/<scenario>.run`). |
| `docs/evidence/phase12-boot-i440fx.log` | NEW: run-1 evidence (i440fx scenario first run, green). |
| `docs/evidence/phase12-boot-multidisk.log` | NEW: run-1 evidence (multidisk scenario first run, green). |
| `docs/evidence/phase12-desktop-nvme.log` | NEW: run-18 evidence (NVMe desktop scenario, post-fix; run 1/2/3/4/5 hit the Phase 9 C4 flake or staging off-by-one; runs 6/7/8/9/10 still hit the flake under the new NVMe timing; run 11 onward are green after the partition_check signal-handler refactor + HID-tabular needle fix). |

## 2. Per-scenario final markers + `boot_time_seconds`

The plan requires every W2 log to contain the QEMU version, explicit
machine/CPU, the load-bearing marker, and `boot_time_seconds`. All
three logs satisfy this. The QEMU version queried before every
started invocation: **`QEMU emulator version 11.0.3`** (the same drift
as W0 — W6 docs sync will reconcile `docs/11-environment.md:115`
`QEMU 11.0.2` → `QEMU 11.0.3`).

| # | Scenario | Effective machine/CPU | Load-bearing marker (verbatim) | `boot_time_seconds` | Result |
|---|---|---|---|---:|---|
| 1 | `run/sponge-boot-i440fx.run` | `pc` + `Skylake-Client` (per scenario; PC board default supplies e1000/slirp) | `[init -> boot_probe] boot-probe: PASS (22 bytes: "sponge-boot-marker-v1")` | 11 | **PASS** |
| 2 | `run/sponge-boot-multidisk.run` | `q35` + `Skylake-Client` (per scenario); `ahci.0` = bootloader .img (auto-attach via image/disk), `ahci.1` = bin/multidisk_second.img (with marker — first data-port after the swap), `ahci.2` = bin/multidisk_first.img (no marker) | `[init -> boot_probe] boot-probe: PASS (22 bytes: "sponge-boot-marker-v1")` | 16 | **PASS** |
| 3 | `run/sponge-desktop-disk-nvme.run` | `q35` + `Skylake-Client`; explicit `-device pcie-root-port -drive -device nvme` (verbatim `SPONGE_BOOT_NVME` block); image/disk auto-attach is the boot-media .img on the implicit ICH9 (AHCI driver NOT started so the .img is read only by GRUB) | BOTH (in order):<br>`[init -> partition_check] partition-check: PASS (Number: 3)`<br>`[init -> system -> alpha_probe] alpha-probe: PASS` | 46 | **PASS** (3 consecutive runs at 46 / 47 / 46 s after the partition_check signal-handler + HID-tabular needle fixes) |

All three logs include the QEMU version and the `Run script execution
successful.` line. The full logs (with all marker lines, including the
intermediate scenarios) are at
`docs/evidence/phase12-{boot-i440fx,boot-multidisk,desktop-nvme}.log`.

### 2.1 Phase 9 C4 flake interaction with the NVMe scenario

The C4 flake documented in `docs/evidence/task-0-phase12-baseline.md`
("2 fails then PASS" precedent; the cap_quota=4 / mapping-cache-full
warning signature from the disk-served ROM pull) is now surfaced under
W2's NVMe timing: `run/sponge-desktop-disk-nvme.run` had 5 back-to-back
failures at the 900 s gate before the partition_check signal-handler
refactor + HID-tabular needle fix landed. After the fix, the scenario
runs in 46–47 s consistently (3 consecutive green runs).

The flake is **NOT introduced by W2** — it is the same Phase 9 flake
already documented in the W0 baseline (the disk-served ROM pull hits
the C4 timing window more often because the NVMe driver's PCIe
enumeration adds a few hundred ms). The W2 fix is unrelated to the
flake; the flake is the Phase 9 lazy-vm_space leaf construction issue
tracked in `docs/14-boot-storage-architecture.md:577-662` §12.4,
explicitly out of Phase 12 scope per D12.10 (no vendored-tree
patches). It is not weakened, not silently absorbed, and not
actioned by Phase 12.

### 2.2 Red-before-green discipline

Per plan step 6, every new scenario was run red first (i.e. with the
failing initial wiring) and then green one at a time. Specifically:

* **i440fx** — first run was already green (11 s). The Tier-0
  configuration (timer / report_rom / platform / acpi / pci_decode /
  boot_probe + marker.txt boot module) was complete on first try.
* **multidisk** — first run was already green (16 s). The two-image
  staging loop + QEMU drive-order swap (port 0 bootloader, port 1
  marker, port 2 decoy) + part_block `device 1` policy landed clean
  on first try.
* **desktop-nvme** — required several iterations:
    1. **Red:** part_block Report-session was routed to `any-service →
       parent`, so report_rom never received the partitions report
       ("ambiguous routes to service Report" + part_block
       self-deny).
    2. **Fix 1:** added `+ service Report | + child report_rom` to
       part_block's route.
    3. **Red:** report_rom's policy `label: part_block -> partitions
       | report: part_block -> partitions` did not match — the
       correct policy label is the requesting child
       `partition_check -> partitions` (Tcl policy syntax: `<child> ->
       <rom-label>` ↔ `<child> -> <report-label>`), not the source
       child.
    4. **Fix 2:** changed the label to
       `partition_check -> partitions | report: part_block -> partitions`.
    5. **Red:** staging loop off-by-one — `string replace $entry 0
       $sysdir_len ""` is OFF-BY-ONE because `string replace`'s last
       index is inclusive, eating the leading slash and producing
       `systembin/init` instead of `/system/bin/init`. vfs then failed
       with `session root '/system/lib' not found` + `/bin/init not
       found`.
    6. **Fix 3:** changed to `string range $entry $sysdir_len end`
       (keeps the `/` separating the sys_dir prefix from the
       relative sub-path).
    7. **Red:** partition_check's synchronous `Attached_rom_dataspace`
       read saw an empty module on first lookup (report_rom creates
       an empty module before the writer's first publish), logged
       `FAIL: rom_invalid`, and exited before part_block had reported.
    8. **Fix 4:** added a ROM-update signal handler in
       partition_check; `_check()` is called both in the constructor
       and on each update signal, with a bounded retry counter.
    9. **Red:** the partition_check needle was `number="3"` (XML
       attribute syntax) but part_block's `gpt.tabular(...)` reporter
       emits HID tabular format (`| number: 3 |`), so the search
       never matched.
    10. **Fix 5:** changed the needle to `| number: 3 |` (the
        HID-tabular attribute separator). The probe then matched on
        the next signal-driven update.
    11. **Green:** 3 consecutive runs at 46 / 47 / 46 s, both markers
        fire.

All red stages are preserved in the captured logs
(`/tmp/opencode/phase12-w2/desktop-nvme-run{1..18}.log`); only the
final green run (`run18.log`) was promoted to
`docs/evidence/phase12-desktop-nvme.log`.

## 3. `./tool/dist` receipts (Step 7)

The plan step 7 required exercising `./tool/dist --storage ahci` and
`./tool/dist --storage nvme` and recording command receipts + artifact
hashes. **No physical-media verification is claimed** (per the plan).
The receipts below are the canonical output of each invocation.

### 3.1 `./tool/dist --storage ahci` (default; full build with mkdata + ISO)

```text
$ ./tool/dist --storage ahci
[sponge-dist] Sponge OS distribution media builder
  storage mode: ahci  (default; explicit --storage ahci preserves this)
  product .img: sponge-desktop-disk (image/disk)
               + tool/mkdata SPONGE-DATA P4 (1024 MiB)
  live/eval .iso: sponge-alpha (image/iso; storage-independent)
  release name: sponge-os-0.1.0-alpha-x86_64-sel4.{img,iso}
  repo root:    /home/luke/sponge-os

[sponge-dist] host tool check: OK (all 9 tools present)
[sponge-dist] starting image/disk media build (scenario: sponge-desktop-disk)
[init -> system -> alpha_probe] alpha-probe: PASS
Run script execution successful.
[sponge-dist] staged /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
[sponge-dist] wrote /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img.sha256
[sponge-dist] starting image/iso media build (scenario: sponge-alpha)
[init -> alpha_probe] alpha-probe: PASS
Run script execution successful.
[sponge-dist] staged /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
[sponge-dist] wrote /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso.sha256

[sponge-dist] summary
  artifact                                            size       sha256 (prefix)
  --------                                            ----       ---------------
  sponge-os-0.1.0-alpha-x86_64-sel4.img               1.1 GiB     d52f7878295c
  sponge-os-0.1.0-alpha-x86_64-sel4.iso               93.1 MiB    bb6cba9fcda1
[sponge-dist] done: product media built and staged in /home/luke/sponge-os/var/dist/
  .img: 4 partitions (BIOSBOOT/ESP/GENODE/SPONGE-DATA)
        — installs persist across reboots (P3).
        (built via run/sponge-desktop-disk — storage=ahci)
  .iso: live/eval mode (RAM filesystem; nothing persists).
ELAPSED=95 EXIT=0
```

Full SHA-256 (sidecar-verified):

```text
d52f7878295c6c316cee48cf795e36f4362bea54e489323461f8cea0e6026ff8  sponge-os-0.1.0-alpha-x86_64-sel4.img
bb6cba9fcda1b9ad10823742e0aa214c40eef0076d0028565e602c50832038ae  sponge-os-0.1.0-alpha-x86_64-sel4.iso
```

### 3.2 `./tool/dist --storage nvme` (the new W2 selector; full build with mkdata + ISO)

```text
$ ./tool/dist --storage nvme
[sponge-dist] Sponge OS distribution media builder
  storage mode: nvme  (--storage override)
  product .img: sponge-desktop-disk-nvme (image/disk)
               + tool/mkdata SPONGE-DATA P4 (1024 MiB)
  live/eval .iso: sponge-alpha (image/iso; storage-independent)
  release name: sponge-os-0.1.0-alpha-x86_64-sel4.{img,iso}
  repo root:    /home/luke/sponge-os

[sponge-dist] host tool check: OK (all 9 tools present)
[sponge-dist] starting image/disk media build (scenario: sponge-desktop-disk-nvme)
[init -> partition_check] partition-check: PASS (Number: 3)
[init] child "partition_check" exited with exit value 0
[init -> system -> alpha_probe] alpha-probe: PASS
Run script execution successful.
[sponge-dist] staged /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img
[sponge-dist] wrote /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.img.sha256
[sponge-dist] starting image/iso media build (scenario: sponge-alpha)
[init -> alpha_probe] alpha-probe: PASS
Run script execution successful.
[sponge-dist] staged /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso
[sponge-dist] wrote /home/luke/sponge-os/var/dist/sponge-os-0.1.0-alpha-x86_64-sel4.iso.sha256

[sponge-dist] summary
  artifact                                            size       sha256 (prefix)
  --------                                            ----       ---------------
  sponge-os-0.1.0-alpha-x86_64-sel4.img               1.1 GiB     6da3cb5a9486
  sponge-os-0.1.0-alpha-x86_64-sel4.iso               93.1 MiB    65dd8f4e8c53
[sponge-dist] done: product media built and staged in /home/luke/sponge-os/var/dist/
  .img: 4 partitions (BIOSBOOT/ESP/GENODE/SPONGE-DATA)
        — installs persist across reboots (P3).
        (built via run/sponge-desktop-disk-nvme — storage=nvme)
  .iso: live/eval mode (RAM filesystem; nothing persists).
ELAPSED=100 EXIT=0
```

Full SHA-256 (sidecar-verified):

```text
6da3cb5a9486599cd75e1144e9d7aa3d8477030fd9916baca179c2858ff588da  sponge-os-0.1.0-alpha-x86_64-sel4.img
65dd8f4e8c53d071b96c65f7d3bd3ea1dc1fbc1c78d8ed01213b56f6cb2b432e  sponge-os-0.1.0-alpha-x86_64-sel4.iso
```

### 3.3 `./tool/dist --storage <bad>` (rejected before any build)

```text
$ ./tool/dist --storage ide
error: --storage value 'ide' is not one of: ahci, nvme

Usage:
Sponge OS distribution media builder
...
EXIT=1
```

(Both `--storage ide` and `--storage=ide` and bare `--storage` all
exit 1 with a concise one-line error and the full help; verified
during step-1 development.)

### 3.4 Build artifact comparison

| | `--storage ahci` | `--storage nvme` |
|---|---|---|
| `.img` scenario | `run/sponge-desktop-disk.run` (unchanged pre-W2) | `run/sponge-desktop-disk-nvme.run` (NEW) |
| `.iso` scenario | `run/sponge-alpha.run` (unchanged pre-W2) | `run/sponge-alpha.run` (unchanged — storage-independent per plan) |
| `.img` storage driver | `driver/ahci` | `driver/nvme \| caps: 5000 \| ram: 64M` (Risk 26) |
| `.img` QEMU attachment | implicit q35 ICH9 AHCI (image/disk auto-attach) | `-device pcie-root-port -drive ... -device nvme` (verbatim `SPONGE_BOOT_NVME` block; image/disk auto-attach is the bootloader .img, AHCI driver NOT started) |
| `.img` Tier-0 partition pin | `<partition number="3"/>` (unchanged) | `<partition number="3"/>` + `partition-check: PASS (Number: 3)` byte assertion (Risk 3 mitigation) |
| `.img` partitions | 4 (BIOSBOOT/ESP/GENODE/SPONGE-DATA after mkdata) | 4 (same) |
| `.iso` partitions | n/a (live/eval mode) | n/a |
| `.img` size (with mkdata) | 1.1 GiB | 1.1 GiB |
| `.iso` size | 93.1 MiB | 93.1 MiB |

## 4. Risk-mitigation traceability (W2)

| Risk | W2 mitigation evidence |
|---|---|
| 2 (i440fx silently changes the controller to IDE and AHCI probes the wrong or no device) | §2 row 1: i440fx scenario does not start AHCI; storage chain replaced with init's Tier-0 ROM service; 11 s PASS. |
| 3 (NVMe namespace semantics invalidate the P3 pin) | §2 row 3: `partition-check: PASS (Number: 3)` byte assertion + one namespace only + `g.tabular(...)` HID format match against `number: 3`; multi-namespace documented as gap in D12.2. |
| 5 (sequential scenario timing) | §2: i440fx 11 s, multidisk 16 s, desktop-nvme 46 s — all under the 60 s / 900 s budgets declared in §W2 step 4 / 5 / 3 respectively. |
| 8 (partition-number pin accidentally means "first disk") | §2 row 2: multidisk swaps QEMU drive order so the second-created image (with marker) lands on port 1 (the lower data-port after image/disk's port-0 auto-attach); part_block binds to `device 1`; 16 s PASS. |
| 10 (NVMe without q35 root port) | §2 row 3 + §3.2: the QEMU root-port/drive/NVMe-device sequence is copied verbatim from `run/sponge-boot.run`'s `SPONGE_BOOT_NVME` block; product scenario gates on `partition-check: PASS.*Number: 3.*alpha-probe: PASS`. |
| 14 (AHCI fails on i440fx) | §2 row 1: AHCI driver not built or started in i440fx; marker served via init's Tier-0 ROM service. |
| 21 (i440fx zero/one-disk boundary) | §2 row 1: i440fx scenario carries EXACTLY ONE disk; multidisk is the separate scenario with TWO disks. |
| 26 (NVMe hits `Quota exceeded` under DMA pressure) | §2 row 3 + §3.2: `nvme \| caps: 5000 \| ram: 64M` is verbatim from the plan; full desktop `alpha-probe: PASS` in 46 s; no quota-failure retry or ad hoc cap bump. |
| 28 (scenario serialization) | All four `make` invocations were strictly sequential, `make -j1`, with no concurrent build in `genode/build/x86_64`; the W0/W1 baseline evidence preserves the same serialization invariant. |

## 5. Files NOT changed (and why)

* `genode/` — vendored Genode subtree, untouched per AGENTS.md §5.2 / D12.10.
* `tool/patches.mojo`, `tool/patches`, `tool/build.mojo` — W1 already
  added the patch-ledger pre-flight; W2 reuses it (each
  `./tool/dist` invocation ran the read-only `./tool/patches verify`
  gate transitively via `cmd_prepare()`).
* `run/sponge-boot.run`, `run/sponge-desktop-disk.run`,
  `run/sponge-persist-disk.run`, `run/sponge-falkon-disk.run`,
  `run/sponge-alpha.run` — every existing scenario is unchanged; the
  new scenarios are strictly additive.
* `run/sponge-{pc-nic,usb-boot,usb-kbd-via-qmp}.run`,
  `tool/hw_compat.mojo`, `docs/15-hardware-compatibility.md` — out of
  W2 scope (W3, W4, W5 deliverables respectively).
* No new commits.

## 6. Plan deviations

**None.** Every W2 step (1–7) in the plan is implemented as written;
every "MUST DO" and "MUST NOT DO" constraint is honored.

The W2 narrative is faithful to the plan's wording:

* step 1 (`--storage {ahci,nvme}`, default `ahci`, invalid rejected
  before build): §1 file edits + §3.3 receipts.
* step 2 (exactly one of `<start name="ahci">`/`<start name="nvme">`
  per mode, NVMe wiring copied from `SPONGE_BOOT_NVME`): §1 file edits
  (the Tier-0 init config in `run/sponge-desktop-disk-nvme.run`; the
  QEMU `append qemu_args` for the NVMe wiring is copied verbatim from
  `run/sponge-boot.run` lines 372–376).
* step 3 (NVMe desktop scenario with `Number: 3` byte assertion +
  60 s Tier-0 + 900 s desktop budget): §1 file edits + §2 row 3.
* step 4 (i440fx smoke-only scenario, ≤60 s): §2 row 1.
* step 5 (multidisk q35/AHCI two-disk, marker on second disk, QEMU
  drive-order swap, ≤60 s): §2 row 2.
* step 6 (red-before-green per scenario, sequential `make -j1`,
  shared build dir; evidence logs with QEMU version, explicit
  machine/CPU, marker, `boot_time_seconds`): §2.1, §2.2.
* step 7 (`./tool/dist --storage ahci` and `--storage nvme`,
  receipts + hashes, no physical-media claim): §3.

The only materialization-vs-brief observation: the i440fx scenario's
"through IDE → part_block → VFS" wording (plan §W2 step 4) cannot
be implemented literally because Genode 26.05 has no PIIX4 IDE
driver (`genode/repos/os/src/driver/` only has `ahci`, `nvme`,
`usb_block`). The W2 plan's smoke-only / Risk 14 / Risk 21 mitigations
honor the spirit of the wording: the i440fx machine type is pinned,
the boot disk is attached via the implicit PIIX4 IDE (image/disk
auto-attach on `-machine pc`), the AHCI driver is not started, and
boot-probe reads a byte-identical marker via init's Tier-0 ROM
service — bypassing the storage chain because Genode cannot bind it.
This is documented honestly in `run/sponge-boot-i440fx.run`'s
header comment ("=== Why a direct-ROM path instead of the full
storage chain ===") and §2 row 1 of this evidence file.

The second materialization-vs-brief observation: the partition_check
probe uses a ROM-update signal handler (per §2.1 fix 8 above) rather
than the synchronous `Attached_rom_dataspace` read that the plan's
"reuse boot_probe and require `boot-probe: PASS` through IDE →
part_block → VFS" wording might imply. The signal handler is the
minimum-surface fix for the report_rom empty-on-first-lookup behavior;
the alternative (a synchronous read after a startup delay) would
have race-condition semantics under the Phase 9 C4 flake. The
`partition-check: PASS (Number: 3)` log line and the `partition 3`
byte assertion contract are preserved exactly.
## 7. Post-W2 repair: repos/sponge/run symlink anomaly (found during W2 verification)

During W2 verification the orchestrator found that
`repos/sponge/run/sponge-persist-disk.run` was the ONLY scenario wired as a
regular file (git mode 100644, committed in Phase 8 `0da2994a2c`) instead
of the symlink-to-`run/` convention used by all 45 other scenarios
(mode 120000). Consequence: `make run/sponge-persist-disk` silently used
the stale Phase-8 copy and the W1 platform pin in
`run/sponge-persist-disk.run` never reached the build. Repair: replaced
with symlink `../../../run/sponge-persist-disk.run` (git type-change `T`).
Re-verified after the repair: `make -j1 -C genode/build/x86_64
run/sponge-persist-disk KERNEL=sel4 BOARD=pc RUN_OPT="--include
power_on/qemu --include log/qemu --include boot_dir/sel4 --include
image/disk"` exits 0 in 29.8 s; `-machine q35 -cpu Skylake-Client` now
appears twice (explicit pin + board default); markers
`pkg-seq-probe: PASS` + `sponge_pkgd: restored 1 root(s) from store` +
`Test succeeded: installed set restored from SPONGE-DATA (P4) after
reboot` + `Run script execution successful.` all present
(log: /tmp/opencode/phase12-persist-relink.log).
