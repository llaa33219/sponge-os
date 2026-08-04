# phase8-storage - Work Plan

> Design authority: `docs/14-boot-storage-architecture.md` (approved 2026-08-03).
> This plan tracks the P1-P5 rollout defined there §10. Each phase is
> separately committable and scenario-gated.

## P1 — storage-chain smoke (current)
- [ ] 1. `prepare_port dde_rump` + hash into docs/11 §5 (acceptance gate; fallback to vfs_fatfs documented if it fails)
- [ ] 2. `run/sponge-boot.run` (+symlink+README): seL4 Tier-0-ish chain — platform/acpi/pci_decode → storage driver → part_block (pin partition 3 by number) → vfs (Vfs_block + rump ext2fs) → cached_fs_rom → probe reads `/system/marker.txt` from the GENODE ext2 and asserts content (`boot-probe: PASS`, bounded)
- [ ] 3. Both storage variants proven: ahci (default `-drive` on q35) AND nvme (`-device nvme`); record which is primary for the product media
- [ ] 4. Failure path: boot with a corrupted/no GENODE partition → distinguishable, bounded failure (no silent hang)

## P2 — desktop from disk
- [ ] 5. On-disk `system.config` + nested system init with `label_last="ld.lib.so" → parent` routing; full desktop (drivers/nitpicker/wm/sponge-de/backends) served from `/system` via cached_fs_rom; alpha-probe-equivalent PASS from the .img boot

## P3 — persistence on SPONGE-DATA
- [ ] 6. tool/dist P4 creation sequence (docs/14 §4.3) + pkgd/configd stores wired to vfs_data; two-boot persistence scenario on seL4 media

## P4 — falkon from disk
- [ ] 7. falkon payload under /system/pkg; launch from disk; caps/RAM measured and set (start caps≥30000, QEMU ≥6G); fixture page load verified

## P5 — media + docs
- [ ] 8. `tool/dist` full 4-partition media; ISO live-mode (Tier 2 on RAM fs); docs/13 limitations rewrite (falkon+persistence caveats removed); docs/08 dev flow update

## Commit strategy
Conventional commits; one phase-gate per commit; doc updates ride with their phase (AGENTS.md §5.4).
