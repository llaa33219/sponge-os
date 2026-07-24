/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_seq_probe — config-driven sponge_pkgd sequence verifier.
 *
 * vct is short-lived (one command per boot), so multi-step flows like
 * install-then-remove or install-then-list cannot be one vct run. This
 * headless probe drives the Report/ROM channel directly, executing a
 * sequence of operations declared in its config ROM and asserting each
 * result. It is the package-backend analogue of sponge_de_probe.
 *
 * Config (supplied by the run scenario):
 *   <config>
 *     <step op="install" pkg="hello"/>
 *     <step op="remove"  pkg="hello"/>
 *     <step op="list"    expect="hello"/>   <!-- list asserts presence -->
 *   </config>
 *
 * For install/remove: assert result status="ok".
 * For list: assert result status="ok" AND a <package name="expect"/>
 *           appears in <packages>.
 *
 * Logs "pkg-seq-probe: PASS" only if every step passes; otherwise
 * "pkg-seq-probe: FAIL <reason>" and the run scenario times out.
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

struct Probe
{
	Genode::Env &_env;

	Timer::Connection              _timer    { _env };
	Genode::Expanding_reporter     _request  { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result   { _env, "result" };
	Genode::Attached_rom_dataspace _config   { _env, "config" };

	unsigned _seq { 0 };

	Probe(Genode::Env &env) : _env(env) { }

	bool _result_matches(char const *op, char const *pkg)
	{
		if (!_result.valid())
			return false;

		try {
			Genode::Xml_node const r = _result.xml();
			if (!r.has_type("result"))
				return false;
			if (r.attribute_value("op",  Genode::String<32>()) != Genode::String<32>(op))
				return false;
			/* list carries no pkg; only match pkg for ops that have one. */
			if (Genode::strcmp(op, "list") != 0 &&
			    r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg))
				return false;
			return r.has_attribute("status");
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _send_and_wait(char const *op, char const *pkg)
	{
		++_seq;
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("seq", _seq);
			if (Genode::strcmp(pkg, "") != 0)
				g.attribute("pkg", pkg);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (_result_matches(op, pkg))
				return true;
			_timer.msleep(100);
		}
		return false;
	}

	bool _list_contains(char const *expect)
	{
		try {
			Genode::Xml_node const r = _result.xml();
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

	void run()
	{
		_config.update();
		if (!_config.valid()) {
			_fail("no config ROM");
			return;
		}

		unsigned step_no { 0 };
		_config.xml().for_each_sub_node("step", [&](Genode::Xml_node const &step) {
			if (!_ok) return;
			++step_no;

			Genode::String<32>  const op = step.attribute_value("op",
			                                          Genode::String<32>());
			Genode::String<128> const pkg = step.attribute_value("pkg",
			                                          Genode::String<128>());
			Genode::String<64>  const expect = step.attribute_value("expect",
			                                          Genode::String<64>());

			Genode::log("pkg-seq-probe: [", step_no, "] ", op,
			            Genode::strcmp(pkg.string(), "") == 0 ? "" : " ",
			            pkg);

			if (!_send_and_wait(op.string(), pkg.string())) {
				_fail(Genode::String<96>(op, " did not answer").string());
				return;
			}

			Genode::Xml_node const r = _result.xml();
			if (r.attribute_value("status", Genode::String<32>()) != Genode::String<32>("ok")) {
				_fail(Genode::String<128>(op, " returned error: ",
				    r.attribute_value("error", Genode::String<128>())).string());
				return;
			}

			if (Genode::strcmp(op.string(), "list") == 0 &&
			    Genode::strcmp(expect.string(), "") != 0) {
				if (!_list_contains(expect.string())) {
					_fail(Genode::String<128>("list did not contain ", expect).string());
					return;
				}
			}

			/* Let pkg_runtime act on a real install before the next step
			 * (e.g. start the child) so remove/list observe live state. */
			if (Genode::strcmp(op.string(), "install") == 0)
				_timer.msleep(500);

			Genode::log("pkg-seq-probe: [", step_no, "] ", op, " ok");
		});

		if (!_ok) return;

		Genode::log("pkg-seq-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}

	bool _ok { true };

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("pkg-seq-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }
