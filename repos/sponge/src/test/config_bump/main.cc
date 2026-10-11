/* SPDX-License-Identifier: Apache-2.0
 *
 * config_bump — the only thing that ever un-parks a deferred child
 * resource request: a NEW configuration applied to the sandbox that
 * hosts the waiting child.
 *
 * Its role in run/sponge-resource-request.run is causal, not incidental.
 * Once resq_probe has logged DEFER, a reader could still wonder whether
 * the grant was merely slow. This component publishes a replacement
 * config (raising one child's RAM/caps quota, leaving the topology
 * otherwise intact) through report_rom. If the parked request then
 * resolves, the resolution is attributable to the config application —
 * and to nothing above the sandbox, because this component is a sibling
 * of the sandbox, not its parent.
 *
 * It publishes TWICE: the initial topology at t=0 (sandbox_c's config
 * comes from this writer, so nothing would exist without it) and the
 * bumped one after <delay_ms>.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

static char const *const PROVIDED[] = { "LOG", "PD", "CPU", "ROM",
	                                    "IRQ", "IO_MEM", "IO_PORT", "Timer" };

struct Config_bump
{
	struct Child
	{
		Genode::String<16> name;
		Genode::String<16> caps  { "200" };
		Genode::String<16> ram   { "12M" };
		Genode::String<16> probe { "child" };
		Genode::String<16> expect { "grant" };
		Genode::String<16> req    { };
	};

	static constexpr unsigned MAX_CHILDREN = 8;

	Genode::Env &_env;

	Timer::Connection _timer { _env };

	Genode::Expanding_reporter _reporter { _env, "bump", "bump" };

	unsigned _delay_ms { 12000 };

	Child    _initial[MAX_CHILDREN] { };
	unsigned _num_initial           { 0 };
	Child    _bumped[MAX_CHILDREN]  { };
	unsigned _num_bumped            { 0 };

	Config_bump(Genode::Env &env) : _env(env) { }

	template <typename NODE>
	void _collect(NODE const &n)
	{
		_delay_ms = n.attribute_value("delay_ms", 12000u);
	}

	template <typename NODE>
	void _collect_child(NODE const &n, Child *dst, unsigned &num)
	{
		if (num >= MAX_CHILDREN) return;
		Child &c = dst[num++];
		c.name = n.attribute_value("name", Genode::String<16>());
		c.caps   = n.attribute_value("caps",   Genode::String<16>("200"));
		c.ram    = n.attribute_value("ram",    Genode::String<16>("12M"));
		c.probe  = n.attribute_value("probe",  Genode::String<16>("child"));
		c.expect = n.attribute_value("expect", Genode::String<16>("grant"));
		c.req    = n.attribute_value("req",    Genode::String<16>());
	}

	template <typename NODE>
	void _collect_section(NODE const &n, Child *dst, unsigned &num)
	{
		/* Xml_node filters by type in for_each_sub_node; Hid_node takes
		 * only the callback and filters with has_type. The single-argument
		 * form plus has_type works for both. */
		n.for_each_sub_node([&] (NODE const &c) {
			if (c.has_type("child")) _collect_child(c, dst, num); });
	}

	void _parse_config()
	{
		Genode::Attached_rom_dataspace cfg { _env, "config" };
		cfg.update();
		if (!cfg.valid() || cfg.size() == 0) return;

		char const *const base = cfg.local_addr<char>();
		Genode::size_t  const sz  = cfg.size();
		if (base[0] == '<') {
			Genode::Xml_node const root(base, sz);
			if (!root.has_type("config")) return;
			_collect(root);
			root.for_each_sub_node("initial",
				[&] (Genode::Xml_node const &n) {
					_collect_section(n, _initial, _num_initial); });
			root.for_each_sub_node("bumped",
				[&] (Genode::Xml_node const &n) {
					_collect_section(n, _bumped, _num_bumped); });
		} else {
			Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
			if (!root.has_type("config")) return;
			_collect(root);
			root.for_each_sub_node([&] (Genode::Hid_node const &n) {
				if (n.has_type("initial"))
					_collect_section(n, _initial, _num_initial);
				if (n.has_type("bumped"))
					_collect_section(n, _bumped, _num_bumped);
			});
		}
	}

	void _publish(Child const *children, unsigned num, char const *what)
	{
		/*
		 * The <report> node is carried in both publications on purpose:
		 * dropping it would make init destroy its state reporter
		 * mid-experiment, and that report is the evidence this scenario
		 * exists to produce.
		 */
		_reporter.generate_xml([&](Genode::Xml_generator &g) {
			/*
			 * parent-provides and default-route are part of the published
			 * config, not decoration: init builds a nested sandbox from
			 * exactly what this writer emits, and without them every child
			 * is stopped with "PD/LOG/ROM environment session denied" the
			 * moment the config lands.
			 */
			g.node("parent-provides", [&] {
				for (char const *n : PROVIDED)
					g.node("service", [&] { g.attribute("name", n); });
			});
			g.node("default-route", [&] {
				g.node("any-service", [&] {
					g.node("parent");
					g.node("any-child");
				});
			});
			g.node("report", [&] {
				g.attribute("delay_ms", "1000");
				g.attribute("init_ram", "yes");
			});
			for (unsigned i = 0; i < num; ++i) {
				Child const &c = children[i];
				g.node("start", [&] {
					g.attribute("name", c.name);
					g.attribute("caps", c.caps);
					g.node("binary", [&] {
						g.attribute("name", "resq_probe"); });
					g.node("resource", [&] {
						g.attribute("name",    "RAM");
						g.attribute("quantum", c.ram); });
					/*
					 * Each child needs its own probe config: init builds a
					 * child from exactly this <config>, and resq_probe with
					 * none falls into ballast mode.
					 */
					g.node("config", [&] {
						g.node("probe", [&] {
							/* "id", not "name": Genode's HID generator folds
							 * a lone `name` attribute into the node CONTENT,
							 * leaving nothing for the probe to read back. */
							g.attribute("id",      c.probe);
							g.attribute("expect",  c.expect);
							g.attribute("req_ram", c.req); });
					});
					g.node("route", [&] {
						g.node("any-service", [&] {
							g.node("parent");
							g.node("any-child"); });
					});
				});
			}
		});
		Genode::log("config-bump: published ", what, " (", num,
		            " child/children)");
	}

void _run()
	{
		_parse_config();
		if (_num_initial == 0 || _num_bumped == 0) {
			Genode::error("config-bump: <initial> and <bumped> must both "
			              "list at least one <child>");
			_env.parent().exit(1);
			Genode::sleep_forever();
		}

		_publish(_initial, _num_initial, "the initial topology");
		_timer.msleep(_delay_ms);
		_publish(_bumped, _num_bumped, "the raised quota");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}

void Component::construct(Genode::Env &env)
{
	static Config_bump bump { env };
	bump._run();
}

Genode::size_t Component::stack_size() { return 32 * 1024; }