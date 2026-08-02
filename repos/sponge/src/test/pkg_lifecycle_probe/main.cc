/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_lifecycle_probe — Phase 7 todo 9 installed-vs-running verifier.
 *
 * Models on test/pkg_seq_probe (same HID/XML config pattern, same
 * request/result Report/ROM channel) and adds two lifecycle-specific
 * pieces:
 *
 *   (1) launch op handling: the result's `status` attribute is one of
 *       "ok" / "not-installed" / "already-running" (docs/12 §9.2.1), so
 *       a step may set `expect_status` to assert the exact outcome
 *       (default "ok" for install/remove, "ok" for launch unless the
 *       step says otherwise).
 *
 *   (2) installed-broadcast assertions: opens pkgd's "installed" ROM
 *       (relayed by report_rom) and provides two assertion step types:
 *         <step op="assert_installed" name="X" running="yes|no"/>
 *         <step op="assert_absent"    name="X"/>
 *       Both poll the broadcast for a bounded number of iterations
 *       (state changes are async via report_rom) and fail loud on
 *       mismatch.
 *
 * Step list is declared in the probe config ROM; each step may also
 * carry `pkg=` as an alias for `name=` (kept for parity with
 * pkg_seq_probe).
 *
 * Logs "lifecycle-probe: PASS" only if every step passed; otherwise
 * "lifecycle-probe: FAIL <reason>" and exit non-zero — the run
 * scenario then fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
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

struct Lifecycle_probe
{
	static constexpr unsigned MAX_STEPS = 32;
	static constexpr unsigned POLL_ITERS = 80;     /* x 100ms = 8s ceiling per assertion */
	static constexpr unsigned OP_ITERS   = 120;    /* x 100ms = 12s ceiling per request */

	struct Step
	{
		Genode::String<32>  op;             /* install|launch|remove|assert_installed|assert_absent */
		Genode::String<128> id;             /* pkg (op steps) or name (assert steps) */
		Genode::String<16>  expect_status;  /* default "ok" for op steps */
		Genode::String<8>   running;        /* "yes"|"no" for assert_installed */
	};

	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	Genode::Expanding_reporter     _request   { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result    { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	unsigned _seq { 0 };
	bool     _ok  { true };

	Step     _steps[MAX_STEPS] { };
	unsigned _num_steps        { 0 };

	Lifecycle_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("lifecycle-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/* ---- config parse (HID or XML, same dual path as pkg_seq_probe) ---- */

	void _parse_config()
	{
		char const *const base = _config_rom.local_addr<char>();
		Genode::size_t  const sz  = _config_rom.size();

		bool const is_xml = (sz > 0 && base[0] == '<');
		if (is_xml) {
			Genode::Xml_node const root(base, sz);
			if (!root.has_type("config")) return;
			root.for_each_sub_node("step", [&](Genode::Xml_node const &n) {
				_collect(n);
			});
		} else {
			Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
			if (!root.has_type("config")) return;
			root.for_each_sub_node([&](Genode::Hid_node const &n) {
				if (n.has_type("step"))
					_collect(n);
			});
		}
	}

	template <typename NODE>
	void _collect(NODE const &n)
	{
		if (_num_steps >= MAX_STEPS) return;
		Step &s = _steps[_num_steps++];
		s.op            = n.attribute_value("op",            Genode::String<32>());
		s.expect_status = n.attribute_value("expect_status", Genode::String<16>("ok"));
		s.running       = n.attribute_value("running",       Genode::String<8>());

		Genode::String<128> const name = n.attribute_value("name", Genode::String<128>());
		s.id = (Genode::strcmp(name.string(), "") != 0)
		     ? name
		     : n.attribute_value("pkg", Genode::String<128>());
	}

	/* ---- request/result primitives ---- */

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
		++_seq;
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
			g.attribute("seq", _seq);
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

	/* ---- broadcast (installed ROM) primitives ---- */

	/*
	 * Walk the installed broadcast looking for a <package name="X"/>.
	 * Returns true and fills `out_running` if found; false otherwise.
	 */
	bool _broadcast_find(char const *name, Genode::String<8> &out_running)
	{
		if (!_installed.valid()) return false;
		try {
			Genode::Xml_node const root = _installed.xml();
			bool found { false };
			root.with_optional_sub_node("packages",
				[&](Genode::Xml_node const &pkgs) {
					pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
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

	/*
	 * Poll the installed broadcast until either the package is present
	 * with the expected running= attribute, or POLL_ITERS is exhausted.
	 */
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

	/*
	 * Poll the installed broadcast until the package is ABSENT (used
	 * after remove). Returns true if absent within the budget.
	 */
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

	/* ---- per-step runners ---- */

	void _run_op_step(unsigned step_no, Step const &s)
	{
		char const *const op  = s.op.string();
		char const *const pkg = s.id.string();

		Genode::log("lifecycle-probe: [", step_no, "] ", op, " ", pkg);

		if (!_send_and_wait(op, pkg)) {
			_fail(Genode::String<96>(op, " did not answer").string());
			return;
		}

		Genode::String<32> status { };
		try {
			status = _result.xml().attribute_value("status", Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) { }

		if (status != Genode::String<32>(s.expect_status.string())) {
			_fail(Genode::String<160>(op, " ", pkg, " expected status=",
			    s.expect_status, " got status=", status).string());
			return;
		}

		/*
		 * The autostart install and the launch transition need a beat
		 * for pkgd to regenerate the config and init to start the new
		 * <start> node before subsequent assertions poll the broadcast.
		 * 200ms is the same ceiling pkg_seq_probe uses after install.
		 */
		_timer.msleep(300);

		Genode::log("lifecycle-probe: [", step_no, "] ", op, " ", pkg,
		            " -> ", status);
	}

	void _run_assert_installed(unsigned step_no, Step const &s)
	{
		char const *const name = s.id.string();
		char const *const want = s.running.string();

		Genode::log("lifecycle-probe: [", step_no, "] assert_installed ",
		            name, " running=", want);

		if (!_broadcast_sees(name, want)) {
			_fail(Genode::String<160>("broadcast did not show ", name,
			    " with running=", want).string());
			return;
		}
		Genode::log("lifecycle-probe: [", step_no, "] assert_installed ",
		            name, " running=", want, " ok");
	}

	void _run_assert_absent(unsigned step_no, Step const &s)
	{
		char const *const name = s.id.string();

		Genode::log("lifecycle-probe: [", step_no, "] assert_absent ", name);

		if (!_broadcast_absent(name)) {
			_fail(Genode::String<160>("broadcast still shows ", name,
			    " after remove").string());
			return;
		}
		Genode::log("lifecycle-probe: [", step_no, "] assert_absent ",
		            name, " ok");
	}

	void run()
	{
		_config_rom.update();
		if (!_config_rom.valid()) {
			_fail("no config ROM");
			return;
		}

		_parse_config();

		/* Prime both ROMs so the very first assertion can read them. */
		_result.update();
		_installed.update();

		for (unsigned i = 0; i < _num_steps && _ok; ++i) {
			Step const &s = _steps[i];
			unsigned const n = i + 1;

			if (s.op == Genode::String<32>("assert_installed"))
				_run_assert_installed(n, s);
			else if (s.op == Genode::String<32>("assert_absent"))
				_run_assert_absent(n, s);
			else
				_run_op_step(n, s);
		}

		if (!_ok) return;

		Genode::log("lifecycle-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Lifecycle_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }
