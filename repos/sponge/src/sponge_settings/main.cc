/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_settings — on-demand Settings control-center shell
 * (Criterion 18).
 *
 * Discovery (docs/12-package-format.md §14.4): reads the "installed"
 * metadata ROM published by sponge_pkgd and refreshes on that ROM's
 * update signal — no polling, no per-package code. Entries are
 * grouped by the closed category enum (other fallback) and rendered
 * with title / icon-if-present / keywords.
 *
 * Activation (docs/12 §14.5, §14.7): clicking an entry sends only the
 * identity '(package, settings-id)' on this shell's OWN single-writer
 * settings launch labels (report "settings_request", ROM
 * "settings_result") and waits boundedly for exactly one result:
 * ok action="started" / action="focused", "not-installed",
 * "binary-missing", "insufficient-resources", or the shell-derived
 * "package-manager-unavailable". The shell never resolves target
 * binaries, never holds a package File_system capability, never
 * writes configuration, and never loads provider code — a provider is
 * a separate component in its own address space (AGENTS.md §1.2).
 *
 * Lifecycle: on-demand and stateless w.r.t. providers. It keeps no
 * provider window handles and accumulates no start state, so closing
 * it reclaims everything it holds and a later re-open cannot
 * duplicate provider windows (singleton + focus are pkgd's §14.7
 * contract: a second open answers ok action="focused"). Closing the
 * shell window (the v1.1 app-visible close channel, docs/05
 * §4.6) exits the component.
 *
 * Capability surface (docs/05-sponge-de.md §5.4, verbatim-minimal):
 * ROM "installed", Report "settings_request", ROM "settings_result",
 * Gui, Timer. No configd channel, no File_system, no GL, no plugins.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <nitpicker_gfx/tff_font.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/xml_generator.h>

#include "model.h"
#include "view.h"

extern "C" char _binary_font_tff_start[];

namespace Sponge::Settings {

namespace {

enum : unsigned {
	DEFAULT_W       = 440,
	DEFAULT_H       = 560,
	RESULT_TIMEOUT_US = 6 * 1000 * 1000
};

}

struct Main
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	/*
	 * Discovery input: the installed metadata broadcast (§14.4).
	 * Eager — it is the contract surface; the update signal is the
	 * only refresh trigger.
	 */
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	/*
	 * Settings launch channel: opened only when <settings_launch
	 * source="pkgd"/> opts in. List-only topologies still render the
	 * cached listing and answer opens with "package-manager-unavailable"
	 * instead of attempting a denied session (§14.7).
	 */
	Genode::Constructible<Genode::Expanding_reporter>     _launch_request { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _launch_result  { };

	bool _launch_gate_checked { false };
	bool _launch_gate_enabled { false };

	Model _model { };

	Gui::Connection _gui { _env, "sponge_settings" };
	Gui::View_id const _view_id { 1 };
	Genode::Constructible<Genode::Attached_dataspace> _fb_ds { };
	Gui::Area _area { DEFAULT_W, DEFAULT_H };

	Tff_font::Static_glyph_buffer<2048> _glyphs { };
	Tff_font _font { _binary_font_tff_start, _glyphs };

	int _pointer_x { 0 }, _pointer_y { 0 };
	int _scroll { 0 };
	int _highlight { -1 };

	bool _pending { false };
	Genode::String<64> _pending_pkg { };
	Genode::String<33> _pending_id  { };

	Genode::String<128> _status { "Select an entry to open its settings" };

	/* last discovery counters shown in the log (bounded diagnostics) */
	unsigned _logged_invalid   { ~0U };
	unsigned _logged_duplicate { ~0U };
	unsigned _logged_overflow  { ~0U };
	unsigned _logged_fallbacks { ~0U };

	Genode::Signal_handler<Main> _installed_handler {
		_env.ep(), *this, &Main::_handle_installed };

	Genode::Signal_handler<Main> _input_handler {
		_env.ep(), *this, &Main::_handle_input };

	Genode::Signal_handler<Main> _info_handler {
		_env.ep(), *this, &Main::_handle_info };

	Genode::Signal_handler<Main> _result_handler {
		_env.ep(), *this, &Main::_handle_result };

	Genode::Signal_handler<Main> _timeout_handler {
		_env.ep(), *this, &Main::_handle_timeout };


	Main(Genode::Env &env) : _env(env)
	{
		_gui.buffer({ .area = _area, .alpha = false });
		_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());

		_gui.view(_view_id, { .title = "Sponge Settings",
		                      .rect  = Gui::Rect(Gui::Point(0, 0), _area),
		                      .front = true });
		_gui.execute();

		_installed.sigh(_installed_handler);
		_gui.input.sigh(_input_handler);
		_gui.info_sigh(_info_handler);
		_timer.sigh(_timeout_handler);

		_handle_installed();
	}


	int _list_h() const
	{
		return (int)_area.h - Layout::STATUS_H > 0
		     ? (int)_area.h - Layout::STATUS_H : 0;
	}


	void _paint()
	{
		if (!_fb_ds.constructed())
			return;

		Genode::Surface<Genode::Pixel_rgb888> surface(
			_fb_ds->local_addr<Genode::Pixel_rgb888>(), _area);

		paint_listing(surface, _font, _model, _scroll, _status.string(),
		              _highlight);

		_gui.framebuffer.refresh(Gui::Rect(Gui::Point(0, 0), _area));
	}


	void _handle_installed()
	{
		_installed.update();

		if (_installed.valid())
			_model.parse(_installed.node());

		_scroll = Layout::clamp_scroll(_model, _scroll, _list_h());

		if (_model.dropped_invalid   != _logged_invalid ||
		    _model.dropped_duplicate != _logged_duplicate ||
		    _model.dropped_overflow  != _logged_overflow ||
		    _model.category_fallbacks != _logged_fallbacks) {

			_logged_invalid   = _model.dropped_invalid;
			_logged_duplicate = _model.dropped_duplicate;
			_logged_overflow  = _model.dropped_overflow;
			_logged_fallbacks = _model.category_fallbacks;

			Genode::log("sponge_settings: ", _model.num_entries,
			            " entries (dropped: ", _model.dropped_invalid,
			            " invalid, ", _model.dropped_duplicate,
			            " duplicate, ", _model.dropped_overflow,
			            " overflow; ", _model.category_fallbacks,
			            " category fallbacks)");
		}

		_paint();
	}


	bool _open_channels()
	{
		if (!_launch_gate_checked) {
			_launch_gate_checked = true;
			Genode::Attached_rom_dataspace const config(_env, "config");
			config.node().with_optional_sub_node("settings_launch",
				[&] (Genode::Node const &n) {
					_launch_gate_enabled =
						n.attribute_value("source", Genode::String<16>())
						== "pkgd";
				});
		}

		if (!_launch_gate_enabled)
			return false;

		if (_launch_request.constructed() && _launch_result.constructed())
			return true;

		try {
			if (!_launch_request.constructed())
				_launch_request.construct(_env, "request", "settings_request");

			if (!_launch_result.constructed()) {
				_launch_result.construct(_env, "settings_result");
				_launch_result->sigh(_result_handler);
			}
		}
		catch (Genode::Exception &) {
			_launch_request.destruct();
			_launch_result.destruct();
			return false;
		}

		return _launch_request.constructed() && _launch_result.constructed();
	}


	void _activate(Entry const &entry)
	{
		int const index = (int)(&entry - _model.entries);

		/* one open request in flight per entry: a re-click is a
		 * de-duplicated no-op, never a second start (§14.7) */
		if (_pending && _pending_pkg == entry.pkg && _pending_id == entry.id)
			return;

		if (!_open_channels()) {
			_status = status_text(Launch_status::PACKAGE_MANAGER_UNAVAILABLE);
			_paint();
			return;
		}

		_pending     = true;
		_pending_pkg = entry.pkg;
		_pending_id  = entry.id;
		_highlight   = index;
		_status      = Genode::String<128>("Opening ", entry.title, " ...");

		_launch_request->generate_xml([&] (Genode::Xml_generator &g) {
			gen_settings_request(g, entry.pkg.string(), entry.id.string());
		});

		Genode::log("sponge_settings: open ", entry.pkg, "/", entry.id);

		_timer.trigger_once(RESULT_TIMEOUT_US);
		_paint();
	}


	void _handle_input()
	{
		bool repaint = false;

		_gui.input.for_each_event([&] (Input::Event const &ev) {

			ev.handle_absolute_motion([&] (int x, int y) {
				_pointer_x = x;
				_pointer_y = y;
			});

			ev.handle_wheel([&] (int, int y) {
				_scroll = Layout::clamp_scroll(
					_model, _scroll - y * Layout::SCROLL_STEP, _list_h());
				repaint = true;
			});

			if (ev.key_press(Input::BTN_LEFT)) {

				int const index = Layout::hit_entry(
					_model, _scroll, _list_h(), _pointer_y);

				if (index >= 0) {
					_activate(_model.entries[index]);
					return;
				}
			}
		});

		if (repaint)
			_paint();
	}


	/*
	 * v1.1 app-visible resize/close channel (docs/05 §4.6): the Gui
	 * info ROM carries the client rect and the sticky close as
	 * <capture closed="yes"/>. Close exits the component so every
	 * session and byte it holds is reclaimed at once.
	 */
	void _handle_info()
	{
		bool closed = false;
		long new_w = (long)_area.w, new_h = (long)_area.h;

		_gui.with_info_node([&] (Genode::Node const &info) {
			info.for_each_sub_node("capture", [&] (Genode::Node const &capture) {
				if (capture.attribute_value("closed", false))
					closed = true;
				else {
					new_w = capture.attribute_value("width",  (long)_area.w);
					new_h = capture.attribute_value("height", (long)_area.h);
				}
			});
		});

		if (closed) {
			Genode::log("sponge_settings: window closed, exiting");
			_env.parent().exit(0);
			return;
		}

		if (new_w <= 0 || new_h <= 0)
			return;

		Gui::Area const area { (unsigned)new_w, (unsigned)new_h };

		if (area.w == _area.w && area.h == _area.h)
			return;

		_area = area;

		_gui.buffer({ .area = _area, .alpha = false });
		_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());
		_gui.enqueue<Gui::Session::Command::Geometry>(
			_view_id, Gui::Rect(Gui::Point(0, 0), _area));
		_gui.execute();

		_scroll = Layout::clamp_scroll(_model, _scroll, _list_h());
		_paint();
	}


	void _finish_pending(Launch_status status)
	{
		_pending   = false;
		_highlight = -1;
		_status    = status_text(status);
		_paint();
	}


	void _handle_result()
	{
		if (!_launch_result.constructed() || !_pending)
			return;

		_launch_result->update();

		if (!_launch_result->valid())
			return;

		/* Node construction swallows malformed documents into an
		 * empty node; 'parse_settings_result' then declines it */
		Launch_result result;

		bool const answered = parse_settings_result(
			_launch_result->node(),
			_pending_pkg.string(), _pending_id.string(), result);

		if (!answered)
			return;

		Genode::log("sponge_settings: open ", _pending_pkg, "/", _pending_id,
		            " -> ", result.raw_status.string(),
		            result.raw_action.string()[0] != '\0'
		                ? result.raw_action.string() : "");

		_finish_pending(result.status);
	}


	void _handle_timeout()
	{
		if (!_pending)
			return;

		Genode::log("sponge_settings: open ", _pending_pkg, "/", _pending_id,
		            " -> package-manager-unavailable (bounded wait elapsed)");

		_finish_pending(Launch_status::PACKAGE_MANAGER_UNAVAILABLE);
	}
};

} /* namespace Sponge::Settings */


void Component::construct(Genode::Env &env)
{
	static Sponge::Settings::Main main { env };
}
