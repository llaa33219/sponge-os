/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_layouter — native layout policy component (Criterion 16,
 * D16W.1 in docs/plans/phase16-sponge-wm.md). The policy/state model
 * lives in layout_model.{h,cc} (pure); this file is the Genode
 * session glue:
 *
 *   consumed ROMs  (report_rom relays):
 *     window_list        core      -> layouter  (identity source, wid)
 *     focus_request      tasklist  -> layouter  (sole writer, id dedup)
 *     rules              tasklist  -> layouter  (sole writer, last-wins)
 *     focus_state        core      -> layouter  (focus truth)
 *     decorator_margins  decorator -> layouter  (may be late/zero)
 *     configd            sponge_configd broadcast (optional; the
 *                        wm.layout.mode key, default floating)
 *
 *   produced reports (report_rom relays):
 *     window_layout      protocol v1  (boundary = work area)
 *     window_request     v1.1 complete desired state (INNER geometry,
 *                        sticky action="close")
 *     focus              v1.1 steering snapshot
 *
 *   input: a role:layouter Gui session to sponge_wm carries the
 *   lossless core->layouter Input forwarding over chrome; this
 *   component hit-tests with the shared sponge_wm_protocol chrome
 *   helpers (title move, 8-zone resize, close/fullscreen/minimize
 *   clicks) and takes the public 'exclusive' input grab during
 *   move/resize drags.
 *
 *   config (init <config>): rules="rom" selects the tasklist rules
 *   ROM as the update source (the static inline <rules> document
 *   provides initial placement and is the fallback when the ROM is
 *   invalid); the optional <work_area x= y= width= height=/> node
 *   pins the work area (also reported as the window_layout boundary);
 *   without it the work area falls back to the public Gui info ROM
 *   (mode/screen rect) minus the top <panel_height> band (default
 *   28) — documented in README.md.
 *
 * The four frozen Tasklist-facing labels (window_list, window_layout,
 * focus_request, rules) and the v1.1 channel set are consumed and
 * produced EXACTLY as routed in run/sponge-wm-native*.run. This
 * component never writes focus_request or rules (D16W.6).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <gui_session/connection.h>
#include <os/reporter.h>
#include <rom_session/client.h>

#include "layout_model.h"

namespace {

using namespace Sponge;


/*
 * Optional ROM session (exceptions-free): 'Genode::Rom_connection'
 * raises on a denied session, but the configd broadcast is routed
 * only in topologies that run sponge_configd. The explicit
 * initialization pattern (Env::try_session, as in
 * os/src/server/event_filter/main.cc) keeps a missing route a logged
 * default instead of a component failure.
 */
struct Optional_rom
{
	using Local_rm = Genode::Env::Local_rm;

	Genode::Env &_env;

	Genode::Parent::Client _parent_client { };

	Genode::Id_space<Genode::Parent::Client>::Element const
		_client_id { _parent_client, _env.id_space() };

	Genode::Env::Session_result _session;

	Genode::Constructible<Genode::Rom_session_client> _rom { };

	Local_rm::Result _attached = Local_rm::Error::INVALID_DATASPACE;

	void _try_attach()
	{
		if (!_rom.constructed())
			return;
		_attached = _env.rm().attach(_rom->dataspace(), { });
	}

	Optional_rom(Genode::Env &env, char const *label,
	             Genode::Signal_context_capability sigh)
	:
		_env(env),
		_session(Genode::Session_error::DENIED)
	{
		char argbuf[Genode::Parent::Session_args::MAX_SIZE];
		Genode::copy_cstring(argbuf, "", sizeof(argbuf));
		Genode::Arg_string::set_arg(argbuf, sizeof(argbuf), "label", label);

		Genode::Ram_quota ram { Genode::Rom_session::RAM_QUOTA };
		Genode::Cap_quota caps { Genode::Rom_session::CAP_QUOTA };

		for (unsigned i = 0; i < 16; i++) {
			Genode::Arg_string::set_arg(argbuf, sizeof(argbuf),
			                           "ram_quota",
			                           Genode::String<32>(ram).string());
			Genode::Arg_string::set_arg(argbuf, sizeof(argbuf),
			                           "cap_quota",
			                           Genode::String<32>(caps).string());

			_session = _env.try_session(
				Genode::Rom_session::service_name(), _client_id.id(),
				Genode::Parent::Session_args(argbuf), Genode::Affinity { });

			if (_session == Genode::Session_error::INSUFFICIENT_RAM) {
				ram.value += 4096;
				continue;
			}
			if (_session == Genode::Session_error::INSUFFICIENT_CAPS) {
				caps.value += 4;
				continue;
			}
			break;
		}

		_session.with_result(
			[&] (Genode::Session_capability cap) {
				_rom.construct(
					static_cap_cast<Genode::Rom_session>(cap));
				_try_attach();
				_rom->sigh(sigh);
			},
			[&] (Genode::Session_error) {
				Genode::log("sponge_layouter: optional ROM \"", label,
				            "\" not routed (registered defaults apply)");
			});
	}

	~Optional_rom()
	{
		_session.with_result(
			[&] (Genode::Session_capability) {
				_env.close(_client_id.id()); },
			[&] (Genode::Session_error) { });
	}

	void update()
	{
		if (!_rom.constructed())
			return;

		/* 'update() == true': content refreshed in place */
		if (_attached.ok() && _rom->update())
			return;

		_try_attach();
	}

	template <typename FN>
	void with_node(FN const &fn) const
	{
		_attached.with_result(
			[&] (Local_rm::Attachment const &a) {
				Genode::Node const node(
					Genode::Const_byte_range_ptr(
						(char const *)a.ptr, a.num_bytes));
				fn(node);
			},
			[&] (Local_rm::Error) { });
	}
};


struct Sponge_layouter
{
	Genode::Env &_env;

	Layout_model::Model _model { };

	Genode::Expanding_reporter _window_layout {
		_env, "window_layout", "window_layout" };
	Genode::Expanding_reporter _window_request {
		_env, "window_request", "window_request" };
	Genode::Expanding_reporter _focus {
		_env, "focus", "focus" };

	Genode::Attached_rom_dataspace _window_list   { _env, "window_list" };
	Genode::Attached_rom_dataspace _focus_request { _env, "focus_request" };
	Genode::Attached_rom_dataspace _rules_rom     { _env, "rules" };
	Genode::Attached_rom_dataspace _focus_state   { _env, "focus_state" };
	Genode::Attached_rom_dataspace _margins_rom   { _env, "decorator_margins" };
	Genode::Attached_rom_dataspace _config        { _env, "config" };

	Genode::Signal_handler<Sponge_layouter> _rom_sigh {
		_env.ep(), *this, &Sponge_layouter::_handle_roms };
	Genode::Signal_handler<Sponge_layouter> _input_sigh {
		_env.ep(), *this, &Sponge_layouter::_handle_input };

	Optional_rom _configd { _env, "configd", _rom_sigh };

	Gui::Connection _gui { _env };

	int  _px { 0 }, _py { 0 };
	bool _exclusive { false };
	bool _rules_from_rom { false };

	/* H1: rejected window_list snapshots (see _handle_roms) */
	unsigned _window_list_rejects { 0 };

	Sponge_layouter(Genode::Env &env)
	:
		_env(env)
	{
		Layout_model::Rules_doc static_rules { };

		Genode::Node const config = _config.node();

		_rules_from_rom =
			config.attribute_value("rules", Genode::String<16>())
			 == Genode::String<16>("rom");

		config.with_sub_node("rules",
			[&] (Genode::Node const &n) {
				if (!Layout_model::rules_doc_from_node(
					    n, Layout_model::Rule_frame::ABSOLUTE, static_rules))
					Genode::warning("sponge_layouter: static <rules> "
					                "document is malformed");
			},
			[&] { });

		Layout_model::Work_area const work = _resolve_work_area();
		Layout_model::Mode const mode = _read_mode();

		_model.configure(work, Wm_protocol::Margins { }, mode, static_rules);

		Genode::log("sponge_layouter: work area (", work.x, ",", work.y, ") ",
		            work.w, "x", work.h, " mode=",
		            mode == Layout_model::Mode::TILING ? "tiling" : "floating",
		            " rules=", _rules_from_rom ? "rom" : "static");

		_window_list.sigh(_rom_sigh);
		_focus_request.sigh(_rom_sigh);
		_rules_rom.sigh(_rom_sigh);
		_focus_state.sigh(_rom_sigh);
		_margins_rom.sigh(_rom_sigh);

		_gui.input.sigh(_input_sigh);

		_handle_roms();
		_publish_if_dirty();
	}

	/* work area: explicit config, else the public Gui info ROM minus
	 * the top panel band (README.md "Work area") */
	Layout_model::Work_area _resolve_work_area()
	{
		Layout_model::Work_area work { };

		if (Layout_model::work_area_from_node(_config.node(), work))
			return work;

		unsigned const panel_h =
			_config.node().attribute_value("panel_height", 28U);

		int  sx = 0, sy = 0;
		long sw = 0, sh = 0;
		_gui.with_info_node([&] (Genode::Node const &info) {
			sx = info.attribute_value("xpos",   0);
			sy = info.attribute_value("ypos",   0);
			sw = info.attribute_value("width",  0L);
			sh = info.attribute_value("height", 0L);
		});

		if (sw > 0 && sh > (long)panel_h) {
			Genode::log("sponge_layouter: work area from public Gui info "
			            "(", sx, ",", (int)(sy + (int)panel_h), ") ", sw,
			            "x", sh - (long)panel_h);
			return Layout_model::Work_area {
				sx, sy + (int)panel_h, sw, sh - (long)panel_h };
		}

		Genode::warning("sponge_layouter: no <work_area> config and no "
		                "public info rect; defaulting to 1024x768 minus a ",
		                panel_h, " px panel");
		return Layout_model::Work_area {
			0, (int)panel_h, 1024, 768 - (long)panel_h };
	}

	Layout_model::Mode _read_mode()
	{
		Layout_model::Mode mode = Layout_model::Mode::FLOATING;
		_configd.update();
		_configd.with_node([&] (Genode::Node const &n) {
			mode = Layout_model::parse_layout_mode(n); });
		return mode;
	}

	void _handle_roms()
	{
		_window_list.update();
		_focus_request.update();
		_rules_rom.update();
		_focus_state.update();
		_margins_rom.update();
		_configd.update();

		/* window_list: complete snapshots, identity source */
		if (_window_list.valid()) {
			Layout_model::Window_list_entry
				entries[Layout_model::MAX_WINDOWS] { };
			Genode::size_t n = 0;
			bool const parsed = Wm_protocol::parse_window_list(
				_window_list.node(),
				[&] (Wm_protocol::Window_list_entry const &e) {
					if (n < Layout_model::MAX_WINDOWS)
						entries[n++] = e;
				});

			if (parsed) {
				_window_list_rejects = 0;
				_model.window_list(entries, n);
			} else {
				/*
				 * H1 hardening: a malformed snapshot must not read as
				 * an empty last-state-wins update (feeding n == 0 wiped
				 * every tracked window). Keep the last good snapshot and
				 * warn in a bounded way: once per failure streak, then
				 * every 64th rejection.
				 */
				_window_list_rejects++;
				if (_window_list_rejects == 1
				 || (_window_list_rejects & 63) == 0)
					Genode::warning(
						"sponge_layouter: ignoring invalid window_list "
						"snapshot (", _window_list_rejects,
						" rejected); keeping last good snapshot");
			}
		}

		/* focus_request: id-deduped steering (+restore-with-focus) */
		if (_focus_request.valid()) {
			Wm_protocol::Focus_request req;
			if (Wm_protocol::parse_focus_request(_focus_request.node(), req))
				_model.focus_request(req);
		}

		/* rules: last-wins tasklist ROM, static doc when invalid */
		if (_rules_from_rom) {
			Layout_model::Rules_doc doc { };
			bool const ok = _rules_rom.valid()
			             && Layout_model::rules_doc_from_node(
			                    _rules_rom.node(),
			                    Layout_model::Rule_frame::BOUNDARY_RELATIVE,
			                    doc);
			if (ok)
				_model.rules(doc);
			else
				_model.rules_invalid();
		}

		/* focus_state: the sole truth of window_layout focused="yes" */
		if (_focus_state.valid()) {
			Wm_protocol::Focus_target t;
			if (Wm_protocol::parse_focus_state(_focus_state.node(), t))
				_model.focus_state(t);
		}

		/* decorator_margins: absent/zero is legal, updates reflow */
		if (_margins_rom.valid()) {
			Wm_protocol::Margins m;
			if (Wm_protocol::parse_decorator_margins(_margins_rom.node(), m))
				_model.margins(m);
		}

		/* configd broadcast: wm.layout.mode (default floating) */
		_configd.with_node([&] (Genode::Node const &n) {
			_model.mode(Layout_model::parse_layout_mode(n)); });

		_sync_exclusive();
		_publish_if_dirty();
	}

	void _handle_input()
	{
		_gui.input.for_each_event([&] (Input::Event const &ev) {
			ev.handle_absolute_motion([&] (int x, int y) {
				_px = x;
				_py = y;
				_model.motion(x, y);
			});

			if (ev.key_press(Input::BTN_LEFT))
				_model.press(_px, _py);
			else if (ev.key_release(Input::BTN_LEFT))
				_model.release(_px, _py);
		});

		_sync_exclusive();
		_publish_if_dirty();
	}

	/* public exclusive_input grab during chrome drags (v1.1) */
	void _sync_exclusive()
	{
		bool const drag = _model.drag_active();
		if (drag == _exclusive)
			return;
		_exclusive = drag;
		_gui.input.exclusive(drag);
	}

	void _publish_if_dirty()
	{
		if (!_model.consume_dirty())
			return;

		_window_layout.generate_xml(
			[&] (Genode::Xml_generator &g) { _model.gen_window_layout(g); });
		_window_request.generate_xml(
			[&] (Genode::Xml_generator &g) { _model.gen_window_request(g); });
		_focus.generate_xml(
			[&] (Genode::Xml_generator &g) { _model.gen_focus(g); });
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Sponge_layouter layouter { env };
	(void)layouter;
}


Genode::size_t Component::stack_size() { return 256 * 1024; }
