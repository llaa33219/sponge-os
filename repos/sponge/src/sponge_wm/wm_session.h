/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_session — sponge_wm Gui-session virtualization (the transport
 * side of the core, D16W.1/D16W.3).
 *
 * One 'Wm_session' is one virtual Gui session served to a client of
 * the sponge_wm core. The role comes from the run-side session policy
 * (docs/plans/phase16-sponge-wm.md §11 run-side routing):
 *
 *   CONTENT   — a client window. One window per session; the core
 *               assigns the monotonic wid. Pixels pass straight
 *               through ('framebuffer()' hands out the REAL nitpicker
 *               framebuffer capability of the session's backing
 *               connection); the command layer is virtualized: view
 *               placement is core-owned (the v1.1 window_request
 *               authority), the top-level view title feeds the frozen
 *               window_list 'title', and the info ROM carries the
 *               v1.1 app-visible resize/close channel (the 0x0 close
 *               convention).
 *   DECORATOR — the chrome renderer. Chrome views are associated to
 *               windows by their title (the decimal wid,
 *               'Wm_protocol::view_title'); the core owns their
 *               placement (outer frame) and co-moves them with the
 *               content. The decorator's input stream is a DUMMY: no
 *               real input is ever delivered to it (v1.1).
 *   LAYOUTER  — the policy component. Its input stream is the target
 *               for all chrome input (core->layouter Input forwarding)
 *               and the exclusive-grab source ('Input::Session::
 *               exclusive' on its input session routes ALL core input
 *               to the layouter while the grab is held, so chrome
 *               drags keep receiving motion over content).
 *
 * Every session wraps exactly one REAL Gui connection to nitpicker
 * (the backing connection); every view command is forwarded there
 * with the core's placement applied. No private upstream headers are
 * used: the session implements the public 'Gui::Session' contract
 * (os/include/gui_session/gui_session.h) directly.
 */

#pragma once

#include <base/attached_ram_dataspace.h>
#include <base/session_object.h>
#include <gui_session/connection.h>
#include <input/component.h>
#include <os/dynamic_rom_session.h>
#include <util/reconstructible.h>

#include "wm_registry.h"

namespace Sponge::Wm_core {


enum class Role { CONTENT, DECORATOR, LAYOUTER };


struct Wm_session;


/*
 * Hub interface implemented by Main (main.cc): everything a session
 * needs from the registry, the reports, and the input router.
 */
struct Wm_ops : Genode::Interface
{
	/* window identity (content role): returns the fresh wid (0 = full) */
	virtual unsigned long new_window(Genode::Session_label const &label) = 0;

	/* client truth (content role) -> model + report dirt */
	virtual void client_geometry(unsigned long wid,
	                             int x, int y, long w, long h) = 0;
	virtual void client_title(unsigned long wid, char const *title) = 0;
	virtual void client_alpha(unsigned long wid, bool alpha) = 0;

	/* input routing (called from the session's real-input pump) */
	virtual void route_input(Input::Event const &ev,
	                         Role source_role, unsigned long source_wid) = 0;

	/* a content press is a focus-truth click (also raises) */
	virtual void content_pressed(unsigned long wid) = 0;

	/* stacking target (raise the window's chrome + content pair) */
	virtual void raise_window(unsigned long wid) = 0;

	/* chrome-view association (decorator role) */
	virtual void chrome_attach(unsigned long wid) = 0;
	virtual void chrome_detach(unsigned long wid) = 0;

	/* info-ROM sigh interest -> frozen window_list 'resizeable' */
	virtual void resizeable_changed(unsigned long wid, bool value) = 0;

	/* exclusive grab request (layouter role) */
	virtual void set_grab(bool enabled) = 0;

	/* info ROM data (app-visible resize/close channel) */
	virtual void info_state(unsigned long wid,
	                        long &w, long &h, bool &close) const = 0;
	virtual void screen_area(long &w, long &h) const = 0;

	/*
	 * Map a virtual Gui::Session capability (of one of the core's own
	 * sessions) to its real nitpicker counterpart, for the 'focus'
	 * delegation RPC. Returns an invalid capability when unknown.
	 */
	virtual Gui::Session_capability real_cap(
		Gui::Session_capability virtual_cap) const = 0;

	/* view placement data */
	virtual Wm_protocol::Rect content_rect(unsigned long wid) const = 0;
	virtual Wm_protocol::Rect chrome_rect(unsigned long wid) const = 0;

	/*
	 * Input translation offset for a content window:
	 * client = screen + offset (= virt origin - inner origin).
	 */
	virtual void input_offset(unsigned long wid, int &dx, int &dy) const = 0;

	/* lifecycle */
	virtual void register_session(Wm_session &session) = 0;

	/*
	 * The first top-level view of a content session (re)opens its
	 * window entry: a previously destroyed window re-registers under
	 * its SAME wid with fresh per-window state (same-session view
	 * recreation). No-op while the entry is present.
	 */
	virtual void content_view_created(unsigned long wid,
	                                  Genode::Session_label const &label) = 0;
	virtual void content_view_destroyed(unsigned long wid) = 0;
	virtual void session_destroyed(Wm_session &session) = 0;
};


class Wm_session : public Genode::Session_object<Gui::Session>
{
	private:

		Genode::Env &_env;
		Wm_ops     &_ops;

		Role const _role;

		/*
		 * Window identity (CONTENT role; the wid the decorator's
		 * chrome views are associated with is per-view, not per-
		 * session). Zero for non-content sessions.
		 */
		unsigned long const _wid;

		/*
		 * Session-derived window truth (buffer mode / info-ROM sigh),
		 * remembered so a recreated top-level view can re-apply it
		 * after same-wid resurrection.
		 */
		bool _buffer_alpha    { false };
		bool _info_sigh_valid { false };

		/* the real backing session at nitpicker */
		Gui::Connection _real;

		/* virtual input handed to the client (never fed for the
		 * decorator role — the documented dummy) */
		struct Input_action : Input::Session_component::Action
		{
			Wm_session &_session;

			void exclusive_input_requested(bool enabled) override;

			Input_action(Wm_session &s) : _session(s) { }
		} _input_action { *this };

		Input::Session_component _input {
			_env.ep(), _env.ram(), _env.rm(), _input_action };

		/* the v1.1 app-visible resize/close channel (Gui::Session::info) */
		struct Info_rom : Genode::Dynamic_rom_session::Xml_producer,
		                  Genode::Dynamic_rom_session
		{
			Wm_session &_session;

			void produce_xml(Genode::Xml_generator &g) override;

			void sigh(Genode::Signal_context_capability sigh) override;

			Info_rom(Wm_session &session)
			:
				Genode::Dynamic_rom_session::Xml_producer("info"),
				Genode::Dynamic_rom_session(session._env.ep(), session._env.ram(),
				                            session._env.rm(), *this),
				_session(session)
			{ }
		};

		Genode::Constructible<Info_rom> _info_rom { };

		/* the client-side command buffer (Gui::Session::command_dataspace) */
		Genode::Attached_ram_dataspace _command_ds {
			_env.ram(), _env.rm(), sizeof(Gui::Session::Command_buffer) };

		Gui::Session::Command_buffer &_command_buffer {
			*_command_ds.local_addr<Gui::Session::Command_buffer>() };

		/* view bookkeeping: client Gui::View_id <-> real Gui::View_id */
		enum { MAX_VIEWS = 64 };

		struct View_slot
		{
			bool              used       { false };
			Gui::View_ref     ref        { };
			Genode::Constructible<Gui::View_ids::Element> real_id { };

			/* content role: true for the window's top-level view */
			bool              top_level  { false };

			/* created via 'child_view' (parent-relative, forwarded verbatim) */
			bool              child      { false };

			/* decorator role: the associated wid (0 = plain view) */
			unsigned long     chrome_wid { 0 };

			void clear()
			{
				real_id.destruct();
				used       = false;
				top_level  = false;
				child      = false;
				chrome_wid = 0;
			}
		};

		View_slot _views[MAX_VIEWS] { };

		View_slot *_slot(Gui::View_id id)
		{
			for (unsigned i = 0; i < MAX_VIEWS; i++)
				if (_views[i].used && _views[i].real_id->id().value == id.value)
					return &_views[i];
			return nullptr;
		}

		View_slot *_alloc_slot()
		{
			for (unsigned i = 0; i < MAX_VIEWS; i++)
				if (!_views[i].used)
					return &_views[i];
			return nullptr;
		}

		/*
		 * Real view geometry for a forwarded top-level view:
		 * client position + (inner origin - virt origin). Child views
		 * are parent-relative and forwarded verbatim.
		 */
		void _forward_geometry(View_slot const &slot, Gui::Rect const &rect);

		void _execute_command(Gui::Session::Command const &cmd);

		void _handle_real_input();

		Genode::Signal_handler<Wm_session> _input_handler {
			_env.ep(), *this, &Wm_session::_handle_real_input };

	public:

		/*
		 * Constructor. 'wid' is the registry wid for CONTENT
		 * sessions (0 otherwise). The real backing session label is
		 * the client's own session label (kept verbatim so
		 * nitpicker-side policies and reports stay meaningful).
		 */
		Wm_session(Genode::Env &env, Wm_ops &ops, Role role,
		           unsigned long wid,
		           Genode::Session::Resources const &res,
		           Genode::Session_label const &label);

		~Wm_session();

		Role          role() const { return _role; }
		unsigned long wid()  const { return _wid; }

		/* deliver a routed input event into the client's input stream */
		void submit_input(Input::Event const &ev);

		/* core-owned view placement (called by the hub) */
		void place_content(Wm_protocol::Rect const &rect);
		void place_chrome(unsigned long wid, Wm_protocol::Rect const &rect);
		void raise_content();
		void raise_chrome(unsigned long wid);

		/* info ROM refresh (close owed, placement/screen changes) */
		void trigger_info();

		/* the real nitpicker Gui session behind this virtual session */
		Gui::Session_capability real_cap() const { return _real.cap(); }


		/******************************
		 ** Gui::Session interface **
		 ******************************/

		Framebuffer::Session_capability framebuffer() override;
		Input::Session_capability        input() override;
		Info_result                      info() override;

		View_result          view(Gui::View_id, View_attr const &) override;
		Child_view_result    child_view(Gui::View_id, Gui::View_id parent,
		                               View_attr const &) override;
		void                 destroy_view(Gui::View_id) override;
		Associate_result     associate(Gui::View_id, Gui::View_capability) override;
		View_capability_result view_capability(Gui::View_id) override;
		void                 release_view_id(Gui::View_id) override;

		Genode::Dataspace_capability command_dataspace() override;
		void                 execute() override;
		Buffer_result        buffer(Framebuffer::Mode) override;
		void                 focus(Gui::Session_capability) override;
};


}  /* namespace Sponge::Wm_core */
