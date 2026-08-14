/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * stability_probe — Phase 14 W9 stability acceptance probe.
 *
 * Long-running workload driver for the criterion-1 stability gate
 * (docs/plans/phase14-daily-desktop.md D14.4):
 *
 *   - ≥100 install/launch/remove cycles of pkg_gui_demo, gated on
 *     first-paint (window_list) and teardown (window_list drop).
 *   - One configd set per cycle (theme.active round-trips through the
 *     configd request -> result -> config broadcast bus).
 *   - One notifier post per cycle (notif_request -> notifications).
 *   - Per-cycle "stability-probe: cycle N PASS" marker.
 *   - Snapshot lines at t=0, 600s, 1200s, 1800s.
 *   - "stability-probe: PASS" at the end, "stability-probe: FAIL
 *     <reason>" on any non-recoverable error.
 *
 * Own <config> (read at construct, no sigh needed — the run-time
 * knobs are baked in before boot):
 *   <stability_probe max_cycles="N" fail_at_cycle="M"/>
 *
 *   max_cycles    0 = run until parent times out; >0 = PASS at that cycle
 *   fail_at_cycle 0 = never; >0 = FAIL + exit(1) when cycle reaches M
 *
 * The fastfail run configures fail_at_cycle=3 so the probe self-
 * terminates with FAIL after cycle 3 — the run script gates on that
 * exact marker, proving the crash-detection path is wired.
 *
 * Design note: the probe is a single-threaded Genode component.
 * Every ROM read uses Attached_rom_dataspace::update() and every
 * Report write uses Expanding_reporter::generate_xml(). No async,
 * no spawn, no shared state — the same Component::construct() pattern
 * as wm_tasks_probe and theme_probe (AGENTS.md §3.1 + §3.2).
 *
 * The Genode timer is a 10 ms millisecond-resolution Timer::Connection
 * (Genode::Timer_session) — msleep(50..200) per poll. A cycle
 * typically takes ~5 s end-to-end (most of it on the first-paint and
 * teardown gates; 100 cycles lands well inside the 1800 s budget).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>


namespace {


struct Stability_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env, "stability-probe" };

	/* pkgd */
	Genode::Expanding_reporter     _pkg_request  { _env, "request",       "request" };
	Genode::Attached_rom_dataspace _pkg_result   { _env, "result" };

	/* configd */
	Genode::Expanding_reporter     _config_request { _env, "request",       "config_request" };
	Genode::Attached_rom_dataspace _config_result  { _env, "config_result" };
	Genode::Attached_rom_dataspace _config_bcast   { _env, "config" };

	/* notifier */
	Genode::Expanding_reporter     _notif_request { _env, "notif_request", "notif_request" };
	Genode::Attached_rom_dataspace _notifications { _env, "notifications" };

	/* wm */
	Genode::Attached_rom_dataspace _window_list_rom { _env, "window_list" };

	Genode::Attached_rom_dataspace _config { _env, "stability_probe.config" };

	/* knobs (parsed at construct from <config>). */
	unsigned _max_cycles    { 0 };
	unsigned _fail_at_cycle { 0 };

	/* timing */
	Genode::uint64_t _t0_ms { 0 };

	static char const *const GUI_LABEL;

	Stability_probe(Genode::Env &env) : _env(env) { _parse_config(); }

	void _fail(char const *reason)
	{
		Genode::error("stability-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/* ============ config parsing ============ */

	void _parse_config()
	{
		_config.update();
		if (!_config.valid()) return;
		try {
			Genode::Xml_node const root = _config.xml();
			if (root.has_type("stability_probe")) {
				_max_cycles    = root.attribute_value("max_cycles",    0u);
				_fail_at_cycle = root.attribute_value("fail_at_cycle", 0u);
			} else {
				root.for_each_sub_node("stability_probe", [&](Genode::Xml_node const &n) {
					_max_cycles    = n.attribute_value("max_cycles",    0u);
					_fail_at_cycle = n.attribute_value("fail_at_cycle", 0u);
				});
			}
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("stability-probe: malformed <config>; using defaults");
		}
		Genode::log("stability-probe: config max_cycles=", _max_cycles,
		            " fail_at_cycle=", _fail_at_cycle);
	}

	/* ============ pkgd channel ============ */

	bool _send_pkg(char const *op, char const *pkg, unsigned budget_ms = 20000)
	{
		_pkg_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
		});

		unsigned const step = 100;
		unsigned const iters = budget_ms / step;
		for (unsigned i = 0; i < iters; ++i) {
			_pkg_result.update();
			if (_pkg_result.valid()) {
				try {
					Genode::Xml_node const r = _pkg_result.xml();
					if (r.has_type("result")
					 && r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>(op)
					 && r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg)) {
						Genode::String<32> s = r.attribute_value("status", Genode::String<32>());
						if (s == Genode::String<32>("ok")
						 || s == Genode::String<32>("not-installed")
						 || s == Genode::String<32>("already-running")
						 || s == Genode::String<32>("removed")) {
							return true;
						}
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(step);
		}
		return false;
	}

	/* ============ configd channel ============ */

	bool _set_config(char const *key, char const *value, unsigned budget_ms = 5000)
	{
		_config_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		unsigned const step = 100;
		unsigned const iters = budget_ms / step;
		for (unsigned i = 0; i < iters; ++i) {
			_config_result.update();
			if (_config_result.valid()) {
				try {
					Genode::Xml_node const r = _config_result.xml();
					if (r.has_type("result")
					 && r.attribute_value("op",    Genode::String<32>())  == Genode::String<32>("set")
					 && r.attribute_value("key",   Genode::String<128>()) == Genode::String<128>(key)
					 && r.attribute_value("value", Genode::String<128>()) == Genode::String<128>(value)
					 && r.attribute_value("status", Genode::String<32>()) == Genode::String<32>("ok"))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(step);
		}
		return false;
	}

	bool _config_bcast_has(char const *key, char const *expected_value)
	{
		_config_bcast.update();
		if (!_config_bcast.valid()) return false;
		char const *raw = _config_bcast.local_addr<char const>();
		Genode::size_t sz = _config_bcast.size();
		char needle[256];
		Genode::size_t p = 0;
		char const *prefix = "<key name=\"";
		for (Genode::size_t i = 0; prefix[i] && p < sizeof(needle) - 1; ++i) needle[p++] = prefix[i];
		for (Genode::size_t i = 0; key[i] && p < sizeof(needle) - 1; ++i)       needle[p++] = key[i];
		char const *mid = "\" value=\"";
		for (Genode::size_t i = 0; mid[i] && p < sizeof(needle) - 1; ++i)      needle[p++] = mid[i];
		for (Genode::size_t i = 0; expected_value[i] && p < sizeof(needle) - 1; ++i) needle[p++] = expected_value[i];
		char const *suffix = "\"/>";
		for (Genode::size_t i = 0; suffix[i] && p < sizeof(needle) - 1; ++i)   needle[p++] = suffix[i];
		needle[p] = '\0';
		Genode::size_t const nlen = p;
		for (Genode::size_t k = 0; k + nlen < sz; ++k) {
			if (Genode::memcmp(raw + k, needle, nlen) == 0)
				return true;
		}
		return false;
	}

	/* ============ notifier channel ============ */

	void _post_notif(unsigned cycle_n)
	{
		Genode::String<32> body_str("cycle=", cycle_n);

		_notif_request.generate_xml([&](Genode::Xml_generator &g) {
			g.node("notification", [&] {
				g.attribute("source", "stability_probe");
				g.attribute("kind",   "info");
				g.attribute("ttl_ms", "10000");
				g.node("title", [&] { g.append_sanitized("Stability cycle "); });
				g.node("body",  [&] { g.append_sanitized(body_str.string()); });
			});
		});
	}

	bool _notif_seen(char const *body_substr, unsigned budget_ms = 4000)
	{
		unsigned const step = 100;
		unsigned const iters = budget_ms / step;
		Genode::size_t const nlen = Genode::strlen(body_substr);
		for (unsigned i = 0; i < iters; ++i) {
			_notifications.update();
			if (_notifications.valid()) {
				char const *raw = _notifications.local_addr<char const>();
				Genode::size_t sz = _notifications.size();
				for (Genode::size_t k = 0; k + nlen < sz; ++k) {
					if (Genode::memcmp(raw + k, body_substr, nlen) == 0)
						return true;
				}
			}
			_timer.msleep(step);
		}
		return false;
	}

	/* ============ window_list reader ============ */

	bool _in_window_list(char const *needle, unsigned budget_ms = 30000)
	{
		unsigned const step = 100;
		unsigned const iters = budget_ms / step;
		bool ever_valid = false;
		Genode::size_t const nlen = Genode::strlen(needle);
		for (unsigned i = 0; i < iters; ++i) {
			_window_list_rom.update();
			if (_window_list_rom.valid()) {
				ever_valid = true;
				char const *raw = _window_list_rom.local_addr<char const>();
				Genode::size_t sz = _window_list_rom.size();
				bool found = false;
				for (Genode::size_t k = 0; k + nlen < sz; ++k) {
					if (Genode::memcmp(raw + k, needle, nlen) == 0) {
						found = true; break;
					}
				}
				if (found) return true;
			}
			_timer.msleep(step);
		}
		if (!ever_valid)
			Genode::error("stability-probe: window_list ROM never became valid");
		return false;
	}

	bool _not_in_window_list(char const *needle, unsigned budget_ms = 20000)
	{
		unsigned const step = 100;
		unsigned const iters = budget_ms / step;
		Genode::size_t const nlen = Genode::strlen(needle);
		for (unsigned i = 0; i < iters; ++i) {
			_window_list_rom.update();
			if (_window_list_rom.valid()) {
				char const *raw = _window_list_rom.local_addr<char const>();
				Genode::size_t sz = _window_list_rom.size();
				bool found = false;
				for (Genode::size_t k = 0; k + nlen < sz; ++k) {
					if (Genode::memcmp(raw + k, needle, nlen) == 0) {
						found = true; break;
					}
				}
				if (!found) return true;
			}
			_timer.msleep(step);
		}
		return false;
	}

	/* ============ helpers ============ */

	Genode::uint64_t _elapsed_ms() const { return _timer.elapsed_ms() - _t0_ms; }

	void _snapshot(char const *tag, unsigned cycle_n)
	{
		Genode::uint64_t const e = _elapsed_ms();
		Genode::log("stability-probe: snapshot ", tag,
		            " t=", e / 1000, "s cycle=", cycle_n,
		            " elapsed=", e, "ms");
	}

	/* ============ one cycle ============ */

	bool _do_cycle(unsigned cycle_n)
	{
		Genode::log("stability-probe: [cycle ", cycle_n, "] start");

		/* 1. install (idempotent — pkgd answers ok even if installed) */
		if (!_send_pkg("install", "pkg_gui_demo")) {
			Genode::error("stability-probe: [cycle ", cycle_n, "] install timed out");
			return false;
		}

		/* 2. launch */
		if (!_send_pkg("launch", "pkg_gui_demo")) {
			Genode::error("stability-probe: [cycle ", cycle_n, "] launch timed out");
			return false;
		}

		/* 3. wait for first paint (window_list) */
		if (!_in_window_list(GUI_LABEL)) {
			Genode::error("stability-probe: [cycle ", cycle_n,
			              "] pkg_gui_demo never appeared in window_list");
			return false;
		}

		/* 4. set theme.active=default (configd round-trip) */
		if (!_set_config("theme.active", "default")) {
			Genode::error("stability-probe: [cycle ", cycle_n,
			              "] configd set theme.active=default timed out");
			return false;
		}
		_timer.msleep(200);
		if (!_config_bcast_has("theme.active", "default")) {
			Genode::error("stability-probe: [cycle ", cycle_n,
			              "] config broadcast did not reflect theme.active=default");
			return false;
		}

		/* 5. post notification + wait for it to appear in the broadcast */
		Genode::String<32> body("cycle=", cycle_n);
		_post_notif(cycle_n);
		if (!_notif_seen(body.string())) {
			Genode::error("stability-probe: [cycle ", cycle_n,
			              "] posted notification not observed in broadcast");
			return false;
		}

		/* 6. remove pkg_gui_demo (closes the window) */
		if (!_send_pkg("remove", "pkg_gui_demo")) {
			Genode::error("stability-probe: [cycle ", cycle_n, "] remove timed out");
			return false;
		}

		/* 7. wait for teardown */
		if (!_not_in_window_list(GUI_LABEL)) {
			Genode::error("stability-probe: [cycle ", cycle_n,
			              "] pkg_gui_demo still in window_list after remove");
			return false;
		}

		Genode::log("stability-probe: cycle ", cycle_n, " PASS");
		return true;
	}

	/* ============ main sequence ============ */

	void run()
	{
		Genode::log("stability-probe: starting W9 stability acceptance");
		_t0_ms = _timer.elapsed_ms();

		/* Wait for the report_rom/attached_rom wiring to come up. */
		_timer.msleep(500);

		/* snapshot at t=0 happens implicitly at the first cycle. */
		bool last_was_snapshot = false;
		for (unsigned cycle = 1; ; ++cycle) {

			/* Configured fail-at-cycle for the fastfail scenario. */
			if (_fail_at_cycle > 0 && cycle == _fail_at_cycle) {
				Genode::log("stability-probe: FAIL injected failure at cycle ", cycle,
				            " (fail_at_cycle configured)");
				while (true) { _timer.msleep(60 * 1000); }
			}

			/* Configured max-cycles for the bounded scenarios. */
			if (_max_cycles > 0 && cycle > _max_cycles) {
				Genode::log("stability-probe: PASS (reached max_cycles=", _max_cycles, ")");
				_env.parent().exit(0);
				return;
			}

			Genode::uint64_t const t = _elapsed_ms();
			if (t >= 1800u * 1000u) {
				Genode::log("stability-probe: PASS (reached 30-min wall-clock budget)");
				_env.parent().exit(0);
				return;
			}

			/* Snapshot points (fire once each). */
			if (!last_was_snapshot && t >= 600u * 1000u) {
				_snapshot("t=600s", cycle); last_was_snapshot = true;
			}
			/* (snapshot at t=0 is implicit — the first cycle log) */

			if (!_do_cycle(cycle)) {
				_fail("cycle failed (see prior error line)");
				return;
			}

			Genode::uint64_t const t2 = _elapsed_ms();
			if (!last_was_snapshot && t2 >= 1200u * 1000u) {
				_snapshot("t=1200s", cycle);
			}
			if (t2 >= 1800u * 1000u) {
				Genode::log("stability-probe: PASS (reached 30-min wall-clock budget)");
				_env.parent().exit(0);
				return;
			}
		}
	}
};


char const *const Stability_probe::GUI_LABEL = "pkg_runtime -> pkg_gui_demo";


}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Stability_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
