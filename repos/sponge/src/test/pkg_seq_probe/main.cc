/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_seq_probe — install/remove lifecycle verification (Phase 4b).
 *
 * Drives the sponge_pkgd Report/ROM channel directly (vct is short-lived
 * and takes one command, so it cannot do install-then-remove in one boot).
 *
 * Sequence:
 *   1. install hello  -> expect <result status="ok" op="install" ...>
 *   2. remove  hello  -> expect <result status="ok" op="remove"  ...>
 *
 * Logs "pkg-seq-probe: PASS" only if both results are ok; otherwise
 * "pkg-seq-probe: FAIL <reason>" and the run scenario times out.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/xml_node.h>

namespace {

struct Probe
{
	Genode::Env &_env;

	Timer::Connection              _timer    { _env };
	Genode::Expanding_reporter     _request  { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result   { _env, "result" };

	unsigned _seq { 0 };

	Probe(Genode::Env &env) : _env(env) { }

	bool _request_matches(char const *op, char const *pkg)
	{
		if (!_result.valid())
			return false;

		try {
			Genode::Xml_node const r = _result.xml();
			if (!r.has_type("result"))
				return false;
			if (r.attribute_value("op",  Genode::String<32>()) != Genode::String<32>(op))
				return false;
			if (r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg))
				return false;
			return r.has_attribute("status");
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _do(char const *op, char const *pkg)
	{
		/* Bump a per-call sequence attribute so report_rom always sees a
		 * fresh report (it relays on content change). */
		++_seq;
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
			g.attribute("seq", _seq);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (_request_matches(op, pkg)) {
				Genode::Xml_node const r = _result.xml();
				Genode::String<32> const status =
					r.attribute_value("status", Genode::String<32>());
				return status == Genode::String<32>("ok");
			}
			_timer.msleep(100);
		}
		return false;
	}

	void run()
	{
		Genode::log("pkg-seq-probe: install hello");
		if (!_do("install", "hello")) {
			_fail("install did not succeed");
			return;
		}
		Genode::log("pkg-seq-probe: install ok");

		/* Give pkg_runtime a moment to actually start hello before
		 * tearing it down, so the remove exercises a live child. */
		_timer.msleep(500);

		Genode::log("pkg-seq-probe: remove hello");
		if (!_do("remove", "hello")) {
			_fail("remove did not succeed");
			return;
		}
		Genode::log("pkg-seq-probe: remove ok");

		Genode::log("pkg-seq-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}

	void _fail(char const *reason)
	{
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
