/* SPDX-License-Identifier: Apache-2.0
 *
 * exclusivity_probe — exclusive-group placement regression verifier
 * (Phase 16 P3.3, run/sponge-pkg-exclusive.run).
 *
 * Proves two properties of the install pre-flight (docs/12 §4.6):
 *
 *   (1) FIRST-EXECUTION REFUSAL: installing the challenger package
 *       while the incumbent holds the shared exclusive group is
 *       refused with `exclusive-conflict` naming the running peer on
 *       the FIRST request — and the refusal mutates nothing (the
 *       challenger never appears in the installed broadcast).
 *
 *       The regression this pins: the pre-flight used to live in the
 *       duplicate-REPLAY path (_replay_last_result), so the first
 *       execution installed the conflicting package without any check
 *       and only a byte-identical re-send reported the conflict —
 *       after the damage was done.
 *
 *   (2) BYTE-IDENTICAL REPLAY WITHOUT POLICY RE-RUN: re-sending the
 *       BYTE-IDENTICAL request (same op, pkg AND seq) on the Sponge DE
 *       launcher channel must be answered on "launcher_result" with a
 *       result byte-identical to the first refusal. The replay must
 *       re-emit the cached recipe, never re-run the exclusive policy
 *       (the run script independently asserts the absence of a second
 *       "exclusive-group check" log line in the replay window).
 *
 *       "launcher_result" carries no prior content in this scenario, so
 *       any bytes observed there can only have come from the replay —
 *       the comparison cannot pass on a stale ROM.
 *
 * Sequence (one boot, fixed):
 *   1. install <first>            -> ok (autostart: enters the running set)
 *   2. broadcast: <first> running="yes"
 *   3. install <second>           -> exclusive-conflict peer=<first>
 *   4. broadcast: <second> absent (refusal left no state)
 *   5. duplicate install <second> on launcher_request -> byte-identical
 *
 * Logs "exclusivity-probe: PASS" only if every step passed; otherwise
 * "exclusivity-probe: FAIL <reason>" and exit non-zero — the run
 * scenario then fails by bounded run_genode_until timeout (fail-loud).
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

struct Exclusivity_probe
{
	static constexpr unsigned POLL_ITERS = 80;   /* x 100ms = 8s per assertion */
	static constexpr unsigned OP_ITERS   = 120;  /* x 100ms = 12s per request  */
	static constexpr unsigned DUP_ITERS  = 40;   /* x 100ms = 4s for the replay */

	/* Raw result bytes: the replay must reproduce them byte-identically. */
	static constexpr unsigned MAX_RESULT = 1024;

	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Genode::Expanding_reporter     _request   { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result    { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	/* Launcher channel: the transport of the duplicate replay. */
	Genode::Expanding_reporter     _launch_request { _env, "request",
	                                                "launcher_request" };
	Genode::Attached_rom_dataspace _launch_result  { _env, "launcher_result" };

	Genode::String<64> _first  { "excl_a" };
	Genode::String<64> _second { "excl_b" };
	Genode::String<16> _group  { "testfb" };

	char          _first_bytes[MAX_RESULT] { };
	Genode::size_t _first_len { 0 };

	Exclusivity_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		Genode::error("exclusivity-probe: FAIL ", reason);
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
			_group  = root.attribute_value("group",  _group);
		} catch (Genode::Xml_node::Invalid_syntax) { }
	}

	/* ---- request/result primitives ---- */

	bool _result_is_for(char const *op, char const *pkg)
	{
		if (!_result.valid()) return false;
		try {
			Genode::Xml_node const r = _result.xml();
			if (!r.has_type("result")) return false;
			if (r.attribute_value("op",  Genode::String<32>()) !=
			    Genode::String<32>(op)) return false;
			if (r.attribute_value("pkg", Genode::String<128>()) !=
			    Genode::String<128>(pkg)) return false;
			return r.has_attribute("status");
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _send_and_wait(char const *op, char const *pkg, unsigned seq)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
			g.attribute("seq", seq);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < OP_ITERS; ++i) {
			_result.update();
			if (_result_is_for(op, pkg))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	Genode::String<32> _result_status()
	{
		try {
			return _result.xml().attribute_value("status",
			                                     Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<32>();
		}
	}

	Genode::String<64> _result_attr(char const *name)
	{
		try {
			return _result.xml().attribute_value(name,
			                                     Genode::String<64>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<64>();
		}
	}

	/* ---- installed-broadcast primitives ---- */

	bool _broadcast_find(char const *name, Genode::String<8> &out_running)
	{
		if (!_installed.valid()) return false;
		try {
			Genode::Xml_node const root = _installed.xml();
			bool found { false };
			root.with_optional_sub_node("packages",
				[&](Genode::Xml_node const &pkgs) {
					pkgs.for_each_sub_node("package",
						[&](Genode::Xml_node const &p) {
						if (!found &&
						    p.attribute_value("name", Genode::String<64>())
						    == Genode::String<64>(name)) {
							found = true;
							out_running = p.attribute_value("running",
							                                Genode::String<8>("no"));
						}
					});
				});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _broadcast_sees(char const *name, char const *want_running)
	{
		for (unsigned i = 0; i < POLL_ITERS; ++i) {
			_installed.update();
			Genode::String<8> got { };
			if (_broadcast_find(name, got) &&
			    got == Genode::String<8>(want_running))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	bool _broadcast_absent(char const *name)
	{
		for (unsigned i = 0; i < POLL_ITERS; ++i) {
			_installed.update();
			Genode::String<8> dummy { };
			if (!_broadcast_find(name, dummy))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	/* ---- raw result bytes (the replay comparison) ---- */

	/*
	 * Content length of a report_rom module. The ROM dataspace capacity
	 * is page-granular and may exceed the content (stale bytes beyond
	 * it), but report_rom NUL-terminates every report (rom_module.h
	 * write_content), so the content ends at the first NUL.
	 */
	static Genode::size_t _content_len(Genode::Attached_rom_dataspace const &rom)
	{
		if (!rom.valid()) return 0;
		char const *p = rom.local_addr<char const>();
		Genode::size_t n { 0 };
		while (n < rom.size() && p[n] != 0) ++n;
		return n;
	}

	void _cache_first_result()
	{
		_result.update();
		_first_len = _content_len(_result);
		if (_first_len == 0 || _first_len > MAX_RESULT) {
			_fail("result ROM has no content (or oversized) after the refusal");
			return;
		}
		char const *src = _result.local_addr<char const>();
		for (Genode::size_t i = 0; i <= _first_len; ++i)
			_first_bytes[i] = src[i];
		Genode::log("exclusivity-probe: cached ", _first_len,
		            " result bytes for the replay comparison");
	}

	bool _launch_result_byte_identical()
	{
		_launch_result.update();
		if (!_launch_result.valid())
			return false;
		if (_content_len(_launch_result) != _first_len)
			return false;
		char const *got = _launch_result.local_addr<char const>();
		for (Genode::size_t i = 0; i <= _first_len; ++i)
			if (got[i] != _first_bytes[i])
				return false;
		return true;
	}

	void run()
	{
		_config_rom.update();
		_parse_config();

		_result.update();
		_installed.update();

		char const *const first  = _first.string();
		char const *const second = _second.string();

		/* 1. install the incumbent (autostart: becomes the peer). */
		Genode::log("exclusivity-probe: sent install ", first);
		if (!_send_and_wait("install", first, 1))
			_fail("install of the incumbent did not answer");
		if (_result_status() != Genode::String<32>("ok")) {
			_fail(Genode::String<96>("install ", first,
			    " expected ok, got ", _result_status()).string());
			return;
		}
		Genode::log("exclusivity-probe: got install ", first, " status=ok");

		/* 2. the incumbent entered the running set (autostart rule). */
		if (!_broadcast_sees(first, "yes")) {
			_fail(Genode::String<96>("broadcast did not show ", first,
			    " running=yes").string());
			return;
		}
		Genode::log("exclusivity-probe: ", first, " running=yes");

		/* 3. install the challenger: FIRST-EXECUTION REFUSAL. */
		Genode::log("exclusivity-probe: sent install ", second);
		if (!_send_and_wait("install", second, 2))
			_fail("install of the challenger did not answer");
		if (_result_status() != Genode::String<32>("exclusive-conflict")) {
			_fail(Genode::String<128>("install ", second,
			    " expected exclusive-conflict, got ",
			    _result_status()).string());
			return;
		}
		if (_result_attr("peer") != Genode::String<64>(first)) {
			_fail(Genode::String<128>("conflict must name peer ", first,
			    ", got '", _result_attr("peer"), "'").string());
			return;
		}
		if (_result_attr("group") != Genode::String<64>(_group)) {
			_fail(Genode::String<128>("conflict must name group ", _group,
			    ", got '", _result_attr("group"), "'").string());
			return;
		}
		Genode::log("exclusivity-probe: first-refusal ok (",
		            second, " refused, peer=", first, ")");

		/* 4. the refusal mutated nothing. */
		if (!_broadcast_absent(second)) {
			_fail(Genode::String<96>("broadcast shows ", second,
			    " after the refusal — the refusal mutated state").string());
			return;
		}
		Genode::log("exclusivity-probe: refusal left no state");

		/* 5. duplicate replay: byte-identical, via the launcher channel. */
		_cache_first_result();

		Genode::log("exclusivity-replay: SENT install ", second,
		            " seq=2 on launcher_request");
		_launch_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", second);
			g.attribute("seq", 2);
		});

		_timer.msleep(200);
		bool answered { false };
		for (unsigned i = 0; i < DUP_ITERS; ++i) {
			_launch_result.update();
			if (_launch_result.valid() && _launch_result.size() > 0) {
				answered = true;
				break;
			}
			_timer.msleep(100);
		}
		if (!answered) {
			_fail("duplicate install produced no launcher_result — "
			      "the replay swallowed the reply");
			return;
		}
		if (!_launch_result_byte_identical()) {
			Genode::error("exclusivity-replay: FAIL size ", _launch_result.size(),
			              " vs ", _first_len);
			_fail("replayed result is not byte-identical to the first refusal");
			return;
		}
		Genode::log("exclusivity-replay: DONE byte-identical (",
		            _first_len, " bytes)");

		Genode::log("exclusivity-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Exclusivity_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }
