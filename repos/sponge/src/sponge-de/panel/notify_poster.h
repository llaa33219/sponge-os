/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * NotifyPoster — the in-sponge-de notif_request writer.
 *
 * A small QObject that owns the "notif_request" Report session and
 * exposes a `post()` slot for any other in-sponge-de component to
 * publish a notification. The session is opened lazily (on the first
 * post() call) and only when the component config opts in via
 * `<notifier source="daemon"/>`. The opt-in matches the pattern used
 * by the other controllers (theme / configd / pkgd) and keeps
 * scenarios without sponge_notifier in the topology from being
 * killed by the parent when the Report session is denied.
 *
 * When the opt-in is absent, the poster stays disabled and post()
 * emits the D14.1 warning line for each unique title. When the
 * opt-in is present but the daemon is absent at runtime (no policy
 * routes the notif_request label), the lazy-opening try/catch
 * degrades to the same no-op poster.
 *
 * Wire contract:
 *   <notif_request>
 *     <notification source="sponge-de" kind="info" ttl_ms="5000">
 *       <title>...</title>
 *       <body>...</body>
 *     </notification>
 *   </notif_request>
 *
 * Threading:
 *   The Report session is opened on the first post() call (lazy).
 *   All post() calls are expected to come from the GUI thread.
 */

#pragma once

#include <base/component.h>
#include <os/reporter.h>

#include <QObject>
#include <QString>

namespace Sponge::Sponge_DE {

/*
 * Read the <notifier source="daemon"/> attribute from the component
 * config. Absent defaults to false (opt-in — the alpha run does not
 * include the notifier source and is unaffected by the poster's
 * existence). Same pattern as config_asks_for_configd.
 */
bool notifier_asks_for_daemon(Genode::Env &env);


class NotifyPoster : public QObject
{
	Q_OBJECT

	public:

		explicit NotifyPoster(Genode::Env &env, QObject *parent = nullptr);

		bool enabled() const { return _enabled; }

	public slots:

		void post(QString title, QString body, QString kind, unsigned ttl_ms);

	private:

		Genode::Env &_env;

		bool _opt_in { false };
		Genode::Constructible<Genode::Expanding_reporter> _request { };

		bool _enabled { false };

		Genode::uint64_t _last_post_ms      { 0 };
		QString          _last_post_title;
		QString          _last_post_body;
};

}  /* namespace Sponge::Sponge_DE */
