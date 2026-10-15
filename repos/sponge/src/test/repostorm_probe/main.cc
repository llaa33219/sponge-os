/* SPDX-License-Identifier: Apache-2.0
 *
 * repostorm_probe — per-child resource-growth measurement for the
 * SPONGE DEBUG scroll-storm reproduction.
 *
 * QUESTION UNDER TEST
 * -------------------
 * Field report (2026-10): in the SPONGE DEBUG window, rapidly dragging
 * the scrollbar until text "loads fast" freezes the whole system; the
 * freeze point moved when the package quota was raised. Two competing
 * hypotheses:
 *
 *   H-leak       one component's RAM/caps usage grows without bound
 *                during sustained repaints;
 *   H-saturation no component grows, but the shared render substrate
 *                (sponge_debug + wm + decorator + nitpicker + fb, all
 *                CPU-rasterized via Mesa softpipe) saturates the CPU
 *                (docs/17-runtime-component-topology.md section 4).
 *
 * This probe is the measuring instrument for H-leak. It does not drive
 * the storm; the storm is driven by the application under test (see
 * repos/sponge/src/sponge_debug/main.cc's config-gated storm knobs and
 * run/sponge-debug-scrollstorm.run). This probe only watches.
 *
 * WIRE CONTRACT
 * -------------
 *   ROM "state"   the sandbox's periodic state report. The scenario arms
 *                it with `+ report | child_ram: yes | child_caps: yes |
 *                delay_ms: 1000` (genode/repos/os/src/lib/sandbox/
 *                report.h:44-55) and routes the probe's "state" ROM to
 *                the sibling report_rom, whose policy serves it from the
 *                sandbox's OWN `state` reporter. Per-child schema,
 *                emitted by
 *                genode/repos/os/src/lib/sandbox/child.cc:368-426:
 *
 *                  child name=<name> binary=<bin> [state=incomplete]
 *                    ram  assigned=.. quota=.. used=.. avail=..
 *                    caps assigned=.. quota=.. used=.. avail=..
 *
 *   ROM "config" the probe's inline config node, read through
 *                Genode::Node so XML and HID deliveries both work
 *                (the sandbox serves child configs in HID by default
 *                since 26.05; the same reasoning as sponge-de's
 *                main.cc:186-214).
 *
 * Config attributes (all optional, defaults in brackets):
 *   settle_ms     [20000] quiet period after boot before the baseline
 *                 snapshot. Long enough for Qt6's first paint under
 *                 Mesa softpipe on seL4 to finish allocating.
 *   report_ms     [1000]   sampling interval (must be >= the state
 *                 report's own delay_ms or samples are duplicates).
 *   duration_ms   [60000]  measurement window after the baseline.
 *   threshold_kb  [2048]   per-child growth budget over the window.
 *                 Above it the probe reports FAIL and names the child.
 *
 * OUTPUT
 *   repostorm: t=<ms> name=<child> ram_used=<B> ram_avail=<B> caps_used=<n>
 *       (only for children whose figures changed since the last tick)
 *   repostorm-probe: TABLE <name> baseline=<B> last=<B> delta=<B> ...
 *   repostorm-probe: VERDICT <PASS|FAIL> delta_max=<B> worst=<name>
 *       threshold=<B> samples=<n>
 *
 * The single VERDICT line is the run script's gate; the per-tick lines
 * are the evidence.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/node.h>
#include <timer_session/connection.h>
#include <util/string.h>

namespace {

unsigned const MAX_CHILDREN = 48;

/*
 * Suffix-aware byte-count parser. The sandbox Generator prints a
 * resource figure through Number_of_bytes::print(), which emits "64M"
 * for every round value (genode/repos/os/src/lib/sandbox/types.h:47-49)
 * — a plain strtoul would read "64M" as 64 BYTES and silently report
 * a leak that is not there (or hide one that is). Same parser as
 * test/admission_probe/main.cc:130-157; deliberately duplicated rather
 * than shared because probes are standalone scenario components.
 */
bool _parse_count(Genode::String<32> const &s, Genode::size_t &out)
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


struct Child_sample
{
	/* `seen` distinguishes "not observed yet" from "observed as zero". */
	bool           seen          { false };
	char           name[32]      { };
	Genode::size_t ram_used      { 0 };
	Genode::size_t ram_avail     { 0 };
	unsigned long  caps_used     { 0 };
	bool           baselined     { false };
	Genode::size_t ram_baseline  { 0 };
	Genode::size_t ram_last      { 0 };
	unsigned long  caps_last     { 0 };
};


struct Repstorm_probe
{
	Genode::Env &_env;

	Timer::Connection            _timer  { _env };
	Genode::Attached_rom_dataspace _state { _env, "state" };
	Genode::Attached_rom_dataspace _config { _env, "config" };

	Child_sample   _children[MAX_CHILDREN];
	unsigned       _num_children { 0 };
	unsigned       _ticks        { 0 };
	unsigned long  _settle_ms    { 20000 };
	unsigned long  _report_ms    { 1000 };
	unsigned long  _duration_ms  { 60000 };
	Genode::size_t _threshold    { 2 * 1024 * 1024 };


	Repstorm_probe(Genode::Env &env) : _env(env) { }


	/*
	 * Config through Genode::Node — transparently accepts the XML and
	 * the HID serialization of the same document, so this probe works
	 * on base-linux (core embeds init, Generator emits XML) and on
	 * base-sel4 (dynamically linked init, Generator emits HID) without
	 * a kernel-conditional branch.
	 */
	void _read_config()
	{
		_config.update();
		if (!_config.valid() || _config.size() == 0) return;

		Genode::Node const cfg(Genode::Const_byte_range_ptr(
			_config.local_addr<char const>(), _config.size()));

		_settle_ms   = cfg.attribute_value("settle_ms",   _settle_ms);
		_report_ms   = cfg.attribute_value("report_ms",   _report_ms);
		_duration_ms = cfg.attribute_value("duration_ms", _duration_ms);

		Genode::size_t threshold_kb { 2048 };
		threshold_kb = cfg.attribute_value("threshold_kb", threshold_kb);
		_threshold = threshold_kb * 1024;

		Genode::log("repostorm-probe: config settle_ms=",
		            _settle_ms, " report_ms=", _report_ms,
		            " duration_ms=", _duration_ms,
		            " threshold_kb=", threshold_kb);
	}


	/*
	 * Snapshot every child's ram/caps figures from the state report.
	 * Returns the number of children seen in this report.
	 */
	unsigned _sample()
	{
		_state.update();
		if (!_state.valid() || _state.size() == 0) return 0;

		Genode::Node const root(Genode::Const_byte_range_ptr(
			_state.local_addr<char const>(), _state.size()));

		unsigned n { 0 };
		_num_children = 0;

		root.for_each_sub_node("child", [&] (Genode::Node const &c) {

			Genode::String<32> const name =
				c.attribute_value("name", Genode::String<32>());

			/* A child that exited is reported with `exited=`; keep
			 * its last row (so a disappearing child is visible in
			 * the table) but do not count it as live. */
			bool const alive = !c.has_attribute("exited");

			Child_sample *s { nullptr };
			for (unsigned i = 0; i < _num_children; i++) {
				if (Genode::strcmp(_children[i].name, name.string()) == 0) {
					s = &_children[i];
					break;
				}
			}
			if (!s) {
				if (_num_children >= MAX_CHILDREN) return;
				s = &_children[_num_children++];
				Genode::size_t n { 0 };
				for (char const *c = name.string(); *c && n + 1 < sizeof(s->name); c++, n++)
					s->name[n] = *c;
				s->name[n] = '\0';
			}

			s->ram_used  = s->ram_avail;
			s->caps_used = s->caps_last;

			c.with_sub_node("ram",
				[&] (Genode::Node const &r) {
					Genode::size_t v { 0 };
					Genode::String<32> const u =
						r.attribute_value("used", Genode::String<32>());
					if (_parse_count(u, v)) s->ram_used = v;
					Genode::String<32> const a =
						r.attribute_value("avail", Genode::String<32>());
					if (_parse_count(a, v)) s->ram_avail = v;
				},
				[] () {});

			c.with_sub_node("caps",
				[&] (Genode::Node const &k) {
					Genode::size_t v { 0 };
					Genode::String<32> const u =
						k.attribute_value("used", Genode::String<32>());
					if (_parse_count(u, v)) s->caps_used = (unsigned long)v;
				},
				[] () {});

			s->seen = alive;
			n++;
		});

		return n;
	}


	void _print_tick()
	{
		for (unsigned i = 0; i < _num_children; i++) {
			Child_sample &s = _children[i];
			if (!s.seen) continue;
			if (s.ram_used == s.ram_last && s.caps_used == s.caps_last)
				continue;
			Genode::log("repostorm: t=", _ticks * _report_ms,
			            "ms name=", Genode::Cstring(s.name),
			            " ram_used=", s.ram_used,
			            " ram_avail=", s.ram_avail,
			            " caps_used=", s.caps_used);
			s.ram_last  = s.ram_used;
			s.caps_last = s.caps_used;
		}
	}


	[[noreturn]] void _verdict()
	{
		Genode::size_t delta_max { 0 };
		Genode::String<32> worst { "none" };

		for (unsigned i = 0; i < _num_children; i++) {
			Child_sample &s = _children[i];

			if (!s.baselined) {
				/*
				 * A child that only appeared mid-window (a late
				 * starter) has no baseline; its first observation
				 * becomes the baseline so a late start is not
				 * mistaken for growth.
				 */
				if (!s.seen) continue;
				s.ram_baseline = s.ram_used;
				s.baselined    = true;
			}

			Genode::size_t const delta = (s.ram_used > s.ram_baseline)
				? s.ram_used - s.ram_baseline : 0;

			Genode::log("repostorm-probe: TABLE ", Genode::Cstring(s.name),
			            " baseline=", s.ram_baseline,
			            " last=", s.ram_used,
			            " avail=", s.ram_avail,
			            " delta=", delta,
			            " caps_used=", s.caps_used,
			            s.seen ? "" : " (exited)");

			if (delta > delta_max) {
				delta_max = delta;
				worst = Genode::String<32>(s.name);
			}
		}

		bool const ok = delta_max <= _threshold;

		/*
		 * The verdict line is kept SHORT and the numbers go to a
		 * separate DETAIL line: run_genode_until terminates QEMU as
		 * soon as the gate pattern matches, which truncates whatever
		 * the serial capture has not flushed yet. A one-line verdict
		 * whose tail is the measured delta would lose exactly the
		 * number the run exists to produce.
		 */
		Genode::log("repostorm-probe: DETAIL delta_max=", delta_max,
		            " worst=", worst,
		            " threshold=", _threshold,
		            " samples=", _ticks,
		            " children=", _num_children);
		Genode::log("repostorm-probe: VERDICT ", ok ? "PASS" : "FAIL");

		_env.parent().exit(ok ? 0 : 1);
		Genode::sleep_forever();
	}


	void run()
	{
		Genode::log("repostorm-probe: starting");
		_read_config();

		/*
		 * Phase 1 — settle. The application's own first paint, Mesa's
		 * softpipe screen allocation and the Qt font cache all land in
		 * this window; measuring across them would report a startup
		 * transient as a leak.
		 */
		_timer.msleep(_settle_ms);

		unsigned const n = _sample();
		if (n == 0) {
			_state.update();
			Genode::error("repostorm-probe: FAIL state report carried no <child> node",
			              " valid=", _state.valid(), " bytes=",
			              _state.valid() ? _state.size() : 0);
			_env.parent().exit(1);
			Genode::sleep_forever();
			return;
		}
		Genode::log("repostorm-probe: baseline ", n, " children");

		for (unsigned i = 0; i < _num_children; i++) {
			Child_sample &s = _children[i];
			if (!s.seen) continue;
			s.ram_baseline = s.ram_used;
			s.baselined    = true;
			s.ram_last     = s.ram_used;
			s.caps_last    = s.caps_used;
			Genode::log("repostorm-probe: BASE ", Genode::Cstring(s.name),
			            " ram_used=", s.ram_used);
		}

		/*
		 * Phase 2 — measurement window. Every tick samples; the
		 * application under test runs its repaint storm in parallel.
		 */
		unsigned long elapsed { 0 };
		while (elapsed < _duration_ms) {
			_timer.msleep(_report_ms);
			elapsed += _report_ms;
			_ticks++;
			_sample();
			_print_tick();
		}

		_verdict();
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Repstorm_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }