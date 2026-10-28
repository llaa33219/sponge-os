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
