# boot_probe — Phase 8 P1 storage-chain smoke probe.
#
# Tiny Genode component (no libc, no Qt) that proves the Tier-0 disk-read
# chain end-to-end: it opens ROM "marker.txt" (served by cached_fs_rom,
# which reads it from the ext2 GENODE partition via vfs_rump, which
# mounts the Block session from part_block partition 3, which reads from
# the ahci/nvme driver), validates the ROM's exact content against the
# expected string from its <config>, and logs one of:
#
#   boot-probe: PASS (<byte count> bytes: "<content>")
#   boot-probe: FAIL: rom_invalid       (ROM module absent / empty)
#   boot-probe: FAIL: content_mismatch (expected vs actual)
#
# If the storage chain is broken upstream (ahci did not bind, part_block
# could not parse partition 3, vfs_rump could not mount ext2, or
# cached_fs_rom could not open the file), the ROM session construction
# blocks and the probe never logs — the bounded run_genode_until timeout
# in run/sponge-boot.run catches this, and the upstream component logs
# (part_block's "partition 3 not found", vfs/rump's mount error, etc.)
# identify the failed stage. This is by design: the probe's silence IS
# the diagnostic signal that the chain broke before the ROM became
# available (docs/14 §11 risk 4 — never a silent hang; the timeout bounds
# it, the logs identify it).
#
# On PASS or FAIL the probe calls env.parent().exit() so the scenario
# ends cleanly (no lingering component).
#
# AGENTS.md §3.1: qualified Genode types (Genode::size_t), snake_case
# methods, PascalCase classes, no exceptions, #pragma once not needed
# (single TU).

TARGET   := boot_probe
SRC_CC   := main.cc
LIBS     := base
