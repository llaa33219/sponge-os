/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * sponge_notifier — Sponge OS notification backend daemon (Phase 14 W4).
 *
 * A long-lived, signal-driven Genode component (no Qt, no libc, no
 * exceptions). It is the middle hop of the notification bus:
 *
 *   clients (sponge-de, vct, notify_probe)
 *       --[Report "notif_request"]-->  report_rom
 *       --[ROM "notif_request"]-->     sponge_notifier
 *
 *   sponge_notifier
 *       --[Report "notifications"]-->  report_rom
 *       --[ROM "notifications"]-->     clients (panel notifier_widget,
 *                                          notify_probe)
 *
 * Capability boundary (the D14.1 contract, AGENTS.md §1.2):
 *   - The daemon PROVIDES Report + ROM (the "notifications" channel)
 *   - The daemon REQUESTS Timer + ROM (config) + ROM (notif_request)
 *   - No PD, no RM, no GUI (the notifier is a pure backend)
 *
 * What it does:
 *   - Watches the "notif_request" ROM (relayed by report_rom from each
 *     client's Report("notif_request")). On every sigh, parses the
 *     <notification> child(ren), validates (kind, ttl_ms, title/body
 *     length), assigns a monotonic id and the current uptime as ts.
 *   - Maintains the active list FIFO (max_live from config, default
 *     8). When the list is full, the oldest entry is dropped silently
 *     (a warning is logged once per drop).
 *   - Publishes the full active list as the "notifications" ROM on
 *     every state change (initial publish + insert + expiry).
 *   - A periodic Timer (500 ms) sweeps expired entries (now - ts >=
 *     ttl_ms) and re-emits the list. The Timer is the single
 *     authoritative "is anything expired" source — re-broadcast is not
 *     driven by elapsed time on the request side, so a client that
 *     posts with ttl_ms=30000 gets exactly 30 s of life and the Timer
 *     sweep is the only way that becomes visible.
 *
 * Validation table (fail-soft, never crash):
 *   - kind:    "info" | "warn" | "error" (default "info" if missing)
 *   - ttl_ms:  1..30000 (cap 30000 per D14.1); 0/missing default to
 *              default_ttl_ms (default 5000); negative is rejected
 *   - source:  non-empty printable string (default "unknown" if empty)
 *   - title:   non-empty, <= 96 chars (empty POST rejected with a
 *              warning; the notifier is the dumb side, callers are
 *              responsible for meaningful titles)
 *   - body:    optional, <= 256 chars (omitted silently)
 *   - id:      assigned by the daemon, monotonic per-process
 *   - ts:      assigned by the daemon (uptime ms)
 *   Unknown root element or <notif_request> missing → request is
 *   silently dropped (no spurious warnings for invariants we cannot
 *   prevent, e.g. report_rom's initial empty buffer).
 *
 * Pure-function contract: the broadcast <notifications> is a pure
 * function of the active list. The state<->broadcast generator is
 * byte-deterministic for a given list (insertion order = id order).
 * Two consecutive broadcasts with the same ids are byte-identical, so
 * a watcher can poll the ROM cheaply and only re-style on change.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Notif {

class Main;

namespace { }

}  /* namespace Sponge::Notif */


class Sponge::Notif::Main
{
	public:

		explicit Main(Genode::Env &env);

	private:

		/*
		 * Bounds. MAX_LIVE is the absolute ceiling the daemon enforces
		 * even when the config asks for more; MAX_TITLE / MAX_BODY are
		 * the truncation caps applied to incoming text (over-length
		 * input is truncated, not rejected — we keep the entrance
		 * permissive and the title always readable in the popover).
		 */
		static constexpr unsigned MAX_LIVE   = 32;
		static constexpr unsigned MAX_TITLE  = 96;
		static constexpr unsigned MAX_BODY   = 256;
		static constexpr unsigned MAX_SOURCE = 32;
		static constexpr unsigned MAX_KIND   = 16;

		static constexpr unsigned DEFAULT_MAX_LIVE        = 8;
		static constexpr unsigned DEFAULT_DEFAULT_TTL_MS  = 5000;
		static constexpr unsigned TTL_MAX_MS              = 30000;
		static constexpr unsigned TIMER_PERIOD_MS         = 500;

		Genode::Env &_env;

		/* Input ROM: clients' "notif_request" reports relayed by report_rom. */
		Genode::Attached_rom_dataspace _notif_request { _env, "notif_request" };

		/* Optional config ROM (defaults if absent). */
		Genode::Attached_rom_dataspace _config_rom { _env, "config" };

		Genode::Signal_handler<Main> _notif_request_handler {
			_env.ep(), *this, &Main::_handle_notif_request };

		Genode::Signal_handler<Main> _config_handler {
			_env.ep(), *this, &Main::_handle_config };

		/*
		 * The output channel. The Expanding_reporter publishes a
		 * <notifications>...</notifications> document; report_rom
		 * relays it to every consumer ("notifications" ROM label).
		 */
		Genode::Expanding_reporter _notifications_reporter { _env, "notifications", "notifications" };

		/*
		 * Periodic sweep for TTL expiry. The Timer connection is the
		 * single source of "now" for the daemon; "ts" is the
		 * millisecond-uptime captured at insertion time, and the
		 * sweep computes (now - ts) >= ttl_ms.
		 */
		Timer::Connection _timer { _env };
		Genode::uint64_t  _boot_ms { 0 };
		Genode::Constructible<Timer::Periodic_timeout<Main>> _expiry_sweep { };

		/*
		 * Active list. FIFO by id (monotonic counter). When the list
		 * is full and a new entry arrives, the oldest is dropped
		 * (a warning is logged once per drop).
		 */
		struct Entry
		{
			Genode::String<16>   id;
			Genode::uint64_t     ts            { 0 };
			Genode::String<MAX_SOURCE> source;
			Genode::String<MAX_KIND>   kind;
			unsigned             ttl_ms        { 0 };
			Genode::String<MAX_TITLE>  title;
			Genode::String<MAX_BODY>   body;
		};

		Entry     _entries[MAX_LIVE] { };
		unsigned  _num_entries        { 0 };
		Genode::uint64_t _next_id     { 1 };

		/* Config (mutable on every config sigh). */
		unsigned  _max_live        { DEFAULT_MAX_LIVE };
		unsigned  _default_ttl_ms  { DEFAULT_DEFAULT_TTL_MS };

		/* ---- input handling ---- */
		void _handle_notif_request();
		void _handle_config();

		/* ---- storage ---- */
		bool _parse_and_apply(Genode::Xml_node const &req);
		bool _accept_one(Genode::Xml_node const &n);
		void _drop_oldest();

		/* ---- periodic sweep ---- */
		void _handle_expiry(Genode::Duration curr_time);

		/* ---- output ---- */
		void _generate_broadcast();
		Genode::uint64_t _now_ms();

		/* ---- validation helpers ---- */
		static bool _kind_valid(char const *kind);
		static unsigned _coerce_ttl_ms(unsigned requested, unsigned default_ttl_ms);
		static Genode::String<MAX_TITLE> _trim_title(Genode::String<MAX_TITLE> t);
		static Genode::String<MAX_BODY>  _trim_body(Genode::String<MAX_BODY> b);
		static Genode::String<MAX_SOURCE> _trim_source(Genode::String<MAX_SOURCE> s);
};


bool Sponge::Notif::Main::_kind_valid(char const *kind)
{
	return Genode::strcmp(kind, "info")  == 0
	    || Genode::strcmp(kind, "warn")  == 0
	    || Genode::strcmp(kind, "error") == 0;
}


unsigned Sponge::Notif::Main::_coerce_ttl_ms(unsigned requested,
                                              unsigned default_ttl_ms)
{
	if (requested == 0)
		return default_ttl_ms;
	if (requested > TTL_MAX_MS)
		return TTL_MAX_MS;
	return requested;
}


Genode::String<Sponge::Notif::Main::MAX_TITLE>
Sponge::Notif::Main::_trim_title(Genode::String<MAX_TITLE> t)
{
	/* Truncate to 96 chars maximum. */
	return t;
}


Genode::String<Sponge::Notif::Main::MAX_BODY>
Sponge::Notif::Main::_trim_body(Genode::String<MAX_BODY> b)
{
	return b;
}


Genode::String<Sponge::Notif::Main::MAX_SOURCE>
Sponge::Notif::Main::_trim_source(Genode::String<MAX_SOURCE> s)
{
	if (s.length() == 0)
		return Genode::String<MAX_SOURCE>("unknown");
	return s;
}


Genode::uint64_t Sponge::Notif::Main::_now_ms()
{
	Genode::Duration const now = _timer.curr_time();
	Genode::uint64_t     const now_ms = (Genode::uint64_t)now.trunc_to_plain_ms().value;
	if (_boot_ms == 0 || now_ms < _boot_ms)
		_boot_ms = now_ms;
	return now_ms - _boot_ms;
}


/* ===================== input handling ===================== */

void Sponge::Notif::Main::_handle_notif_request()
{
	_notif_request.update();
	if (!_notif_request.valid())
		return;

	/*
	 * report_rom's initial empty buffer is a valid (empty) ROM, so
	 * an "empty" request is a no-op, not an error. We only act when
	 * the content actually carries a <notif_request> root.
	 */
	try {
		Genode::Xml_node const root = _notif_request.xml();
		if (!root.has_type("notif_request"))
			return;
		_parse_and_apply(root);
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_notifier: malformed notif_request; dropped");
	}
}


void Sponge::Notif::Main::_handle_config()
{
	_config_rom.update();
	if (!_config_rom.valid())
		return;

	/*
	 * The config is optional — an empty <config>...</config> triggers
	 * this sigh but is a no-op. Only <max_live> and <default_ttl_ms>
	 * are recognized; anything else is rejected with a warning per
	 * the closed-registry pattern (sponge_configd's mirror).
	 */
	try {
		Genode::Xml_node const root = _config_rom.xml();
		root.for_each_sub_node([&](Genode::Xml_node const &n) {
			if (n.has_type("max_live")) {
				unsigned v = n.attribute_value("value", _max_live);
				if (v == 0) { v = DEFAULT_MAX_LIVE; }
				if (v > MAX_LIVE) v = MAX_LIVE;
				_max_live = v;
			}
			else if (n.has_type("default_ttl_ms")) {
				unsigned v = n.attribute_value("value", _default_ttl_ms);
				if (v == 0) { v = DEFAULT_DEFAULT_TTL_MS; }
				if (v > TTL_MAX_MS) v = TTL_MAX_MS;
				_default_ttl_ms = v;
			}
			else {
				Genode::warning("sponge_notifier: unknown config element <",
				                n.type().string(), ">");
			}
		});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_notifier: malformed config; kept previous");
	}
}


/* ===================== storage ===================== */

bool Sponge::Notif::Main::_parse_and_apply(Genode::Xml_node const &req)
{
	bool accepted_any = false;
	req.for_each_sub_node("notification", [&](Genode::Xml_node const &n) {
		if (_accept_one(n))
			accepted_any = true;
	});
	if (accepted_any)
		_generate_broadcast();
	return accepted_any;
}


bool Sponge::Notif::Main::_accept_one(Genode::Xml_node const &n)
{
	Genode::String<MAX_TITLE> title { };
	n.with_optional_sub_node("title", [&](Genode::Xml_node const &t) {
		title = _trim_title(t.decoded_content<Genode::String<MAX_TITLE>>());
	});

	if (title.length() == 0) {
		Genode::warning("sponge_notifier: dropped notification with empty title");
		return false;
	}

	Genode::String<MAX_BODY> body { };
	n.with_optional_sub_node("body", [&](Genode::Xml_node const &b) {
		body = _trim_body(b.decoded_content<Genode::String<MAX_BODY>>());
	});

	Genode::String<MAX_SOURCE> source = _trim_source(
		n.attribute_value("source", Genode::String<MAX_SOURCE>()));

	Genode::String<MAX_KIND> kind = n.attribute_value("kind", Genode::String<MAX_KIND>("info"));
	if (!_kind_valid(kind.string())) {
		Genode::warning("sponge_notifier: unknown kind '", kind,
		                "', defaulting to info");
		kind = Genode::String<MAX_KIND>("info");
	}

	unsigned const ttl_requested = n.attribute_value("ttl_ms", 0U);
	unsigned const ttl_ms = _coerce_ttl_ms(ttl_requested, _default_ttl_ms);

	/* FIFO: cap at max_live (default 8). If the list is full, drop
	 * the oldest entry (FIFO drop) and continue. The drop is logged
	 * once per drop so reviewers can spot flood attacks, but never
	 * warned for ordinary overflow because the publish rate is part
	 * of the daemon's contract. */
	if (_num_entries >= _max_live) {
		_drop_oldest();
	}

	Entry &e = _entries[_num_entries++];
	e.id     = Genode::String<16>(_next_id);
	e.ts     = _now_ms();
	e.source = source;
	e.kind   = kind;
	e.ttl_ms = ttl_ms;
	e.title  = title;
	e.body   = body;

	++_next_id;
	return true;
}


void Sponge::Notif::Main::_drop_oldest()
{
	/* FIFO drop: shift the array left by one. With MAX_LIVE=32 the
	 * inner loop is short enough that a linear-shift is the simplest
	 * sound implementation. */
	if (_num_entries == 0)
		return;
	Genode::log("sponge_notifier: list full, dropping oldest id=",
	            _entries[0].id);
	for (unsigned i = 1; i < _num_entries; ++i)
		_entries[i - 1] = _entries[i];
	--_num_entries;
}


/* ===================== expiry sweep ===================== */

void Sponge::Notif::Main::_handle_expiry(Genode::Duration curr_time)
{
	/*
	 * Linear scan; with MAX_LIVE=32 this is trivially fast. We track
	 * whether ANY entry was removed so we only re-emit when the
	 * broadcast actually changes — that is the same "expensive
	 * re-broadcast avoided" property sponge_configd's set path
	 * provides.
	 */
	Genode::uint64_t const now = (Genode::uint64_t)curr_time.trunc_to_plain_ms().value;
	bool changed = false;

	unsigned w = 0;
	for (unsigned r = 0; r < _num_entries; ++r) {
		Entry &e = _entries[r];
		if (now >= e.ts + e.ttl_ms) {
			/* expired — drop it (compact in place) */
			changed = true;
			continue;
		}
		_entries[w++] = e;
	}
	_num_entries = w;

	if (changed)
		_generate_broadcast();
}


/* ===================== output ===================== */

void Sponge::Notif::Main::_generate_broadcast()
{
	/*
	 * ids are monotonic by construction, so emitting in array order
	 * is the canonical FIFO order. The generator is deterministic
	 * for a given _entries state.
	 */
	_notifications_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("count", _num_entries);
		g.attribute("max_live", _max_live);

		for (unsigned i = 0; i < _num_entries; ++i) {
			Entry const &e = _entries[i];
			g.node("notification", [&] {
				g.attribute("id",     e.id);
				g.attribute("ts",     e.ts);
				g.attribute("source", e.source);
				g.attribute("kind",   e.kind);
				g.attribute("ttl_ms", e.ttl_ms);
				g.node("title", [&] { g.append_sanitized(e.title.string()); });
				if (e.body.length() > 0)
					g.node("body", [&] { g.append_sanitized(e.body.string()); });
			});
		}
	});
}


/* ===================== component wiring ===================== */

Sponge::Notif::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_notifier: ready (max_live=", _max_live,
	            " default_ttl_ms=", _default_ttl_ms, ")");

	/* Apply config if present (a sigh still fires on the first update). */
	_config_rom.sigh(_config_handler);
	_config_rom.update();
	_handle_config();

	/* Wire the request ROM and handle any pre-sigh input. */
	_notif_request.sigh(_notif_request_handler);
	_notif_request.update();
	_handle_notif_request();

	/* Start the periodic TTL sweep. Constructed in the body (the
	 * member-init list cannot capture `this` for the handler). */
	_expiry_sweep.construct(_timer, *this, &Main::_handle_expiry,
	                        Genode::Microseconds(TIMER_PERIOD_MS * 1000U));

	/* Publish the initial (empty) broadcast before any watcher can
	 * request it — init starts children in config order, so a
	 * watcher booting immediately after this daemon will see the
	 * empty list. */
	_generate_broadcast();
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Notif::Main main { env };
}


/*
 * Touches a single ROM dataspace and a periodic Timer on the stack;
 * keep it comfortable.
 */
Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
