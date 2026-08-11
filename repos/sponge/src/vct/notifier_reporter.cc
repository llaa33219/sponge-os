/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of NotifierReporter. See notifier_reporter.h for the
 * wire contract and the failure modes.
 */

#include "notifier_reporter.h"

#include <base/log.h>
#include <timer_session/connection.h>
#include <util/xml_generator.h>

using namespace Sponge::Vct;


NotifierReporter::NotifierReporter(Genode::Env &env)
:
	_env(env),
	_last_post_title(),
	_last_post_body()
{
	/*
	 * Open the Report session. The session is denied at the parent
	 * when no sponge_notifier is in the topology (no report_rom
	 * policy routes the "notif_request" label to sponge_notifier).
	 * We catch the failure and degrade to a no-op poster — the
	 * D14.1 contract: never a silent drop, never a crash.
	 */
	try {
		_request.construct(_env, "notif_request", "notif_request");
		_enabled = true;
		Genode::log("vct: notifier poster wired to sponge_notifier");
	}
	catch (...) {
		Genode::log("vct: notifier poster disabled "
		            "(sponge_notifier not in topology)");
		_enabled = false;
	}
}


void NotifierReporter::post(char const *title, char const *body,
                            char const *kind, unsigned ttl_ms)
{
	if (!_enabled) {
		/* D14.1: never a silent drop, never a crash. The warning is
		 * the audit trail. Coalesced by (title, body) so a tight
		 * loop doesn't spam the log. */
		Genode::String<128> const t(title);
		Genode::String<256> const b(body);
		if (t != _last_post_title || b != _last_post_body) {
			Genode::warning("notifier unavailable, dropping: ", title);
			_last_post_title = t;
			_last_post_body  = b;
		}
		return;
	}

	/* Normalize kind. */
	Genode::String<16> k(kind);
	if (k != "info" && k != "warn" && k != "error")
		k = Genode::String<16>("info");

	/* De-dup: identical (title, body) within 1 second is suppressed. */
	{
		Timer::Connection t(_env);
		Genode::uint64_t const now_ms = (Genode::uint64_t)t.curr_time().trunc_to_plain_ms().value;
		Genode::String<128> const tstr(title);
		Genode::String<256> const bstr(body);
		if (tstr == _last_post_title
		 && bstr == _last_post_body
		 && now_ms - _last_post_ms < 1000)
			return;
		_last_post_title  = tstr;
		_last_post_body   = bstr;
		_last_post_ms     = now_ms;
	}

	Genode::String<32> source("vct");
	_request->generate_xml([&](Genode::Xml_generator &g) {
		g.node("notification", [&] {
			g.attribute("source", source);
			g.attribute("kind",   k);
			g.attribute("ttl_ms", ttl_ms);
			g.node("title", [&] { g.append_sanitized(title); });
			if (body && body[0] != '\0')
				g.node("body", [&] { g.append_sanitized(body); });
		});
	});
}
