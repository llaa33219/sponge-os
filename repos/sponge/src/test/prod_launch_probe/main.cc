/*
 * prod_launch_probe — automated replay of the launcher's
 * click-to-launch backend on the PRODUCT media topology.
 *
 * The user-facing launch flow: Qt menu click → LauncherController
 * posts <request op="launch" pkg="X"/> on launcher_request →
 * sponge_pkgd _do_launch → the pkg_runtime config regains the
 * package's start node → the package boots and its window appears.
 * The Qt pixel side was proven by the Phase-10 interactive
 * scenarios; this probe replays the BACKEND identically (same
 * request shape, pkgd's shared handle body, over the dedicated
 * probe_request transport because report_rom is single-writer per
 * label and sponge-de owns launcher_request here) for the three
 * user-reported packages: terminal, files, falkon.
 *
 * The scenario's boot gate waits for each package's real boot
 * markers in the serial — closing the gap between "autostart boots"
 * (what earlier gates proved) and "launch works" (what users do).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/component.h>
#include <base/log.h>
#include <timer_session/connection.h>
#include <util/string.h>

/* Sponge includes */
#include <sponge/backend_client.h>

namespace Prod_launch_probe { struct Main; }

struct Prod_launch_probe::Main
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	Sponge::Backend::ReportRomClient _launcher {
		_env, "probe_request", "probe_result" };

	Main(Genode::Env &env) : _env(env)
	{
		/* Let the system settle after boot before the first launch. */
		_settle(3000);

		char const *const pkgs[] = { "terminal", "files", "falkon" };
		for (char const *pkg : pkgs) {
			bool const ok = _launcher.request("launch", pkg);
			Genode::log("prod-launch-probe: launch ", pkg, " -> ",
			            ok ? "ok" : "FAIL");
			_settle(6000);
		}

		Genode::log("prod-launch-probe: all requests posted");
	}

	void _settle(unsigned ms)
	{
		unsigned const step { 100 };
		for (unsigned done = 0; done < ms; done += step)
			_timer.msleep(step);
	}
};

void Component::construct(Genode::Env &env)
{
	static Prod_launch_probe::Main main(env);
}
