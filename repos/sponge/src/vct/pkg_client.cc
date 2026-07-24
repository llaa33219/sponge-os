/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of PkgClient.
 */

#include "pkg_client.h"

#include <base/log.h>
#include <util/string.h>

using namespace Sponge;
using namespace Sponge::Vct;


PkgClient::PkgClient(Genode::Env &env,
                     char const *request_label,
                     char const *result_label)
:
	_env(env),
	_request_label(request_label),
	_result_label(result_label)
{ }


bool PkgClient::_result_matches(char const *op, char const *pkg) const
{
	if (!_result_rom.valid())
		return false;

	try {
		Genode::Xml_node const r = _result_rom.xml();

		if (!r.has_type("result"))
			return false;

		/* Freshness: the result must answer THIS request. report_rom is
		 * fresh each boot, so matching on pkg+op is sufficient to reject
		 * any stale answer for a different package or operation. */
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


bool PkgClient::_config_result_matches(char const *op, char const *key,
                                       char const *value) const
{
	if (!_result_rom.valid())
		return false;

	try {
		Genode::Xml_node const r = _result_rom.xml();

		if (!r.has_type("result"))
			return false;

		if (r.attribute_value("op",  Genode::String<32>()) != Genode::String<32>(op))
			return false;

		/* list carries no key; match on op alone. */
		if (Genode::strcmp(op, "list") != 0 &&
		    r.attribute_value("key", Genode::String<128>()) != Genode::String<128>(key))
			return false;

		/* For set, also require the echoed value to match so a stale
		 * result for the same key but an old value cannot satisfy a
		 * set to a new value. get/list do not know the value up front.
		 * Error results omit `value`, so the check applies only when
		 * the result actually carries it. */
		if (value != nullptr && r.has_attribute("value") &&
		    r.attribute_value("value", Genode::String<128>()) != Genode::String<128>(value))
			return false;

		return r.has_attribute("status");
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}
}


/* ---- sponge_pkgd requests ---- */

bool PkgClient::request(char const *op, char const *pkg)
{
	_request_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op",  op);
		g.attribute("pkg", pkg);
	});

	_timer.msleep(200);
	for (unsigned i = 0; i < 60; ++i) {
		_result_rom.update();
		if (_result_matches(op, pkg))
			return true;
		_timer.msleep(100);
	}

	return false;
}


bool PkgClient::request(char const *op)
{
	_request_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op", op);
	});

	_timer.msleep(200);
	for (unsigned i = 0; i < 60; ++i) {
		_result_rom.update();
		if (_result_matches(op, ""))
			return true;
		_timer.msleep(100);
	}

	return false;
}


/* ---- sponge_configd requests ---- */

bool PkgClient::config_get(char const *key)
{
	_request_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op",  "get");
		g.attribute("key", key);
	});

	_timer.msleep(200);
	for (unsigned i = 0; i < 60; ++i) {
		_result_rom.update();
		if (_config_result_matches("get", key, nullptr))
			return true;
		_timer.msleep(100);
	}

	return false;
}


bool PkgClient::config_set(char const *key, char const *value)
{
	_request_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op",    "set");
		g.attribute("key",   key);
		g.attribute("value", value);
	});

	_timer.msleep(200);
	for (unsigned i = 0; i < 60; ++i) {
		_result_rom.update();
		if (_config_result_matches("set", key, value))
			return true;
		_timer.msleep(100);
	}

	return false;
}


bool PkgClient::config_list()
{
	_request_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op", "list");
	});

	_timer.msleep(200);
	for (unsigned i = 0; i < 60; ++i) {
		_result_rom.update();
		if (_config_result_matches("list", "", nullptr))
			return true;
		_timer.msleep(100);
	}

	return false;
}
