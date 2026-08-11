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
	 * The Report session is opened lazily on the first post() call.
	 * Opening it eagerly in the constructor would cause the child to
	 * be killed when the report_rom is absent (the parent denies the
	 * session; the child is destroyed before the catch can run).
	 * Lazy opening lets the post() call observe the failure and
	 * log the D14.1 warning line instead.
	 */
}


void NotifierReporter::post(char const *title, char const *body,
                            char const *kind, unsigned ttl_ms)
{
	Genode::String<128> const tstr(title);
	Genode::String<256> const bstr(body);
	{
		Genode::uint64_t now_ms = 0;
		try {
			Timer::Connection t(_env);
			now_ms = (Genode::uint64_t)t.curr_time().trunc_to_plain_ms().value;
		} catch (...) { }
		if (tstr == _last_post_title
		 && bstr == _last_post_body
		 && now_ms - _last_post_ms < 1000)
			return;
		_last_post_title  = tstr;
		_last_post_body   = bstr;
		_last_post_ms     = now_ms;
	}

	if (!_enabled) {
		try {
			_request.construct(_env, "notif_request", "notif_request");
			_enabled = true;
			Genode::log("vct: notifier poster wired to sponge_notifier");
		}
		catch (...) {
			_enabled = false;
		}
	}

	if (!_enabled) {
		if (tstr != _last_post_title || bstr != _last_post_body) {
			Genode::warning("notifier unavailable, dropping: ", title);
			_last_post_title = tstr;
			_last_post_body  = bstr;
		}
		return;
	}

	Genode::String<16> k(kind);
	if (k != "info" && k != "warn" && k != "error")
		k = Genode::String<16>("info");

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
