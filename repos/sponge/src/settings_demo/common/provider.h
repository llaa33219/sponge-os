/* SPDX-License-Identifier: Apache-2.0
 *
 * provider.h — the shared provider lifecycle for the settings_demo
 * third-party settings entries (docs/12-package-format.md §14).
 *
 * One Provider_main per entry, one address space, one window
 * (AGENTS.md §1.2). The provider owns its window, its input
 * validation, its persistence and its errors (docs/16 §9): it paints
 * on start (first paint), validates every keystroke before it can
 * become the value, validates again before persisting, and writes
 * only its own file in the package-private store (D16.10 tier (b)).
 *
 * Capability surface (minimum privilege, §14.5): Gui (window + the
 * input channel) and File_system via the <config><vfs><fs/> mount —
 * nothing else. No Report session, no configd label, no ROM beyond
 * the parent config, no Timer, no plugins, no interpreter.
 *
 * Singleton/focus (§14.7) belongs to sponge_pkgd: a second open of
 * the same (package, settings-id) must focus this window instead of
 * starting a second instance. This component creates exactly one
 * view and never a second one; the de-duplication itself is the
 * launcher-side contract, not something two providers can arbitrate
 * without a shared channel.
 *
 * Lifecycle: closing the window (the v1.1 app-visible close channel,
 * docs/05 §4.6) commits any pending valid change and exits, so every
 * session is reclaimed and the next open starts a fresh instance —
 * the §14.7 crash path, taken deliberately.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>

#include "paint.h"
#include "store.h"
#include "store_io.h"

namespace Sponge::Settings_demo {

struct Provider_config
{
	char const *who;
	char const *window_title;
	char const *header;
	char const *value_label;
	char const *entry_name;
	char const *store_path;
	char const *default_value;
	char const *hint;
	Value_rules rules;
};

struct Provider_main
{
	private:

		enum : unsigned {
			DEFAULT_W = 440,
			DEFAULT_H = 240,
			STATUS_MAX = ERROR_TEXT_MAX,
		};

		Genode::Env &_env;
		Provider_config const &_cfg;

		Genode::Attached_rom_dataspace _config_rom { _env, "config" };
		Store_io _store { _env };

		char _value[MAX_VALUE_LEN + 1] { };
		bool _dirty { false };
		char _status[STATUS_MAX] { };

		Gui::Connection _gui;
		Gui::View_id const _view_id { 1 };
		Genode::Constructible<Genode::Attached_dataspace> _fb_ds { };
		Gui::Area _area { DEFAULT_W, DEFAULT_H };

		Genode::Signal_handler<Provider_main> _input_handler {
			_env.ep(), *this, &Provider_main::_handle_input };

		Genode::Signal_handler<Provider_main> _info_handler {
			_env.ep(), *this, &Provider_main::_handle_info };


		void _paint()
		{
			if (!_fb_ds.constructed())
				return;

			Genode::Surface<Genode::Pixel_rgb888> surface(
				_fb_ds->local_addr<Genode::Pixel_rgb888>(), _area);

			Provider_paint const p {
				.header      = _cfg.header,
				.value_label = _cfg.value_label,
				.value       = _value,
				.hint        = _cfg.hint,
				.status      = _status,
				.caret_on    = true,
			};
			paint_provider(surface, p);

			_gui.framebuffer.refresh(
				Gui::Rect(Gui::Point(0, 0), _area));
		}


		void _set_status(char const *text)
		{
			unsigned i = 0;
			for (; text[i] != '\0' && i + 1 < STATUS_MAX; ++i)
				_status[i] = text[i];
			_status[i] = '\0';
		}


		void _reject(Value_error err)
		{
			fill_error_text(err, _cfg.rules, _cfg.value_label,
			                _status, STATUS_MAX);
			Genode::warning(_cfg.who, ": ", (char const *)_status);
			_paint();
		}


		void _insert(char c)
		{
			unsigned const len = str_len(_value);

			if (len >= _cfg.rules.max_len) {
				_reject(Value_error::too_long);
				return;
			}
			if (!char_allowed(c)) {
				_reject(Value_error::bad_char);
				return;
			}

			_value[len] = c;
			_value[len + 1] = '\0';
			_dirty = true;
			_set_status("");
			_paint();
		}


		void _backspace()
		{
			unsigned const len = str_len(_value);
			if (len == 0)
				return;

			_value[len - 1] = '\0';
			_dirty = true;
			_set_status("");
			_paint();
		}


		void _commit(bool at_close)
		{
			Value_error const err = validate_value(_value, _cfg.rules);

			if (err != Value_error::ok) {
				if (at_close) {
					fill_error_text(err, _cfg.rules, _cfg.value_label,
					                _status, STATUS_MAX);
					Genode::warning(_cfg.who, ": discarding change: ",
					                (char const *)_status);
				} else
					_reject(err);
				return;
			}

			if (!_store.enabled()) {
				_dirty = false;
				_set_status("Saved (memory only).");
				Genode::log(_cfg.who, ": ", _cfg.entry_name,
				            " kept in memory (persistence disabled)");
				if (!at_close)
					_paint();
				return;
			}

			_dirty = false;
			if (_store.save(_cfg.who, _cfg.store_path, _cfg.entry_name,
			                _value)) {
				_set_status("Saved.");
				if (!at_close)
					_paint();
			} else {
				_set_status("Save failed - see log.");
				_paint();
			}
		}


		void _handle_input()
		{
			_gui.input.for_each_event([&] (Input::Event const &ev) {

				ev.handle_press([&] (Input::Keycode key,
				                     Input::Codepoint cp) {
					if (key == Input::KEY_BACKSPACE) { _backspace(); return; }
					if (key == Input::KEY_ENTER)     { _commit(false); return; }
					if (cp.valid() && cp.value < 128)
						_insert((char)cp.value);
				});

				ev.handle_repeat([&] (Input::Codepoint cp) {
					if (cp.valid() && cp.value < 128)
						_insert((char)cp.value);
				});
			});
		}


		void _handle_info()
		{
			bool closed = false;
			long new_w = (long)_area.w, new_h = (long)_area.h;

			_gui.with_info_node([&] (Genode::Node const &info) {
				info.for_each_sub_node("capture", [&] (Genode::Node const &cap) {
					if (cap.attribute_value("closed", false))
						closed = true;
					else {
						new_w = cap.attribute_value("width",  (long)_area.w);
						new_h = cap.attribute_value("height", (long)_area.h);
					}
				});
			});

			if (closed) {
				if (_dirty)
					_commit(true);
				Genode::log(_cfg.who, ": window closed, exiting");
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
			_paint();
		}

	public:

		Provider_main(Genode::Env &env, Provider_config const &config)
		: _env(env), _cfg(config), _gui(env, config.who)
		{
			_store.init(_config_rom.node(), _cfg.who, _cfg.store_path);

			if (!_store.load(_cfg.who, _cfg.store_path, _cfg.entry_name,
			                 _cfg.rules, _value, sizeof(_value))) {
				unsigned i = 0;
				for (; _cfg.default_value[i] != '\0' &&
				       i < MAX_VALUE_LEN; ++i)
					_value[i] = _cfg.default_value[i];
				_value[i] = '\0';
				_set_status("Started with default value.");
			} else
				_set_status("Loaded saved value.");

			_gui.buffer({ .area = _area, .alpha = false });
			_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());

			_gui.view(_view_id, { .title = _cfg.window_title,
			                      .rect  = Gui::Rect(Gui::Point(0, 0), _area),
			                      .front = true });
			_gui.execute();

			_gui.input.sigh(_input_handler);
			_gui.info_sigh(_info_handler);

			_paint();
		}
};

} /* namespace Sponge::Settings_demo */
