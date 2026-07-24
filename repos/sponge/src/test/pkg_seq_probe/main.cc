/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_seq_probe — config-driven backend sequence verifier.
 *
 * vct is short-lived (one command per boot), so multi-step flows like
 * install-then-remove or config-set-then-get cannot be one vct run. This
 * headless probe drives a backend's Report/ROM channel directly,
 * executing a sequence of operations declared in its config ROM and
 * asserting each result. It is the package/config-backend analogue of
 * sponge_de_probe.
 *
 * init delivers the child config ROM in HID (human-intelligible data)
 * format, so the step list is parsed with Genode::Hid_node (the same
 * dual HID/XML handling vct's args.cc uses).
 *
 * The probe speaks one of two channels, selected by the `channel`
 * attribute on its config root (default "pkg"):
 *
 *   channel="pkg"    (sponge_pkgd): labels request/result, request
 *                    shape <request op pkg seq/>, result matched on
 *                    op+pkg. Steps carry `pkg` and optional `expect`.
 *
 *   channel="config" (sponge_configd): labels config_request/config_result,
 *                    request shape <request op key value/>, result matched
 *                    on op+key (+value for set). Steps carry `key`,
 *                    optional `value`, optional `expect`. A step may assert
 *                    an error outcome with `expect_status="error"`.
 *
 * Config (pkg):
 *   <config>
 *     <step op="install" pkg="hello"/>
 *     <step op="remove"  pkg="hello"/>
 *     <step op="list"    expect="hello"/>
 *   </config>
 *
 * Config (config):
 *   <config channel="config">
 *     <step op="set"  key="theme.active"   value="dark"/>
 *     <step op="get"  key="theme.active"   expect="dark"/>
 *     <step op="list" expect="theme.active"/>
 *     <step op="get"  key="no.such.key"    expect_status="error"/>
 *   </config>
 *
 * Logs "config-seq-probe: PASS" (config) or "pkg-seq-probe: PASS" (pkg)
 * only if every step passes; otherwise "...: FAIL <reason>" and the run
 * scenario times out.
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
#include <util/xml_node.h>

namespace {

struct Probe
{
	static constexpr unsigned MAX_STEPS = 32;

	struct Step
	{
		Genode::String<32>  op;
		Genode::String<128> id;            /* pkg (pkg channel) or key (config) */
		Genode::String<128> value;
		Genode::String<64>  expect;
		Genode::String<16>  expect_status; /* "" or "error" */
	};

	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };

	/*
	 * Constructed in run() once the channel is known, so the labels can
	 * be chosen per backend. pkgd: request/result; configd:
	 * config_request/config_result.
	 */
	Genode::Constructible<Genode::Expanding_reporter>     _request { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _result  { };

	bool     _is_config { false };
	unsigned _seq       { 0 };
	bool     _ok        { true };

	Step     _steps[MAX_STEPS] { };
	unsigned _num_steps        { 0 };

	Probe(Genode::Env &env) : _env(env) { }

	char const *_prefix() const { return _is_config ? "config-seq-probe" : "pkg-seq-probe"; }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error(_prefix(), ": FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/* Parse the child config (HID or XML) into _steps and set _is_config. */
	void _parse_config()
	{
		char const *const base = _config_rom.local_addr<char>();
		Genode::size_t  const sz  = _config_rom.size();

		bool const is_xml = (sz > 0 && base[0] == '<');

		if (is_xml) {
			Genode::Xml_node const root(base, sz);
			if (!root.has_type("config"))
				return;
			_is_config = root.attribute_value("channel", Genode::String<16>())
			             == Genode::String<16>("config");
			root.for_each_sub_node("step", [&](Genode::Xml_node const &n) {
				_collect(n);
			});
		} else {
			Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
			if (!root.has_type("config"))
				return;
			_is_config = root.attribute_value("channel", Genode::String<16>())
			             == Genode::String<16>("config");
			root.for_each_sub_node([&](Genode::Hid_node const &n) {
				if (n.has_type("step"))
					_collect(n);
			});
		}
	}

	/* `node` is either an Xml_node or a Hid_node step; both expose
	 * attribute_value(name, default). */
	template <typename NODE>
	void _collect(NODE const &n)
	{
		if (_num_steps >= MAX_STEPS)
			return;
		Step &s = _steps[_num_steps++];
		s.op            = n.attribute_value("op",            Genode::String<32>());
		s.value         = n.attribute_value("value",         Genode::String<128>());
		s.expect        = n.attribute_value("expect",        Genode::String<64>());
		s.expect_status = n.attribute_value("expect_status", Genode::String<16>());

		/* id holds pkg (pkg channel) or key (config channel). */
		Genode::String<128> const key = n.attribute_value("key", Genode::String<128>());
		s.id = (Genode::strcmp(key.string(), "") != 0)
		     ? key
		     : n.attribute_value("pkg", Genode::String<128>());
	}

	/* ===================== result matching ===================== */

	bool _result_valid_and_result()
	{
		if (!_result->valid())
			return false;
		try {
			return _result->xml().has_type("result");
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	/* ===================== pkg channel (sponge_pkgd) ===================== */

	bool _pkg_result_matches(char const *op, char const *pkg)
	{
		if (!_result_valid_and_result())
			return false;

		try {
			Genode::Xml_node const r = _result->xml();
			if (r.attribute_value("op", Genode::String<32>()) != Genode::String<32>(op))
				return false;
			if (Genode::strcmp(op, "list") != 0 &&
			    r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg))
				return false;
			return r.has_attribute("status");
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _pkg_send_and_wait(char const *op, char const *pkg)
	{
		++_seq;
		_request->generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("seq", _seq);
			if (Genode::strcmp(pkg, "") != 0)
				g.attribute("pkg", pkg);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result->update();
			if (_pkg_result_matches(op, pkg))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	bool _pkg_list_contains(char const *expect)
	{
		try {
			Genode::Xml_node const r = _result->xml();
			bool found { false };
			r.with_optional_sub_node("packages",
				[&](Genode::Xml_node const &pkgs) {
					pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
						if (p.attribute_value("name", Genode::String<64>())
						    == Genode::String<64>(expect))
							found = true;
					});
				});
			return found;
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	/* ===================== config channel (sponge_configd) ===================== */

	bool _cfg_result_matches(char const *op, char const *key, char const *value)
	{
		if (!_result_valid_and_result())
			return false;

		try {
			Genode::Xml_node const r = _result->xml();
			if (r.attribute_value("op", Genode::String<32>()) != Genode::String<32>(op))
				return false;
			if (Genode::strcmp(op, "list") != 0 &&
			    r.attribute_value("key", Genode::String<128>()) != Genode::String<128>(key))
				return false;
			if (value != nullptr && r.has_attribute("value") &&
			    r.attribute_value("value", Genode::String<128>()) != Genode::String<128>(value))
				return false;
			return r.has_attribute("status");
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _cfg_send_and_wait(char const *op, char const *key, char const *value)
	{
		_request->generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			if (Genode::strcmp(key, "") != 0)
				g.attribute("key", key);
			if (value != nullptr && Genode::strcmp(value, "") != 0)
				g.attribute("value", value);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result->update();
			if (_cfg_result_matches(op, key, value))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	/* Read the value of `key` from the last result (get: attribute, or
	 * list: a <key> child under <keys>). */
	bool _cfg_value_of(char const *key, Genode::String<128> &out)
	{
		try {
			Genode::Xml_node const r = _result->xml();
			if (r.has_attribute("value")) {
				out = r.attribute_value("value", Genode::String<128>());
				return true;
			}
			bool found { false };
			r.with_optional_sub_node("keys",
				[&](Genode::Xml_node const &keys) {
					keys.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
						if (!found &&
						    k.attribute_value("name", Genode::String<64>())
						    == Genode::String<64>(key)) {
							out = k.attribute_value("value", Genode::String<128>());
							found = true;
						}
					});
				});
			return found;
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	/* ===================== driver ===================== */

	void run()
	{
		_config_rom.update();
		if (!_config_rom.valid()) {
			_fail("no config ROM");
			return;
		}

		_parse_config();

		if (_is_config) {
			_request.construct(_env, "request", "config_request");
			_result.construct(_env, "config_result");
		} else {
			_request.construct(_env, "request", "request");
			_result.construct(_env, "result");
		}

		for (unsigned i = 0; i < _num_steps && _ok; ++i) {
			Step const &s = _steps[i];
			if (_is_config)
				_run_config_step(i + 1, s);
			else
				_run_pkg_step(i + 1, s);
		}

		if (!_ok) return;

		Genode::log(_prefix(), ": PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}

	/* ---- one pkg step ---- */
	void _run_pkg_step(unsigned step_no, Step const &s)
	{
		char const *const op  = s.op.string();
		char const *const pkg = s.id.string();

		Genode::log(_prefix(), ": [", step_no, "] ", op,
		            Genode::strcmp(pkg, "") == 0 ? "" : " ", pkg);

		if (!_pkg_send_and_wait(op, pkg)) {
			_fail(Genode::String<96>(op, " did not answer").string());
			return;
		}

		Genode::Xml_node const r = _result->xml();
		if (r.attribute_value("status", Genode::String<32>()) != Genode::String<32>("ok")) {
			_fail(Genode::String<128>(op, " returned error: ",
			    r.attribute_value("error", Genode::String<128>())).string());
			return;
		}

		if (Genode::strcmp(op, "list") == 0 &&
		    Genode::strcmp(s.expect.string(), "") != 0) {
			if (!_pkg_list_contains(s.expect.string())) {
				_fail(Genode::String<128>("list did not contain ", s.expect).string());
				return;
			}
		}

		if (Genode::strcmp(op, "install") == 0)
			_timer.msleep(500);

		Genode::log(_prefix(), ": [", step_no, "] ", op, " ok");
	}

	/* ---- one config step ---- */
	void _run_config_step(unsigned step_no, Step const &s)
	{
		char const *const op  = s.op.string();
		char const *const key = s.id.string();

		Genode::log(_prefix(), ": [", step_no, "] ", op,
		            Genode::strcmp(key, "") == 0 ? "" : " ", key,
		            Genode::strcmp(s.value.string(), "") == 0 ? "" : " = ",
		            s.value);

		char const *const val_arg = (Genode::strcmp(op, "set") == 0)
		                          ? s.value.string() : nullptr;

		if (!_cfg_send_and_wait(op, key, val_arg)) {
			_fail(Genode::String<96>(op, " did not answer").string());
			return;
		}

		Genode::Xml_node const r = _result->xml();
		Genode::String<32> const status =
			r.attribute_value("status", Genode::String<32>());

		/* An error is a pass iff the step expected one. */
		if (status != Genode::String<32>("ok")) {
			if (s.expect_status == Genode::String<16>("error")) {
				Genode::log(_prefix(), ": [", step_no, "] ", op,
				            " ok (error as expected)");
				return;
			}
			_fail(Genode::String<128>(op, " returned error: ",
			    r.attribute_value("error", Genode::String<128>())).string());
			return;
		}

		/* A non-error result that was expected to error is a failure. */
		if (s.expect_status == Genode::String<16>("error")) {
			_fail(Genode::String<128>(op, " expected error but got ok").string());
			return;
		}

		/* get: assert the returned value matches `expect`. */
		if (Genode::strcmp(op, "get") == 0 &&
		    Genode::strcmp(s.expect.string(), "") != 0) {
			Genode::String<128> got { };
			if (!_cfg_value_of(key, got) ||
			    got != Genode::String<128>(s.expect.string())) {
				_fail(Genode::String<128>("get ", key, " expected ", s.expect,
				    " got ", got).string());
				return;
			}
		}

		/* list: assert the expected key appears in the enumeration. */
		if (Genode::strcmp(op, "list") == 0 &&
		    Genode::strcmp(s.expect.string(), "") != 0) {
			Genode::String<128> dummy { };
			if (!_cfg_value_of(s.expect.string(), dummy)) {
				_fail(Genode::String<128>("list did not contain ", s.expect).string());
				return;
			}
		}

		Genode::log(_prefix(), ": [", step_no, "] ", op, " ok");
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }
