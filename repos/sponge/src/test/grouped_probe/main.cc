/* SPDX-License-Identifier: Apache-2.0
 *
 * grouped_probe — sponge_pkgd grouped-runtime + per-class admission
 * verifier (Phase 16 P2.4 commit 3).
 *
 * One bounded headless boot (run/sponge-pkg-grouped.run) proves the
 * grouped-mode integration end to end:
 *
 *   1. CLASS PLACEMENT / ISOLATION. Each fixture package's <start>
 *      node appears in its own class config (runtime_simple /
 *      runtime_terminal / runtime_browser / driver_runtime, read via
 *      the report_rom fan-out) and in NO other, and each class
 *      runtime really started its own child — the shared
 *      config_witness payload's monotonic beat counter, one per class
 *      runtime, must reach 1+.
 *
 *   2. PER-CLASS BUDGET REFUSAL FIGURES. A terminal-class launch that
 *      does not fit pkg_runtime_terminal's budget must be refused with
 *      'insufficient-resources' citing terminal's OWN report figures —
 *      while pkg_runtime_simple still has ample budget (twice the
 *      demand). A cross-class read would cite simple's large avail and
 *      fail the bound below; with distinct grants per class the ranges
 *      are disjoint, so the refusal figures identify their source.
 *      The refusal must be a clean no-op (never running, no config
 *      republication), and freeing the budget must make the very same
 *      launch succeed.
 *
 *   3. MALFORMED QUOTAS, APP AND DRIVER. An autostart package with an
 *      unparsable <quota ram="garbage"/> must be refused at install
 *      ('insufficient-resources' with the zero-figure convention)
 *      BEFORE any mutation — the quota sanity runs independently of
 *      whether the class's admission gate is armed (no driver admission
 *      is armed in this scenario) — and a later valid install + launch
 *      must still succeed (no emission wedge). The driver case is
 *      reached as a dependency of the request root (badriver under
 *      badriver_root), so the refusal must also name the OFFENDER
 *      (badriver) in its machine-readable pkg attribute, not the
 *      request root.
 *
 *   4. MIXED-CLASS EMISSION. pkgd's <repro_malformed_from> knob
 *      corrupts the SIMPLE render at a pinned write; the probe fires
 *      that write as an install whose resolved plan dirties BOTH the
 *      simple and terminal slots (mixroot + mixdep). The P1.5
 *      publication seam must then publish NOTHING — both class configs
 *      must keep their exact bytes even though the terminal render was
 *      clean — and the running children must be undisturbed (beats
 *      still rising). The knob's write number is cross-checked against
 *      the installed broadcast's config_writes counter first, so any
 *      sequence drift fails loud instead of silently weakening the
 *      test.
 *
 * State sources read, never written: the three per-class sandbox state
 * reports (HID, the same ROMs sponge_pkgd admits against), the four
 * class config reports, the installed broadcast, and the four beat
 * counters.
 *
 * Logs "grouped-runtime-probe: PASS" only if every assertion held;
 * otherwise "grouped-runtime-probe: FAIL <reason>" and exit non-zero —
 * the scenario then fails loud on its bounded expect timeout
 * (docs/09-roadmap.md §11.1 — never a silent hang).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

struct Grouped_probe
{
	static constexpr unsigned OP_ITERS   = 120;  /* x 100ms = 12 s ceiling per request  */
	static constexpr unsigned POLL_ITERS = 80;   /* x 100ms =  8 s ceiling per assertion */
	static constexpr unsigned STATE_WAIT = 40;   /* x 500ms = 20 s ceiling for the first
	                                               * per-class state reports (periodic,
	                                               * delay_ms=2000, published once each
	                                               * class runtime applied its config) */
	static constexpr unsigned SETTLE_MAX = 40;   /* x 500ms = 20 s ceiling for a budget
	                                               * change to appear AND stabilise */

	/* Fixture names, pinned by run/sponge-pkg-grouped.run + pkg/. */
	static constexpr char const *G_SIMPLE   = "g_simple";
	static constexpr char const *G_TERMINAL = "g_terminal";
	static constexpr char const *G_BROWSER  = "g_browser";
	static constexpr char const *G_DRIVER   = "g_driver";
	static constexpr char const *T_HOG      = "t_hog";
	static constexpr char const *T_FAT      = "t_fat";
	static constexpr char const *BADQUOTA   = "badquota";
	static constexpr char const *BADRIVER   = "badriver";
	static constexpr char const *BADIVEROOT = "badriver_root";
	static constexpr char const *S_AFTER    = "s_after";
	static constexpr char const *MIXROOT    = "mixroot";
	static constexpr char const *MIXDEP     = "mixdep";

	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Genode::Expanding_reporter     _request   { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result    { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	Genode::Attached_rom_dataspace _cfg_simple   { _env, "cfg_simple" };
	Genode::Attached_rom_dataspace _cfg_terminal { _env, "cfg_terminal" };
	Genode::Attached_rom_dataspace _cfg_browser  { _env, "cfg_browser" };
	Genode::Attached_rom_dataspace _cfg_driver   { _env, "cfg_driver" };

	Genode::Attached_rom_dataspace _st_simple   { _env, "st_simple" };
	Genode::Attached_rom_dataspace _st_terminal { _env, "st_terminal" };
	Genode::Attached_rom_dataspace _st_browser  { _env, "st_browser" };

	Genode::Attached_rom_dataspace _b_simple   { _env, "b_simple" };
	Genode::Attached_rom_dataspace _b_terminal { _env, "b_terminal" };
	Genode::Attached_rom_dataspace _b_browser  { _env, "b_browser" };
	Genode::Attached_rom_dataspace _b_driver   { _env, "b_driver" };

	/* Probe config: the knob write + the pinned t_fat demand. */
	unsigned       _malform_from { 0 };
	Genode::size_t _need_ram     { 0 };
	unsigned       _need_caps    { 0 };

	unsigned _seq { 0 };

	Grouped_probe(Genode::Env &env) : _env(env) { }

	[[noreturn]]
	void _fail(char const *reason)
	{
		Genode::error("grouped-runtime-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/* ---- config parse (HID or XML, same dual path as admission_probe) ---- */

	template <typename NODE>
	void _collect(NODE const &n)
	{
		_malform_from = n.attribute_value("malform_from", 0u);

		Genode::String<32> const s_ram  =
			n.attribute_value("need_ram",  Genode::String<32>());
		Genode::String<32> const s_caps =
			n.attribute_value("need_caps", Genode::String<32>());

		_need_ram  = 0;
		_need_caps = 0;
		if (!_parse_bytes(s_ram, _need_ram) || _need_ram == 0)
			_fail("config: need_ram is missing or unparsable");
		Genode::size_t tmp { 0 };
		if (!_parse_bytes(s_caps, tmp) || tmp == 0 || tmp > 0xffffffu)
			_fail("config: need_caps is missing or unparsable");
		_need_caps = (unsigned)tmp;
	}

	void _parse_config()
	{
		char const *const base = _config_rom.local_addr<char>();
		Genode::size_t  const sz  = _config_rom.size();

		bool const is_xml = (sz > 0 && base[0] == '<');
		if (is_xml) {
			Genode::Xml_node const root(base, sz);
			if (root.has_type("config")) _collect(root);
		} else {
			Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
			if (root.has_type("config")) _collect(root);
		}

		if (_malform_from == 0)
			_fail("config: malform_from is missing (the knob write must be pinned)");
	}

	/*
	 * Suffix-aware byte-count parser. The sandbox state report prints
	 * every round figure through Number_of_bytes::print() ("64M"), so a
	 * plain numeric conversion would read 64M as 64 bytes. Accepts the
	 * plain decimal form too (the <result> figures are raw bytes).
	 */
	static bool _parse_bytes(Genode::String<32> const &s, Genode::size_t &out)
	{
		char const *p = s.string();
		Genode::size_t v { 0 };
		unsigned digits { 0 };
		while (*p >= '0' && *p <= '9') {
			v = v * 10 + Genode::size_t(*p - '0');
			++p; ++digits;
		}
		if (!digits) return false;

		switch (*p) {
		case 'K': case 'k': v *= 1024; break;
		case 'M': case 'm': v *= 1024 * 1024; break;
		case 'G': case 'g': v *= 1024 * 1024 * 1024; break;
		case '\0':            break;
		default:               return false;
		}
		out = v;
		return true;
	}

	/* ---- per-class sandbox state reports (HID) ---- */

	struct Budget
	{
		bool          ram_known  { false };
		bool          caps_known { false };
		Genode::size_t ram        { 0 };
		unsigned      caps       { 0 };

		bool known() const { return ram_known && caps_known; }
	};

	/*
	 * Read one class runtime's own top-level <ram>/<caps> nodes — the
	 * quota that runtime has NOT yet donated to a child. HID, not XML
	 * (the 26.08 sandbox Generator); per-child nodes are ignored.
	 */
	Budget _budget(Genode::Attached_rom_dataspace &state)
	{
		Budget b;
		state.update();
		if (!state.valid() || state.size() == 0)
			return b;

		Genode::Hid_node const root(Genode::Const_byte_range_ptr(
			state.local_addr<char const>(), state.size()));

		Genode::String<32> ram_avail  { }, caps_avail { };
		root.for_each_sub_node([&] (Genode::Hid_node const &n) {
			if (n.has_type("ram") && !b.ram_known) {
				ram_avail = n.attribute_value("avail", Genode::String<32>());
				if (_parse_bytes(ram_avail, b.ram)) b.ram_known = true;
			}
			if (n.has_type("caps") && !b.caps_known) {
				caps_avail = n.attribute_value("avail", Genode::String<32>());
				Genode::size_t v { 0 };
				if (_parse_bytes(caps_avail, v) && v > 0 && v <= 0xffffffu) {
					b.caps = (unsigned)v;
					b.caps_known = true;
				}
			}
		});
		return b;
	}

	bool _await_first_budget(Genode::Attached_rom_dataspace &state, Budget &out)
	{
		for (unsigned i = 0; i < STATE_WAIT; ++i) {
			out = _budget(state);
			if (out.known()) return true;
			_timer.msleep(500);
		}
		return false;
	}

	/*
	 * Wait until the report differs from `before` and then holds still
	 * (three consecutive equal samples), so the next request is judged
	 * against a settled number rather than one still catching up with
	 * the previous transition (the report's 2 s period). Same shape as
	 * admission_probe's settle, on ram+caps jointly.
	 */
	bool _settle(Genode::Attached_rom_dataspace &state,
	             Budget const &before, Budget &out)
	{
		Budget last { };
		unsigned stable { 0 };

		for (unsigned i = 0; i < SETTLE_MAX; ++i) {
			Budget const b = _budget(state);
			if (b.known()) {
				if (b.ram != last.ram || b.caps != last.caps) {
					last = b;
					stable = 0;
				} else if (++stable >= 3 && b.ram != before.ram) {
					out = b;
					return true;
				}
			}
			_timer.msleep(500);
		}
		Genode::warning("grouped-runtime-probe: budget never settled (still ram=",
		                last.ram, ") — continuing with the last observed value");
		out = last;
		return false;
	}

	/* ---- beat counters (one per class runtime) ---- */

	unsigned _beat_n(Genode::Attached_rom_dataspace &beat)
	{
		beat.update();
		if (!beat.valid()) return 0;
		try {
			Genode::Xml_node const b = beat.xml();
			if (!b.has_type("beat")) return 0;
			return b.attribute_value("n", 0u);
		} catch (Genode::Xml_node::Invalid_syntax) { return 0; }
	}

	bool _await_first_beat(Genode::Attached_rom_dataspace &beat, unsigned &out)
	{
		for (unsigned i = 0; i < STATE_WAIT; ++i) {
			out = _beat_n(beat);
			if (out > 0) return true;
			_timer.msleep(500);
		}
		return false;
	}

	/* ---- request / result primitives (admission_probe shape) ---- */

	bool _result_is_for(char const *op, char const *pkg)
	{
		if (!_result.valid()) return false;
		try {
			Genode::Xml_node const r = _result.xml();
			if (!r.has_type("result")) return false;
			if (r.attribute_value("op",  Genode::String<32>())  != Genode::String<32>(op))  return false;
			if (r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg)) return false;
			return r.has_attribute("status");
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _send_and_wait(char const *op, char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
			g.attribute("seq", ++_seq);
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

	Genode::String<32> _status()
	{
		try {
			return _result.xml().attribute_value("status", Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<32>();
		}
	}

	Genode::String<128> _result_pkg()
	{
		try {
			return _result.xml().attribute_value("pkg", Genode::String<128>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<128>();
		}
	}

	/*
	 * Send a request for `req_pkg` and wait for a result naming EITHER
	 * `offender` or `req_pkg` — the malformed-quota refusal must name
	 * the offending package (c.name), so the caller asserts which one
	 * arrived instead of assuming the request root.
	 */
	bool _send_and_wait_pair(char const *op, char const *req_pkg,
	                         char const *offender)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", req_pkg);
			g.attribute("seq", ++_seq);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < OP_ITERS; ++i) {
			_result.update();
			if (_result_is_for(op, offender) || _result_is_for(op, req_pkg))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	bool _expect(char const *op, char const *pkg, char const *want)
	{
		if (!_send_and_wait(op, pkg)) {
			_fail(Genode::String<96>(op, " ", pkg, " was not answered").string());
			return false;
		}
		Genode::String<32> const got = _status();
		if (got != Genode::String<32>(want)) {
			_fail(Genode::String<192>(op, " ", pkg, " expected status=", want,
			                          " but got ", got).string());
			return false;
		}
		Genode::log("grouped-runtime-probe: ", op, " ", pkg, " -> ", got);
		return true;
	}

	struct Figures
	{
		bool          known     { false };
		Genode::size_t need_ram  { 0 };
		Genode::size_t avail_ram { 0 };
		unsigned      need_caps  { 0 };
		unsigned      avail_caps { 0 };
	};

	Figures _figures()
	{
		Figures f;
		Genode::String<32> s[4] { };
		try {
			Genode::Xml_node const r = _result.xml();
			s[0] = r.attribute_value("need_ram",   Genode::String<32>());
			s[1] = r.attribute_value("avail_ram",  Genode::String<32>());
			s[2] = r.attribute_value("need_caps",  Genode::String<32>());
			s[3] = r.attribute_value("avail_caps", Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) {
			return f;
		}
		Genode::size_t tmp { 0 };
		if (!_parse_bytes(s[0], f.need_ram))  return f;
		if (!_parse_bytes(s[1], f.avail_ram)) return f;
		if (!_parse_bytes(s[2], tmp) || tmp > 0xffffffu) return f;
		f.need_caps = (unsigned)tmp;
		if (!_parse_bytes(s[3], tmp) || tmp > 0xffffffu) return f;
		f.avail_caps = (unsigned)tmp;
		f.known = true;
		return f;
	}

	/* ---- installed broadcast ---- */

	struct Entry { bool found { false }; bool running { false }; };

	Entry _broadcast_entry(char const *name)
	{
		Entry e;
		_installed.update();
		if (!_installed.valid()) return e;
		try {
			Genode::Xml_node const root = _installed.xml();
			root.with_optional_sub_node("packages",
				[&](Genode::Xml_node const &pkgs) {
					pkgs.for_each_sub_node("package",
						[&](Genode::Xml_node const &p) {
							if (!e.found &&
							    p.attribute_value("name", Genode::String<64>())
							    == Genode::String<64>(name)) {
								e.found   = true;
								e.running = p.attribute_value("running",
								                              Genode::String<8>("no"))
								        == Genode::String<8>("yes");
							}
						});
				});
		} catch (Genode::Xml_node::Invalid_syntax) { }
		return e;
	}

	bool _await_running(char const *name)
	{
		for (unsigned i = 0; i < POLL_ITERS; ++i) {
			Entry const e = _broadcast_entry(name);
			if (e.found && e.running) return true;
			_timer.msleep(100);
		}
		return false;
	}

	unsigned _config_writes()
	{
		_installed.update();
		if (!_installed.valid()) return 0;
		try {
			return _installed.xml().attribute_value("config_writes", 0u);
		} catch (Genode::Xml_node::Invalid_syntax) { return 0; }
	}

	/* ---- class config reports (XML documents from the emitter) ---- */

	struct Cfg_hash { Genode::uint64_t h { 0 }; Genode::size_t len { 0 };
	                  bool valid { false }; };

	Cfg_hash _cfg_hash(Genode::Attached_rom_dataspace &rom)
	{
		Cfg_hash r;
		rom.update();
		if (!rom.valid() || rom.size() == 0) return r;
		char const *const base = rom.local_addr<char const>();
		r.len = rom.size();
		r.h = 1469598103934665603ull;   /* FNV-1a */
		for (Genode::size_t i = 0; i < r.len; ++i) {
			r.h ^= (Genode::uint64_t)(unsigned char)base[i];
			r.h *= 1099511628211ull;
		}
		r.valid = true;
		return r;
	}

	bool _same(Cfg_hash const &a, Cfg_hash const &b)
	{ return a.valid && b.valid && a.h == b.h && a.len == b.len; }

	bool _cfg_has_start(Genode::Attached_rom_dataspace &rom, char const *name)
	{
		rom.update();
		if (!rom.valid() || rom.size() == 0) return false;
		bool found = false;
		try {
			Genode::Xml_node const root(rom.local_addr<char const>(), rom.size());
			if (!root.has_type("config")) return false;
			root.for_each_sub_node("start", [&](Genode::Xml_node const &s) {
				if (!found &&
				    s.attribute_value("name", Genode::String<64>())
				    == Genode::String<64>(name))
					found = true;
			});
		} catch (Genode::Xml_node::Invalid_syntax) { return false; }
		return found;
	}

	bool _await_start(Genode::Attached_rom_dataspace &rom, char const *name)
	{
		for (unsigned i = 0; i < POLL_ITERS; ++i) {
			if (_cfg_has_start(rom, name)) return true;
			_timer.msleep(100);
		}
		return false;
	}

	/* Raw substring scan: a package name must not appear anywhere in
	 * another class's config (it only ever appears as its <start> name
	 * or a "<pkg>-ro" session label; the fixtures declare no such
	 * session). */
	bool _cfg_mentions(Genode::Attached_rom_dataspace &rom, char const *name)
	{
		rom.update();
		if (!rom.valid()) return false;
		char const *const base = rom.local_addr<char const>();
		Genode::size_t  const sz  = rom.size();
		Genode::size_t  const n   = Genode::strlen(name);
		if (n == 0 || sz < n) return false;
		for (Genode::size_t i = 0; i + n <= sz; ++i)
			if (Genode::strcmp(base + i, name, n) == 0)
				return true;
		return false;
	}

	/* ---- the sequence ---- */

	void _run()
	{
		_parse_config();
		Genode::log("grouped-runtime-probe: malform_from=", _malform_from,
		            " need_ram=", _need_ram, " need_caps=", _need_caps);

		/* Phase 0: every class runtime must publish its state report. */
		Budget sb, tb, bb;
		if (!_await_first_budget(_st_simple, sb) ||
		    !_await_first_budget(_st_terminal, tb) ||
		    !_await_first_budget(_st_browser, bb)) {
			_fail("a class runtime never published its state report "
			      "(per-class state relays not armed?)");
			return;
		}
		Genode::log("grouped-runtime-probe: budgets at boot: simple ram=",
		            sb.ram, " terminal ram=", tb.ram, " browser ram=", bb.ram);

		/*
		 * Phase 1: class placement + isolation (writes 2..5).
		 * One install per fixture; each class config must carry its
		 * own package and none of the others.
		 */
		if (!_expect("install", G_SIMPLE,   "ok")) return;
		if (!_expect("install", G_TERMINAL, "ok")) return;
		if (!_expect("install", G_BROWSER,  "ok")) return;
		if (!_expect("install", G_DRIVER,   "ok")) return;

		if (!_await_start(_cfg_simple,   G_SIMPLE)) {
			_fail("g_simple never appeared in the runtime_simple config");
			return;
		}
		if (!_await_start(_cfg_terminal, G_TERMINAL)) {
			_fail("g_terminal never appeared in the runtime_terminal config");
			return;
		}
		if (!_await_start(_cfg_browser,  G_BROWSER)) {
			_fail("g_browser never appeared in the runtime_browser config");
			return;
		}
		if (!_await_start(_cfg_driver,   G_DRIVER)) {
			_fail("g_driver never appeared in the driver_runtime config");
			return;
		}

		struct Leak { Genode::Attached_rom_dataspace *rom;
		              char const *n1; char const *n2; char const *n3; };
		Leak leaks[] {
			{ &_cfg_simple,   G_TERMINAL, G_BROWSER, G_DRIVER   },
			{ &_cfg_terminal, G_SIMPLE,   G_BROWSER, G_DRIVER   },
			{ &_cfg_browser,  G_SIMPLE,   G_TERMINAL, G_DRIVER  },
			{ &_cfg_driver,   G_SIMPLE,   G_TERMINAL, G_BROWSER },
		};
		for (Leak const &l : leaks) {
			if (_cfg_mentions(*l.rom, l.n1) ||
			    _cfg_mentions(*l.rom, l.n2) ||
			    _cfg_mentions(*l.rom, l.n3)) {
				_fail("a package leaked across class configs");
				return;
			}
		}
		Genode::log("grouped-runtime-probe: class placement + isolation ok");

		/* Each class runtime really started ITS OWN child. */
		unsigned bs { 0 }, bt { 0 }, bb2 { 0 }, bd { 0 };
		if (!_await_first_beat(_b_simple, bs) ||
		    !_await_first_beat(_b_terminal, bt) ||
		    !_await_first_beat(_b_browser, bb2) ||
		    !_await_first_beat(_b_driver, bd)) {
			_fail("a class runtime never started its own fixture child "
			      "(beat missing — the config did not reach it?)");
			return;
		}
		Genode::log("grouped-runtime-probe: beats alive (simple=", bs,
		            " terminal=", bt, " browser=", bb2, " driver=", bd, ")");

		/*
		 * Phase 2: per-class budget refusal (writes 6..9; t_fat was
		 * already registered as g_terminal's dependency at write 3,
		 * so no extra install write is needed here).
		 */
		if (!_expect("install", T_HOG, "ok")) return;

		Budget const tb0 = _budget(_st_terminal);
		if (!_expect("launch", T_HOG, "ok")) return;

		Budget tb1;
		_settle(_st_terminal, tb0, tb1);
		Genode::log("grouped-runtime-probe: terminal after t_hog: ram=",
		            tb1.ram, " caps=", tb1.caps);
		Budget const sb1 = _budget(_st_simple);
		Genode::log("grouped-runtime-probe: simple is at ram=", sb1.ram);

		if (sb1.ram < 2 * _need_ram) {
			_fail("simple does NOT have ample budget — the cross-class "
			      "pin would be vacuous");
			return;
		}

		Cfg_hash const h_simple_before_refusal = _cfg_hash(_cfg_simple);
		Cfg_hash const h_terminal_before_refusal = _cfg_hash(_cfg_terminal);

		if (!_expect("launch", T_FAT, "insufficient-resources")) return;

		Figures const f = _figures();
		if (!f.known) {
			_fail("the refusal carries no usable figure attributes");
			return;
		}
		Genode::log("grouped-runtime-probe: refusal need_ram=", f.need_ram,
		            " avail_ram=", f.avail_ram, " need_caps=", f.need_caps,
		            " avail_caps=", f.avail_caps);

		if (f.need_ram != _need_ram || f.need_caps != _need_caps) {
			_fail("the refusal does not cite the fixture's declared quota");
			return;
		}
		if (f.need_ram <= f.avail_ram || f.need_caps <= f.avail_caps) {
			_fail("the refusal is incoherent (need not above avail)");
			return;
		}
		if (f.avail_ram > tb1.ram) {
			_fail("the refusal cites more free RAM than terminal's own "
			      "report carried — a cross-class budget read?");
			return;
		}
		if (f.avail_caps > tb1.caps) {
			_fail("the refusal cites more free caps than terminal's own "
			      "report carried — a cross-class budget read?");
			return;
		}

		/* The refusal must be a clean no-op ... */
		if (_await_running(T_FAT)) {
			_fail("the refused t_fat is reported running");
			return;
		}
		/* ... and publish nothing (dirty scoping: a refusal is not a
		 * write at all). */
		if (!_same(_cfg_hash(_cfg_simple), h_simple_before_refusal) ||
		    !_same(_cfg_hash(_cfg_terminal), h_terminal_before_refusal)) {
			_fail("a class config changed across the refused launch");
			return;
		}
		Genode::log("grouped-runtime-probe: per-class refusal figures ok");

		/* Positive control: free terminal's budget, same launch works. */
		if (!_expect("remove", T_HOG, "ok")) return;
		Budget tb2;
		_settle(_st_terminal, tb1, tb2);
		if (!_expect("launch", T_FAT, "ok")) return;
		if (!_await_running(T_FAT)) {
			_fail("t_fat did not start once terminal's budget allowed it "
			      "— the gate latched");
			return;
		}
		if (!_await_start(_cfg_terminal, T_FAT)) {
			_fail("t_fat's <start> never appeared in the terminal config");
			return;
		}
		Genode::log("grouped-runtime-probe: per-class positive control ok");

		/*
		 * Phase 3: malformed quotas (app then driver) refused before
		 * mutation, then a valid request still succeeds (no emission
		 * wedge). Neither refusal may increment config_writes.
		 */
		Cfg_hash const h_simple_before_bad = _cfg_hash(_cfg_simple);
		if (!_expect("install", BADQUOTA, "insufficient-resources")) return;

		Figures const f0 = _figures();
		if (!f0.known ||
		    f0.need_ram != 0 || f0.avail_ram != 0 ||
		    f0.need_caps != 0 || f0.avail_caps != 0) {
			_fail("the malformed-quota refusal must carry the zero-figure "
			      "convention");
			return;
		}
		/* Before any mutation: the package must not be registered. */
		_timer.msleep(500);
		if (_broadcast_entry(BADQUOTA).found) {
			_fail("badquota was registered despite the malformed quota");
			return;
		}
		if (!_same(_cfg_hash(_cfg_simple), h_simple_before_bad)) {
			_fail("the simple config changed across the malformed-quota "
			      "refusal");
			return;
		}

		/*
		 * Phase 3b: the GROUPED DRIVER malformed quota — the Oracle
		 * blocker. No <driver_admission/> is armed here, so only the
		 * quota sanity (not any budget) is in play, and it must refuse
		 * BEFORE mutation. The offender is badriver, a dependency of
		 * the request root badriver_root, so the refusal must name the
		 * offender in its machine-readable pkg attribute.
		 */
		Cfg_hash const h_driver_before_bad = _cfg_hash(_cfg_driver);
		Cfg_hash const h_simple_before_drv = _cfg_hash(_cfg_simple);
		if (!_send_and_wait_pair("install", BADIVEROOT, BADRIVER)) {
			_fail("install badriver_root was not answered");
			return;
		}
		if (_status() != Genode::String<32>("insufficient-resources")) {
			_fail(Genode::String<160>("install badriver_root expected "
			                          "status=insufficient-resources but got ",
			                          _status()).string());
			return;
		}
		Figures const fd = _figures();
		if (!fd.known ||
		    fd.need_ram != 0 || fd.avail_ram != 0 ||
		    fd.need_caps != 0 || fd.avail_caps != 0) {
			_fail("the driver malformed-quota refusal must carry the "
			      "zero-figure convention");
			return;
		}
		if (_result_pkg() != Genode::String<128>(BADRIVER)) {
			_fail(Genode::String<192>("the malformed-quota refusal names '",
			                          _result_pkg(), "' instead of the "
			                          "offending package 'badriver'").string());
			return;
		}
		/* Before any mutation: neither package may be registered ... */
		_timer.msleep(500);
		if (_broadcast_entry(BADRIVER).found ||
		    _broadcast_entry(BADIVEROOT).found) {
			_fail("a malformed-quota package was registered (driver)");
			return;
		}
		/* ... and no class config may change (bad quota must not reach
		 * emission/abort). */
		if (!_same(_cfg_hash(_cfg_driver), h_driver_before_bad) ||
		    !_same(_cfg_hash(_cfg_simple), h_simple_before_drv)) {
			_fail("a class config changed across the driver "
			      "malformed-quota refusal");
			return;
		}
		Genode::log("grouped-runtime-probe: driver malformed-quota "
		            "refusal (offender-named) ok");

		if (!_expect("install", S_AFTER, "ok")) return;
		if (!_expect("launch", S_AFTER, "ok")) return;
		if (!_await_start(_cfg_simple, S_AFTER)) {
			_fail("s_after never reached the simple config — the malformed "
			      "quota wedged later writes");
			return;
		}
		Genode::log("grouped-runtime-probe: malformed-quota refusal + "
		            "recovery ok");

		/*
		 * Phase 4: mixed-class emission all-or-nothing (the knob's
		 * pinned write). The drift guard proves the next write really
		 * is the knob's one, so the negative cannot pass vacuously.
		 */
		unsigned writes { 0 };
		for (unsigned i = 0; i < POLL_ITERS; ++i) {
			writes = _config_writes();
			if (writes + 1 >= _malform_from) break;
			_timer.msleep(100);
		}
		if (writes + 1 != _malform_from) {
			_fail(Genode::String<192>(
				"write-count drift: repro_malformed_from=", _malform_from,
				" but the next write would be ", writes + 1,
				" — re-tune the knob").string());
			return;
		}
		Genode::log("grouped-runtime-probe: firing the mixed write (write ",
		            writes + 1, ")");

		Cfg_hash const h_s = _cfg_hash(_cfg_simple);
		Cfg_hash const h_t = _cfg_hash(_cfg_terminal);
		unsigned const bs1 = _beat_n(_b_simple);
		unsigned const bt1 = _beat_n(_b_terminal);
		unsigned const bb3 = _beat_n(_b_browser);
		unsigned const bd1 = _beat_n(_b_driver);

		/* Plan [mixdep(terminal), mixroot(simple)]: ONE write dirtying
		 * both class slots, with the simple render corrupted. */
		if (!_expect("install", MIXROOT, "error")) return;

		if (!_same(_cfg_hash(_cfg_simple), h_s)) {
			_fail("the simple config was published despite the malformed "
			      "injection");
			return;
		}
		if (!_same(_cfg_hash(_cfg_terminal), h_t)) {
			_fail("the terminal config was published although its render "
			      "was clean — the dirty set is not all-or-nothing");
			return;
		}
		unsigned const bs2 = _beat_n(_b_simple);
		unsigned const bt2 = _beat_n(_b_terminal);
		unsigned const bb4 = _beat_n(_b_browser);
		unsigned const bd2 = _beat_n(_b_driver);
		if (bs2 <= bs1 || bt2 <= bt1 || bb4 <= bb3 || bd2 <= bd1) {
			_fail("a running child stopped counting across the rejected "
			      "mixed write");
			return;
		}
		Genode::log("grouped-runtime-probe: mixed-class all-or-nothing ok");

		Genode::log("grouped-runtime-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Grouped_probe probe { env };
	probe._run();
}
