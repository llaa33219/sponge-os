/*
 * \brief  Top-level system supervisor
 * \author Sponge OS contributors
 * \date   2026
 *
 * Closes the containment gap recorded in
 * docs/17-runtime-component-topology.md §5 ("Broker fault containment
 * - top level"). The top-level init cannot run a heartbeat because it
 * asks its PARENT (core) for a Timer session and core provides none
 * (genode/repos/base/src/lib/timeout/timer_connection.cc:43 - the
 * session goes to env.parent(), and for the top-level init that is
 * core). Nothing above the `system` sandbox can therefore detect, kill
 * or restart it. This component is that "above".
 *
 * It is the pkgd pattern (docs/evidence/config-regen-survival.md) moved
 * one level up:
 *
 *   - it OWNS the config of the supervised `system` sandbox and of a
 *     sibling `recovery` sandbox, both delivered through report_rom
 *     (AGENTS.md §1.2: parent-to-child configuration stays explicit
 *     and inspectable);
 *   - it READS the supervised sandbox's own `state` report, relayed by
 *     the TOP-LEVEL report_rom. Never by a relay inside the supervised
 *     subtree: that relay is part of the failure domain we exist to
 *     survive;
 *   - it has a Timer of its own, so its idea of time is independent of
 *     the subtree it supervises.
 *
 * Restart semantics (the one non-obvious mechanic, verified against
 * genode/repos/os/src/lib/sandbox/config_model.h:171):
 * `Start_model::matches()` compares ONLY the `name` and `version`
 * attributes of a `<start>` node. A config rewrite with identical
 * name+version therefore leaves every running child untouched - the
 * finding behind docs/evidence/config-regen-survival.md. To actually
 * kill a wedged child the supervisor must CHANGE its version, which
 * makes the node fail to match, which abandons the old Child and
 * creates a new one. Every `<start>` node in the templates therefore
 * carries version="@KILL@", substituted with a monotonically rising
 * counter on every rewrite. This is the only restart lever a config
 * owner has, and it is deliberately total: a wedged GUI stack is not
 * partially salvageable from up here.
 *
 * Detection signal. Three candidates were available:
 *
 *   1. state-report staleness - REJECTED, and not even as a secondary
 *      signal. A Genode state report is event-driven: it is emitted when
 *      a child starts, exits, changes session state, or changes its
 *      skipped heartbeat count. It is NOT periodic unless `child_ram`/
 *      `child_caps` reporting is enabled, and even then the periodic
 *      timer only reports when a child's RAM accounting actually changed
 *      (genode/repos/os/src/lib/sandbox/state_reporter.h:76). A quiet
 *      sandbox is therefore indistinguishable from a dead one by report
 *      age alone. This was not assumed - the control run of
 *      run/sponge-supervisor-takeover.run (SPONGE_TAKE_NO_FAULT=1)
 *      produced exactly this false positive at state_age_ms=10250 on a
 *      healthy system, and the detector was rewritten in response. The
 *      only staleness statement that survives is the boot-time one: a
 *      sandbox that has published nothing at all since it was created is
 *      not publishing.
 *   2. `skipped_heartbeats` on a child - PRIMARY. The supervised
 *      sandbox is configured with `<heartbeat rate_ms=.../>` and its
 *      critical children carry a bare `<heartbeat/>` node, which turns
 *      monitoring ON with `restart_after_skipped` defaulting to 0, i.e.
 *      local auto-restart OFF
 *      (genode/repos/os/src/lib/sandbox/child.h:207). That is
 *      deliberate: this supervisor is the single authority that
 *      restarts them. Monotonic while a child is wedged, and already
 *      reported on every tick
 *      (genode/repos/os/src/lib/sandbox/heartbeat.h:54).
 *   3. `exited` on a child - also a fault, since an exited desktop
 *      child is a dead desktop.
 *
 * Phases:
 *
 *   BOOT     publish the nominal config once, then NOMINAL
 *   NOMINAL  healthy; the state report is polled but nothing is written
 *   CONTAIN  fault seen: rewrite `system` with a fresh @KILL@ (full
 *            teardown + respawn of the subtree) and start a grace timer
 *   CONSOLE  the fault survived the grace window: publish the recovery
 *            config that starts the fault console, and swap the
 *            supervised config to its desktop-less variant so the
 *            console is the ONLY Gui client. Ordered takeover: the
 *            wedged stack was already destroyed by the CONTAIN rewrite
 *            before the console is started.
 *   NOMINAL  after a bounded hold, republish the nominal config and
 *            stop the console. This is "automation = default, control =
 *            a door" applied to faults: nothing is asked of the user,
 *            and vct remains the manual door.
 *
 * Timing uses a TICK COUNT, not a wall-clock read: the supervisor's own
 * Timer is the only clock inside the failure domain it trusts, and a
 * periodic signal is by definition a tick source. Every deadline below
 * is expressed in ticks, with the policy attributes given in
 * milliseconds and converted once.
 *
 * Sessions (minimum privilege, AGENTS.md §1.2):
 *   Timer, Report (via report_rom), ROM "config" (policy),
 *   ROM "templates" (the XML config templates, forwarded verbatim),
 *   ROM "supervisor -> system_state" (the supervised subtree's report).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/string.h>

/* local includes */
#include "text_scan.h"

namespace Sponge {

	struct Supervisor { struct Main; };
}

using namespace Genode;

struct Sponge::Supervisor::Main
{
	Env &_env;

	/* ---- input -------------------------------------------------- */

	/* own policy */
	Attached_rom_dataspace _config { _env, "config" };

	/*
	 * The supervised sandbox's state report, relayed by the top-level
	 * report_rom. The internal relay lives INSIDE the supervised
	 * subtree and is therefore part of the failure domain.
	 */
	Attached_rom_dataspace _state { _env, "supervisor -> system_state" };

	/* XML config templates, forwarded verbatim */
	Attached_rom_dataspace _templates { _env, "templates" };

	/* ---- output ------------------------------------------------- */

	/*
	 * Fixed-size, never growing. `Reporter::generate()` reports
	 * Buffer_error::EXCEEDED *without* touching the session, whereas
	 * `Expanding_reporter` destructs and reconstructs the session when
	 * it grows - and during that window the report carries no payload.
	 * sponge_pkgd's README documents that hazard; a pre-sized plain
	 * Reporter avoids it by construction, at the cost of logging loudly
	 * instead of resizing.
	 *
	 * Both reporters are constructed on the FIRST TICK rather than in
	 * this component's constructor. A Report session is a service
	 * request, and a denied session is fatal; report_rom is a sibling
	 * that announces its service from its own constructor, so the only
	 * race-free moment to request it is after the Timer has delivered at
	 * least one tick - by which point every Tier-0 sibling has long
	 * announced. The relayed ROM and config sessions have no such race:
	 * report_rom serves any reader whose label matches a configured
	 * policy, handing out an EMPTY module until the report arrives.
	 */
	Constructible<Reporter> _system_reporter   { };
	Constructible<Reporter> _armed_reporter    { };

	/* ---- policy -------------------------------------------------- */

	unsigned _poll_ms         { 250 };
	unsigned _fault_skipped   { 3 };
	unsigned _boot_grace_ms   { 30000 };
	unsigned _grace_ms        { 4000 };
	unsigned _console_hold_ms { 4000 };
	unsigned _settle_ms       { 3000 };

	/* deadlines in ticks */
	unsigned _boot_grace_ticks { 0 };
	unsigned _grace_ticks      { 0 };
	unsigned _console_ticks    { 0 };
	unsigned _settle_ticks     { 0 };

	unsigned _kill_version { 0 };
	unsigned _ticks        { 0 };
	unsigned _last_idle    { 0 };

	enum class Phase { BOOT, NOMINAL, CONTAIN, CONSOLE, RELEASE };

	Phase    _phase       { Phase::BOOT };
	unsigned _phase_since { 0 };

	/* first tick at which the subtree looked healthy after the last kill */
	unsigned _healthy_since { 0 };
	bool     _ever_healthy { false };

	/*
	 * Number of children reported as `exited` the last time the subtree
	 * was healthy. Genode keeps an exited child in the state report with
	 * its `exited` attribute until it is abandoned, so a single death
	 * would otherwise latch the supervisor into a permanent fault. The
	 * signal is therefore EDGE-TRIGGERED: only a RISE above the
	 * healthy baseline counts, which is the actual difference between
	 * "a component restarted once" and "components are dying now".
	 */
	unsigned _exited_baseline { 0 };

	/* ---- observation --------------------------------------------- */

	bool     _state_seen  { false };
	unsigned _state_at    { 0 };
	unsigned _max_skipped { 0 };
	unsigned _exited      { 0 };

	Timer::Connection   _timer { _env };
	Signal_handler<Main> _tick  { _env.ep(), *this, &Main::_handle_tick };

	/*
	 * The state ROM's change signal is how new state arrives; polling
	 * cannot tell 'unchanged' from 'no report', so the change signal is
	 * the only correct trigger. It is deliberately NOT used as a
	 * liveness timer - see _faulted() and the class comment.
	 */
	Signal_handler<Main> _state_sigh { _env.ep(), *this, &Main::_handle_state };

	/* ---- configuration -------------------------------------------- */

	unsigned _num(char const *cfg, size_t sz, char const *key, unsigned dflt) const
	{
		char const *v = nullptr;
		size_t      n = 0;

		if (!Sponge::scan_attr(cfg, sz, key, v, n) || n == 0)
			return dflt;

		unsigned r = 0;
		for (size_t i = 0; i < n; i++) {
			if (v[i] < '0' || v[i] > '9')
				break;
			r = r * 10 + unsigned(v[i] - '0');
		}
		return r;
	}

	void _read_policy()
	{
		_config.update();

		if (!_config.valid())
			return;

		char const *cfg = _config.local_addr<char const>();
		size_t      const sz = _config.size();

		_poll_ms         = _num(cfg, sz, "poll_ms",         _poll_ms);
		_fault_skipped   = _num(cfg, sz, "fault_skipped",   _fault_skipped);
		_boot_grace_ms   = _num(cfg, sz, "boot_grace_ms",   _boot_grace_ms);
		_grace_ms        = _num(cfg, sz, "grace_ms",        _grace_ms);
		_console_hold_ms = _num(cfg, sz, "console_hold_ms", _console_hold_ms);
		_settle_ms       = _num(cfg, sz, "settle_ms",       _settle_ms);

		unsigned const poll = _poll_ms ? _poll_ms : 250;

		auto to_ticks = [poll] (unsigned ms) { return (ms + poll - 1) / poll; };

			_boot_grace_ticks = to_ticks(_boot_grace_ms);
		_grace_ticks      = to_ticks(_grace_ms);
		_console_ticks    = to_ticks(_console_hold_ms);
		_settle_ticks     = to_ticks(_settle_ms);
	}

	/* ---- templates ----------------------------------------------- */

	/**
	 * Return the <config> element of the named template.
	 *
	 * The templates wrap each sandbox config in a named element
	 * (<system>, <system_console>) so this component can find them; the
	 * wrapper is stripped here because init applies the <config>
	 * ELEMENT and not whatever happens to be outermost.
	 * Attached_rom_dataspace::node() resolves to the document's first
	 * element child (genode/repos/base/src/lib/base/node.cc:151), so
	 * forwarding the wrapper verbatim would hand init an element type it
	 * does not know and it would silently apply nothing.
	 */
	bool _template(char const *name, char const *&text, size_t &len) const
	{
		if (!_templates.valid())
			return false;

		char const *wrapped = nullptr;
		size_t      wrapped_len = 0;

		if (!Sponge::element_text(_templates.local_addr<char const>(),
		                          _templates.size(), name,
		                          wrapped, wrapped_len))
			return false;

		return Sponge::element_text(wrapped, wrapped_len, "config", text, len);
	}

	void _construct_reporters()
	{
		_system_reporter.construct(_env, "config", "system_config", 128*1024);
		_armed_reporter .construct(_env, "config", "console_state",   4*1024);
		_system_reporter->enabled(true);
		_armed_reporter ->enabled(true);
	}

	/**
	 * Arm or disarm the fault console. The report is the console's whole
	 * configuration, so the supervisor decides the message as well as
	 * the switch - there is no second source of truth to drift.
	 */
	void _arm_console(bool armed)
	{
		if (!_armed_reporter.constructed())
			return;

		static char const ON[] =
			"<config armed=\"yes\" color=\"#601010\""
			" title=\"SYSTEM FAULT - RESTARTING\"/>";
		static char const OFF[] = "<config armed=\"no\"/>";

		char const *text = armed ? ON : OFF;
		size_t      len  = armed ? (sizeof(ON) - 1) : (sizeof(OFF) - 1);

		if (_armed_reporter->generate(Const_byte_range_ptr { text, len }) ==
		    Buffer_error::EXCEEDED)
			warning("sponge_supervisor: console_state report overflow");
	}

	bool _publish(Reporter &reporter, char const *template_name)
	{
		char const *text = nullptr;
		size_t      len  = 0;

		if (!_template(template_name, text, len)) {
			warning("sponge_supervisor: template <", template_name,
			        "> not found in the templates module - not publishing");
			return false;
		}

		static char buf[160*1024];

		size_t const out = Sponge::substitute_decimal(text, len, "@KILL@",
		                                               ++_kill_version,
		                                               buf, sizeof(buf));

		if (out + 1 >= sizeof(buf)) {
			warning("sponge_supervisor: template <", template_name,
			        "> truncated at ", sizeof(buf) - 1,
			        " bytes - not publishing");
			return false;
		}

		reporter.enabled(true);

		if (reporter.generate(Const_byte_range_ptr { buf, out }) ==
		    Buffer_error::EXCEEDED)
		{
			warning("sponge_supervisor: report '", template_name,
			        "' (", out, " bytes) exceeds the report buffer - "
			        "NOT publishing (previous config stays in effect)");
			return false;
		}

		log("sponge_supervisor: published <", template_name, "> kill=",
		    _kill_version, " (", out, " bytes)");
		return true;
	}

	void _publish_nominal()
	{
		_publish(*_system_reporter, "system");
	}

	/* ---- observation ---------------------------------------------- */

	void _scan_state()
	{
		if (!_state.valid())
			return;

		_state_seen = true;
		_state_at   = _ticks;

		_max_skipped = 0;
		_exited      = 0;

		/*
		 * The 26.08 state report is HID-serialised - the same lesson
		 * sponge_pkgd (runtime_state) and sponge-de (C: readout)
		 * recorded: an Xml_node read silently yields zero children.
		 */
		Hid_node const root(Const_byte_range_ptr {
			_state.local_addr<char const>(), _state.size() });

		root.for_each_sub_node([&] (Hid_node const &n) {

			if (!n.has_type("child"))
				return;

			unsigned const skipped = n.attribute_value("skipped_heartbeats", 0u);
			bool     const gone    = n.has_attribute("exited");

			if (skipped > _max_skipped) _max_skipped = skipped;
			if (gone)                   _exited++;
		});
	}

	void _handle_state()
	{
		_state.update();
		_scan_state();
	}

	bool _faulted() const
	{
		if (_max_skipped >= _fault_skipped)
			return true;

		if (_exited > _exited_baseline)
			return true;

		/*
		 * Staleness is NOT a fault while the sandbox has reported at
		 * least once - and the control run is why
		 * (run/sponge-supervisor-takeover.run with
		 * SPONGE_TAKE_NO_FAULT=1 produced exactly this false positive
		 * at state_age_ms=10250 on a perfectly healthy system): a Genode
		 * state report is EVENT-DRIVEN. It is emitted when a child
		 * starts, exits, changes session state or changes its skipped
		 * heartbeat count, and at no other time. A quiet sandbox is
		 * therefore indistinguishable from a dead one by report age
		 * alone, and treating it as a fault makes the supervisor restart
		 * the desktop at random intervals.
		 *
		 * The one staleness statement that IS sound is the boot-time one:
		 * a sandbox that has published nothing at all since it was
		 * created is not publishing, and no amount of waiting changes
		 * that. A sandbox whose init wedges LATER remains unobservable
		 * this way in v1 - documented as the residual gap in
		 * docs/17-runtime-component-topology.md §5.
		 */
		return !_state_seen && (_ticks > _boot_grace_ticks);
	}

	void _log_fault() const
	{
		log("sponge_supervisor: FAULT max_skipped=", _max_skipped,
		    " exited=",      _exited,
		    " state_seen=",  _state_seen,
		    " state_age_ms=",
		    _state_seen ? (_ticks - _state_at) * _poll_ms : 0);
	}

	unsigned to_idle_ticks() const
	{
		unsigned const poll = _poll_ms ? _poll_ms : 250;
		return (10000 + poll - 1) / poll;
	}

	void _enter(Phase phase, char const *why)
	{
		_phase       = phase;
		_phase_since = _ticks;
		log("sponge_supervisor: phase=", (int)phase, " (", why, ")");
	}

	/* ---- state machine --------------------------------------------- */

	void _handle_tick()
	{
		_ticks++;

		bool const fault = _faulted();

		switch (_phase) {

		case Phase::BOOT:
			/* BOOT is never treated as a fault: the supervised subtree
			 * is not up yet, so "no state report" is expected. The
			 * console starts disarmed - the report is written before the
			 * console has asked for it, so its first sight of the world
			 * is "stay out of the way". */
			_construct_reporters();
			_arm_console(false);
			_publish_nominal();
			_enter(Phase::NOMINAL, "initial publish");
			break;

		case Phase::NOMINAL:

			if (!fault) {

				/*
				 * Periodic liveness line. It is not decoration: on
				 * real hardware there is no serial console, so this
				 * is the only in-guest evidence that the supervisor is
				 * alive AND that it considers the system healthy - the
				 * pair is what distinguishes "supervisor working,
				 * nothing wrong" from "supervisor dead" after the fact.
				 */
				if (_ticks - _last_idle >= to_idle_ticks()) {
					_last_idle = _ticks;
					log("sponge_supervisor: nominal — subtree healthy,",
					    " no action (", _ticks * _poll_ms / 1000, "s)");
				}
				break;
			}

			_log_fault();

			/* Kill: bump @KILL@ on every <start> node of the supervised
			 * config. The wedged children are abandoned and respawned
			 * from the same template. */
			_publish(*_system_reporter, "system");
			_ever_healthy = false;
			_enter(Phase::CONTAIN, "fault detected, subtree killed");
			break;

		case Phase::CONTAIN:

			if (!fault) {

				/*
				 * A respawned subtree is briefly healthy before a
				 * persistent fault has had time to wedge again -
				 * skipped_heartbeats needs several heartbeat periods
				 * to climb back over the threshold. Accepting the
				 * FIRST healthy report here would therefore report
				 * "contained" for a fault that is about to come
				 * back, and the console phase would never be
				 * reached. The subtree must therefore stay healthy
				 * for a whole settle window before the kill counts
				 * as having worked.
				 */
				if (!_ever_healthy) {
					_ever_healthy = true;
					_healthy_since = _ticks;
				}

				if ((_ticks - _healthy_since) < _settle_ticks)
					break;

				/* healthy for a whole settle window: re-baseline the
				 * exited count, so the deaths that were part of THIS
				 * fault do not count against the next one */
				_exited_baseline = _exited;

				log("sponge_supervisor: contained - subtree healthy for ",
				    (_ticks - _healthy_since) * _poll_ms,
				    "ms after the kill, no console needed");
				_enter(Phase::NOMINAL, "contained");
				break;
			}

			_ever_healthy = false;

			if ((_ticks - _phase_since) < _grace_ticks)
				break;

			_log_fault();

			/*
			 * Ordered takeover. The wedged GUI stack is already
			 * destroyed (CONTAIN rewrote every <start> version); now
			 * the recovery sandbox gets the console, and the supervised
			 * config is swapped for its desktop-less variant so the
			 * console is the only Gui client. Two reports, one
			 * direction: the console starts, the desktop stops.
			 */
			_publish(*_system_reporter, "system_console");

			/* The wedged stack is already gone at this point (the CONTAIN
			 * rewrite above destroyed it); arming the console is the
			 * second half of the ordered takeover. */
			_arm_console(true);

			_enter(Phase::CONSOLE,
			       "unrecoverable - console owns the display");
			break;

		case Phase::CONSOLE:

			if (fault) {

				_ever_healthy = false;

				if ((_ticks - _phase_since) < _console_ticks)
					break;

				log("sponge_supervisor: console hold elapsed, still faulted");

				_arm_console(false);
				log("sponge_supervisor: CONSOLE RELEASED — display handed "
				    "back, waiting for the console session to close");
				_enter(Phase::RELEASE, "console hold elapsed");
				break;
			}

			/*
			 * The subtree healed underneath the console. The same settle
			 * discipline as CONTAIN applies: a rewritten subtree is
			 * briefly healthy before a persistent fault has had time to
			 * wedge again, so handing the display back on the FIRST
			 * healthy report would shorten every console hold to nothing.
			 */
			if (!_ever_healthy) {
				_ever_healthy = true;
				_healthy_since = _ticks;
			}

			if ((_ticks - _healthy_since) < _settle_ticks)
				break;

			log("sponge_supervisor: subtree healed under the console");

			_arm_console(false);
			log("sponge_supervisor: CONSOLE RELEASED — display handed "
			    "back, waiting for the console session to close");
			_enter(Phase::RELEASE, "console disarmed");
			break;

		case Phase::RELEASE:

			if ((_ticks - _phase_since) < _settle_ticks)
				break;

			_publish_nominal();
			_enter(Phase::NOMINAL, "desktop restored");
			break;
		}
	}

	Main(Env &env) : _env(env)
	{
		_read_policy();

		/* prime the report and arm the change signal */
		_state.sigh(_state_sigh);
		_handle_state();

		/*
		 * BOOT handling runs on the first tick, not here: the Report
		 * sessions must not be requested from the constructor, because
		 * report_rom announces its service from its own constructor and
		 * a denied session is fatal (see the reporter comment). The
		 * supervised sandbox meanwhile receives an EMPTY but VALID
		 * config ROM from report_rom - which init applies as "no
		 * children" - so it is alive and waiting when the real config
		 * lands.
		 */
		_timer.sigh(_tick);
		_timer.trigger_periodic(_poll_ms * 1000);

		log("sponge_supervisor: armed poll=", _poll_ms,
		    "ms fault_skipped=", _fault_skipped,
		    " grace=",          _grace_ms,
		    "ms console_hold=", _console_hold_ms,
		    "ms settle=",       _settle_ms,
		    "ms boot_grace=",   _boot_grace_ms, "ms");
	}
};

void Component::construct(Genode::Env &env)
{
	static Sponge::Supervisor::Main main { env };
}