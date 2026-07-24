/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * PkgClient — vct's thin client for the sponge_pkgd backend.
 *
 * The backend channel is Report/ROM, not RPC (settled design,
 * docs/04-components.md §5). PkgClient writes a "request" report that
 * report_rom relays to sponge_pkgd as a ROM, then polls the "result"
 * ROM that report_rom relays back from sponge_pkgd.
 *
 *   vct --[Report "request"]--> report_rom --[ROM "request"]--> sponge_pkgd
 *   vct <--[ROM "result"]------ report_rom <--[Report "result"]-- sponge_pkgd
 *
 * The result report carries structured XML (the install plan or an
 * error); vct renders both the human-readable and --json output from
 * it (presentation stays in vct, matching existing command conventions).
 *
 * The poll loop mirrors InitStateReader's (init_state.cc): vct is
 * short-lived and blocks until sponge_pkgd answers or the budget runs out.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/xml_node.h>

namespace Sponge::Vct {

class PkgClient
{
	public:

		explicit PkgClient(Genode::Env &env);

		/* Send a request (`op` in {explain, install, remove}) for `pkg`
		 * and poll the result ROM until sponge_pkgd answers with a
		 * matching result (or the poll budget is spent). Returns true
		 * on a matching answer, after which result_xml() exposes the
		 * structured <result/>. */
		bool request(char const *op, char const *pkg);

		/* No-package overload for ops that take none (currently `list`). */
		bool request(char const *op);

		/* Structured result from the last request. Only valid to call
		 * when request() returned true. */
		Genode::Xml_node result_xml() const { return _result_rom.xml(); }

	private:

		Genode::Env &_env;

		Timer::Connection                _timer            { _env };
		Genode::Expanding_reporter       _request_reporter { _env, "request", "request" };
		Genode::Attached_rom_dataspace   _result_rom       { _env, "result" };

		bool _result_matches(char const *op, char const *pkg) const;
};

}  /* namespace Sponge::Vct */
