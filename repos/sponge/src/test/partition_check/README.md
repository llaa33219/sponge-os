# partition_check — Phase 12 W2 NVMe Tier-0 P3 partition-number probe

> docs/plans/phase12-hardware.md §"W2: Storage variants and product-
> media selector", Risk 3 mitigation (NVMe namespace semantics invalidate
> the P3 pin) and Risk 10 mitigation (NVMe QEMU root-port/drive/device
> sequence copied from `SPONGE_BOOT_NVME`).

A tiny Genode component (no libc, no Qt) that proves the Tier-0
`part_block` reporter pins the **partition by number** on the NVMe
namespace, not by index or first-found. The check is the byte
assertion the W2 plan step 3 requires (`Number: 3`) — emitted
verbatim by the probe so the scenario's `run_genode_until` regex can
match it.

## What it does

1. Opens ROM module `"partitions"` (served by `report_rom` after a
   policy captures `part_block`'s Report session — see the
   `run/sponge-desktop-disk-nvme.run` Tier-0 init for the policy).
2. Scans the ROM for the ASCII substring `number="3"` (the expected
   P3 partition-number attribute rendered by part_block's GPT or MBR
   reporter — see `genode/repos/os/src/server/part_block/gpt.h:456` /
   `mbr.h:238`).
3. Logs one of:
   - `partition-check: PASS (Number: 3)` — the part_block reporter
     lists partition 3 by number (the GENODE ext2 partition on the
     NVMe namespace).
   - `partition-check: FAIL: rom_invalid` — the ROM module is absent
     or empty (no policy routed the partitions report, or the chain
     never produced it).
   - `partition-check: FAIL: number_mismatch (expected number="3",
     rom_ds_size=N)` — the ROM was served but no `number="3"`
     partition row was found.
4. Calls `env.parent().exit()` so the scenario ends cleanly.

## Why this check, not a probe in boot_probe

`boot_probe` reads the marker *contents* — it proves the storage chain
can read a known byte string from a known partition. `partition_check`
reads the part_block reporter — it proves the partition was pinned by
**number** and that the part_block layer enumerates partition 3 on the
new NVMe namespace the same way it does on the q35 ICH9 AHCI chain.
Both gates are required for the W2 acceptance (Risk 3 + Risk 10).

## Run-script contract

```tcl
# Tier-0 init: enable part_block's partition report and route it via report_rom.
+ start part_block
  + config
    + report | partitions: yes
    + policy | label_prefix: vfs | partition: 3 | writeable: yes
  ...
+ start report_rom
  + config
    + policy | label: part_block -> partitions | report: part_block -> partitions
    ...
+ start partition_check | caps: 100 | ram: 4M
  + route
    + service ROM | label: partitions | + child report_rom
    + service ROM | label_last: ld.lib.so | + parent
    + any-service
      + parent
      + any-child

# Verification gate: Number: 3 must appear, then alpha-probe: PASS.
run_genode_until {.*partition-check: PASS.*Number: 3.*alpha-probe: PASS.*} 900
```

AGENTS.md §3.1: qualified Genode types (`Genode::size_t`), snake_case
methods, PascalCase classes, no exceptions, no `#pragma once` needed
(single TU).