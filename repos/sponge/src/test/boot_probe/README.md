# boot_probe

> Phase 8 P1 storage-chain smoke probe (docs/14-boot-storage-architecture.md
> §3, §4.4; `.omo/plans/phase8-storage.md` P1 item 2).

A tiny Genode component (no libc, no Qt) that proves the Tier-0 disk-read
chain end-to-end:

```
ahci/nvme (Block) → part_block (Block/P3) → vfs (Vfs_block + rump ext2fs,
File_system) → cached_fs_rom (ROM, chroot /system) → boot_probe
```

## What it does

1. Opens ROM module `"marker.txt"` (served by `cached_fs_rom`).
2. Validates the ROM's exact content against the `expected` attribute of
   its `<config>`.
3. Logs one of:
   - `boot-probe: PASS (<N> bytes: "<actual>")` — the full chain works
     and the marker content round-tripped byte-identical from disk.
   - `boot-probe: FAIL: rom_invalid` — the ROM dataspace is absent/empty
     (cached_fs_rom could not serve the file).
   - `boot-probe: FAIL: content_mismatch (expected "..." got "...")` —
     the ROM was served but the bytes differ (disk corruption, wrong
     file, stale image).
4. Calls `env.parent().exit()` so the scenario ends cleanly.

## Why a silent probe identifies the broken stage

If any upstream stage of the Tier-0 chain fails (ahci did not bind the
AHCI device, `part_block` could not find partition 3, `vfs` could not
mount ext2, or `cached_fs_rom` could not read the file), the ROM session
construction in the probe's constructor blocks — the probe never logs.
The bounded `run_genode_until` timeout in `run/sponge-boot.run` catches
this, and the **upstream component logs** identify the stage:

| Broken stage    | Distinguishing log line                                |
|-----------------|--------------------------------------------------------|
| storage driver  | no `[init -> ahci]` or `[init -> nvme]` boot marker    |
| part_block      | `[init -> part_block] ... partition ... not found`     |
| vfs / rump mount| `[init -> vfs] ... ext2fs: mount ... failed` / rump error |
| cached_fs_rom   | file open error inside cached_fs_rom's log             |

This is the docs/14 §11 risk-4 design: never a silent hang; the timeout
bounds the failure and the logs identify it.

## Config

```xml
<start boot_probe>
  <config expected="sponge-boot-marker-v1"/>
  <route>
    <service ROM label="marker.txt"> <child cached_fs_rom/> </service>
    ...
  </route>
</start>
```

The `expected` attribute pins the exact byte string; the run script's
marker file (staged into the GENODE ext2 P3 via `image/disk`) must match
it. Changing the marker content is a one-line change in the run script
(no probe recompilation).
