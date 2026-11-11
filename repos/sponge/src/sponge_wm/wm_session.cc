/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_session — sponge_wm Gui-session virtualization. See wm_session.h
 * for the role semantics and the v1.1 contract references.
 *
 * Public Genode interfaces only (AGENTS.md §3.1/D16W.2): the session
 * implements os/include/gui_session/gui_session.h directly and wraps
 * one real Gui::Connection to nitpicker per session.
 */

#include "wm_session.h"

#include <input/keycodes.h>

using namespace Sponge::Wm_core;
using Sponge::Wm_protocol::parse_view_title;


void Wm_session::Input_action::exclusive_input_requested(bool enabled)
{
	switch (_session._role) {
	case Role::CONTENT:
		_session._real.input.exclusive(enabled);
		break;
	case Role::LAYOUTER:
		_session._ops.set_grab(enabled);
		break;
	case Role::DECORATOR:
		break;
	}
}


void Wm_session::Info_rom::produce_xml(Genode::Xml_generator &g)
{
	long sw = 0, sh = 0;
	_session._ops.screen_area(sw, sh);
	g.attribute("width",  sw);
	g.attribute("height", sh);

	long w = 0, h = 0;
	bool close = false;
	_session._ops.info_state(_session._wid, w, h, close);

	if (close) {
		g.node("capture", [&] { g.attribute("closed", "yes"); });
		return;
	}

	g.node("capture", [&] {
		g.attribute("width",  w);
		g.attribute("height", h); });
}


void Wm_session::Info_rom::sigh(Genode::Signal_context_capability sigh)
{
	Genode::Dynamic_rom_session::sigh(sigh);

	if (_session._role == Role::CONTENT)
		_session._ops.resizeable_changed(_session._wid, sigh.valid());
}


Wm_session::Wm_session(Genode::Env &env, Wm_ops &ops, Role role,
                       unsigned long wid,
                       Genode::Session::Resources const &res,
                       Genode::Session_label const &label)
:
	Genode::Session_object<Gui::Session>(env.ep(), res, label),
	_env(env), _ops(ops), _role(role), _wid(wid),
	_real(env, label)
{
	if (_role != Role::DECORATOR)
		_input.event_queue().enabled(true);

	_real.input.sigh(_input_handler);
	_ops.register_session(*this);
}


Wm_session::~Wm_session()
{
	_ops.session_destroyed(*this);
}


void Wm_session::submit_input(Input::Event const &ev)
{
	_input.submit(ev);
}


void Wm_session::place_content(Wm_protocol::Rect const &rect)
{
	for (unsigned i = 0; i < MAX_VIEWS; i++) {
		View_slot &s = _views[i];
		if (!s.used || !s.top_level)
			continue;
		_real.enqueue<Gui::Session::Command::Geometry>(
			s.real_id->id(), Gui::Rect { { rect.x, rect.y },
			                             { (unsigned)rect.w, (unsigned)rect.h } });
	}
	_real.execute();
}


void Wm_session::place_chrome(unsigned long wid, Wm_protocol::Rect const &rect)
{
	for (unsigned i = 0; i < MAX_VIEWS; i++) {
		View_slot &s = _views[i];
		if (!s.used || s.chrome_wid != wid || wid == 0)
			continue;
		_real.enqueue<Gui::Session::Command::Geometry>(
			s.real_id->id(), Gui::Rect { { rect.x, rect.y },
			                             { (unsigned)rect.w, (unsigned)rect.h } });
	}
	_real.execute();
}


void Wm_session::raise_content()
{
	for (unsigned i = 0; i < MAX_VIEWS; i++) {
		View_slot &s = _views[i];
		if (!s.used || !s.top_level)
			continue;
		_real.enqueue<Gui::Session::Command::Front>(s.real_id->id());
	}
	_real.execute();
}


void Wm_session::raise_chrome(unsigned long wid)
{
	if (wid == 0)
		return;
	for (unsigned i = 0; i < MAX_VIEWS; i++) {
		View_slot &s = _views[i];
		if (!s.used || s.chrome_wid != wid)
			continue;
		_real.enqueue<Gui::Session::Command::Front>(s.real_id->id());
	}
	_real.execute();
}


void Wm_session::trigger_info()
{
	if (_info_rom.constructed())
		_info_rom->trigger_update();
}


void Wm_session::_forward_geometry(View_slot const &slot, Gui::Rect const &rect)
{
	if (slot.child) {
		_real.enqueue<Gui::Session::Command::Geometry>(slot.real_id->id(), rect);
		return;
	}

	if (_role == Role::CONTENT) {
		if (slot.top_level) {
			/* placement authority: the v1.1 inner rect */
			Wm_protocol::Rect const inner = _ops.content_rect(_wid);
			_real.enqueue<Gui::Session::Command::Geometry>(
				slot.real_id->id(),
				Gui::Rect { { inner.x, inner.y },
				            { (unsigned)inner.w, (unsigned)inner.h } });
			return;
		}

		/* extra top-level: client space translated to screen space */
		int dx = 0, dy = 0;
		_ops.input_offset(_wid, dx, dy);
		_real.enqueue<Gui::Session::Command::Geometry>(
			slot.real_id->id(),
			Gui::Rect { { rect.x1() - dx, rect.y1() - dy }, rect.area });
		return;
	}

	if (_role == Role::DECORATOR && slot.chrome_wid) {
		/* core-owned chrome placement (outer frame) */
		Wm_protocol::Rect const outer = _ops.chrome_rect(slot.chrome_wid);
		_real.enqueue<Gui::Session::Command::Geometry>(
			slot.real_id->id(),
			Gui::Rect { { outer.x, outer.y },
			            { (unsigned)outer.w, (unsigned)outer.h } });
		return;
	}

	_real.enqueue<Gui::Session::Command::Geometry>(slot.real_id->id(), rect);
}


void Wm_session::_execute_command(Gui::Session::Command const &cmd)
{
	using Command = Gui::Session::Command;

	switch (cmd.opcode) {

	case Command::GEOMETRY:
	{
		View_slot *s = _slot(cmd.geometry.view);
		if (!s)
			return;

		if (_role == Role::CONTENT && s->top_level) {
			_ops.client_geometry(_wid, cmd.geometry.rect.x1(), cmd.geometry.rect.y1(),
			                     (long)cmd.geometry.rect.w(),
			                     (long)cmd.geometry.rect.h());
		}
		_forward_geometry(*s, cmd.geometry.rect);
		return;
	}

	case Command::OFFSET:
	{
		View_slot *s = _slot(cmd.offset.view);
		if (!s)
			return;
		_real.enqueue<Command::Offset>(s->real_id->id(), cmd.offset.offset);
		return;
	}

	case Command::TITLE:
	{
		View_slot *s = _slot(cmd.title.view);
		if (!s)
			return;

		if (_role == Role::CONTENT && s->top_level)
			_ops.client_title(_wid, cmd.title.title.string());

		if (_role == Role::DECORATOR) {
			unsigned long wid = 0;
			parse_view_title(cmd.title.title.string(), wid);
			if (s->chrome_wid != wid) {
				if (s->chrome_wid)
					_ops.chrome_detach(s->chrome_wid);
				s->chrome_wid = wid;
				if (wid)
					_ops.chrome_attach(wid);
			}
		}
		_real.enqueue<Command::Title>(s->real_id->id(), cmd.title.title);
		return;
	}

	case Command::FRONT:
	{
		View_slot *s = _slot(cmd.front.view);
		if (!s)
			return;

		if (_role == Role::CONTENT && s->top_level) {
			/* the client raises its window -> the core raises the pair */
			_ops.raise_window(_wid);
			return;
		}
		if (_role == Role::DECORATOR && s->chrome_wid)
			return;  /* chrome stacking is core-owned */
		_real.enqueue<Command::Front>(s->real_id->id());
		return;
	}

	case Command::BACK:
	case Command::FRONT_OF:
	case Command::BEHIND_OF:
	{
		Gui::View_id const id =
			(cmd.opcode == Command::BACK)         ? cmd.back.view :
			(cmd.opcode == Command::FRONT_OF)     ? cmd.front_of.view
			                                      : cmd.behind_of.view;

		View_slot *s = _slot(id);
		if (!s)
			return;

		if (_role == Role::DECORATOR && s->chrome_wid)
			return;

		switch (cmd.opcode) {
		case Command::BACK:
			_real.enqueue<Command::Back>(s->real_id->id());
			break;
		case Command::FRONT_OF:
		{
			View_slot *n = _slot(cmd.front_of.neighbor);
			if (n)
				_real.enqueue<Command::Front_of>(s->real_id->id(),
				                                n->real_id->id());
			break;
		}
		default:
		{
			View_slot *n = _slot(cmd.behind_of.neighbor);
			if (n)
				_real.enqueue<Command::Behind_of>(s->real_id->id(),
				                                 n->real_id->id());
			break;
		}
		}
		return;
	}

	case Command::BACKGROUND:
	{
		View_slot *s = _slot(cmd.background.view);
		if (!s)
			return;
		_real.enqueue<Command::Background>(s->real_id->id());
		return;
	}

	case Command::NOP:
		return;
	}
}


void Wm_session::_handle_real_input()
{
	_real.input.for_each_event([&] (Input::Event const &ev) {
		_ops.route_input(ev, _role, _role == Role::CONTENT ? _wid : 0);
	});
}


Framebuffer::Session_capability Wm_session::framebuffer()
{
	return _real.framebuffer.rpc_cap();
}


Input::Session_capability Wm_session::input()
{
	return _input.cap();
}


Wm_session::Info_result Wm_session::info()
{
	if (!_info_rom.constructed()) {
		using Ram  = Genode::Ram_quota;
		using Caps = Genode::Cap_quota;

		if (!_ram_quota_guard().try_withdraw(Ram { 8*1024 }))
			return Info_error::OUT_OF_RAM;
		if (!_cap_quota_guard().try_withdraw(Caps { 2 }))
			return Info_error::OUT_OF_CAPS;

		_info_rom.construct(*this);
	}
	return Info_result { _info_rom->cap() };
}


Wm_session::View_result Wm_session::view(Gui::View_id id, View_attr const &attr)
{
	View_slot *s = _alloc_slot();
	if (!s) {
		warning("view table exhausted");
		return View_result::OUT_OF_RAM;
	}

	s->used = true;
	s->real_id.construct(s->ref, _real.view_ids, id);

	_real.view(id, attr);

	if (_role == Role::CONTENT) {
		bool first_top_level = true;
		for (unsigned i = 0; i < MAX_VIEWS; i++)
			if (_views[i].used && _views[i].top_level)
				first_top_level = false;

		if (first_top_level) {
			s->top_level = true;
			_ops.client_title(_wid, attr.title.string());
			_ops.client_geometry(_wid, attr.rect.x1(), attr.rect.y1(),
			                     (long)attr.rect.w(), (long)attr.rect.h());
		}
		_forward_geometry(*s, attr.rect);
		_real.enqueue<Gui::Session::Command::Title>(id, attr.title);
		if (attr.front)
			_real.enqueue<Gui::Session::Command::Front>(id);
		_real.execute();
		return View_result::OK;
	}

	if (_role == Role::DECORATOR) {
		unsigned long wid = 0;
		parse_view_title(attr.title.string(), wid);
		s->chrome_wid = wid;
		if (wid) {
			_ops.chrome_attach(wid);
			_forward_geometry(*s, attr.rect);
			_real.execute();
			return View_result::OK;
		}
	}

	return View_result::OK;
}


Wm_session::Child_view_result Wm_session::child_view(Gui::View_id id, Gui::View_id parent,
                                                     View_attr const &attr)
{
	if (!_slot(parent))
		return Child_view_result::INVALID;

	View_slot *s = _alloc_slot();
	if (!s) {
		warning("view table exhausted");
		return Child_view_result::OUT_OF_RAM;
	}

	s->used  = true;
	s->child = true;
	s->real_id.construct(s->ref, _real.view_ids, id);

	_real.child_view(id, parent, attr);
	return Child_view_result::OK;
}


void Wm_session::destroy_view(Gui::View_id id)
{
	View_slot *s = _slot(id);
	if (!s)
		return;

	if (_role == Role::CONTENT && s->top_level)
		_ops.content_view_destroyed(_wid);

	if (_role == Role::DECORATOR && s->chrome_wid)
		_ops.chrome_detach(s->chrome_wid);

	_real.destroy_view(id);
	s->clear();
}


Wm_session::Associate_result Wm_session::associate(Gui::View_id id,
                                                   Gui::View_capability view)
{
	if (_slot(id))
		release_view_id(id);

	View_slot *s = _alloc_slot();
	if (!s) {
		warning("view table exhausted");
		return Associate_result::OUT_OF_RAM;
	}

	s->used = true;
	s->real_id.construct(s->ref, _real.view_ids, id);

	_real.associate(id, view);
	return Associate_result::OK;
}


Wm_session::View_capability_result Wm_session::view_capability(Gui::View_id id)
{
	if (!_slot(id))
		return View_capability_error::OUT_OF_RAM;

	return View_capability_result { _real.view_capability(id) };
}


void Wm_session::release_view_id(Gui::View_id id)
{
	View_slot *s = _slot(id);
	if (!s)
		return;

	_real.release_view_id(id);
	s->clear();
}


Genode::Dataspace_capability Wm_session::command_dataspace()
{
	return _command_ds.cap();
}


void Wm_session::execute()
{
	unsigned const n = _command_buffer.num();
	for (unsigned i = 0; i < n; i++)
		_execute_command(_command_buffer.get(i));
}


Wm_session::Buffer_result Wm_session::buffer(Framebuffer::Mode mode)
{
	_real.buffer(mode);

	if (_role == Role::CONTENT)
		_ops.client_alpha(_wid, mode.alpha);

	return Buffer_result::OK;
}


void Wm_session::focus(Gui::Session_capability focused)
{
	_real.focus(_ops.real_cap(focused));
}
