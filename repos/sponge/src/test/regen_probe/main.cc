/* SPDX-License-Identifier: Apache-2.0
 *
 * regen_probe — configuration-regeneration survival verifier.
 *
 * Answers one question empirically, on real hardware or in QEMU: does a
 * sponge_pkgd config regeneration that crosses the Expanding_reporter
 * report-buffer boundary kill already-running pkg_runtime children?
 *
 * Sequence:
 *
 *   1. install the witness package (autostart) and wait until its beat
 *      counter is live,
 *   2. record the counter,
 *   3. per cycle, issue one operation on a second, never-autostarted
 *      package (cycle 0 installs it, later cycles remove it). Every
 *      operation makes sponge_pkgd regenerate the whole pkg_runtime
 *      config; with the test-only <repro_pad_grow> knob every such write
 *      crosses the Expanding_reporter buffer boundary,
 *   4. sample the witness beat counter for a bounded window after each
 *      operation and require it to stay non-decreasing and to keep
 *      advancing. A counter that returns to 1 means the witness was
 *      abandoned by pkg_runtime and recreated — the regeneration killed
 *      a running child.
 *
 * Config:
 *   <config witness="witness" second="second" cycles="2"/>
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/xml_node.h>

namespace {

struct Probe
{
	Genode::Env                  &_env;
	Timer::Connection             _timer     { _env };
	Genode::Attached_rom_dataspace _config_rom { _env, "config" };
	Genode::Attached_rom_dataspace _result_rom { _env, "result" };
	Genode::Attached_rom_dataspace _beat_rom   { _env, "beat" };
	Genode::Expanding_reporter    _request    { _env, "request", "request" };

	Genode::String<64> _witness { "witness" };
	Genode::String<64> _second  { "second" };
	unsigned           _cycles  { 2 };

	unsigned _seq { 0 };

	Probe(Genode::Env &env) : _env(env) { }

	void _finish(bool ok, char const *reason)
	{
		if (!ok)
			Genode::error("regen-probe: FAIL ", reason);
		else
			Genode::log("regen-probe: PASS");
		_env.parent().exit(ok ? 0 : 1);
		Genode::sleep_forever();
	}

	/* 'node' is either an Xml_node or a Hid_node config root. */
	template <typename NODE> void _parse(NODE const &root)
	{
		if (!root.has_type("config"))
			return;
		_witness = root.attribute_value("witness", _witness);
		_second  = root.attribute_value("second",  _second);
		_cycles  = root.attribute_value("cycles",  _cycles);
	}

	void _parse_config()
	{
		char const *const base = _config_rom.local_addr<char>();
		Genode::size_t  const sz  = _config_rom.size();
		if (sz == 0) return;

		if (base[0] == '<')
			_parse(Genode::Xml_node(base, sz));
		else
			_parse(Genode::Hid_node(Genode::Const_byte_range_ptr(base, sz)));
	}

	/*
	 * Issue one package operation and wait for pkgd's answer on the
	 * result channel. Returns true iff the answer carried the expected
	 * status ('expected' is copied into 'status' for the failure
	 * message).
	 */
	bool _request_op(char const *op, char const *pkg,
	                 Genode::String<32> const &expected,
	                 Genode::String<128> &status)
	{
		++_seq;
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("seq", _seq);
			g.attribute("pkg", pkg); });

		status = "timeout";

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result_rom.update();
			if (_result_rom.valid()) {
				try {
					Genode::Xml_node const r = _result_rom.xml();
					if (r.has_type("result") &&
					    r.attribute_value("op", Genode::String<32>())
					       == Genode::String<32>(op) &&
					    r.attribute_value("pkg", Genode::String<64>())
					       == Genode::String<64>(pkg)) {
						Genode::String<32> const got =
							r.attribute_value("status", Genode::String<32>(""));
						status = got;
						return got == expected;
					}
				}
				catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/* Last beat counter published by the witness, 0 when none is live. */
	unsigned _beat_n()
	{
		_beat_rom.update();
		if (!_beat_rom.valid()) return 0;
		try {
			Genode::Xml_node const b = _beat_rom.xml();
			if (!b.has_type("beat")) return 0;
			return b.attribute_value("n", 0u);
		}
		catch (Genode::Xml_node::Invalid_syntax) { return 0; }
	}

	/*
	 * Wait until the witness counter reaches 'target'. Returns the
	 * counter actually reached.
	 */
	unsigned _wait_for_beat(unsigned target)
	{
		unsigned n = 0;
		for (unsigned i = 0; i < 200; ++i) {
			n = _beat_n();
			if (n >= target) break;
			_timer.msleep(100);
		}
		return n;
	}

	void run()
	{
		_config_rom.update();
		if (!_config_rom.valid())
			{ _finish(false, "no config ROM"); return; }

		_parse_config();

		Genode::log("regen-probe: installing witness '", _witness, "'");
		{
			Genode::String<128> status { };
			if (!_request_op("install", _witness.string(),
			                 Genode::String<32>("ok"), status))
			{
				Genode::String<160> const reason(
					"install ", _witness, " answered status='", status, "'");
				_finish(false, reason.string());
				return;
			}
		}

		unsigned baseline = _wait_for_beat(3);
		if (baseline < 3)
			{ _finish(false, "witness never reported a live beat"); return; }

		Genode::log("regen-probe: witness live at n=", baseline);

		for (unsigned cycle = 0; cycle < _cycles; ++cycle) {

			/* Alternate install/remove so every cycle is a real
			 * state change on the trigger package (a repeated
			 * remove of an absent package is an error, not a
			 * regeneration). */
			char const *op = (cycle % 2 == 0) ? "install" : "remove";

			Genode::log("regen-probe: cycle ", cycle + 1, "/", _cycles,
			            " — ", op, " ", _second,
			            " (forces a pkg_runtime config regeneration)");

			{
				Genode::String<128> status { };
				if (!_request_op(op, _second.string(),
				                 Genode::String<32>("ok"), status))
				{
					Genode::String<160> const reason(
						op, " ", _second, " answered status='", status, "'");
					_finish(false, reason.string());
					return;
				}
			}

			/*
			 * Bounded observation window: sample the counter for 3 s.
			 * A restart shows up as a counter below the recorded
			 * baseline (the fresh process starts again at 1).
			 */
			_timer.msleep(1000);
			for (unsigned i = 0; i < 30; ++i) {
				unsigned const n = _beat_n();
				if (n < baseline) {
					Genode::String<96> const reason(
						"witness was killed by the config regeneration "
						"(counter reset to ", n, " from ", baseline, ")");
					_finish(false, reason.string());
					return;
				}
				_timer.msleep(100);
			}

			unsigned const after = _beat_n();
			if (after < baseline + 2)
				{ _finish(false, "witness stopped reporting across the "
				                  "config regeneration"); return; }

			Genode::log("regen-probe: cycle ", cycle + 1,
			            " survived (n=", after, ")");
			baseline = after;
		}

		_finish(true, "");
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }