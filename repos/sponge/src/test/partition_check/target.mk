# partition_check — Phase 12 W2 NVMe Tier-0 P3 partition-number probe.
#
# Tiny Genode component (no libc, no Qt) that proves the Tier-0
# part_block reporter pins the partition BY NUMBER on the NVMe
# namespace. Scans the part_block "partitions" ROM for the ASCII
# substring `number="3"` (the expected P3 attribute rendered by
# part_block's GPT or MBR reporter; see genode/repos/os/src/server/
# part_block/gpt.h:456 and mbr.h:238), then logs:
#
#   partition-check: PASS (Number: 3)
#   partition-check: FAIL: rom_invalid       (ROM module absent / empty)
#   partition-check: FAIL: number_mismatch  (expected number="3")
#
# The PASS line intentionally echoes the literal "Number: 3" suffix
# (plan §W2 step 3: "add a P3 report/byte assertion (`Number: 3`)
# before the existing `alpha-probe: PASS` gate") even though part_block
# renders the attribute as lowercase `number="3"` — the run script
# grep is on the probe's PASS line, not on part_block's XML.
#
# On PASS or FAIL the probe calls env.parent().exit() so the scenario
# ends cleanly (no lingering component). If the upstream chain is
# broken (part_block has no Block session, or report_rom has no policy
# for the partitions report), the Attached_rom_dataspace constructor
# blocks and the bounded run_genode_until in the scenario catches the
# silence.
#
# AGENTS.md §3.1: qualified Genode types (Genode::size_t), snake_case
# methods, PascalCase classes, no exceptions, #pragma once not needed
# (single TU).

TARGET   := partition_check
SRC_CC   := main.cc
LIBS     := base