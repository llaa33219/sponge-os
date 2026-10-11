/* SPDX-License-Identifier: Apache-2.0
 *
 * admission_probe — sponge_pkgd resource admission-control verifier.
 *
 * Without admission control a launch that does not fit into
 * pkg_runtime's remaining budget is answered "ok" and then silently
 * evaporates: Genode's sandbox clamps the child's RAM quota to whatever
 * the parent has left (sandbox/child.cc, `ram_quota = avail_ram`) and
 * the child dies before its first instruction, leaving only a kernel
 * warning. run/sponge-pkg-lifecycle.run covers the sibling defect
 * (a staged-but-absent BINARY, answered "binary-missing"); this probe
 * covers the resource half.
 *
 * The sequence (driven entirely by the scenario's pkg list):
 *
 *   1. wait for pkg_runtime's first state report and record its budget;
 *   2. record the witness's monotonic beat counter (a running child that
 *      survives);
 *   3. install + launch the HOG package — it eats most of the budget;
 *   4. launch the OVER-BUDGET package — it must be refused with
 *      "insufficient-resources", carrying need_ram / avail_ram /
 *      need_caps / avail_caps;
 *   5. assert the refusal is a clean no-op:
 *        - the refused package is still running="no" in pkgd's
 *          installed broadcast (it never entered the running set, so
 *          the config was never regenerated for it),
 *        - the witness beat counter kept RISING (the refusal cost no
 *          collateral restart),
 *        - the numbers cite a budget that is coherent (need > avail)
 *          and no larger than the budget the probe itself observed;
 *   6. remove the hog, then launch the SAME package again — it must
 *      now succeed. That is the positive control: the gate is a live
 *      budget check, not a latch and not a permanent refusal.
 *
 * Two state sources are read, never written:
 *   - "runtime_state" — pkg_runtime's own sandbox state report, the
 *     very ROM sponge_pkgd subscribes to. Parsed with Hid_node because
 *     the 26.08 sandbox Generator serializes reports in HID, not XML
 *     (Xml_node would throw and every read would silently see zero).
 *   - "beat"          — the witness's monotonic counter (relayed from
 *     pkg_runtime's child, same convention as run/
 *     sponge-config-regen-survival.run).
 *
 * Logs "admission-probe: PASS" only if every assertion held; otherwise
 * "admission-probe: FAIL <reason>" and exit non-zero — the scenario then
 * fails loud on its bounded run_genode_until timeout
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

struct Admission_probe
{
	static constexpr unsigned OP_ITERS   = 120;  /* x 100ms = 12 s ceiling per request  */
	static constexpr unsigned POLL_ITERS = 80;   /* x 100ms =  8 s ceiling per assertion */
	static constexpr unsigned STATE_WAIT = 60;   /* x 500ms = 30 s ceiling for the first
	                                               * state report (it is periodic with
	                                               * delay_ms=2000 and only exists once
	                                               * pkg_runtime has applied the config) */
	static constexpr unsigned SETTLE_MAX = 60;   /* x 500ms = 30 s ceiling for a budget
	                                               * change to appear AND stabilise in
	                                               * pkg_runtime's state report */

	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Genode::Expanding_reporter     _request   { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result    { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };
	Genode::Attached_rom_dataspace _state     { _env, "runtime_state" };
	Genode::Attached_rom_dataspace _beat      { _env, "beat" };

	Genode::String<32>  _witness { };
	Genode::String<32>  _hog     { };
	Genode::String<32>  _fat     { };

	unsigned _seq { 0 };
	bool     _ok  { true };

	Admission_probe(Genode::Env &env) : _env(env) { }

	[[noreturn]]
	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("admission-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/* ---- config parse (HID or XML, same dual path as pkg_lifecycle_probe) ---- */

	template <typename NODE>
	void _collect(NODE const &n)
	{
		_witness = n.attribute_value("witness", Genode::String<32>("witness"));
		_hog     = n.attribute_value("hog",     Genode::String<32>("fat_a"));
		_fat     = n.attribute_value("fat",     Genode::String<32>("fat_b"));
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
	}

	/*
	 * Suffix-aware byte-count parser. The sandbox Generator prints a
	 * resource figure with Number_of_bytes::print(), which emits "64M"
	 * for every round value — so a plain strtoul would read "64M" as 64
	 * BYTES and silently admit an out-of-budget package. Every number
	 * that comes out of the state report therefore goes through here.
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

	/* ---- pkg_runtime state report (HID) ---- */

	struct Budget { bool ram_known { false }; bool caps_known { false };
	                Genode::size_t ram { 0 }; unsigned caps { 0 }; };

	/*
	 * Read pkg_runtime's own top-level <ram>/<caps> nodes. These are the
	 * numbers init's PD session reports for pkg_runtime itself, i.e. the
	 * quota it has NOT yet donated to a child — exactly the budget a new
	 * <start> node would have to come out of. Per-child <ram>/<caps>
	 * nodes live one level deeper (inside <child>) and are ignored here.
	 */
	Budget _budget()
	{
		Budget b;
		_state.update();
		if (!_state.valid() || _state.size() == 0)
			return b;

		Genode::Hid_node const root(Genode::Const_byte_range_ptr(
			_state.local_addr<char const>(), _state.size()));

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

	/* Wait until pkg_runtime has produced at least one state report. */
	bool _await_first_budget(Budget &out)
	{
		for (unsigned i = 0; i < STATE_WAIT; ++i) {
			out = _budget();
			if (out.ram_known && out.caps_known) return true;
			_timer.msleep(500);
		}
		return false;
	}

	/*
	 * Wait until pkg_runtime's reported budget differs from `before` and
	 * then holds still for 1.5 s (three consecutive equal samples), so
	 * the next request is judged against a settled number rather than a
	 * report that is still catching up with the previous transition.
	 *
	 * The "held still" requirement is what makes the scenario immune to
	 * the state report's 2 s period: a stale report would otherwise be
	 * sampled and the admission decision taken against it.
	 */
	bool _settle(Genode::size_t before, Genode::size_t &out)
	{
		Genode::size_t last { 0 };
		unsigned      stable { 0 };
		bool          moved { false };

		for (unsigned i = 0; i < SETTLE_MAX; ++i) {
			Budget const b = _budget();
			if (b.ram_known) {
				if (b.ram != last) { last = b.ram; stable = 0; }
				else if (++stable >= 3 && (b.ram != before || moved)) {
					out = b.ram;
					return true;
				}
			}
			_timer.msleep(500);
		}
		Genode::warning("admission-probe: budget never settled (still ", last,
		                ") — continuing with the last observed value");
		out = last;
		return false;
	}

	/* ---- witness beat ---- */

	unsigned _beat_n()
	{
		_beat.update();
		if (!_beat.valid()) return 0;
		try {
			Genode::Xml_node const b = _beat.xml();
			if (!b.has_type("beat")) return 0;
			return b.attribute_value("n", 0u);
		} catch (Genode::Xml_node::Invalid_syntax) { return 0; }
	}

	/* ---- request / result / broadcast primitives ---- */

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

	bool _broadcast_running(char const *name)
	{
		for (unsigned i = 0; i < POLL_ITERS; ++i) {
			_installed.update();
			if (_installed.valid()) {
				try {
					Genode::Xml_node const root = _installed.xml();
					bool found { false }, running { false };
					root.with_optional_sub_node("packages",
						[&](Genode::Xml_node const &pkgs) {
							pkgs.for_each_sub_node("package",
								[&](Genode::Xml_node const &p) {
									if (!found &&
									    p.attribute_value("name", Genode::String<64>())
									    == Genode::String<64>(name)) {
										found   = true;
										running = p.attribute_value("running",
										                           Genode::String<8>("no"))
										        == Genode::String<8>("yes");
									}
								});
						});
					if (found && running) return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
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
			_fail(Genode::String<160>(op, " ", pkg, " expected status=", want,
			                          " but got ", got).string());
			return false;
		}
		Genode::log("admission-probe: ", op, " ", pkg, " -> ", got);
		return true;
	}

	void _run()
	{
		_parse_config();
		Genode::log("admission-probe: witness='", _witness, "' hog='", _hog,
		            "' fat='", _fat, "'");

		Budget b0;
		if (!_await_first_budget(b0)) {
			_fail("pkg_runtime never published a usable state report "
			      "(runtime_state relay not armed?)");
			return;
		}
		Genode::log("admission-probe: pkg_runtime budget avail ram=", b0.ram,
		            " caps=", b0.caps);

		unsigned const beat0 = _beat_n();
		if (beat0 == 0) {
			_fail("the witness never published a beat — the survival "
			      "assertion would be blind");
			return;
		}
		if (!_broadcast_running(_witness.string())) {
			_fail(Genode::String<96>("the witness package '", _witness,
			                         "' is not running at boot").string());
			return;
		}

		/* Consume most of the budget. */
		if (!_expect("install", _hog.string(), "ok")) return;
		if (!_expect("launch", _hog.string(), "ok"))  return;

		Genode::size_t avail_after_hog { 0 };
		_settle(b0.ram, avail_after_hog);
		Genode::log("admission-probe: after the hog, pkg_runtime has ",
		            avail_after_hog, " bytes free");

		/* The launch that must be refused. */
		if (!_expect("launch", _fat.string(), "insufficient-resources"))
			return;

		Genode::String<32> need_ram_s { }, avail_ram_s { };
		Genode::String<32> need_caps_s { }, avail_caps_s { };
		try {
			Genode::Xml_node const r = _result.xml();
			need_ram_s   = r.attribute_value("need_ram",   Genode::String<32>());
			avail_ram_s  = r.attribute_value("avail_ram",  Genode::String<32>());
			need_caps_s  = r.attribute_value("need_caps",  Genode::String<32>());
			avail_caps_s = r.attribute_value("avail_caps", Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) { }

		Genode::log("admission-probe: refusal numbers need_ram=", need_ram_s,
		            " avail_ram=", avail_ram_s, " need_caps=", need_caps_s,
		            " avail_caps=", avail_caps_s);

		Genode::size_t need_ram { 0 }, avail_ram { 0 }, tmp { 0 };
		unsigned need_caps { 0 }, avail_caps { 0 };

		if (!_parse_bytes(need_ram_s, need_ram)) {
			_fail("the refusal carries no usable need_ram attribute");
			return;
		}
		if (!_parse_bytes(avail_ram_s, avail_ram)) {
			_fail("the refusal carries no usable avail_ram attribute");
			return;
		}
		if (!_parse_bytes(need_caps_s, tmp) || tmp > 0xffffffu) {
			_fail("the refusal carries no usable need_caps attribute");
			return;
		}
		need_caps = (unsigned)tmp;
		if (!_parse_bytes(avail_caps_s, tmp) || tmp > 0xffffffu) {
			_fail("the refusal carries no usable avail_caps attribute");
			return;
		}
		avail_caps = (unsigned)tmp;

		if (need_ram <= avail_ram) {
			_fail("the refusal is incoherent: need_ram is not above avail_ram");
			return;
		}
		if (avail_ram > avail_after_hog) {
			_fail("the refusal cites more free RAM than the probe observed "
			      "— the budget pkgd reads is not the real one");
			return;
		}
		if (need_caps <= avail_caps) {
			_fail("the refusal is incoherent: need_caps is not above avail_caps");
			return;
		}
		if (avail_caps > b0.caps) {
			_fail("the refusal cites more free caps than the probe observed");
			return;
		}

		/* The refusal must be a no-op: never in the running set ... */
		if (_broadcast_running(_fat.string())) {
			_fail(Genode::String<96>("the refused package '", _fat,
			                         "' is reported running").string());
			return;
		}
		/* ... and it must cost no collateral restart. */
		unsigned const beat1 = _beat_n();
		if (beat1 <= beat0) {
			_fail("the witness stopped counting across the refusal "
			      "(the running children were disturbed)");
			return;
		}
		Genode::log("admission-probe: witness beat ", beat0, " -> ", beat1,
		            " (unaffected)");

		/* Positive control: free the budget, the same launch must work. */
		if (!_expect("remove", _hog.string(), "ok")) return;

		Genode::size_t avail_after_free { 0 };
		_settle(avail_after_hog, avail_after_free);
		Genode::log("admission-probe: after the release, pkg_runtime has ",
		            avail_after_free, " bytes free");

		if (!_expect("launch", _fat.string(), "ok")) return;
		if (!_broadcast_running(_fat.string())) {
			_fail(Genode::String<96>("the package '", _fat,
			                         "' did not start once the budget "
			                         "allowed it — the gate latched").string());
			return;
		}

		Genode::log("admission-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}

void Component::construct(Genode::Env &env)
{
	static Admission_probe probe { env };
	probe._run();
}

Genode::size_t Component::stack_size() { return 32 * 1024; }