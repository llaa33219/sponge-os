/* SPDX-License-Identifier: Apache-2.0
 *
 * config_witness — the configuration-regeneration survival witness.
 *
 * A package payload whose only observable behaviour is a counter on its
 * Report session. The counter starts at 1 on every process start, so an
 * observer can distinguish "the child survived the config change"
 * (counter keeps rising) from "the child was killed and recreated"
 * (counter jumps back to 1).
 *
 * The scenario run/sponge-config-regen-survival.run installs this
 * package (its metadata declares <autostart/>) and then forces
 * sponge_pkgd to regenerate the whole pkg_runtime config across the
 * Expanding_reporter report-buffer boundary on every write. If init
 * applies a transiently invalid config, pkg_runtime abandons every
 * <start> node it holds and this witness restarts — which regen_probe
 * observes as a counter reset.
 */

#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>

namespace {

struct Main
{
	Genode::Env                 &_env;
	Timer::Connection          _timer    { _env };
	Genode::Expanding_reporter   _reporter { _env, "beat" };

	unsigned _n { 0 };

	Main(Genode::Env &env) : _env(env)
	{
		Genode::log("config-witness: start");

		for (;;) {
			++_n;
			_reporter.generate_xml([&](Genode::Xml_generator &g) {
				g.attribute("n", _n); });
			_timer.msleep(200);
		}
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env) { static Main main { env }; }


Genode::size_t Component::stack_size() { return 32 * 1024; }