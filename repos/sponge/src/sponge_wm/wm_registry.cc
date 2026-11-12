/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_registry — sponge_wm window registry and protocol-state model.
 * See wm_registry.h for the contract. Pure: no Env, no sessions, no
 * allocation (fixed window table).
 */

#include "wm_registry.h"

using namespace Sponge::Wm_registry;
using Sponge::Wm_protocol::Window_chrome;
using Sponge::Wm_protocol::Window_list_entry;
using Sponge::Wm_protocol::gen_window_chrome_entry;
using Sponge::Wm_protocol::gen_window_list_entry;
using Sponge::Wm_protocol::outer_from_inner;


unsigned long Registry::create_window(Genode::String<256> const &label)
{
	if (_count >= MAX_WINDOWS)
		return 0;

	for (unsigned i = 0; i < MAX_WINDOWS; i++) {
		if (_windows[i].used)
			continue;

		Window &w = _windows[i];
		w = Window { };
		w.used = true;
		w.wid  = _next_wid++;
		w.label = label;
		_count++;
		return w.wid;
	}
	return 0;
}


Dirty Registry::recreate_window(unsigned long wid, Genode::String<256> const &label)
{
	if (wid == 0 || _slot(wid) || _count >= MAX_WINDOWS)
		return Dirty::NONE;

	for (unsigned i = 0; i < MAX_WINDOWS; i++) {
		if (_windows[i].used)
			continue;

		Window &w = _windows[i];
		w = Window { };
		w.used = true;
		w.wid  = wid;
		w.label = label;
		_count++;
		return Dirty::WINDOW_LIST | Dirty::CHROME;
	}
	return Dirty::NONE;
}


Dirty Registry::destroy_window(unsigned long wid)
{
	Window *w = _slot(wid);
	if (!w)
		return Dirty::NONE;

	Dirty dirty = Dirty::WINDOW_LIST | Dirty::CHROME;

	if (_focus.set && _focus.wid == wid) {
		_focus = Focus_target { };
		dirty |= Dirty::FOCUS;
	}

	*w = Window { };
	_count--;
	return dirty;
}


Window const *Registry::by_label(char const *label) const
{
	if (!label || label[0] == '\0')
		return nullptr;

	for (unsigned i = 0; i < MAX_WINDOWS; i++) {
		Window const &w = _windows[i];
		if (!w.used)
			continue;
		if (Genode::strcmp(w.label.string(), label) == 0)
			return &w;
	}
	return nullptr;
}


Dirty Registry::set_title(unsigned long wid, char const *title)
{
	Window *w = _slot(wid);
	if (!w)
		return Dirty::NONE;

	Genode::String<256> const next(title ? title : "");
	if (w->title == next)
		return Dirty::NONE;

	w->title = next;
	return Dirty::WINDOW_LIST | Dirty::CHROME;
}


Dirty Registry::set_client_geometry(unsigned long wid, int virt_x, int virt_y,
                                    long w, long h)
{
	Window *win = _slot(wid);
	if (!win)
		return Dirty::NONE;

	bool const size_changed = (win->client_w != w) || (win->client_h != h);

	win->virt_x = virt_x;
	win->virt_y = virt_y;
	win->client_w = w;
	win->client_h = h;

	if (!size_changed)
		return Dirty::NONE;

	/* the window_list size is the client size (frozen pass-through) */
	Dirty dirty = Dirty::WINDOW_LIST;

	/*
	 * While unplaced, the transient inner rect follows the client
	 * rect, so a client size change also changes the outer frame.
	 */
	if (!win->placed)
		dirty |= Dirty::CHROME;

	return dirty;
}


Dirty Registry::set_has_alpha(unsigned long wid, bool value)
{
	Window *w = _slot(wid);
	if (!w || w->has_alpha == value)
		return Dirty::NONE;

	w->has_alpha = value;
	return Dirty::WINDOW_LIST;
}


Dirty Registry::set_resizeable(unsigned long wid, bool value)
{
	Window *w = _slot(wid);
	if (!w || w->resizeable == value)
		return Dirty::NONE;

	w->resizeable = value;
	return Dirty::WINDOW_LIST | Dirty::CHROME;
}


Dirty Registry::place(unsigned long wid, int x, int y, long w, long h)
{
	Window *win = _slot(wid);
	if (!win)
		return Dirty::NONE;

	bool const size_changed = !win->placed
	                       || win->inner_w != w || win->inner_h != h;

	win->placed  = true;
	win->inner_x = x;
	win->inner_y = y;
	win->inner_w = w;
	win->inner_h = h;

	/* v1.1: publish SIZE changes, never move */
	return size_changed ? Dirty::CHROME : Dirty::NONE;
}


bool Registry::request_close(unsigned long wid)
{
	Window *w = _slot(wid);
	if (!w)
		return false;

	w->close_requested = true;

	if (w->close_applied)
		return false;

	w->close_applied = true;
	return true;
}


Dirty Registry::set_focus(Focus_target target)
{
	/*
	 * Normalize: a set target naming a vanished window clears the
	 * focus (the truth snapshot must never reference a dead wid).
	 */
	if (target.set && !_slot(target.wid))
		target = Focus_target { };

	if (_focus.set == target.set && _focus.wid == target.wid)
		return Dirty::NONE;

	_focus = target;
	return Dirty::FOCUS | Dirty::CHROME;
}


Dirty Registry::set_margins(Margins const &m)
{
	if (_margins.top == m.top && _margins.bottom == m.bottom
	 && _margins.left == m.left && _margins.right == m.right)
		return Dirty::NONE;

	_margins = m;
	return Dirty::CHROME;
}


bool Registry::attach_chrome(unsigned long wid)
{
	Window *w = _slot(wid);
	if (!w)
		return false;
	w->chrome_present = true;
	return true;
}


bool Registry::detach_chrome(unsigned long wid)
{
	Window *w = _slot(wid);
	if (!w)
		return false;
	w->chrome_present = false;
	return true;
}


Rect Registry::inner_rect(unsigned long wid) const
{
	Window const *w = _slot(wid);
	if (!w)
		return Rect { };

	if (w->placed)
		return Rect { w->inner_x, w->inner_y, w->inner_w, w->inner_h };

	/* transient initial placement: the client rect */
	return Rect { w->virt_x, w->virt_y, w->client_w, w->client_h };
}


Rect Registry::outer_rect(unsigned long wid) const
{
	return outer_from_inner(inner_rect(wid), _margins);
}


void Registry::requested_size(unsigned long wid, long &w, long &h) const
{
	Window const *win = _slot(wid);
	if (!win) {
		w = 0;
		h = 0;
		return;
	}
	if (win->placed) {
		w = win->inner_w;
		h = win->inner_h;
		return;
	}
	w = win->client_w;
	h = win->client_h;
}


void Registry::gen_window_list(Genode::Xml_generator &g) const
{
	for_each_window([&] (Window const &w) {
		Window_list_entry e;
		e.id         = w.wid;
		e.label      = w.label;
		e.title      = w.title;
		e.width      = w.client_w;
		e.height     = w.client_h;
		e.has_alpha  = w.has_alpha;
		e.hidden     = w.hidden;
		e.resizeable = w.resizeable;

		/* omission rules (no label identity / no size) live in the helper */
		gen_window_list_entry(g, e);
	});
}


void Registry::gen_focus_state(Genode::Xml_generator &g) const
{
	Wm_protocol::gen_focus_state(g, _focus);
}


void Registry::gen_window_chrome(Genode::Xml_generator &g) const
{
	for_each_window([&] (Window const &w) {
		/*
		 * Chrome exists for visible (sized) windows only: a window
		 * without a defined size has no frame to draw.
		 */
		if (!w.sized())
			return;

		Rect const outer = outer_from_inner(inner_rect(w.wid), _margins);

		Window_chrome c;
		c.wid        = w.wid;
		c.label      = w.label;
		c.title      = w.title;
		c.resizeable = w.resizeable;
		c.focused    = _focus.set && _focus.wid == w.wid;
		c.fw         = outer.w;
		c.fh         = outer.h;

		gen_window_chrome_entry(g, c);
	});
}
