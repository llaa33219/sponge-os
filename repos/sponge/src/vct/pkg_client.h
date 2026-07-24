/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * PkgClient — vct's thin Report/ROM client for the Sponge OS backends.
 *
 * The backend channel is Report/ROM, not RPC (settled design,
 * docs/04-components.md §5). The client writes a request report that
 * report_rom relays to the backend as a ROM, then polls the result
 * ROM that report_rom relays back from the backend.
 *
 *   vct --[Report <req-label>]--> report_rom --[ROM <req-label>]--> backend
 *   vct <--[ROM <res-label>]------ report_rom <--[Report <res-label>]-- backend
 *
 * The result report carries structured XML (the install plan, a config
 * value, or an error); vct renders both the human-readable and --json
 * output from it (presentation stays in vct, matching existing command
 * conventions).
 *
 * The poll loop mirrors InitStateReader's (init_state.cc): vct is
 * short-lived and blocks until the backend answers or the budget runs out.
 *
 * The request/result ROM labels are constructor parameters so the SAME
 * client serves both sponge_pkgd (labels "request"/"result") and
 * sponge_configd (labels "config_request"/"config_result"). The two
 * backends share report_rom but occupy distinct label slots, so they do
 * not collide (docs/04-components.md §5, single-writer note). The pkg
 * methods (request/install/remove/list) and the config methods
 * (config_get/set/list) differ only in the request XML shape and the
 * result-matching discriminator; the Report/ROM plumbing is identical.
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

		/*
		 * `request_label` / `result_label` name the Report session the
		 * client opens and the ROM it polls. Defaults ("request" /
		 * "result") preserve the sponge_pkgd wiring; sponge_configd is
		 * reached with "config_request" / "config_result".
		 */
		explicit PkgClient(Genode::Env &env,
		                   char const *request_label = "request",
		                   char const *result_label  = "result");

		/* ---- sponge_pkgd requests ---- */

		/* Send a request (`op` in {explain, install, remove}) for `pkg`
		 * and poll the result ROM until sponge_pkgd answers with a
		 * matching result (or the poll budget is spent). Returns true
		 * on a matching answer, after which result_xml() exposes the
		 * structured <result/>. */
		bool request(char const *op, char const *pkg);

		/* No-package overload for ops that take none (currently `list`). */
		bool request(char const *op);

		/* ---- sponge_configd requests ---- */

		/* `vct config <key>` — fetch one key's value. */
		bool config_get(char const *key);

		/* `vct config <key> <value>` — store a value. The result is
		 * matched on op+key+value so a set to a different value is not
		 * satisfied by a stale prior result. */
		bool config_set(char const *key, char const *value);

		/* `vct config list` — list every known key/value. */
		bool config_list();

		/* Structured result from the last request. Only valid to call
		 * when the preceding request() returned true. */
		Genode::Xml_node result_xml() const { return _result_rom.xml(); }

	private:

		Genode::Env &_env;

		/* Declared before the reporter/ROM so their default member
		 * initializers observe the already-constructed label strings
		 * (members initialize in declaration order). */
		Genode::String<32> const _request_label;
		Genode::String<32> const _result_label;

		Timer::Connection                _timer            { _env };
		Genode::Expanding_reporter       _request_reporter { _env, "request", _request_label.string() };
		Genode::Attached_rom_dataspace   _result_rom       { _env, _result_label.string() };

		bool _result_matches(char const *op, char const *pkg) const;

		/* Config result matcher. `value` is matched only when non-null
		 * (set); get/list ignore it (a fresh get/list answer is
		 * identified by op[+key] alone). */
		bool _config_result_matches(char const *op, char const *key,
		                            char const *value) const;
};

}  /* namespace Sponge::Vct */
