/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * high_phys_probe — QEMU-verifiable gate for the row-13/14
 * high-phys IO_MEM machinery (docs/11-environment.md rows 13/14).
 *
 * Motivation: real-hardware boards place device MMIO high in the
 * 64-bit window (the LG gram 17ZD90N's GOP framebuffer at
 * 0x4000000000 works — the panel shows the background — while its
 * PCH xHCI BAR at 0x601d140000 stalls the boot chain). QEMU cannot
 * place a PCI BAR in the [RAM-top, 512 GiB) range (QEMU 11 fixes
 * 64-bit BARs at 54 TiB, beyond the kernel window), so the
 * machinery cannot be exercised with a real device in QEMU. This
 * probe exercises it with two synthetic requests instead:
 *
 *   stage1 @16 GiB — inside the row-13 HIGH device-untyped coverage
 *     ([ROUND_UP(highest_map_addr, 2 MiB), 1 TiB)), outside RAM,
 *     outside every e820 region in QEMU, above HIGH_PHYS_BASE
 *     (8 GiB) but below the 512 GiB kernel window. This is the same
 *     class as the 17ZD90N's xHCI BAR. Expected: ATTACH OK —
 *     proves the high-phys io_mem registration (platform.cc),
 *     the bootinfo untyped scan, the watermark fast-forward, the
 *     dedicated high-phys CNode retype, and the map-time slot
 *     lookup (row 14) are all alive.
 *
 *   stage2 @2 TiB — beyond the 1 TiB coverage ceiling but still
 *     inside the kernel window. Expected: a CLEAN refusal (core's
 *     "I/O memory ... not available" + an Invalid_dataspace to the
 *     client) — the failure path must degrade, not crash core.
 *
 * The probe prints per-stage lines and one final verdict:
 *   high-phys-probe: PASS   (stage1 OK AND stage2 refused-as-expected)
 *   high-phys-probe: FAIL   (anything else)
 *
 * AGENTS.md §3.1: qualified Genode types, snake_case, single TU.
 */

#include <base/component.h>
#include <base/attached_io_mem_dataspace.h>
#include <base/log.h>

namespace High_phys_probe {

	using namespace Genode;

	bool request(Env &env, addr_t phys, char const *tag, bool expect_ok)
	{
		try {
			Attached_io_mem_dataspace ds(env, phys, 0x1000, false);
			uint32_t const v = *(uint32_t volatile *)ds.local_addr<void>();

			if (expect_ok) {
				log("high-phys-probe: ", tag, " OK (attached; first word=",
				    Hex(v), ")");
				return true;
			}
			error("high-phys-probe: ", tag,
			      " UNEXPECTED-OK (attached beyond the coverage ceiling)");
			return false;
		}
		catch (Attached_dataspace::Invalid_dataspace &) {
			if (expect_ok) {
				error("high-phys-probe: ", tag,
				      " REFUSED (core reported I/O memory not available)");
				return false;
			}
			log("high-phys-probe: ", tag,
			    " refused-as-expected (beyond untyped coverage)");
			return true;
		}
	}
}

void Component::construct(Genode::Env &env)
{
	using namespace Genode;

	log("high-phys-probe: stage 1 — IO_MEM @16 GiB "
	    "(inside row-13 HIGH untyped coverage)");
	bool const stage1 = High_phys_probe::request(
		env, 0x400000000ULL, "stage1@16GiB", true);

	log("high-phys-probe: stage 2 — IO_MEM @2 TiB "
	    "(beyond the 1 TiB ceiling, inside the kernel window)");
	bool const stage2 = High_phys_probe::request(
		env, 0x20000000000ULL, "stage2@2TiB", false);

	if (stage1 && stage2)
		log("high-phys-probe: PASS");
	else
		error("high-phys-probe: FAIL (see stage lines above)");

	env.parent().exit(0);
}
