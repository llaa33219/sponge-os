/* SPDX-License-Identifier: Apache-2.0
 *
 * dispatch_probe — pkgd dispatch-liveness driver (Phase 16 P3.3,
 * run/sponge-pkg-dispatch-liveness.run).
 *
 * Drives sponge_pkgd and stamps the serial log with tight "sent"/"got"
 * markers around each request, so the run script can open observation
 * WINDOWS over a request's dispatch and assert what pkgd did inside
 * them:
 *
 *   - window "explain": no counting-rom session create may happen
 *     between "sent explain" and "got explain" (P3.3 claim: dispatch
 *     parses from the bootstrap metadata cache, no ROM I/O).
 *
 *   - window "install": the store flush must be attributed to the
 *     "pkgd-store" worker thread (see lib/vfs_store_probe), not to the
 *     entrypoint (P3.3 claim: storage I/O is handed to the existing
 *     Bootstrap worker).
 *
 * Sequence (fixed, one boot):
 *   0. warm-up `list` + settle — the answer proves the bootstrap
 *      request drain has run and the armed handlers own dispatch from
 *      here on, so nothing below can be misattributed to the drain
 *      (which executes on the Bootstrap thread).
 *   1. explain <first>
 *   2. install <first>
 *   3. pipeline: install <second> on the primary channel, then `list`
 *      on the probe channel while the flush is still in flight. The
 *      FIFO contract ("no query overtaking") requires the mutation's
 *      result to be emitted BEFORE the query's — checked live: a list
 *      answer observed while the install is still unanswered is FAIL.
 *
 * Logs "dispatch-probe: PASS" only if every step passed; otherwise
 * "dispatch-probe: FAIL <reason>" and exit non-zero (fail-loud).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

struct Dispatch_probe
{
	static constexpr unsigned OP_ITERS = 120;   /* x 100ms = 12s per request */

	Genode::Env &_env;

	Timer::Connection               _timer      { _env };
	Genode::Attached_rom_dataspace  _config_rom { _env, "config" };

	Genode::Expanding_reporter      _request      { _env, "request", "request" };
	Genode::Attached_rom_dataspace  _result       { _env, "result" };

	Genode::Expanding_reporter      _probe_request {
		_env, "request", "probe_request" };
	Genode::Attached_rom_dataspace  _probe_result { _env, "probe_result" };

	Genode::String<64> _first  { "hello" };
	Genode::String<64> _second { "witness" };

	Dispatch_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		Genode::error("dispatch-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	void _parse_config()
	{
		if (!_config_rom.valid()) return;
		try {
			Genode::Xml_node const root = _config_rom.xml();
			if (!root.has_type("config")) return;
			_first  = root.attribute_value("first",  _first);
			_second = root.attribute_value("second", _second);
		} catch (Genode::Xml_node::Invalid_syntax) { }
	}

	static bool _matches(Genode::Attached_rom_dataspace const &rom,
	                     char const *op, char const *pkg)
	{
		if (!rom.valid()) return false;
		try {
			Genode::Xml_node const r = rom.xml();
			if (!r.has_type("result")) return false;
			if (r.attribute_value("op", Genode::String<32>()) !=
			    Genode::String<32>(op)) return false;
			if (Genode::strlen(pkg) > 0 &&
			    r.attribute_value("pkg", Genode::String<128>()) !=
			    Genode::String<128>(pkg)) return false;
			return r.has_attribute("status");
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	static Genode::String<32> _status(
		Genode::Attached_rom_dataspace const &rom)
	{
		try {
			return rom.xml().attribute_value("status",
			                                 Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<32>();
		}
	}

	bool _send_and_wait(char const *op, char const *pkg, unsigned seq)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			if (Genode::strlen(pkg) > 0)
				g.attribute("pkg", pkg);
			g.attribute("seq", seq);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < OP_ITERS; ++i) {
			_result.update();
			if (_matches(_result, op, pkg))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	void run()
	{
		_config_rom.update();
		_parse_config();

		_result.update();
		_probe_result.update();

		char const *const first  = _first.string();
		char const *const second = _second.string();

		/* 0. warm-up: past the bootstrap drain, then settle. */
		Genode::log("dispatch-probe: sent list (warm-up)");
		if (!_send_and_wait("list", "", 1))
			_fail("warm-up list did not answer");
		Genode::log("dispatch-probe: got list (warm-up)");
		_timer.msleep(1000);

		/* 1. explain: metadata dispatch window. */
		Genode::log("dispatch-probe: sent explain ", first);
		if (!_send_and_wait("explain", first, 2))
			_fail("explain did not answer");
		Genode::log("dispatch-probe: got explain ", first);

		/* 2. install: store-flush attribution window. */
		Genode::log("dispatch-probe: sent install ", first);
		if (!_send_and_wait("install", first, 3))
			_fail("install did not answer");
		if (_status(_result) != Genode::String<32>("ok")) {
			_fail(Genode::String<96>("install ", first,
			    " expected ok, got ", _status(_result)).string());
			return;
		}
		Genode::log("dispatch-probe: got install ", first);

		/* 3. pipeline: install <second> then list during its flush. */
		Genode::log("dispatch-probe: pipeline sent install ", second);
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", second);
			g.attribute("seq", 4);
		});
		_timer.msleep(300);

		Genode::log("dispatch-probe: pipeline sent list");
		_probe_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "list");
			g.attribute("seq", 5);
		});

		/*
		 * No-overtaking guard: the query must not answer before the
		 * mutation whose flush is still in flight.
		 */
		bool install_done { false };
		for (unsigned i = 0; i < OP_ITERS; ++i) {
			_result.update();
			_probe_result.update();

			if (_matches(_result, "install", second))
				install_done = true;

			if (!install_done && _matches(_probe_result, "list", "")) {
				_fail("list answered before install — the query "
				      "overtook the queued mutation");
				return;
			}

			if (install_done && _matches(_probe_result, "list", ""))
				break;

			_timer.msleep(100);
		}
		if (!install_done) {
			_fail("pipelined install did not answer");
			return;
		}
		if (!_matches(_probe_result, "list", "")) {
			_fail("pipelined list did not answer");
			return;
		}
		Genode::log("dispatch-probe: pipeline ok (no query overtaking)");

		Genode::log("dispatch-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Dispatch_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }
