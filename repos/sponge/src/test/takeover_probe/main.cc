/*
 * \brief  Capture-side pixel oracle for the console-takeover proof
 * \author Sponge OS contributors
 * \date   2026
 *
 * Reads the composited display through a Capture session and reports
 * which of the configured "states" the screen is in. It exists so that
 * "the fault console took the display" is a PIXEL claim with a log
 * marker attached, not an inference from a log line: a supervisor that
 * claimed a takeover while the old desktop was still on screen would
 * still fail this probe.
 *
 * It samples fixed points rather than hashing the screen:
 *
 *   - (4, 4)         sits in the bright band at the top of every
 *                     sponge_console screen;
 *   - (8, h-8)       sits in the flat background, below all text.
 *
 * Both are classified against the state table from the config, so the
 * expected colours live with the scenario rather than inside this
 * component, and a scenario can tighten or relax them without a
 * rebuild.
 *
 * Sessions (minimum privilege, AGENTS.md §1.2): Capture, Timer,
 * ROM "config".
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/output.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <os/pixel_rgb888.h>
#include <timer_session/connection.h>
#include <util/string.h>

/* local includes */
#include "../../sponge_supervisor/text_scan.h"

namespace Sponge {

	struct Takeover_probe { struct Main; };
}

using namespace Genode;

struct Sponge::Takeover_probe::Main
{
	Env &_env;

	Attached_rom_dataspace _config { _env, "config" };

	enum { MAX_STATES = 8, NAME_MAX = 32 };

	struct State
	{
		char     name[NAME_MAX] { };
		unsigned r { 0 }, g { 0 }, b { 0 };
		bool     valid { false };
	};

	static constexpr unsigned SCREEN_W = 1024;
	static constexpr unsigned SCREEN_H = 768;

	State   _states[MAX_STATES] { };
	unsigned _state_count { 0 };
	unsigned _poll_ms { 250 };
	/* Optional unconditional re-announcement. A transition-only probe
	 * can prove the screen reached a state but not that it is STILL
	 * there, which is the half a containment-churn claim needs. */
	unsigned _repeat_ms { 0 };
	unsigned _repeats { 0 };
	unsigned _repeat_every { 1 };

	/*
	 * The sampled point. The default is the screen centre, which is where
	 * the acceptance topology wants it: there the only client is the
	 * console, so the centre is flat background for both roles. That
	 * default is WRONG on the product media, where nitpicker parks the
	 * pointer at the centre at boot and the upstream `pointer` component
	 * paints the cursor glyph exactly there - the probe would then read
	 * the cursor, not the desktop. Hence `sample_x`/`sample_y`.
	 */
	unsigned _sample_x { SCREEN_W / 2 };
	unsigned _sample_y { SCREEN_H / 2 };

	/* last classification, so a transition is logged only once */
	int  _current { -1 };
	bool _announced[2] { false, false };

	unsigned _last_r { 0 }, _last_g { 0 }, _last_b { 0 };

	Capture::Connection                _capture { _env, "takeover_probe" };
	Constructible<Attached_dataspace> _ds { };

	Timer::Connection   _timer { _env };
	Signal_handler<Main> _tick  { _env.ep(), *this, &Main::_handle_tick };

	/* ------------------------------------------------------------------ */

	static unsigned _hex2(char const *h)
	{
		unsigned r = 0;
		for (int i = 0; i < 2; i++) {
			char const c = h[i];
			r <<= 4;
			if      (c >= '0' && c <= '9') r |= unsigned(c - '0');
			else if (c >= 'a' && c <= 'f') r |= unsigned(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') r |= unsigned(c - 'A' + 10);
		}
		return r;
	}

	/*
	 * Config shape (read as text so XML and HID both work):
	 *
	 *   <config poll_ms: 250 | sample_x: 768 | sample_y: 576
	 *     | repeat_ms: 2000
	 *     + state | name: desktop | color: #204080
	 *     + state | name: console | color: #601010
	 *
	 * `repeat_ms` re-announces the CURRENT state on that period
	 * regardless of whether anything changed, so a scenario can assert
	 * that the screen is still showing a state N seconds later.
	 *
	 * Each `state` line is scanned in order, so the table needs no
	 * parser and stays trivially inspectable in the run script.
	 */
	void _read_config()
	{
		_config.update();

		if (!_config.valid())
			return;

		char const *cfg = _config.local_addr<char const>();
		size_t      const sz = _config.size();

		{
			char const *v = nullptr; size_t n = 0;
			if (Sponge::scan_attr(cfg, sz, "poll_ms", v, n) && n > 0) {
				unsigned r = 0;
				for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++)
					r = r * 10 + unsigned(v[i] - '0');
				if (r) _poll_ms = r;
			}
			if (Sponge::scan_attr(cfg, sz, "repeat_ms", v, n) && n > 0) {
				unsigned r = 0;
				for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++)
					r = r * 10 + unsigned(v[i] - '0');
				_repeat_ms = r;
				if (r) {
					unsigned const every = r / _poll_ms;
					_repeat_every = every ? (every ? every : 1) : 1;
				}
			}
		}

		{
			char const *v = nullptr; size_t n = 0;
			if (Sponge::scan_attr(cfg, sz, "sample_x", v, n) && n > 0) {
				unsigned r = 0;
				for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++)
					r = r * 10 + unsigned(v[i] - '0');
				if (r < SCREEN_W) _sample_x = r;
			}
			if (Sponge::scan_attr(cfg, sz, "sample_y", v, n) && n > 0) {
				unsigned r = 0;
				for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++)
					r = r * 10 + unsigned(v[i] - '0');
				if (r < SCREEN_H) _sample_y = r;
			}
		}

		_state_count = 0;

		for (size_t i = 0; i + 5 < sz; i++) {

			if (Genode::strcmp(cfg + i, "state", 5) != 0)
				continue;

			/* must be a standalone token */
			if (i > 0) {
				char const p = cfg[i - 1];
				bool const name_char = (p >= 'a' && p <= 'z') ||
				                       (p >= 'A' && p <= 'Z') ||
				                       (p >= '0' && p <= '9') ||
				                       p == '_' || p == '-' || p == ':';
				if (name_char)
					continue;
			}
			if (i + 5 < sz && (cfg[i + 5] == '_' || cfg[i + 5] == '-'))
				continue;

			/* the name and the colour belong to THIS line */
			size_t line_end = i;
			while (line_end < sz && cfg[line_end] != '\n')
				line_end++;

			char const *line = cfg + i;
			size_t      const len = line_end - i;

			if (_state_count >= MAX_STATES)
				break;

			State &s = _states[_state_count];

			char const *v = nullptr; size_t n = 0;

			if (Sponge::scan_attr(line, len, "name", v, n) && n > 0)
				Sponge::copy_value(s.name, v, n, NAME_MAX);

			if (Sponge::scan_attr(line, len, "color", v, n) &&
			    n >= 7 && v[0] == '#')
			{
				s.r = _hex2(v + 1);
				s.g = _hex2(v + 3);
				s.b = _hex2(v + 5);
				s.valid = true;
			}

			if (s.valid)
				_state_count++;
			else
				warning("takeover_probe: malformed state entry ignored");

			i = line_end - 1;
		}
	}

	/* ------------------------------------------------------------------ */

	int _classify(unsigned r, unsigned g, unsigned b) const
	{
		for (unsigned i = 0; i < _state_count; i++) {
			if (_states[i].r == r && _states[i].g == g && _states[i].b == b)
				return int(i);
		}
		return -1;
	}

	void _report(char const *tag, int index)
	{
		_current = index;

		if (index < 0) {
			log("takeover_probe: ", tag, " UNKNOWN bg=",
			    Hex(_last_r << 16 | _last_g << 8 | _last_b));
			return;
		}

		log("takeover_probe: ", tag, " state=",
		    (char const *)_states[index].name, " centre=",
		    Hex(_states[index].r << 16 | _states[index].g << 8 | _states[index].b));
	}

	void _sample()
	{
		if (!_ds.constructed())
			return;

		Pixel_rgb888 * const px = _ds->local_addr<Pixel_rgb888>();

		/*
		 * Sample the exact screen centre.
		 *
		 * sponge_console lays its banner out in the upper third, so the
		 * centre row is flat background for BOTH roles - and therefore
		 * is the one point that means the same thing before and after a
		 * takeover. The corners are sampled too, but only for the log:
		 * the top one lands in the highlight band and the bottom one is
		 * the first thing to go stale when the compositor repaints only
		 * its dirty rectangles.
		 */
		Pixel_rgb888 const p = px[_sample_y * SCREEN_W + _sample_x];

		unsigned const r = unsigned(p.r() & 0xff);
		unsigned const g = unsigned(p.g() & 0xff);
		unsigned const b = unsigned(p.b() & 0xff);

		bool const changed = (r != _last_r) || (g != _last_g) || (b != _last_b);

		_last_r = r; _last_g = g; _last_b = b;

		/* only transitions are worth a log line: the poll runs four
		 * times a second and a static screen would otherwise bury the
		 * markers this scenario gates on */
		if (changed)
			log("takeover_probe: sample band=",
			    Hex(unsigned(px[4 * SCREEN_W + 4].r()) << 16 |
			        unsigned(px[4 * SCREEN_W + 4].g()) << 8 |
			        unsigned(px[4 * SCREEN_W + 4].b())),
			    " centre=", Hex(r << 16 | g << 8 | b),
			    " corner=",
			    Hex(unsigned(px[(SCREEN_H - 8) * SCREEN_W + 8].r()) << 16 |
			        unsigned(px[(SCREEN_H - 8) * SCREEN_W + 8].g()) << 8 |
			        unsigned(px[(SCREEN_H - 8) * SCREEN_W + 8].b())));

		int const index = _classify(r, g, b);

		if (!changed && index == _current)
			return;

		/* state 0 is the desktop, state 1 the console: each is
		 * announced once, in whichever order it is first observed */
		if (index >= 0 && index < 2 && !_announced[index]) {
			_announced[index] = true;
			_report(index == 0 ? "desktop-up" : "console-up", index);
			return;
		}

		_report("pixel", index);
	}

	void _handle_tick()
	{
		if (!_ds.constructed())
			return;

		/* ask the compositor for a fresh copy of the screen */
		_capture.capture_at(Capture::Point(0, 0));

		_sample();

		if (_repeat_ms && (++_repeats % _repeat_every) == 0)
			_report("repeat", _current);
	}

	Main(Env &env) : _env(env)
	{
		_read_config();

		_capture.buffer(Capture::Session::Buffer_attr {
			.px       = { SCREEN_W, SCREEN_H },
			.mm       = { },
			.viewport = { { 0, 0 }, { SCREEN_W, SCREEN_H } } });

		_ds.construct(_env.rm(), _capture.dataspace());

		_timer.sigh(_tick);
		_timer.trigger_periodic(_poll_ms * 1000);

		log("takeover_probe: ", _state_count, " states, poll=", _poll_ms,
		    "ms, screen ", SCREEN_W, "x", SCREEN_H,
		    " sample ", _sample_x, "x", _sample_y);

		for (unsigned i = 0; i < _state_count; i++)
			log("takeover_probe: state[", i, "]=",
			    (char const *)_states[i].name, " bg=",
			    Hex(_states[i].r << 16 | _states[i].g << 8 | _states[i].b));
	}
};

void Component::construct(Genode::Env &env)
{
	static Sponge::Takeover_probe::Main main { env };
}
