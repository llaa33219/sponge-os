/* SPDX-License-Identifier: Apache-2.0 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>


namespace {

bool contains(char const *haystack, char const *needle)
{
	Genode::size_t const n = Genode::strlen(needle);
	if (n == 0)
		return true;

	for (char const *p = haystack; *p; ++p) {
		Genode::size_t i = 0;
		while (i < n && p[i] == needle[i])
			++i;
		if (i == n)
			return true;
	}
	return false;
}


struct Focus_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	Genode::Expanding_reporter     _request        { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result         { _env, "result" };
	Genode::Attached_rom_dataspace _installed      { _env, "installed" };
	Genode::Attached_rom_dataspace _window_list    { _env, "window_list" };
	Genode::Attached_rom_dataspace _focus_request  { _env, "focus_request" };

	Genode::String<2048> _previous_result { };
	unsigned _focus_baseline_id { 0 };

	Focus_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		Genode::error("settings-focus-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	Genode::String<32> _result_status(char const *op, char const *pkg,
	                                  char const *id, bool settings)
	{
		_result.update();
		if (!_result.valid())
			return Genode::String<32>();

		if (_previous_result.length() != 0) {
			Genode::String<2048> const current(
				_result.local_addr<char const>(), _result.size());
			if (current == _previous_result)
				return Genode::String<32>();
		}

		try {
			Genode::Node const r = _result.node();
			if (!r.has_type("result"))
				return Genode::String<32>();
			if (r.attribute_value("op", Genode::String<32>()) != op)
				return Genode::String<32>();
			if (r.attribute_value("pkg", Genode::String<128>()) != pkg)
				return Genode::String<32>();
			if (settings &&
			    r.attribute_value("target", Genode::String<16>()) !=
			    Genode::String<16>("settings"))
				return Genode::String<32>();
			if (settings &&
			    r.attribute_value("id", Genode::String<33>()) != id)
				return Genode::String<32>();

			Genode::String<32> const status =
				r.attribute_value("status", Genode::String<32>());
			if (status == Genode::String<32>("ok") && settings)
				return r.attribute_value("action", Genode::String<32>());
			return status;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<32>();
		}
	}

	Genode::String<32> _request_result(char const *op, char const *pkg,
	                                   char const *id, bool settings)
	{
		_result.update();
		if (_result.valid())
			_previous_result = Genode::String<2048>(
				_result.local_addr<char const>(), _result.size());
		else
			_previous_result = Genode::String<2048>();

		_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
			if (settings) {
				g.attribute("target", "settings");
				g.attribute("id", id);
			}
		});

		for (unsigned i = 0; i < 200; ++i) {
			Genode::String<32> const result = _result_status(op, pkg, id, settings);
			if (result.length() != 0)
				return result;
			_timer.msleep(100);
		}
		return Genode::String<32>();
	}

	int _window_count()
	{
		_window_list.update();
		if (!_window_list.valid())
			return -1;

		int count = 0;
		try {
			_window_list.node().for_each_sub_node("window",
				[&] (Genode::Node const &w) {
					Genode::String<256> const label =
						w.attribute_value("label", Genode::String<256>());
					if (contains(label.string(), "settings_demo.greeting"))
						++count;
				});
		} catch (Genode::Xml_node::Invalid_syntax) {
			return -1;
		}
		return count;
	}

	void _wait_single_window()
	{
		for (unsigned i = 0; i < 200; ++i) {
			if (_window_count() == 1)
				return;
			_timer.msleep(100);
		}
		_fail("provider window did not appear exactly once");
	}

	void _capture_focus_baseline()
	{
		_focus_request.update();
		if (!_focus_request.valid())
			return;
		try {
			_focus_baseline_id =
				_focus_request.node().attribute_value("id", 0u);
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("settings-focus-probe: malformed focus_request baseline");
		}
	}

	void _wait_focus_request()
	{
		for (unsigned i = 0; i < 200; ++i) {
			_focus_request.update();
			if (_focus_request.valid()) {
				try {
					Genode::Node const r = _focus_request.node();
					Genode::String<256> const label =
						r.attribute_value("label", Genode::String<256>());
					unsigned const id = r.attribute_value("id", 0u);
					if (id > _focus_baseline_id &&
					    contains(label.string(), "settings_demo.greeting")) {
						Genode::log("settings-focus-probe: focus_request label='",
						            label, "'");
						return;
					}
				} catch (Genode::Xml_node::Invalid_syntax) {
					Genode::warning("settings-focus-probe: malformed focus_request report");
				}
			}
			_timer.msleep(100);
		}
		_fail("no focus_request for the existing provider");
	}

	void run()
	{
		if (_request_result("install", "settings_demo", "", false) !=
		    Genode::String<32>("ok"))
			_fail("settings_demo install failed");

		_installed.update();
		if (!_installed.valid())
			_fail("installed broadcast unavailable after install");

		if (_request_result("launch", "settings_demo", "greeting", true) !=
		    Genode::String<32>("started"))
			_fail("first open did not answer started");

		_wait_single_window();
		_capture_focus_baseline();

		if (_request_result("launch", "settings_demo", "greeting", true) !=
		    Genode::String<32>("focused"))
			_fail("second open did not answer focused");

		_wait_focus_request();

		if (_window_count() != 1)
			_fail("second open duplicated the provider window");

		Genode::log("settings-focus-probe: PASS");
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	Focus_probe(env).run();
	env.parent().exit(0);
}
