/*
 * ep_wedge — sandbox heartbeat-restart test component.
 *
 * Two modes, selected by the component config:
 *
 *   default (no config / behave unset):
 *     Logs one line and blocks its entrypoint forever.
 *     Component::construct runs on the ep's dispatch thread, so
 *     sleep_forever wedges every RPC and signal processing in this
 *     component — exactly the failure class the sandbox heartbeat is
 *     deployed to contain (docs/17-runtime-component-topology.md §5).
 *     A parent running <heartbeat rate_ms="..."/> with this child
 *     configured <heartbeat restart_after_skipped="N"/> observes
 *     skipped heartbeats, abandons the wedged instance, and
 *     respawns it — which makes the "alive" line appear again.
 *
 *   <config behave="yes"/>:
 *     The witness: logs once and returns from construct (a healthy
 *     idle component). It must SURVIVE any number of sibling
 *     wedge-restart cycles — the independence assertion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>

void Component::construct(Genode::Env &env)
{
	bool behave = false;
	{
		Genode::Attached_rom_dataspace cfg { env, "config" };
		cfg.update();
		if (cfg.valid()) {
			char const * const base = cfg.local_addr<char const>();
			Genode::size_t  const sz  = cfg.size();
			for (Genode::size_t i = 0; i + 12 < sz; ++i) {
				if (Genode::strcmp(base + i, "behave: yes",    11) == 0 ||
				    Genode::strcmp(base + i, "behave=\"yes\"", 12) == 0)
					behave = true;
			}
		}
	}

	if (behave) {
		Genode::log("witness: alive and well");
		return;
	}

	Genode::log("ep_wedge: alive — entrypoint wedging now");
	Genode::sleep_forever();
}
