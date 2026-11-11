/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_wm — the Sponge-native window-manager core (Criterion 16,
 * M1: docs/plans/phase16-sponge-wm.md, D16W.1-D16W.3).
 *
 * Gui virtualization + window registry + input routing + decorator
 * association on PUBLIC Genode interfaces only:
 *
 *   - provides ONLY 'Gui' (the run stanza's <provides>): one virtual
 *     session per client window (CONTENT), one for sponge_decorator
 *     (DECORATOR, chrome views), one for sponge_layouter (LAYOUTER,
 *     the chrome-input target).
 *   - consumes nitpicker 'Gui' as the real backing of every session
 *     (pixels pass through; the command layer is virtualized).
 *   - emits the frozen protocol-v1 'window_list' (docs/05-sponge-de.md
 *     §4.6; id == core-assigned monotonic wid) and the v1.1
 *     'focus_state' + 'window_chrome' snapshots (wm_protocol.h).
 *   - consumes the v1.1 'window_request' (inner placement + sticky
 *     close -> app-visible 0x0 resize), 'focus' (steering to the
 *     input/stacking target), and 'decorator_margins' (inner<->outer
 *     frame math).
 *
 * Input routing (v1.1 component behavior): chrome events go to the
 * layouter's input stream, content events to the owning client
 * (translated into the client's coordinate space), keyboard events to
 * the focus-truth window. A layouter 'exclusive' grab routes ALL core
 * input to the layouter so chrome drags keep receiving motion over
 * content. Content presses are focus-truth clicks (update truth +
 * raise); raise/focus on chrome presses happens through the layouter's
 * 'focus' steering.
 *
 * The registry/protocol model is pure and host-tested
 * (wm_registry.{h,cc}, registry_tests.{h,cc}, self_test.cc). This
 * file is the transport: sessions, reports, ROMs, routing.
 */

#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <input/keycodes.h>
#include <os/reporter.h>

#include "wm_registry.h"
#include "wm_root.h"
#include "wm_session.h"

namespace {

using namespace Sponge;
using namespace Sponge::Wm_core;
using namespace Sponge::Wm_protocol;

using Wm_registry::Dirty;
using Wm_registry::Registry;


/*
 * Mouse-button keycodes occupy the 0x100.. range (input/keycodes.h);
 * KEY_UNKNOWN/KEY_MAX sit at the top of that range. Everything below
 * is a keyboard keycode and follows the focus truth, not the pointer.
 */
bool button_code(Input::Keycode key)
{
	unsigned const v = (unsigned)key;
	return v >= 0x100 && v != 0x1fe && v != 0x1ff;
}


bool keyboard_event(Input::Event const &ev)
{
	bool key = false;
	ev.handle_press([&] (Input::Keycode k, Input::Codepoint) {
		key = !button_code(k); });
	ev.handle_release([&] (Input::Keycode k) {
		key = !button_code(k); });
	return key;
}


/* screen -> client coordinate translation (see Wm_ops::input_offset) */
Input::Event translate_event(Input::Event const &ev, int dx, int dy)
{
	Input::Event out = ev;

	ev.handle_absolute_motion([&] (int x, int y) {
		out = Input::Absolute_motion { x + dx, y + dy }; });

	ev.handle_touch([&] (Input::Touch_id id, float x, float y) {
		out = Input::Touch { id, x + (float)dx, y + (float)dy }; });

	return out;
}


struct Main : Wm_ops
{
	Genode::Env &_env;
	Genode::Heap _heap { _env.ram(), _env.rm() };

	Registry _registry { };

	Genode::Expanding_reporter _window_list {
		_env, "window_list", "window_list" };
	Genode::Expanding_reporter _focus_state {
		_env, "focus_state", "focus_state" };
	Genode::Expanding_reporter _window_chrome {
		_env, "window_chrome", "window_chrome" };

	Genode::Attached_rom_dataspace _focus_rom          { _env, "focus" };
	Genode::Attached_rom_dataspace _window_request_rom { _env, "window_request" };
	Genode::Attached_rom_dataspace _margins_rom        { _env, "decorator_margins" };
	Genode::Attached_rom_dataspace _config             { _env, "config" };

	Genode::Signal_handler<Main> _focus_handler {
		_env.ep(), *this, &Main::_handle_focus_update };
	Genode::Signal_handler<Main> _window_request_handler {
		_env.ep(), *this, &Main::_handle_window_request_update };
	Genode::Signal_handler<Main> _margins_handler {
		_env.ep(), *this, &Main::_handle_margins_update };

	/*
	 * The screen area for the client info ROMs is read from the
	 * core's own nitpicker session (upstream wm does the same on its
	 * focus session).
	 */
	Gui::Connection _screen_gui { _env, "sponge-wm" };
	long _screen_w { 0 }, _screen_h { 0 };

	Genode::Signal_handler<Main> _screen_handler {
		_env.ep(), *this, &Main::_handle_screen_update };

	enum { MAX_SESSIONS = 80 };
	Wm_session *_sessions[MAX_SESSIONS] { };

	bool _grab { false };

	Wm_root _root { _env, *this, _heap, _config };

	Main(Main const &) = delete;
	Main &operator= (Main const &) = delete;

	Main(Genode::Env &env) : _env(env)
	{
		_focus_rom.sigh(_focus_handler);
		_window_request_rom.sigh(_window_request_handler);
		_margins_rom.sigh(_margins_handler);
		_screen_gui.info_sigh(_screen_handler);

		_handle_screen_update();
		_handle_margins_update();

		publish(Dirty::WINDOW_LIST | Dirty::CHROME | Dirty::FOCUS);

		_env.parent().announce(_env.ep().manage(_root));

		Genode::log("sponge_wm: core up (window registry, Gui virtualization, "
		            "input routing)");
	}


	/* ---------------- session bookkeeping ---------------- */

	Wm_session *_by_role(Role role, unsigned long wid)
	{
		for (unsigned i = 0; i < MAX_SESSIONS; i++) {
			Wm_session *s = _sessions[i];
			if (!s || s->role() != role)
				continue;
			if (role == Role::CONTENT && s->wid() != wid)
				continue;
			return s;
		}
		return nullptr;
	}

	Wm_session *_content(unsigned long wid)
	{
		return _by_role(Role::CONTENT, wid);
	}

	Wm_session *_layouter()
	{
		return _by_role(Role::LAYOUTER, 0);
	}


	/* ---------------- report snapshots ---------------- */

	void publish(Wm_registry::Dirty d)
	{
		if (Wm_registry::dirty_any(d, Dirty::WINDOW_LIST))
			_window_list.generate_xml([&] (Genode::Xml_generator &g) {
				_registry.gen_window_list(g); });

		if (Wm_registry::dirty_any(d, Dirty::FOCUS))
			_focus_state.generate_xml([&] (Genode::Xml_generator &g) {
				_registry.gen_focus_state(g); });

		if (Wm_registry::dirty_any(d, Dirty::CHROME))
			_window_chrome.generate_xml([&] (Genode::Xml_generator &g) {
				_registry.gen_window_chrome(g); });
	}


	/* ---------------- view placement ---------------- */

	void apply_placement(unsigned long wid)
	{
		Rect const inner = _registry.inner_rect(wid);
		Rect const outer = _registry.outer_rect(wid);

		for (unsigned i = 0; i < MAX_SESSIONS; i++) {
			Wm_session *s = _sessions[i];
			if (!s)
				continue;
			if (s->role() == Role::CONTENT && s->wid() == wid)
				s->place_content(inner);
			if (s->role() == Role::DECORATOR)
				s->place_chrome(wid, outer);
		}
	}


	/* ---------------- protocol inputs ---------------- */

	void _handle_focus_update()
	{
		_focus_rom.update();
		if (!_focus_rom.valid())
			return;

		Focus_target target { };
		if (!parse_focus(_focus_rom.node(), target)) {
			Genode::warning("focus: malformed steering snapshot ignored");
			return;
		}

		publish(_registry.set_focus(target));

		if (target.set)
			raise_window(target.wid);
	}

	void _handle_window_request_update()
	{
		_window_request_rom.update();
		if (!_window_request_rom.valid())
			return;

		/*
		 * Buffer the whole snapshot first: a malformed snapshot is
		 * dropped as a unit (wm_protocol.h parse contract).
		 */
		Window_request entries[Wm_registry::MAX_WINDOWS];
		unsigned n = 0;
		bool overflow = false;

		bool const ok = parse_window_request(_window_request_rom.node(),
			[&] (Window_request const &r) {
				if (n < Wm_registry::MAX_WINDOWS)
					entries[n++] = r;
				else
					overflow = true;
			});

		if (!ok || overflow) {
			Genode::warning("window_request: malformed snapshot dropped");
			return;
		}

		Dirty dirty = Dirty::NONE;
		bool  close_owed[Wm_registry::MAX_WINDOWS];
		unsigned num_close = 0;

		for (unsigned i = 0; i < n; i++) {
			if (entries[i].close) {
				if (_registry.request_close(entries[i].wid))
					close_owed[num_close++] = entries[i].wid;
				continue;
			}
			dirty |= _registry.place(entries[i].wid, entries[i].x,
			                         entries[i].y, entries[i].w, entries[i].h);
		}

		for (unsigned i = 0; i < n; i++)
			if (!entries[i].close)
				apply_placement(entries[i].wid);

		publish(dirty);

		for (unsigned i = 0; i < n; i++) {
			Wm_session *s = _content(entries[i].wid);
			if (s)
				s->trigger_info();
		}
		for (unsigned i = 0; i < num_close; i++) {
			Genode::log("sponge_wm: close requested for window ",
			            close_owed[i], " (app-visible 0x0)");
		}
	}

	void _handle_margins_update()
	{
		_margins_rom.update();
		if (!_margins_rom.valid())
			return;

		Margins m { };
		if (!parse_decorator_margins(_margins_rom.node(), m)) {
			Genode::warning("decorator_margins: malformed snapshot ignored");
			return;
		}

		Dirty const d = _registry.set_margins(m);
		if (Wm_registry::dirty_any(d, Dirty::CHROME)) {
			_registry.for_each_window([&] (Wm_registry::Window const &w) {
				apply_placement(w.wid); });
			publish(d);
		}
	}

	void _handle_screen_update()
	{
		_screen_gui.with_info_node([&] (Genode::Node const &info) {
			Gui::Area const area = Gui::Area::from_node(info);
			_screen_w = (long)area.w;
			_screen_h = (long)area.h;
		});

		for (unsigned i = 0; i < MAX_SESSIONS; i++)
			if (_sessions[i])
				_sessions[i]->trigger_info();
	}


	/* ---------------- Wm_ops ---------------- */

	unsigned long new_window(Genode::Session_label const &label) override
	{
		unsigned long const wid = _registry.create_window(label);

		if (wid)
			publish(Dirty::WINDOW_LIST);
		return wid;
	}

	void client_geometry(unsigned long wid, int x, int y,
	                     long w, long h) override
	{
		publish(_registry.set_client_geometry(wid, x, y, w, h));
		apply_placement(wid);
	}

	void client_title(unsigned long wid, char const *title) override
	{
		publish(_registry.set_title(wid, title));
	}

	void client_alpha(unsigned long wid, bool alpha) override
	{
		publish(_registry.set_has_alpha(wid, alpha));
	}

	void route_input(Input::Event const &ev, Role source_role,
	                 unsigned long source_wid) override
	{
		if (_grab) {
			if (Wm_session *l = _layouter())
				l->submit_input(ev);
			return;
		}

		if (source_role != Role::CONTENT) {
			/*
			 * Chrome input: lossless forwarding to the layouter in
			 * raw screen coordinates (the layouter hit-tests).
			 */
			if (Wm_session *l = _layouter())
				l->submit_input(ev);
			return;
		}

		if (keyboard_event(ev)) {
			/* keyboard follows the focus truth (steering target) */
			Focus_target const t = _registry.focus();
			if (t.set)
				if (Wm_session *s = _content(t.wid))
					s->submit_input(ev);
			return;
		}

		Wm_session *source = _content(source_wid);
		if (!source)
			return;

		int dx = 0, dy = 0;
		input_offset(source_wid, dx, dy);
		source->submit_input(translate_event(ev, dx, dy));

		bool pressed = false;
		ev.handle_press([&] (Input::Keycode k, Input::Codepoint) {
			if (button_code(k))
				pressed = true; });

		if (pressed)
			content_pressed(source_wid);
	}

	void content_pressed(unsigned long wid) override
	{
		publish(_registry.set_focus(Focus_target { true, wid }));
		raise_window(wid);
	}

	void raise_window(unsigned long wid) override
	{
		for (unsigned i = 0; i < MAX_SESSIONS; i++)
			if (_sessions[i] && _sessions[i]->role() == Role::DECORATOR)
				_sessions[i]->raise_chrome(wid);

		if (Wm_session *s = _content(wid))
			s->raise_content();
	}

	void chrome_attach(unsigned long wid) override
	{
		_registry.attach_chrome(wid);
	}

	void chrome_detach(unsigned long wid) override
	{
		_registry.detach_chrome(wid);
	}

	void resizeable_changed(unsigned long wid, bool value) override
	{
		publish(_registry.set_resizeable(wid, value));
	}

	void set_grab(bool enabled) override
	{
		if (_grab != enabled)
			Genode::log("sponge_wm: exclusive input grab ",
			            enabled ? "enabled" : "released");
		_grab = enabled;
	}

	void info_state(unsigned long wid, long &w, long &h,
	                bool &close) const override
	{
		_registry.requested_size(wid, w, h);
		Wm_registry::Window const *win = _registry.find(wid);
		close = win && win->close_requested;
	}

	void screen_area(long &w, long &h) const override
	{
		w = _screen_w;
		h = _screen_h;
	}

	Gui::Session_capability real_cap(Gui::Session_capability cap) const override
	{
		for (unsigned i = 0; i < MAX_SESSIONS; i++)
			if (_sessions[i] && _sessions[i]->cap() == cap)
				return _sessions[i]->real_cap();
		return Gui::Session_capability { };
	}

	Rect content_rect(unsigned long wid) const override
	{
		return _registry.inner_rect(wid);
	}

	Rect chrome_rect(unsigned long wid) const override
	{
		return _registry.outer_rect(wid);
	}

	void input_offset(unsigned long wid, int &dx, int &dy) const override
	{
		Wm_registry::Window const *w = _registry.find(wid);
		Rect const inner = _registry.inner_rect(wid);
		if (!w) {
			dx = 0;
			dy = 0;
			return;
		}
		dx = w->virt_x - inner.x;
		dy = w->virt_y - inner.y;
	}

	void register_session(Wm_session &session) override
	{
		for (unsigned i = 0; i < MAX_SESSIONS; i++) {
			if (_sessions[i])
				continue;
			_sessions[i] = &session;
			return;
		}
		Genode::warning("sponge_wm: session table full");
	}

	void content_view_destroyed(unsigned long wid) override
	{
		publish(_registry.destroy_window(wid));
	}

	void session_destroyed(Wm_session &session) override
	{
		for (unsigned i = 0; i < MAX_SESSIONS; i++)
			if (_sessions[i] == &session)
				_sessions[i] = nullptr;

		if (session.role() == Role::CONTENT)
			publish(_registry.destroy_window(session.wid()));

		if (session.role() == Role::LAYOUTER && _grab)
			set_grab(false);
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Main main(env);
	(void)main;
}
