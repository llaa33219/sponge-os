/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <sponge/backend_client.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

struct Probe
{
	Genode::Env &_env;
	Timer::Connection _timer { _env };
	Genode::Attached_rom_dataspace _config { _env, "config" };
	Genode::Attached_rom_dataspace _broadcast { _env, "broadcast" };
	Sponge::Backend::ReportRomClient _client {
		_env, "config_request", "config_result" };

	explicit Probe(Genode::Env &env) : _env(env) { }

	void fail(char const *reason)
	{
		Genode::error("bake-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	bool has_value(char const *name, char const *value)
	{
		for (unsigned attempt = 0; attempt < 80; ++attempt) {
			_broadcast.update();
			if (_broadcast.valid()) {
				bool found { false };
				bool parsed { true };
				try {
					_broadcast.xml().for_each_sub_node("key",
						[&] (Genode::Xml_node const &key) {
							if (!found &&
							    key.attribute_value("name", Genode::String<64>()) ==
							        Genode::String<64>(name) &&
							    key.attribute_value("value", Genode::String<128>()) ==
							        Genode::String<128>(value))
								found = true;
						});
				}
				catch (Genode::Xml_node::Invalid_syntax) { parsed = false; }
				if (parsed && found) return true;
			}
			_timer.msleep(100);
		}
		return false;
	}

	void require_value(char const *name, char const *value, char const *reason)
	{
		if (!has_value(name, value)) fail(reason);
	}

	void set(char const *name, char const *value, char const *reason)
	{
		if (!_client.config_set(name, value)) fail(reason);
		Genode::Xml_node const result = _client.result_xml();
		if (result.attribute_value("status", Genode::String<32>()) !=
		    Genode::String<32>("ok"))
			fail(reason);
	}

	void firstboot_write()
	{
		require_value("bake.profile", "minimal", "first boot profile missing");
		require_value("bake.version", "1", "first boot version missing");
		require_value("bake.applied", "yes", "first boot sentinel missing");
		require_value("theme.active", "default", "baked theme was not seeded");
		set("panel.height", "64", "user override was rejected");
		require_value("panel.height", "64", "user override not broadcast");
		Genode::log("bake-firstboot-probe: boot1 seeded profile=minimal theme=default; override panel.height=64");
		Genode::log("bake-firstboot-probe: PASS boot1");
	}

	void firstboot_read()
	{
		require_value("bake.profile", "minimal", "restored profile missing");
		require_value("bake.applied", "yes", "restored sentinel missing");
		require_value("panel.height", "64", "user override was overwritten by re-seed");
		Genode::log("bake-firstboot-probe: boot2 preserved panel.height=64 bake.applied=yes");
		Genode::log("bake-firstboot-probe: PASS boot2");
	}

	void reset()
	{
		require_value("bake.applied", "yes", "initial seed did not complete");
		set("panel.height", "64", "baked-key override was rejected");
		set("panel.position", "top", "non-baked override was rejected");
		require_value("panel.height", "64", "baked-key override missing");
		require_value("panel.position", "top", "non-baked override missing");

		set("bake.applied", "no", "reset request was rejected");
		require_value("panel.height", "28", "reset did not restore baked key");
		require_value("panel.position", "top", "reset changed a non-baked user key");
		require_value("bake.applied", "yes", "reset did not restore sentinel");
		Genode::log("bake-reset-probe: restored panel.height=28; preserved panel.position=top");
		Genode::log("bake-reset-probe: PASS");
	}

	void run()
	{
		_config.update();
		if (!_config.valid()) fail("config ROM unavailable");
		Genode::String<32> mode { };
		try {
			mode = _config.xml().attribute_value("mode", Genode::String<32>());
		}
		catch (Genode::Xml_node::Invalid_syntax) { fail("malformed config ROM"); }

		Genode::log("bake-probe: mode=", mode);
		if (mode == Genode::String<32>() || mode == Genode::String<32>("reset")) reset();
		else if (mode == Genode::String<32>("firstboot-write")) firstboot_write();
		else if (mode == Genode::String<32>("firstboot-read")) firstboot_read();
		else fail("unknown mode");

		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size()
{
	return 32 * 1024 * sizeof(Genode::addr_t);
}
