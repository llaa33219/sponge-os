# Phase 16 P3.1 — Provider-stall isolation RED receipt

**Date:** 2026-10-28  
**Scenario commit:** `66509efd9c` (`test(run): add the provider-stall isolation red scenario`)  
**Machine:** QEMU q35, `Skylake-Client`, KVM, base-sel4. This is not a
real-hardware result.  
**Current verdict:** intentional RED on the shared `vfs_var` provider. The
P3.2 `vfs_pkg` / `vfs_log` split has not been implemented.

## 1. Reproduction

Run serially from the repository root:

```sh
make -j1 -C genode/build/x86_64 run/sponge-provider-stall \
  KERNEL=sel4 BOARD=pc RUN_OPT="--include image/disk"
```

The scenario exits nonzero at its intended RED verdict. It must not be
reported as a passing scenario until P3.2 lands.

Final observed markers:

```text
provider-stall-control: PASS — injection observed, log consumer degraded as expected, container alive, media healthy (software provider stall — NOT a physical media/USB failure)
provider-isolation-probe: PASS path=sys-rom
provider-isolation-probe: RED path=pkgd-store reason=create_parked_30000ms
provider-isolation-probe: DONE
provider-stall: path=sys-rom    PASS (rom_sys served from the healthy disk during the stall)
provider-stall: path=pkgd-store RED (shared vfs_var fate — the package-store create rode the same provider instance as the stalled log path)
provider-stall: path=log-sink   DEGRADED (expected — sponge_logdisk's File_system create is parked on the stalled provider)
provider-stall: RED - today's shared vfs_var gives the package store and the log sink ONE provider path
```

The overall `provider-isolation-probe: PASS` and `provider-stall: PASS`
markers are absent. `PASS path=sys-rom` is only the independent positive
control, not an isolation verdict.

## 2. Why the RED is meaningful

The positive controls rule out a blind detector and a dead machine:

- `blk_stall` logs `ep_wedge: alive — entrypoint wedging now`.
- `sponge_logdisk` never logs `logging to` or `tick alive`, so the injected
  log-path degradation really lands.
- The bystander continues past `bystander: alive t=34000 ms`, so the fault
  does not kill the whole container.
- `partition-check: PASS (Number: 3)` proves that the same disk still
  enumerates P3/P4 through AHCI and `part_block`.
- The independent `rom_sys -> vfs_sys -> part_block/P3` chain returns the
  exact `sponge-provider-stall sys-rom marker\n` payload during the stall.
- Host-side P4 read-back finds no `/pkgd/store/pstall.txt`, corroborating
  that the store create did not secretly complete.

This is a deterministic software provider stall, not a simulated physical
USB/media failure. P3 remains usable while the shared P4 provider takes down
both the log consumer and the otherwise unrelated package-store consumer.

## 3. Harness defects resolved before accepting the receipt

The first versions of the new scenario did not produce a valid RED receipt.
The final test contains these minimal corrections:

1. **Genode warning policy:** `Worker` copy construction and assignment are
   deleted, fixing the `-Werror=effc++` build failure for its pointer member.
2. **Declared-but-unserved fault:** `blk_stall` declares `Block` in
   `<provides>`. Without that declaration init rejected the route immediately
   (`Block ... denied` and `stop because parent denied Block-session`) rather
   than leaving the create pending. With the declaration, `vfs_var` remains
   parked and the store worker reaches `create_parked_30000ms`.
3. **Probe independence:** the ROM worker finishes before the store worker
   starts. Starting the parked store worker first held `Component_env`'s
   component-wide session mutex and prevented the supposedly independent ROM
   worker from issuing its own session create. Serializing the checks changed
   sys-ROM from `read_parked_30000ms` to a completed ROM read.
4. **Page-backed ROM size:** `cached_fs_rom` exposed a 4096-byte dataspace for
   the 37-byte marker. The probe now requires `sz >= expected` and compares
   only the expected payload bytes instead of requiring exact dataspace-size
   equality. Host-side e2cp independently verified the on-media bytes.

The `cap donation is 11 but used caps is 17, 'rom_sys -> /'` warning was not
treated as a root cause: the identical accounting warning occurs in passing
product logs. No quota, route, or Genode base change was made.

## 4. Scope and next gate

P3.1 adds only the scenario, its probe, the committed run symlink, and this
curated receipt. It does not change the product provider topology or partition
layout. P3.2 must split the P4 execution paths into `vfs_pkg` and `vfs_log`
on the existing partition, keep the stall on the log path, and flip this same
scenario to store round-trip PASS plus host-side media read-back without
weakening the positive controls.

## 5. P3.2 green receipt (the split)

**Date:** 2026-11-01. QEMU q35 / OVMF, `Skylake-Client`, KVM, base-sel4.
Not a real-hardware result. These receipts accompany the P3.2 provider split.

P3.2 splits the shared `vfs_var` into `vfs_pkg` (default-policy root
`/pkgd`, Block -> healthy `part_block`) and `vfs_log` (policy
`label_prefix: sponge_logdisk ->`, root `/log`, Block -> `blk_stall`) on
the existing partition 4 — D16R.8/D16R.2, no new partitions, no new
base semantics. Consumer routes re-aimed: the probe's `File_system` to
`vfs_pkg`, `sponge_logdisk`'s to `vfs_log`; the `part_block` P4 row is
`label_prefix: vfs_pkg | partition: 4 | writeable: yes`.

### 5.1 Green receipt — `run/sponge-provider-stall.run`

Command (serial, D16R.13):

```sh
LD_PRELOAD=/tmp/opencode/libclamp_io_uring.so \
  make -j1 -C genode/build/x86_64 run/sponge-provider-stall \
  KERNEL=sel4 BOARD=pc RUN_OPT="--include image/disk"
```

Final observed markers (`Run script execution successful.`, exit 0):

```text
provider-stall-control: PASS — injection observed, log consumer degraded as expected, container alive, media healthy (software provider stall — NOT a physical media/USB failure)
provider-isolation-probe: PASS path=sys-rom
provider-isolation-probe: PASS path=pkgd-store
provider-isolation-probe: PASS
provider-stall: path=sys-rom    PASS (rom_sys served from the healthy disk during the stall)
provider-stall: path=pkgd-store PASS (store round-trip + media read-back)
provider-stall: path=log-sink   DEGRADED (expected — the stall stays on the log path, sponge_logdisk never reached its File_system)
provider-stall: PASS - the log-path stall stayed on its own provider path (the vfs_pkg/vfs_log split holds): system ROM and the package store kept working while the log sink degraded
```

Host-side media read-back (the green branch's e2cp of
`/pkgd/store/pstall.txt`) returned the exact probe payload
`provider-isolation-probe store roundtrip` — the isolated store write
really landed on SPONGE-VAR while the log path stayed parked. Every
positive control and marker from the P3.1 receipt is unchanged; only
the per-path store verdict flipped RED -> PASS.

### 5.2 Two wiring facts the green run settled

1. **The store region is `policy root` + the client-preferred session
   root.** The `vfs` server appends the session label's last element to
   the policy `root` (`os/src/server/vfs/main.cc`), so the probe's
   `<fs/>` needs `label: /store/` for its `/store/pstall.txt` to land
   on the staged `/pkgd/store` region — the label is the documented
   client-preferred root (`docs/evidence/logdisk-field-diagnosis.md`
   §3(c)). With the pre-P3.2 bare label the file would have landed at
   `/pkgd/pstall.txt` and the committed read-back path would not have
   found it.
2. **Upstream `part_block` allows only ONE Block session per
   partition** — the second create is refused `partition 4 already in
   use` (`os/src/server/part_block/main.cc:286`). Two healthy `vfs`
   providers on P4 therefore cannot share one `part_block`. The product
   wiring in `run/sponge-desktop-disk-uefi-usb.run` gives each P4
   provider its own `part_block` instance (`part_block` /
   `part_block_log`, same binary, same row semantics); `usb_block`
   serves both sessions (its `Session_map` allocates one index per
   session, `os/src/driver/usb_block/main.cc`). The stall scenario
   needs only one instance (the log path's Block source is `blk_stall`).
   D16R.9 is respected: no base change, no new semantics.

### 5.3 Product logdisk gate — `sponge-desktop-disk-uefi-usb`, `SPONGE_LOGDISK=on`

Command: `SPONGE_LOGDISK=on LD_PRELOAD=… make -j1 -C genode/build/
x86_64 run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc`. All
structural gates green, `alpha-probe: PASS`, 0 wedge / 0 starvation
lines, and the sink's session through `vfs_log` -> `part_block_log` ->
P4 SPONGE-VAR was granted:

```text
sponge-desktop-disk-uefi-usb: persistent log sink ON (SPONGE_LOGDISK=on) — SPONGE-VAR /log/boot.log (+ .1)
[init -> sponge_logdisk] sponge_logdisk: logging to /boot.log (cap 8388608 bytes)
```

No `already in use`, no `denied Block-session`, no `denied
File_system` anywhere in the serial. The scenario's QEMU runs with
`-snapshot`, so the host-side `.img` cannot show the written file; the
receipt standard is the same as the original logdisk receipts
(`logdisk-field-diagnosis.md` §2): the session was granted and the
file opened on the `/log` region. Host-side media write proof for a
healthy P4 provider is the stall scenario's e2cp read-back above.

### 5.4 Regression receipts

- **P2.4** (`SPONGE_GROUPED_UI=on run/sponge-desktop-disk`, the
  phase16-p26 Run-V command): attempt 4 GREEN —
  `grouped-ui-probe: PASS` + `GROUPED UI PASS` (textedit/terminal/
  falkon first paint + typed input, terminal execution delta).
  Attempts 1-3 hit the scenario family's documented flake classes
  (launcher-set empty at report level; terminal typed-delta cursor-
  blink baseline 17 vs `2*baseline` defense; the p27 §7 H1/H2
  `parent denied ROM-session: label="config"` boot-delivery race) —
  the same multi-attempt pattern the phase16-p26 evidence records for
  this family. The delta under test touches neither this scenario nor
  its probe/includes (file-disjoint).
- **P2.5** (`run/sponge-de-pkgd-failure`, ISO media path): attempt 2
  GREEN — `de-survival-probe: scenario GREEN (P2.5 mechanisms
  present)`, 7 `qmp: dispatching` lines (the documented green
  baseline). Attempt 1 hit the documented delivery race
  (`parent denied Capture-session`, the p26/p29 multi-attempt class).

### 5.5 Honest boundaries

- Both P4 regions share one ext2 partition behind two independent rump
  mounts (D16R.8's wiring-only separation). Concurrent metadata writes
  from the two mounts are a known ext2 coherence hazard; today only the
  log sink writes (pkgd's store is not yet wired to `vfs_pkg` in the
  product), and D16R.8 accepts the wiring-level separation. If both
  regions gain writers, revisit (the alternatives are one provider per
  partition-pair or a shared Block layer — both larger than P3.2).
- The QEMU host carried an unrelated load average of ~10-12 during the
  regression runs (other sessions/builds); the flake rates above are
  measured under that load.
