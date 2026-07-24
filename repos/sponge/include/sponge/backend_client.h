/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * ReportRomClient — shared Report/ROM client for Sponge OS backends.
 *
 * The backend channel is Report/ROM, not RPC (settled design,
 * docs/04-components.md §5). The client writes a request report that
 * report_rom relays to the backend as a ROM, then polls the result
 * ROM that report_rom relays back from the backend.
 *
 *   caller --[Report <req-label>]--> report_rom --[ROM <req-label>]--> backend
 *   caller <--[ROM <res-label>]------ report_rom <--[Report <res-label>]-- backend
 *
 * The result report carries structured XML (the install plan, a config
 * value, the installed-set list, or an error); callers render the
 * human-readable / --json output from it.
 *
 * This header was lifted from src/vct/pkg_client.h (Phase 5c) so that
 * sponge-de's launcher can reuse the exact same channel plumbing that
 * vct uses for `vct install` / `vct config`. vct is the original
 * caller; sponge-de is the second one (genuine cross-component sharing
 * is what triggers the lib extraction, per lib/README.md).
 *
 * POLL MODEL:
 *
 *   The methods block the calling thread for up to ~6s (200ms initial
 *   settle + up to 60 x 100ms polls) waiting for a matching result.
 *   vct is short-lived and blocks happily. sponge-de MUST NOT call
 *   these from the GUI thread (Qt event loop): the launcher uses a
 *   QTimer-driven non-blocking poll path instead (launcher_controller),
 *   reusing only the request-XML shape and result-matching helpers
 *   exposed here.
 *
 * REQUEST/RESULT ROM LABELS:
 *
 *   Constructor parameters so the SAME client reaches both
 *   sponge_pkgd (labels "request"/"result") and sponge_configd
 *   (labels "config_request"/"config_result"). The two backends share
 *   report_rom but occupy distinct label slots (single-writer safe,
 *   docs/04-components.md §5).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/xml_node.h>

namespace Sponge::Backend {

/*
 * Small helper exposed so callers that drive their own poll loop
 * (e.g. sponge-de's launcher, which cannot block the GUI thread) can
 * reuse the same "is this result the answer to MY request?" predicate
 * without re-implementing it.
 */
struct Result_match
{
	/*
	 * pkgd-style result: <result status op pkg/>. `pkg` is ignored for
	 * op="list" (no package argument).
	 */
	static bool pkg(Genode::Xml_node const &r,
	                char const *op, char const *pkg);

	/*
	 * configd-style result: <result status op key [value]/>. `value` is
	 * matched only when non-null (used by set); get/list ignore it.
	 */
	static bool config(Genode::Xml_node const &r,
	                   char const *op, char const *key,
	                   char const *value);
};

class ReportRomClient
{
	public:

		/*
		 * `request_label` / `result_label` name the Report session the
		 * client opens and the ROM it polls. Defaults ("request" /
		 * "result") preserve the sponge_pkgd wiring; sponge_configd is
		 * reached with "config_request" / "config_result".
		 */
		explicit ReportRomClient(Genode::Env &env,
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

		bool config_get(char const *key);
		bool config_set(char const *key, char const *value);
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
};

}  /* namespace Sponge::Backend */
