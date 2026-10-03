/* SPDX-License-Identifier: Apache-2.0
 *
 * cfg_probe — config-module delivery probe.
 *
 * Field-diagnostic companion for the pkg_runtime config-stall
 * investigation (real hardware: N/S readouts advance but C:0/U:0 —
 * pkg_runtime never applies its config). This component opens the
 * EXACT same ROM module that feeds pkg_runtime's config (the
 * 'sponge_pkgd -> runtime' report, wired by the product scenarios via
 * the report_rom policy 'cfg_probe -> config') and republishes the
 * observed byte count as the 'cfg_probe' report plus a LOG line.
 *
 * Panel wiring (product scenarios):
 *   cfg_probe -> config      <- sponge_pkgd -> runtime   (read side)
 *   sponge-de -> cfg_probe_state <- cfg_probe -> cfg_probe (D: readout)
 *
 * Interpretation:
 *   D > 0  — the config module has content and a fresh reader at this
 *            position of the boot order receives it (the delivery path
 *            works; the failure is inside pkg_runtime itself).
 *   D = 0  — the module is empty for this reader despite S: showing
 *            content on the mirror reader (label/module mismatch or
 *            a report_rom defect).
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>

using namespace Genode;

namespace {

struct Main
{
	Env &env;

	Timer::Connection       timer { env };
	Attached_rom_dataspace  config { env, "config" };
	Expanding_reporter     reporter { env, "cfg_probe", "cfg_probe" };
	Signal_handler<Main>    sigh { env.ep(), *this, &Main::handle };

	unsigned last_size { ~0U };

	void handle()
	{
		config.update();

		unsigned const size = config.valid() ? (unsigned)config.size() : 0U;

		if (size == last_size)
			return;

		last_size = size;

		log("cfg_probe: config size=", size);

		reporter.generate_xml([&] (Genode::Xml_generator &xml) {
			xml.attribute("bytes", size); });
	}

	Main(Env &e) : env(e)
	{
		config.sigh(sigh);
		timer.sigh(sigh);
		timer.trigger_periodic(500 * 1000);
		handle();
	}
};
}

void Component::construct(Genode::Env &env) { static Main main { env }; }
