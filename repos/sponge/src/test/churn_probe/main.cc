/*
 * \brief  Containment-churn measurement probe for a Genode sandbox
 * \author Sponge OS contributors
 *
 * Answers the field question of docs/17-runtime-component-topology.md
 * §5 row "Respawn budget and clamp leak": when init abandons and
 * respawns a wedged child over and over, WHAT exhausts, and does it
 * stop?
 *
 * It is a Tier-0 observer. It lives OUTSIDE the sandbox under test and
 * reaches nothing but the sandbox's own periodic `<state>` report, so
 * "the outside is still alive and answering while the inside churns" is
 * measured, not assumed.
 *
 * WIRE CONTRACT
 * -------------
 *   ROM "state"   the sandbox's report, armed by the scenario with
 *                 `+ report | init_ram: yes | child_ram: yes |
 *                 delay_ms: <poll>`. `init_ram` makes the sandbox's
 *                 OWN remaining budget appear as a top-level `<ram/>`
 *                 node (genode/repos/os/src/lib/sandbox/library.cc:
 *                 134-137) — that figure is exactly the one every
 *                 `ram: 512G` child is clamped against, so a per-cycle
 *                 leak in the containment path shows up here as a
 *                 falling `avail` even when no child's own usage moves.
 *   ROM "config"  the probe's inline config node.
 *   Timer         the sampling clock.
 *
 * Config attributes (all optional, defaults in brackets]:
 *   poll_ms      [500] sampling interval; must be >= the report's own
 *                delay_ms or samples are duplicates
 *   ticks        [60]  number of samples before the verdict
 *   label        [sandbox] tag on every line, so two probes can share
 *                one log
 *   leak_bytes   [8192] per-tick drop in sandbox avail RAM that counts
 *                as one leak cycle. Chosen above a single slab block
 *                so allocator noise cannot be counted as a leak
 *   tail_ticks   [10] the final N samples whose avail spread decides
 *                "the leak stopped"
 *
 * OUTPUT
 *   churn-probe: label=<l> tick=<n> t=<ms> init_avail=<B> init_used=<B>
 *       children=<n> <name>=<used> ...
 *   churn-probe: SLOPE tick=<n> avail_delta=<±B> total=<±B> cycles=<n>
 *   churn-probe: VERDICT <PASS|FAIL> ...   (short — the run gate)
 *   churn-probe: DETAIL ...                 (long — read after the gate)
 *
 * The verdict is split in two on purpose: the run tool stops consuming
 * as soon as the gate pattern matches, so a single long line would lose
 * exactly the numbers the run exists to produce.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/node.h>
#include <timer_session/connection.h>
#include <util/string.h>

namespace {

unsigned const MAX_CHILDREN = 32;

/* Suffix-aware byte-count parser: the sandbox Generator prints "64M"
 * for every round value (genode/repos/os/src/lib/sandbox/types.h:47-49),
 * so strtoul alone would read "64M" as 64 BYTES. Same parser as
 * test/repostorm_probe/main.cc; deliberately duplicated rather than
 * shared, because probes are standalone scenario components. */
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


struct Churn_probe
{
	Genode::Env &_env;

	Timer::Connection            _timer  { _env };
	Genode::Attached_rom_dataspace _state { _env, "state" };
	Genode::Attached_rom_dataspace _config { _env, "config" };

	Genode::String<24> _label { "sandbox" };
	unsigned long    _poll_ms   { 500 };
	unsigned         _ticks     { 60 };
	unsigned         _tail      { 10 };
	Genode::size_t   _leak      { 8192 };

	Genode::size_t   _init_avail { 0 };
	Genode::size_t   _init_used  { 0 };
	bool             _have_init  { false };
	Genode::size_t   _prev_avail { 0 };
	long             _total      { 0 };
	unsigned         _cycles     { 0 };
	unsigned         _observed   { 0 };

	Genode::size_t   _child_used  [MAX_CHILDREN] { };
	unsigned         _num_children { 0 };

	Churn_probe(Genode::Env &env) : _env(env) { }

	void _read_config()
	{
		_config.update();
		if (!_config.valid() || _config.size() == 0) return;

		Genode::Node const cfg(Genode::Const_byte_range_ptr(
			_config.local_addr<char const>(), _config.size()));

		_poll_ms = cfg.attribute_value("poll_ms", _poll_ms);
		_ticks   = cfg.attribute_value("ticks",   _ticks);
		_tail    = cfg.attribute_value("tail_ticks", _tail);
		_leak    = cfg.attribute_value("leak_bytes", _leak);

		_label = cfg.attribute_value("label", _label);

		Genode::log("churn-probe: config poll_ms=", _poll_ms, " ticks=", _ticks,
		            " tail_ticks=", _tail, " leak_bytes=", _leak,
		            " label=", Genode::Cstring(_label.string()));
	}

	void _read_ram_node(Genode::Node const &r)
	{
		Genode::size_t v { 0 };
		Genode::String<32> const a =
			r.attribute_value("avail", Genode::String<32>());
		if (_parse_count(a, v)) _init_avail = v;
		Genode::String<32> const u =
			r.attribute_value("used", Genode::String<32>());
		if (_parse_count(u, v)) _init_used = v;
	}

	void _read_child(Genode::Node const &c)
	{
		if (_num_children >= MAX_CHILDREN) return;

		Genode::size_t used { 0 };
		c.with_sub_node("ram", [&] (Genode::Node const &r) {
			Genode::String<32> const u =
				r.attribute_value("used", Genode::String<32>());
			if (_parse_count(u, used)) { }
		}, [] () {});

		_child_used[_num_children++] = used;
	}

	void _sample()
	{
		_state.update();
		if (!_state.valid() || _state.size() == 0) return;

/*
		 * The sandbox's own <ram/> is a top-level sibling of the
		 * <child> nodes only when init_ram is armed; absent that it
		 * would be mistaken for a child called "ram".
		 */
		Genode::Node const root(Genode::Const_byte_range_ptr(
			_state.local_addr<char const>(), _state.size()));

		_init_avail = 0; _init_used = 0; _have_init = false;
		_num_children = 0;

		/* init writes the container's own <ram/> and <caps/> as plain
		 * sub-nodes of the report root (library.cc:133-140), siblings of
		 * the <child> nodes - not as attributes of a wrapper. */
		root.for_each_sub_node("ram", [&] (Genode::Node const &r) {
			_read_ram_node(r);
			_have_init = true; });

		root.for_each_sub_node("child", [&] (Genode::Node const &c) {
			_read_child(c); });
	}

	void _print(unsigned tick)
	{
		Genode::log("churn-probe: label=", Genode::Cstring(_label.string()),
		            " tick=", tick, " t=", tick * _poll_ms,
		            " init_avail=", _have_init ? _init_avail : Genode::size_t(0),
		            " init_used=", _init_used,
		            " children=", _num_children);
	}

	void _tick(unsigned tick)
	{
		_sample();
		_observed++;
		_print(tick);

		if (!_have_init || !_prev_avail) { _prev_avail = _init_avail; return; }

		long const d = long(_init_avail) - long(_prev_avail);
		_total += d;
		_prev_avail = _init_avail;

		if (d < -long(_leak)) _cycles++;

		Genode::log("churn-probe: SLOPE tick=", tick, " avail_delta=", d,
		            " total=", _total, " cycles=", _cycles);
	}

	void run()
	{
		Genode::log("churn-probe: starting");
		_read_config();

		_timer.msleep(_poll_ms);
		_tick(0);

		for (unsigned i = 1; i < _ticks; i++) {
			_timer.msleep(_poll_ms);
			_tick(i);
		}

/*
		 * "The leak stopped" is a claim about the TAIL, not about the
		 * whole window: a churn that is bounded but slow legitimately
		 * moves avail early on. Comparing the last tail_ticks samples
		 * against each other is what separates "settled" from "still
		 * falling at the same rate".
		 */
		Genode::size_t tail_lo { 0 }, tail_hi { 0 };
		for (unsigned i = 0; i < _tail; i++) {
			_timer.msleep(_poll_ms);
			_sample();
			if (i == 0 || _init_avail < tail_lo) tail_lo = _init_avail;
			if (i == 0 || _init_avail > tail_hi) tail_hi = _init_avail;
		}

		Genode::size_t const spread = tail_hi - tail_lo;

		Genode::log("churn-probe: DETAIL label=", Genode::Cstring(_label.string()),
		            " samples=", _observed, " cycles=", _cycles,
		            " total_avail_delta=", _total,
		            " tail_spread=", spread, " tail_ticks=", _tail,
		            " init_avail=", _init_avail, " init_used=", _init_used);

		bool const ok = (spread <= _leak);
		Genode::log("churn-probe: VERDICT ", ok ? "PASS" : "FAIL");

		_env.parent().exit(ok ? 0 : 1);
		Genode::sleep_forever();
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Churn_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }