/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of PkgClient.
 */

#include "pkg_client.h"

#include <base/log.h>
#include <util/string.h>

using namespace Sponge;
using namespace Sponge::Vct;


PkgClient::PkgClient(Genode::Env &env) : _env(env) { }


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
