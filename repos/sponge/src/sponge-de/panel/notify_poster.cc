/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of NotifyPoster. See notify_poster.h for the wire
 * contract and the threading model.
 */

#include "notify_poster.h"

#include <base/log.h>
#include <timer_session/connection.h>
#include <util/xml_generator.h>

#include <QMetaObject>

using namespace Sponge::Sponge_DE;


NotifyPoster::NotifyPoster(Genode::Env &env, QObject *parent)
:
	QObject(parent), _env(env)
{
	/*
	 * Open the Report session. The session is denied at the parent
	 * when no sponge_notifier is in the topology (no report_rom policy
	 * routes the "notif_request" label to sponge_notifier -> notif_request).
	 * We catch the failure and degrade to a no-op poster — the
	 * D14.1 contract: never a silent drop, never a crash. The
	 * `post()` slot logs a warning once per unique dropped title.
	 */
	try {
		_request.construct(_env, "request", "notif_request");
		_enabled = true;
		Genode::log("sponge-de: notify poster wired to sponge_notifier");
	}
	catch (...) {
		Genode::log("sponge-de: notify poster disabled "
		            "(sponge_notifier not in topology)");
		_enabled = false;
	}
}


void NotifyPoster::post(QString title, QString body, QString kind, unsigned ttl_ms)
{
	if (!_enabled) {
		/* D14.1: never a silent drop, never a crash. The warning is
		 * the audit trail. The posting is logged once per UNIQUE
		 * title so a tight loop doesn't spam the log. */
		if (title != _last_post_title) {
			Genode::warning("notifier unavailable, dropping: ", title.toUtf8().constData());
			_last_post_title = title;
		}
		return;
	}

	/* Normalize kind. */
	if (kind != "info" && kind != "warn" && kind != "error")
		kind = QStringLiteral("info");

	/* De-dup: identical (title, body) within 1 second is suppressed. */
	Genode::uint64_t now_ms = 0;
	{
		Timer::Connection t(_env);
		now_ms = (Genode::uint64_t)t.curr_time().trunc_to_plain_ms().value;
	}
	if (title == _last_post_title
	 && body  == _last_post_body
	 && now_ms - _last_post_ms < 1000)
		return;
	_last_post_title = title;
	_last_post_body  = body;
	_last_post_ms    = now_ms;

	Genode::String<96>  source("sponge-de");
	_request->generate_xml([&](Genode::Xml_generator &g) {
		g.node("notif_request", [&] {
			g.node("notification", [&] {
				g.attribute("source", source);
				g.attribute("kind",   kind.toUtf8().constData());
				g.attribute("ttl_ms", ttl_ms);
				g.node("title", [&] { g.append_sanitized(title.toUtf8().constData()); });
				if (!body.isEmpty())
					g.node("body", [&] { g.append_sanitized(body.toUtf8().constData()); });
			});
		});
	});
}
