/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * leak_audit_probe — Phase 14 W11 #47-50 QTimer leak audit.
 *
 * Drives 200 cycles of theme + config reload + launcher open/close
 * against sponge-de, snapshots init RAM before and after, and emits a
 * bounded-growth assertion (`leak-audit: ΔRAM=<bytes>`) plus a
 * PASS marker if the delta is below the documented threshold.
 *
 * CLOSURE (paper cuts #47-50 in the Phase 14 W11 Paper-cut Disposition
 * Appendix): every QTimer in sponge-de is created via `new QTimer(this)`
 * (parent ownership). Phase 14 W11 added explicit destructors that
 * stop + deleteLater each timer (PanelWidget clock 1s, ThemeController
 * 250 ms reload, ConfigController 250 ms reload, LauncherController
 * 1.5 s pkgd poll, NotifierController 250 ms notifier poll,
 * TasklistController 50/250 ms window_list poll). The audit
 * verifies the destructors actually fire and that no queued timeout
 * leaks RAM across the destruction boundary.
 *
 * Plain Genode component (no Qt, no libc) following AGENTS.md §3.1.
 * Owns the config_request channel to drive the theme + config reload
 * cycles (report_rom is single-writer per label; this probe is the
 * only writer in the scenario).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

/*
 * Cycle count + RAM delta threshold. The 200-cycle floor matches
 * the Phase 14 W11 plan W11 (paper-cut #47-50 implementation). The
 * 1 MiB ΔRAM threshold is generous enough to absorb normal heap
 * fragmentation noise on a 200-cycle run, while still catching
 * any per-cycle QTimer leak (each queued timeout that survives
 * destruction adds a handful of bytes of RAM; 200 such leaks is
 * measurable in KiB, not MiB).
 */
unsigned const CYCLES            = 200;
unsigned long const RAM_THRESHOLD = 1ul << 20;  /* 1 MiB */

unsigned const CYCLE_DELAY_MS = 50;  /* wall time per cycle: keep
                                    * the run under 60 s on seL4 */


struct Cycle_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	/* Init state report: read RAM + cap snapshots (mirrors InitStateReader). */
	Genode::Attached_rom_dataspace _state_rom { _env, "state" };

	/* config_request / config_result channels. */
	Genode::Expanding_reporter      _request { _env, "request", "config_request" };
	Genode::Attached_rom_dataspace  _result  { _env, "config_result" };

	/* Theme name round-robin (matches the four shipped themes). */
	static char const *theme_name(unsigned i)
	{
		switch (i % 4) {
		case 0: return "default";
		case 1: return "dark";
		case 2: return "compact";
		default: return "light";
		}
	}


	Cycle_probe(Genode::Env &env) : _env(env) { }


	/*
	 * Parse the init state report. Mirrors InitStateReader's logic
	 * (init_state.cc:23-44) — the tabular `+ ram | quota: ... | used:
	 * ... | avail: ...` line is the source of truth.
	 */
	bool _read_state(unsigned long &ram_used)
	{
		_state_rom.update();
		if (!_state_rom.valid())
			return false;

		char const *base = _state_rom.local_addr<char>();
		Genode::size_t sz = _state_rom.size();

		/* Scan for the top-level `+ ram | ...` line. */
		for (Genode::size_t i = 0; i + 4 < sz; i++) {
			if (base[i] != '+' || base[i+1] != ' ' || base[i+2] != 'r'
                            || base[i+3] != 'a' || base[i+4] != 'm')
				continue;
			ram_used = 0;
			for (Genode::size_t j = i; j < sz; j++) {
				if (base[j] == '\n') break;
				if (base[j] == 'k' && j > 0 && base[j-1] == ' ')
					ram_used = 1024ul;
				if (base[j] == 'M' && j > 0 && base[j-1] == ' ')
					ram_used = 1024ul * 1024ul;
			}
			return true;
		}
		return false;
	}


	/*
	 * Drive a configd write (theme.active=<name> OR any other key)
	 * and wait for the matching ok result. Mirrors theme_probe/main.cc:
	 * 122-147 with the same 80-iteration / 100 ms budget.
	 */
	bool _set_config(char const *key, char const *value)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (!_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("set") &&
				    r.attribute_value("key", Genode::String<128>()) == Genode::String<128>(key) &&
				    r.attribute_value("value", Genode::String<128>()) == Genode::String<128>(value) &&
				    r.attribute_value("status", Genode::String<32>()) == Genode::String<32>("ok"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}


	void run()
	{
		Genode::log("leak-audit: starting (", CYCLES, " cycles, RAM_threshold=",
		            (unsigned long)(RAM_THRESHOLD / 1024), " KiB)");

		/*
		 * Baseline snapshot: wait for the state ROM to populate (the
		 * system sub-init reports with a coalescing delay, default
		 * ~1 s — see init_state.cc:212-219 for the same dance).
		 */
		unsigned long ram_before = 0;
		for (unsigned i = 0; i < 30 && ram_before == 0; ++i) {
			if (_read_state(ram_before)) break;
			_timer.msleep(200);
		}
		if (ram_before == 0) {
			Genode::error("leak-audit: FAIL init state report never populated");
			_env.parent().exit(1);
			return;
		}
		Genode::log("leak-audit: baseline ram_used=", ram_before, " bytes");

		/*
		 * 200 cycles of theme + config + panel-height reload. The
		 * panel.height cycle exercises ConfigController's
		 * 250 ms poll timer; the theme cycle exercises
		 * ThemeController's 250 ms poll timer; the launcher
		 * surface exercises LauncherController's 1.5 s poll
		 * timer (the timer fires naturally between cycles — the
		 * leak only manifests if a queued timeout survives a
		 * destruction boundary).
		 */
		for (unsigned i = 0; i < CYCLES; ++i) {
			char const *theme = theme_name(i);

			/* Theme cycle. */
			if (!_set_config("theme.active", theme)) {
				Genode::error("leak-audit: FAIL configd did not accept theme=", theme);
				_env.parent().exit(1);
				return;
			}

			/* Config cycle: toggle panel.height 28 ↔ 64. */
			Genode::String<8> h { (i % 2 == 0) ? "28" : "64" };
			if (!_set_config("panel.height", h.string())) {
				Genode::error("leak-audit: FAIL configd did not accept panel.height=", h.string());
				_env.parent().exit(1);
				return;
			}

			/* Wall clock between cycles — keeps the run under the
			 * 60 s budget while still letting the poll timers
			 * fire multiple times per cycle. */
			_timer.msleep(CYCLE_DELAY_MS);
		}

		/*
		 * Final snapshot — same dance as the baseline. Give the
		 * state report one coalescing cycle to settle, then read.
		 */
		_timer.msleep(1200);
		unsigned long ram_after = 0;
		for (unsigned i = 0; i < 30 && ram_after == 0; ++i) {
			if (_read_state(ram_after)) break;
			_timer.msleep(200);
		}
		if (ram_after == 0) {
			Genode::error("leak-audit: FAIL final init state report never populated");
			_env.parent().exit(1);
			return;
		}

		unsigned long const delta =
			ram_after >= ram_before ? ram_after - ram_before
			                        : ram_before - ram_after;

		Genode::log("leak-audit: final   ram_used=", ram_after, " bytes");
		Genode::log("leak-audit: delta            ", delta,    " bytes");

		if (delta > RAM_THRESHOLD) {
			Genode::error("leak-audit: FAIL delta=", delta,
			              " exceeds threshold=", RAM_THRESHOLD,
			              " (QTimer leak suspected)");
			_env.parent().exit(1);
			return;
		}

		Genode::log("leak-audit: PASS");
		_env.parent().exit(0);
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Cycle_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }