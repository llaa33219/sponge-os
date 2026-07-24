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


bool PkgClient::_result_matches(char const *pkg) const
{
	if (!_result_rom.valid())
		return false;

	try {
		Genode::Xml_node const r = _result_rom.xml();

		if (!r.has_type("result"))
			return false;

		/* Freshness: the result must answer THIS package. report_rom is
		 * fresh each boot, so matching on pkg+op is sufficient to reject
		 * any stale different-package answer. */
		if (r.attribute_value("op",  Genode::String<32>()) != Genode::String<32>("explain"))
			return false;
		if (r.attribute_value("pkg", Genode::String<128>()) != Genode::String<128>(pkg))
			return false;

		return r.has_attribute("status");
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}
}


bool PkgClient::request_explain(char const *pkg)
{
	/* Publish the request. sponge_pkgd's signal handler picks it up,
	 * resolves, and writes the result that report_rom relays back. */
	_request_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("op",  "explain");
		g.attribute("pkg", pkg);
	});

	/* Poll the result ROM (same idiom as InitStateReader in init_state.cc). */
	_timer.msleep(200);
	for (unsigned i = 0; i < 60; ++i) {
		_result_rom.update();
		if (_result_matches(pkg))
			return true;
		_timer.msleep(100);
	}

	return false;
}
