/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_provider — the shared runtime behind every first-party settings
 * provider (Criterion 18).
 *
 * Each provider is a SEPARATE Genode component that owns its UI,
 * validation, writes, lifecycle, and error reporting (docs/12 §14).
 * This header is the common scaffolding those eight components
 * share; it is not a universal settings controller:
 *
 *   - it writes ONLY through the provider's own configd label
 *     (sp_config_channel.h), whose key-domain ACL is enforced by
 *     sponge_configd — there is no arbitrary key/value entry point;
 *   - it serves ONE closed quick-operation vocabulary per provider
 *     (sp_quick_pair.h) for the quick-menu change lifecycle
 *     (docs/05-sponge-de.md §5.4), never a config relay;
 *   - it owns one window with the provider's rows and bounded
 *     English error text (sp_window.h / sp_rows.h);
 *   - it validates every value through the same sp_validate gate
 *     the host self-test runs (validator parity with configd).
 *
 * Lifecycle notes:
 *   - singleton per (package, settings-id): one process, one window.
 *     A second open is resolved by sponge_pkgd raising/focusing this
 *     instance (docs/12 §14.7) — the provider simply keeps one
 *     window and accepts focus.
 *   - ensure-start support: the quick pair is live from process
 *     start, so a cold quick-menu action (pkgd starts the provider,
 *     then sends the operation) is served immediately. With
 *     `<settings headless="yes"/>` in the component config the Gui
 *     session is skipped and the process serves quick edits only.
 *   - value truthfulness: rows prefill from the configd broadcast
 *     (ROM label "configd", the sponge-de ConfigController naming
 *     that avoids the init `<config>` collision) and re-sync on
 *     every broadcast update, so external writes (vct) are visible.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <input/event.h>
#include <util/reconstructible.h>
#include <util/xml_node.h>

#include "sp_config_channel.h"
#include "sp_quick_pair.h"
#include "sp_rows.h"
#include "sp_window.h"

namespace Sponge::Settings {

/*
 * Static identity of one provider. The label strings are pinned to
 * the Criterion 18 writer table (sponge_configd/key_domain_acl.cc);
 * the quick-pair labels follow the same suffix convention. The
 * (package, settings-id) pair is the stable entry identity of
 * docs/12 §14.1 and derives the window label/title (overridable per
 * deployment via the `<settings .../>` component-config node).
 */
struct Provider_desc
{
	char const      *name;         /* "panel_settings" (TARGET + ACL label) */
	char const      *settings_id;  /* "panel" */
	char const      *package;      /* "sponge-de" */
	char const      *title;        /* "Panel" */
	Config_labels    config;
	Quick_labels     quick;
	Quick_op const  *quick_ops;    /* closed vocabulary (may be empty) */
	unsigned         n_quick_ops;
};


class Provider
{
	protected:

		Genode::Env          &_env;
		Provider_desc const   _desc;

		/* identity actually in effect (component config overridable) */
		char _package[64] { };
		char _id     [33] { };
		char _title  [64] { };
		bool _headless { false };

		Genode::Constructible<Config_channel> _channel { };
		Quick_pair                            _quick    { };

		enum { N_ROWS = ROWS_MAX };
		Row      _rows[N_ROWS] { };
		unsigned _n_rows   { 0 };
		unsigned _selected { 0 };
		unsigned _scroll   { 0 };

		Genode::Constructible<Provider_window>           _window    { };
		Genode::Constructible<Genode::Attached_rom_dataspace> _broadcast { };

		char _status[WHY_MAX] { };
		bool _status_error { false };

		/* ---- signal plumbing ---- */

		Genode::Signal_handler<Provider> _quick_sigh {
			_env.ep(), *this, &Provider::_handle_quick };

		Genode::Signal_handler<Provider> _broadcast_sigh {
			_env.ep(), *this, &Provider::_handle_broadcast };

		Genode::Signal_handler<Provider> _input_sigh {
			_env.ep(), *this, &Provider::_handle_input };

		Genode::Signal_handler<Provider> _info_sigh {
			_env.ep(), *this, &Provider::_handle_info };

		/* ---- row management (provider ctor bodies) ---- */

		void clear_rows()
		{
			_n_rows   = 0;
			_selected = 0;
			_scroll   = 0;
		}

		Row &add_row(Row_kind kind, char const *label, char const *key,
		             Value_spec spec, unsigned step = 1)
		{
			Row &r = _rows[_n_rows < N_ROWS ? _n_rows : N_ROWS - 1];
			if (_n_rows < N_ROWS)
				++_n_rows;

			r = Row { };
			r.kind = kind;
			r.spec = spec;
			r.step = step;
			row_set_label(r, label);
			row_set_key(r, key);
			return r;
		}

		/* ---- write path ---- */

		/*
		 * The one door to configd: validates `value` against `spec`
		 * and writes `key` on this provider's own ACL-limited label.
		 * Used by the window rows and by the closed quick vocabulary.
		 */
		bool write_value(char const *key, Value_spec const &spec,
		                 char const *value, char *why, unsigned cap)
		{
			why_clear(why, cap);

			if (!validate_value(spec, value, why, cap))
				return false;

			if (!_channel.constructed()) {
				why_set(why, cap,
				        "config channel unavailable (label not routed)");
				return false;
			}

			return _channel->set(key, value, why, cap);
		}

		/*
		 * Row application. Default: the row's own key/value through
		 * the shared gate. Providers with custom rows (the shortcuts
		 * table, the defaults reset action) override this.
		 */
		virtual bool on_apply(Row &r, char *why, unsigned cap)
		{
			if (r.key[0] == 0) {
				why_set(why, cap, "this row has no direct write");
				return false;
			}
			return write_value(r.key, r.spec, r.text, why, cap);
		}

		/*
		 * Closed quick-vocabulary dispatch. Default: exact `op`
		 * match against the provider's table; the value is validated
		 * against the op's spec and written to the op's key. An `id`
		 * selector is rejected here — only the Panel provider
		 * understands panel.<id>.* and overrides this hook. Table
		 * misses return UNKNOWN (a bounded "unknown operation"
		 * answer; never a key/value passthrough).
		 */
		virtual Quick_result on_quick(char const *op, char const *id,
		                              char const *value,
		                              char *why, unsigned cap)
		{
			for (unsigned i = 0; i < _desc.n_quick_ops; ++i) {
				Quick_op const &q = _desc.quick_ops[i];
				if (Genode::strcmp(q.op, op) != 0)
					continue;

				if (id[0] != 0) {
					why_set(why, cap,
					        "this operation does not take an id selector");
					return Quick_result::ERROR;
				}

				if (!write_value(q.key, q.spec, value, why, cap))
					return Quick_result::ERROR;

				return Quick_result::OK;
			}

			return Quick_result::UNKNOWN;
		}

		/*
		 * Broadcast sync for one key. Default: match the row that
		 * owns the key and refresh it (skipping rows with an edit in
		 * progress). Providers with composite rows override this.
		 */
		virtual void on_broadcast_key(char const *key, char const *value)
		{
			for (unsigned i = 0; i < _n_rows; ++i) {
				Row &r = _rows[i];
				if (r.key[0] == 0 || r.editing)
					continue;
				if (Genode::strcmp(r.key, key) != 0)
					continue;

				row_set_text(r, value);
				row_sync_state_from_text(r);
				row_set_applied(r);
			}
		}

		/*
		 * Full broadcast document (override point for two-phase
		 * sync, e.g. Panel's panel.ids rebuild before per-id rows
		 * can match). Default: one pass over the <key> nodes.
		 */
		virtual void on_broadcast_doc(Genode::Xml_node const &root)
		{
			root.for_each_sub_node("key", [&] (Genode::Xml_node const &n) {
				Genode::String<ROW_KEY_MAX> const name =
					n.attribute_value("name", Genode::String<ROW_KEY_MAX>());
				Genode::String<VAL_MAX> const value =
					n.attribute_value("value", Genode::String<VAL_MAX>());
				on_broadcast_key(name.string(), value.string());
			});
		}

		/* status line (bounded, English) */
		void set_status(char const *text, bool is_error)
		{
			why_set(_status, WHY_MAX, text);
			_status_error = is_error;
		}

	public:

		Provider(Genode::Env &env, Provider_desc const &desc)
		:
			_env(env), _desc(desc)
		{
			copy(_package, desc.package);
			copy(_id,      desc.settings_id);
			copy(_title,   desc.title);

			_read_identity_config();
		}

		virtual ~Provider() { }

		/*
		 * Wire the channels, window, and signal handlers after the
		 * provider subclass has built its rows. Call exactly once,
		 * at the end of the subclass constructor.
		 */
		void start()
		{
			try {
				_channel.construct(_env, _desc.config);
			}
			catch (...) {
				Genode::error("settings provider ", _desc.name,
				              ": config channel '",
				              _desc.config.request_rom,
				              "' unavailable — writes disabled");
				set_status("config channel unavailable "
				           "(writes disabled)", true);
			}

			_quick.open(_env, _desc.quick, _quick_sigh);

			if (!_headless) {
				char window_id[128] { };
				unsigned pos = 0;
				pos = append(window_id, pos, _package);
				pos = append(window_id, pos, " / ");
				pos = append(window_id, pos, _id);
				_window.construct(_env, window_id);
				_window->gui().input.sigh(_input_sigh);
				_window->gui().info_sigh(_info_sigh);
			}

			_open_broadcast();
			_sync_from_broadcast();
			_repaint();

			Genode::log("settings provider ", _desc.name, " ready (",
			            (char const *)_package, "/",
			            (char const *)_id, _headless
			            ? ", headless quick-edits only)" : ")");
		}

	protected:

		/* ---- identity ---- */

		void _read_identity_config()
		{
			try {
				Genode::Attached_rom_dataspace config { _env, "config" };
				if (!config.valid())
					return;

				Genode::Node const root = config.node();

				root.for_each_sub_node("settings",
					[&] (Genode::Node const &n) {
						Genode::String<64> const package =
							n.attribute_value("package", Genode::String<64>());
						Genode::String<33> const id =
							n.attribute_value("id", Genode::String<33>());
						Genode::String<64> const title =
							n.attribute_value("title", Genode::String<64>());
						Genode::String<8> const headless =
							n.attribute_value("headless", Genode::String<8>());

						if (package.string()[0])
							copy(_package, package.string());
						if (id.string()[0])
							copy(_id, id.string());
						if (title.string()[0])
							copy(_title, title.string());

						_headless =
							Genode::strcmp(headless.string(), "yes") == 0;
					});
			}
			catch (Genode::Rom_connection::Rom_connection_failed) {
				Genode::warning("settings provider ", _desc.name,
				                ": <config> unavailable — identity at defaults");
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				Genode::warning("settings provider ", _desc.name,
				                ": malformed <config> — identity at defaults");
			}
		}

		/* ---- broadcast prefill ---- */

		void _open_broadcast()
		{
			try {
				_broadcast.construct(_env, "configd");
				_broadcast->sigh(_broadcast_sigh);
				_broadcast->update();
			}
			catch (Genode::Rom_connection::Rom_connection_failed) {
				Genode::log("settings provider ", _desc.name,
				            ": configd broadcast not routed — rows at defaults");
			}
			catch (Genode::Service_denied) {
				Genode::log("settings provider ", _desc.name,
				            ": configd broadcast denied — rows at defaults");
			}
		}

		void _sync_from_broadcast()
		{
			if (!_broadcast.constructed())
				return;

			_broadcast->update();
			if (!_broadcast->valid())
				return;

			try {
				on_broadcast_doc(_broadcast->xml());
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				Genode::warning("settings provider ", _desc.name,
				                ": malformed configd broadcast");
			}
		}

		/* ---- repaint ---- */

		void _repaint()
		{
			if (!_window.constructed())
				return;

			/* keep the selection visible */
			if (_selected < _n_rows) {
				if (_selected < _scroll)
					_scroll = _selected;
				if (_selected >= _scroll + _window->rows_visible())
					_scroll = _selected - _window->rows_visible() + 1;
			}

			char window_id[128] { };
			unsigned pos = 0;
			pos = append(window_id, pos, _title);
			pos = append(window_id, pos, "  (");
			pos = append(window_id, pos, _package);
			pos = append(window_id, pos, "/");
			pos = append(window_id, pos, _id);
			pos = append(window_id, pos, ")");

			_window->paint(window_id, _rows, _n_rows, _selected, _scroll,
			               _status[0] ? _status : "ready",
			               _status_error,
			               "Up/Down: select   Left/Right: change   "
			               "Enter: apply   Esc: revert");
		}

		/* ---- quick pair ---- */

		void _handle_quick()
		{
			_quick.serve([&] (char const *op, char const *id,
			                  char const *value, char *why,
			                  unsigned cap) -> Quick_result {
				Quick_result const r = on_quick(op, id, value, why, cap);
				if (r == Quick_result::OK)
					set_status("quick change applied", false);
				else if (r == Quick_result::ERROR)
					set_status(why, true);
				else
					set_status("unknown quick operation (vocabulary is "
					           "closed for this provider)", true);
				_sync_from_broadcast();
				_repaint();
				return r;
			});
		}

		/* ---- broadcast ---- */

		void _handle_broadcast()
		{
			_sync_from_broadcast();
			_repaint();
		}

		void _handle_info()
		{
			if (!_window.constructed())
				return;

			bool closed = false;
			_window->gui().with_info_node([&] (Genode::Node const &info) {
				info.for_each_sub_node("capture", [&] (Genode::Node const &capture) {
					if (capture.attribute_value("closed", false))
						closed = true;
				});
			});

			if (closed) {
				Genode::log("settings provider ", _desc.name, ": window closed, exiting");
				_env.parent().exit(0);
			}
		}

		/* ---- input + row interaction ---- */

		void _handle_input()
		{
			if (!_window.constructed())
				return;

			_window->gui().input.for_each_event([&] (Input::Event const &ev) {
				if (!ev.press())
					return;

				ev.handle_press([&] (Input::Keycode k, Input::Codepoint cp) {
					_on_key(k, cp);
				});
			});

			_repaint();
		}

		void _apply_selected()
		{
			if (_selected >= _n_rows)
				return;

			Row &r = _rows[_selected];
			char why[WHY_MAX] { };

			if (r.kind == Row_kind::INFO)
				return;

			/*
			 * An ENUM row showing an external value (written by vct
			 * or another path, outside the row's closed choices)
			 * cannot be re-applied as-is: say so instead of firing a
			 * misleading validation error at a value the user did
			 * not type.
			 */
			if (r.kind == Row_kind::ENUM && r.choice < 0 && r.text[0]) {
				set_status("value was set outside this window — "
				           "use Left/Right to choose", false);
				return;
			}

			if (r.kind == Row_kind::ACTION) {
				if (!r.confirm) {
					r.confirm = true;
					set_status("press Enter again to confirm, Esc to cancel",
					           false);
					return;
				}
				r.confirm = false;
			}

			if (r.kind != Row_kind::ACTION && !r.custom && r.key[0] != 0) {
				if (!validate_value(r.spec, r.text, why, WHY_MAX)) {
					set_status(why, true);
					/* revert the row to its applied value */
					row_set_text(r, r.applied);
					row_sync_state_from_text(r);
					return;
				}
			}

			if (!on_apply(r, why, WHY_MAX)) {
				set_status(why[0] ? why : "change refused", true);
				if (r.kind != Row_kind::ACTION) {
					row_set_text(r, r.applied);
					row_sync_state_from_text(r);
				}
				return;
			}

			row_set_applied(r);
			char ok[WHY_MAX] { };
			unsigned pos = 0;
			pos = append(ok, pos, "applied: ");
			pos = append(ok, pos, r.label);
			set_status(ok, false);
			_sync_from_broadcast();
		}

		void _revert_selected()
		{
			if (_selected >= _n_rows)
				return;

			Row &r = _rows[_selected];

			if (r.confirm) {
				r.confirm = false;
				set_status("cancelled", false);
				return;
			}

			if (r.kind == Row_kind::TEXT || r.kind == Row_kind::UINT) {
				row_set_text(r, r.applied);
				row_sync_state_from_text(r);
				set_status("reverted", false);
				return;
			}

			set_status("", false);
		}

		void _on_key(Input::Keycode k, Input::Codepoint cp)
		{
			switch (k) {

			case Input::KEY_UP:
				if (_selected > 0)
					--_selected;
				_cancel_confirms();
				return;

			case Input::KEY_DOWN:
				if (_selected + 1 < _n_rows)
					++_selected;
				_cancel_confirms();
				return;

			case Input::KEY_LEFT:
				_cancel_confirms();
				_adjust(-1);
				return;

			case Input::KEY_RIGHT:
				_cancel_confirms();
				_adjust(+1);
				return;

			case Input::KEY_ENTER:
				_apply_selected();
				return;

			case Input::KEY_ESC:
				_revert_selected();
				return;

			case Input::KEY_BACKSPACE:
				_edit_backspace();
				return;

			case Input::KEY_SPACE:
				if (_selected < _n_rows &&
				    _rows[_selected].kind == Row_kind::TOGGLES) {
					_cancel_confirms();
					_adjust(+1, true);
					return;
				}
				break;

			default:
				break;
			}

			/* typed characters (TEXT rows) */
			if (cp.valid() && cp.value >= 32 && cp.value <= 126) {
				_edit_append(char(cp.value));
				_cancel_confirms();
			}
		}

		void _cancel_confirms()
		{
			for (unsigned i = 0; i < _n_rows; ++i)
				_rows[i].confirm = false;
		}

		void _adjust(int dir, bool toggle = false)
		{
			if (_selected >= _n_rows)
				return;

			Row &r = _rows[_selected];

			switch (r.kind) {

			case Row_kind::ENUM: {
				if (r.spec.n_choices == 0)
					return;
				if (r.choice < 0)
					r.choice = dir > 0 ? 0 : (int)r.spec.n_choices - 1;
				else {
					r.choice += dir;
					if (r.choice < 0)
						r.choice = (int)r.spec.n_choices - 1;
					if (r.choice >= (int)r.spec.n_choices)
						r.choice = 0;
				}
				row_set_text(r, r.spec.choices[r.choice]);
				_apply_selected();   /* legacy live-write behavior */
				return;
			}

			case Row_kind::TOGGLES: {
				if (r.spec.n_choices == 0)
					return;
				if (toggle) {
					r.mask ^= (1u << r.cursor);
					row_text_from_mask(r);
					_apply_selected();
					return;
				}
				if (dir > 0) {
					r.cursor = (r.cursor + 1) % r.spec.n_choices;
				} else {
					r.cursor = r.cursor == 0
					         ? r.spec.n_choices - 1 : r.cursor - 1;
				}
				return;
			}

			case Row_kind::UINT: {
				unsigned v = 0;
				if (!uint_in_range(r.text, r.spec.min, r.spec.max, v,
				                   nullptr, 0))
					v = r.spec.min;
				if (dir > 0)
					v = v + r.step > r.spec.max ? r.spec.max : v + r.step;
				else
					v = v < r.spec.min + r.step ? r.spec.min : v - r.step;

				char buf[16] { };
				num_to_cstr(v, buf);
				row_set_text(r, buf);
				_apply_selected();
				return;
			}

			default:
				return;
			}
		}

		void _edit_append(char c)
		{
			if (_selected >= _n_rows)
				return;

			Row &r = _rows[_selected];
			if (r.kind != Row_kind::TEXT && r.kind != Row_kind::UINT)
				return;

			if (r.kind == Row_kind::UINT && (c < '0' || c > '9'))
				return;

			Genode::size_t len = Genode::strlen(r.text);
			if (!r.editing) {
				/* a fresh edit starts from empty, not from the old value */
				r.text[0] = 0;
				len = 0;
				r.editing = true;
			}
			if (len + 1 < sizeof(r.text)) {
				r.text[len]     = c;
				r.text[len + 1] = 0;
			}
		}

		void _edit_backspace()
		{
			if (_selected >= _n_rows)
				return;

			Row &r = _rows[_selected];
			if (r.kind != Row_kind::TEXT && r.kind != Row_kind::UINT)
				return;

			Genode::size_t len = Genode::strlen(r.text);
			if (len == 0)
				return;

			r.text[len - 1] = 0;
			r.editing = true;
		}

		/* ---- small shared string helpers ---- */

		static void copy(char *dst, char const *src)
		{
			unsigned i = 0;
			for (; src[i] && i + 1 < 64; ++i)
				dst[i] = src[i];
			dst[i] = 0;
		}

		static unsigned append(char *dst, unsigned pos, char const *src)
		{
			for (unsigned i = 0; src[i] && pos + 1 < 96; ++i)
				dst[pos++] = src[i];
			dst[pos] = 0;
			return pos;
		}

		static void num_to_cstr(unsigned n, char *out)
		{
			char digits[16] { };
			unsigned d = 0;
			do { digits[d++] = char('0' + n % 10U); n /= 10U; } while (n);
			unsigned pos = 0;
			while (d)
				out[pos++] = digits[--d];
			out[pos] = 0;
		}
};

}  /* namespace Sponge::Settings */
