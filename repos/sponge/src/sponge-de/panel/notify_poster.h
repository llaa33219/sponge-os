/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * NotifyPoster — the in-sponge-de notif_request writer.
 *
 * A small QObject that owns the "notif_request" Report session and
 * exposes a `post()` slot for any other in-sponge-de component to
 * publish a notification. The session is opened conditionally — when
 * sponge_notifier is absent from the topology (no report_rom route for
 * the label), the post() calls are silently dropped with a single
 * warning (the Phase-14 D14.1 contract: "never a silent drop, never a
 * crash" — we log it once per dropped notify attempt, not per call).
 *
 * Wire contract:
 *   The session is a Genode Report with label "notif_request". The
 *   report_rom policy in the run config maps:
 *     label: sponge_notifier -> notif_request | report: sponge-de -> notif_request
 *   The produced payload is molded by the XML contract in
 *   sponge_notifier/README.md:
 *     <notif_request>
 *       <notification source="sponge-de" kind="info" ttl_ms="5000">
 *         <title>...</title>
 *         <body>...</body>
 *       </notification>
 *     </notif_request>
 *
 * Threading:
 *   The Report session is opened on the Genode entrypoint thread (the
 *   Expanding_reporter constructor). All post() calls are expected to
 *   come from the GUI thread (the slot is connected to controller
 *   signals which are emitted on the GUI thread after marshalling).
 *   No further marshalling is needed.
 *
 * Failure modes:
 *   - Report session fails to open (no sponge_notifier in topology):
 *     the internal `_enabled` flag stays false; post() logs a single
 *     warning per (unique-titled) dropped notification, then is a
 *     no-op forever after. The contract is explicit: never a crash,
 *     never a silent drop — the warning is the audit trail.
 *   - Invalid XML generation (impossible by construction here —
 *     Xml_generator handles escaping): reported by report_rom's
 *     policy check, but the poster does not retry.
 *
 * Sponge-de is a multi-controller consumer: theme_ctrl, config_ctrl,
 * and launcher_ctrl each call post() from their apply slots. The
 * poster de-duplicates by title — repeated posts with the same title
 * within `_dedup_window_ms` are coalesced (logged once, never a
 * runaway log flood).
 */

#pragma once

#include <base/component.h>
#include <os/reporter.h>

#include <QObject>
#include <QString>

namespace Sponge::Sponge_DE {

class NotifyPoster : public QObject
{
	Q_OBJECT

	public:

		explicit NotifyPoster(Genode::Env &env, QObject *parent = nullptr);

		/*
		 * Returns true if the poster is wired to a sponge_notifier
		 * (the Report session is open). Used by callers to suppress
		 * audit-trail spam when the daemon is intentionally absent.
		 */
		bool enabled() const { return _enabled; }

	public slots:

		/*
		 * Publish a notification. The kind is one of "info" / "warn"
		 * / "error" (default "info"). The ttl_ms is clamped by
		 * sponge_notifier to 30000 (and defaults to 5000 if 0).
		 */
		void post(QString title, QString body, QString kind, unsigned ttl_ms);

	private:

		Genode::Env &_env;

		Genode::Constructible<Genode::Expanding_reporter> _request { };

		bool    _enabled { false };

		/*
		 * De-dup: a (title, body) tuple posted within the dedup
		 * window is suppressed. The window is a 1-second buffer —
		 * short enough that a real user-driven event (theme apply,
		 * config set) is allowed, but long enough that a tight
		 * storm of identical posts (a re-style loop) is throttled.
		 */
		Genode::uint64_t _last_post_ms      { 0 };
		QString          _last_post_title;
		QString          _last_post_body;
};

}  /* namespace Sponge::Sponge_DE */
