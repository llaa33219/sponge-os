/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * NotifierReporter — vct's notif_request writer (Phase 14 W4).
 *
 * A small helper that owns the "notif_request" Report session and
 * exposes a single `post()` method for vct's commands to announce
 * significant events (install/remove/shutdown/reboot completion).
 *
 * The session is opened conditionally. When no sponge_notifier is in
 * the topology (no report_rom policy routes the label), the session
 * creation fails; the constructor catches the failure and degrades
 * to a no-op reporter. The D14.1 contract: never a silent drop, never
 * a crash — post() logs the warning line exactly once per unique
 * (title, body) tuple, then is a no-op for that tuple.
 *
 * Wire contract:
 *   label: "notif_request"  (relayed by report_rom from vct's
 *   session to sponge_notifier's "notif_request" ROM. The report
 *   XML is molded by the sponge_notifier contract:
 *     <notif_request>
 *       <notification source="vct" ...>
 *         <title>...</title>
 *         <body>...</body>
 *       </notification>
 *     </notif_request>)
 */

#pragma once

#include <base/component.h>
#include <os/reporter.h>
#include <util/noncopyable.h>
#include <util/string.h>

namespace Sponge::Vct {

class NotifierReporter : private Genode::Noncopyable
{
	public:

		explicit NotifierReporter(Genode::Env &env);

		/*
		 * Publish a notification. The kind is "info" / "warn" /
		 * "error" (default "info"). The ttl_ms is clamped by
		 * sponge_notifier to 30000 (default 5000 if 0).
		 *
		 * When the underlying Report session is unavailable
		 * (sponge_notifier not in topology), logs:
		 *   Genode::warning("notifier unavailable, dropping: <title>")
		 * once per unique title. Never a silent drop, never a crash.
		 */
		void post(char const *title, char const *body,
		          char const *kind = "info", unsigned ttl_ms = 5000);

		/*
		 * Returns true if the poster is wired to a sponge_notifier.
		 * Used by callers to suppress audit-trail spam when the
		 * daemon is intentionally absent.
		 */
		bool enabled() const { return _enabled; }

	private:

		Genode::Env &_env;

		Genode::Constructible<Genode::Expanding_reporter> _request { };

		bool _enabled { false };

		/* De-dup: a (title, body) tuple posted within the dedup
		 * window is suppressed. The window is a 1-second buffer —
		 * short enough that a real user-driven event is allowed,
		 * but long enough that a tight storm of identical posts is
		 * throttled. */
		Genode::uint64_t _last_post_ms { 0 };
		Genode::String<128> _last_post_title;
		Genode::String<256> _last_post_body;
};

}  /* namespace Sponge::Vct */
