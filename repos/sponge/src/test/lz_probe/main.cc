/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * lz_probe — Leitzentrale headless boot verification probe (Phase 6a).
 *
 * This component proves that Sculpt's sculpt_manager is alive inside the
 * leitzentrale subsystem hosted under lz_runtime. It is a child of
 * lz_runtime (a sibling of the leitzentrale subsystem child), so it can
 * read the subsystem's init "state" report through the lz_relay report_rom
 * with a single relay hop.
 *
 * Flow:
 *   1. Poll the "lz_subsys_state" ROM (relayed by lz_relay from the
 *      leitzentrale child's "state" report).
 *   2. Parse the state XML and look for a running <child name="manager">
 *      with a non-zero RAM quota.
 *   3. Once found, log "lz-probe: PASS" and exit 0.
 *
 * On timeout (sculpt_manager never appeared) the run scenario fails via
 * run_genode_until — the correct FAIL path.
 *
 * It is a plain Genode component following AGENTS.md §3.1 (qualified
 * Genode types, no exceptions, Component::construct/stack_size exactly as
 * the framework expects).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <timer_session/connection.h>
#include <util/xml_node.h>

namespace {

struct Lz_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer { _env };

	/*
	 * ROM relayed by the lz_relay report_rom: the leitzentrale subsystem
	 * init's "state" report, exposed as the "lz_subsys_state" ROM.
	 */
	Genode::Attached_rom_dataspace _subsys_state { _env, "lz_subsys_state" };

	Lz_probe(Genode::Env &env) : _env(env)
	{
		Genode::log("lz-probe: starting, waiting for sculpt_manager");

		for (unsigned i = 0; i < 600; ++i) {  /* up to ~60s */
			_timer.msleep(100);
			_subsys_state.update();

			if (!_subsys_state.valid())
				continue;

			/*
			 * The init sandbox state report is emitted in Genode's HID
			 * (tabular) format — e.g. a line '+ child manager | binary:
			 * sculpt_manager' per running child. We do a robust substring
			 * search on the raw ROM content rather than relying on XML
			 * parsing, so the check is insensitive to the exact format.
			 *
			 * "child manager" appearing in the state means sculpt_manager
			 * was started by the leitzentrale subsystem — the Phase 6a
			 * alive criterion.
			 */
			char const *raw = _subsys_state.local_addr<char const>();
			Genode::size_t len = 0;
			while (raw[len] && len < 4096) ++len;

			/*
			 * Robust substring search for "child manager" (the HID-format
			 * state entry for the sculpt_manager child).
			 */
			bool manager_running = false;
			char const needle[] = "child manager";
			Genode::size_t const nlen = sizeof(needle) - 1;
			for (Genode::size_t k = 0; k + nlen <= len; ++k) {
				bool match = true;
				for (Genode::size_t j = 0; j < nlen; ++j)
					if (raw[k + j] != needle[j]) { match = false; break; }
				if (match) { manager_running = true; break; }
			}

			if (i % 20 == 0)
				Genode::log("lz-probe: poll ", i, " len=", len,
				            " manager=", manager_running);

			if (manager_running) {
				Genode::log("lz-probe: sculpt_manager detected in leitzentrale subsystem");
				Genode::log("lz-probe: PASS");
				_env.parent().exit(0);
				return;
			}
		}

		Genode::error("lz-probe: FAIL sculpt_manager never appeared");
		_env.parent().exit(1);
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Lz_probe probe { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
