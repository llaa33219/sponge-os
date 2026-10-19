/*
 * ep_wedge — sandbox heartbeat / supervisor fault-injection component.
 *
 * Modes, selected by the component config:
 *
 *   default (no config / behave unset):
 *     Logs one line and blocks its entrypoint forever.
 *     Component::construct runs on the ep's dispatch thread, so
 *     sleep_forever wedges every RPC and signal processing in this
 *     component — exactly the failure class the sandbox heartbeat is
 *     deployed to contain (docs/17-runtime-component-topology.md §5).
 *     A parent running <heartbeat rate_ms="..."/> with this child
 *     configured <heartbeat restart_after_skipped="N"/> observes
 *     skipped heartbeats, abandons the wedged instance, and respawns
 *     it — which makes the "alive" line appear again.
 *
 *   <config behave="yes"/>:
 *     The witness: logs once and returns from construct (a healthy
 *     idle component). It must SURVIVE any number of sibling
 *     wedge-restart cycles — the independence assertion.
 *
 *   <config wedge_ms="N"/>:
 *     A BOUNDED wedge. The component logs which incarnation it is,
 *     then blocks its entrypoint for N milliseconds and afterwards
 *     returns from construct, becoming responsive again. A bounded
 *     wedge is what makes a recovery claim falsifiable: a fault that
 *     heals on a timer AFTER the supervisor has already killed and
 *     respawned the subtree proves the kill worked, and the console
 *     takeover is only reachable once the supervisor has decided the
 *     respawn did not help.
 *
 *   <config behave="yes" wedge_ms="N"/>:
 *     behave wins — a witness is a witness.
 *
 *   <config wedge_after_ms="N"/>:
 *     A wedge AFTER startup. The component returns from construct
 *     immediately — so it registers its heartbeat handler, becomes
 *     responsive and ANSWERS heartbeats — and wedges its entrypoint N
 *     milliseconds later, forever. This is the fault the product
 *     actually suffers (`sponge_pkgd unresponsive(N)` on the LG gram),
 *     and it is the only mode a supervisor above the sandbox can be
 *     expected to detect: `wedge_ms` blocks the entrypoint inside the
 *     constructor, where a child that has never answered a heartbeat
 *     is legitimately classified as STARTING rather than wedged (the
 *     startup grace, docs/evidence/startup-grace.md). A wedge that
 *     happens after the first response has none of that excuse.
 *
 *   <config liveness_ms="N"/>:
 *     A LIVENESS ORACLE. Logs one "bystander: alive t=<ms>" line every
 *     N milliseconds, forever, and never blocks its entrypoint. It is
 *     the in-sandbox half of "a stalled sibling did not take the
 *     container down": the component keeps producing output while a
 *     sibling is being respawned underneath it, so an unbounded
 *     containment loop that starves the whole sandbox is falsifiable.
 *     Without a periodically-emitting component a scenario cannot tell
 *     "the sandbox is fine" from "the sandbox stopped talking", which
 *     is precisely the field failure being reproduced.
 *
 * The incarnation number is not remembered by this component: it is
 * stateless by design. The supervisor's kill-and-respawn is what
 * produces the second "alive" line; that line repeating after a kill is
 * the proof, exactly as in run/sponge-heartbeat-restart.run.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/signal.h>
#include <base/sleep.h>
#include <timer_session/connection.h>
#include <util/string.h>

namespace {

	struct Mode
	{
		bool     behave { false };
		unsigned wedge_ms { 0 };
		unsigned wedge_after_ms { 0 };
		unsigned liveness_ms { 0 };
	};

	/*
	 * The config reaches this component as XML or HID depending on how
	 * the linker delivered it, so both spellings are accepted: a
	 * fault-injection component that silently ignored its own config
	 * would turn every containment test into a false PASS.
	 */
	unsigned _uint_attr(char const *cfg, Genode::size_t sz, char const *key)
	{
		Genode::size_t const key_len = Genode::strlen(key);

		for (Genode::size_t i = 0; i + key_len < sz; i++) {

			if (Genode::strcmp(cfg + i, key, key_len) != 0)
				continue;

			/* must not be the tail of a longer attribute name */
			if (i > 0) {
				char const p = cfg[i - 1];
				bool const name_char = (p >= 'a' && p <= 'z') ||
				                       (p >= 'A' && p <= 'Z') ||
				                       (p >= '0' && p <= '9') ||
				                       p == '_' || p == '-' || p == ':';
				if (name_char)
					continue;
			}

			Genode::size_t j = i + key_len;

			if (j >= sz || (cfg[j] != '=' && cfg[j] != ':'))
				continue;

			j++;
			if (j < sz && cfg[j] == '"') j++;
			while (j < sz && (cfg[j] == ' ' || cfg[j] == '\t')) j++;

			unsigned r = 0;
			while (j < sz && cfg[j] >= '0' && cfg[j] <= '9') {
				r = r * 10 + unsigned(cfg[j] - '0');
				j++;
			}
			return r;
		}

		return 0;
	}

	bool _behave_attr(char const *cfg, Genode::size_t sz)
	{
		Genode::size_t const key_len = 6; /* "behave" */

		for (Genode::size_t i = 0; i + key_len < sz; i++) {

			if (Genode::strcmp(cfg + i, "behave", key_len) != 0)
				continue;

			if (i > 0) {
				char const p = cfg[i - 1];
				bool const name_char = (p >= 'a' && p <= 'z') ||
				                       (p >= 'A' && p <= 'Z') ||
				                       (p >= '0' && p <= '9') ||
				                       p == '_' || p == '-' || p == ':';
				if (name_char)
					continue;
			}

			/* skip separator and optional opening quote */
			Genode::size_t j = i + key_len;
			while (j < sz && (cfg[j] == ' ' || cfg[j] == '=' ||
			                  cfg[j] == ':' || cfg[j] == '"'))
				j++;

			return Genode::strcmp(cfg + j, "yes", 3) == 0;
		}

		return false;
	}

	Mode _mode(Genode::Env &env)
	{
		Genode::Attached_rom_dataspace cfg { env, "config" };
		cfg.update();

		Mode m;
		if (!cfg.valid())
			return m;

		char const *raw = cfg.local_addr<char const>();
		Genode::size_t const sz = cfg.size();

		m.wedge_ms       = _uint_attr(raw, sz, "wedge_ms");
		m.wedge_after_ms = _uint_attr(raw, sz, "wedge_after_ms");
		m.behave         = _behave_attr(raw, sz);
		m.liveness_ms    = _uint_attr(raw, sz, "liveness_ms");

		return m;
	}
}

void Component::construct(Genode::Env &env)
{
	Mode const m = _mode(env);

	if (m.behave) {
		Genode::log("witness: alive and well");
		return;
	}

	/* A periodic line is the one thing that keeps flowing while a sibling
	 * is respawned underneath it, so its continued output is evidence
	 * about the CONTAINER, not about this component. Checked before the
	 * wedge branch so a liveness oracle can never wedge. */
	if (m.liveness_ms) {
		Timer::Connection timer { env };
		Genode::size_t elapsed { 0 };
		while (true) {
			timer.msleep(m.liveness_ms);
			elapsed += m.liveness_ms;
			Genode::log("bystander: alive t=", elapsed, " ms");
		}
	}

	/*
	 * Wedge AFTER startup: return from construct first, so the entrypoint
	 * is free to answer heartbeats, and block it later. The Timer signal
	 * handler runs on the entrypoint, so `sleep_forever` there is the
	 * wedge - the same shape as a component that stops servicing its
	 * entrypoint while it is RUNNING, not while it is being built.
	 */
	if (m.wedge_after_ms) {

		struct Wedge
		{
			void _handle(Genode::Duration) {
				Genode::log("ep_wedge: wedge fired - entrypoint parked");
				Genode::sleep_forever();
			}
		};

		/*
		 * `One_shot_io_timeout` owns the sigh plumbing, so the timeout
		 * signal reaches THIS handler instead of the timer connection's
		 * internal one (a bare `trigger_once()` would wake the connection
		 * and nobody else).
		 */
		static Timer::Connection          timer   { env };
		static Wedge                      wedge;
		static Timer::One_shot_io_timeout<Wedge> timeout {
			timer, wedge, &Wedge::_handle };

		Genode::log("ep_wedge: incarnation alive — wedging in ",
		            m.wedge_after_ms, " ms");

		timeout.schedule(Genode::Microseconds { Genode::uint64_t(m.wedge_after_ms) * 1000 });
		return;
	}

	if (m.wedge_ms == 0) {
		Genode::log("ep_wedge: alive — entrypoint wedging now");
		Genode::sleep_forever();
	}

	/*
	 * Bounded wedge: unresponsive for 'wedge_ms', then alive again.
	 * The Timer session comes from the parent sandbox's `timer`
	 * sibling, so this component depends on nothing it supervises.
	 */
Timer::Connection timer { env };

	Genode::log("ep_wedge: incarnation alive — wedging for ",
	            m.wedge_ms, " ms");

	timer.msleep(m.wedge_ms);

	Genode::log("ep_wedge: incarnation un-wedged after ",
	            m.wedge_ms, " ms");
}