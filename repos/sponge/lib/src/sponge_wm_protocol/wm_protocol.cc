/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Sponge::Wm_protocol — Sponge-owned WM protocol v1
 * report/ROM shapes (docs/05-sponge-de.md §4.6, frozen 2026-11-12).
 *
 * Parse functions live as templates in <sponge/wm_protocol.h> (they
 * read through Genode::Node with the exact attribute names and
 * defaults of the delivered TasklistController consumption,
 * src/sponge-de/panel/tasklist_controller.cc). Generation writes
 * through the caller's Genode::Xml_generator in the upstream producer
 * attribute order (genode/repos/gems/src/server/wm/window_registry.h,
 * genode/repos/gems/src/app/window_layouter/window.h).
 *
 * Purity: no Env, no Report/ROM sessions, no File_system, no hardware
 * access, no exceptions, no mutable global state, no dynamic
 * allocation. Every output buffer is owned by the caller.
 */

#include <sponge/wm_protocol.h>

using namespace Sponge::Wm_protocol;


/* ====================== geometry semantics ====================== */

bool Sponge::Wm_protocol::parked(int xpos, int ypos)
{
	return xpos <= PARKED_COORD && ypos <= PARKED_COORD;
}


Position Sponge::Wm_protocol::parked_position()
{
	return { PARKED_COORD, PARKED_COORD };
}


Position Sponge::Wm_protocol::assign_position(int abs_xpos, int abs_ypos,
                                              int boundary_xpos, int boundary_ypos)
{
	return { abs_xpos - boundary_xpos, abs_ypos - boundary_ypos };
}


Genode::String<256> Sponge::Wm_protocol::layout_title(char const *label,
                                                      char const *title)
{
	if (!label) label = "";
	if (!title) title = "";

	bool const has_title = title[0] != '\0';

	return Genode::String<256>(label, (has_title ? " " : ""),
	                           (has_title ? title : ""));
}


/* ===================== internal helpers ========================= */

namespace {

/* 'String::length()' includes the terminating null. */
template <Genode::size_t CAP>
bool empty(Genode::String<CAP> const &s)
{
	return s.string()[0] == '\0';
}

}  /* anonymous namespace */


/* ======================= generation (XML) ======================= */

bool Sponge::Wm_protocol::gen_window_list_entry(Genode::Xml_generator &g,
                                                Window_list_entry const &e)
{
	/* omit: no label identity, or no defined size (contract §window_list) */
	if (empty(e.label) || e.width <= 0 || e.height <= 0)
		return false;

	g.node("window", [&] {
		g.attribute("id",     e.id);
		g.attribute("label",  e.label.string());
		g.attribute("title",  e.title.string());
		g.attribute("width",  e.width);
		g.attribute("height", e.height);

		if (e.has_alpha)  g.attribute("has_alpha",  "yes");
		if (e.hidden)     g.attribute("hidden",     "yes");
		if (e.resizeable) g.attribute("resizeable", "yes");
	});

	return true;
}


bool Sponge::Wm_protocol::gen_window_layout_window(Genode::Xml_generator &g,
                                                   Layout_window const &w)
{
	/* omit: no title identity (contract §window_layout) */
	if (empty(w.title))
		return false;

	g.node("window", [&] {
		g.attribute("title",  w.title.string());
		g.attribute("xpos",   w.xpos);
		g.attribute("ypos",   w.ypos);
		g.attribute("width",  w.width);
		g.attribute("height", w.height);

		if (w.focused)
			g.attribute("focused", "yes");
	});

	return true;
}


void Sponge::Wm_protocol::gen_boundary(Genode::Xml_generator &g,
                                       Boundary const &b,
                                       Layout_window const *windows,
                                       Genode::size_t num_windows)
{
	g.node("boundary", [&] {
		g.attribute("name",   b.name.string());
		g.attribute("xpos",   b.xpos);
		g.attribute("ypos",   b.ypos);
		g.attribute("width",  b.width);
		g.attribute("height", b.height);

		for (Genode::size_t i = 0; i < num_windows; i++)
			gen_window_layout_window(g, windows[i]);
	});
}


/* ============= internal protocol v1.1 (binding) ================= */

Genode::String<32> Sponge::Wm_protocol::view_title(unsigned long wid)
{
	return Genode::String<32>(wid);
}


bool Sponge::Wm_protocol::parse_view_title(char const *title,
                                           unsigned long &wid)
{
	if (!title || title[0] == '\0')
		return false;

	/* canonical decimal: no sign, no leading zeros */
	if (title[0] == '0' && title[1] != '\0')
		return false;

	unsigned long v = 0;
	for (char const *p = title; *p; p++) {
		if (*p < '0' || *p > '9')
			return false;
		unsigned const d = (unsigned)(*p - '0');
		if (v > (~0UL - d) / 10)
			return false;
		v = v * 10 + d;
	}

	wid = v;
	return true;
}


void Sponge::Wm_protocol::gen_focus_state(Genode::Xml_generator &g,
                                          Focus_target const &t)
{
	if (!t.set)
		return;

	g.node("window", [&] { g.attribute("wid", t.wid); });
}


void Sponge::Wm_protocol::gen_focus(Genode::Xml_generator &g,
                                    Focus_target const &t)
{
	gen_focus_state(g, t);
}


void Sponge::Wm_protocol::gen_window_request_entry(Genode::Xml_generator &g,
                                                   Window_request const &r)
{
	g.node("window", [&] {
		g.attribute("wid", r.wid);

		if (r.close) {
			g.attribute("action", "close");
			return;
		}

		g.attribute("x", r.x);
		g.attribute("y", r.y);
		g.attribute("w", r.w);
		g.attribute("h", r.h);
	});
}


void Sponge::Wm_protocol::gen_window_chrome_entry(Genode::Xml_generator &g,
                                                  Window_chrome const &c)
{
	g.node("window", [&] {
		g.attribute("wid",   c.wid);
		g.attribute("label", c.label.string());
		g.attribute("title", c.title.string());

		if (c.resizeable)
			g.attribute("resizeable", "yes");

		g.attribute("fw", c.fw);
		g.attribute("fh", c.fh);
	});
}


void Sponge::Wm_protocol::gen_decorator_margins(Genode::Xml_generator &g,
                                                Margins const &m)
{
	g.node("floating", [&] {
		g.attribute("top",    m.top);
		g.attribute("bottom", m.bottom);
		g.attribute("left",   m.left);
		g.attribute("right",  m.right);
	});
}


/* ================== inner/outer frame geometry ================== */

Sponge::Wm_protocol::Rect
Sponge::Wm_protocol::outer_from_inner(Rect inner, Margins const &m)
{
	return Rect { inner.x - m.left, inner.y - m.top,
	              inner.w + m.left + m.right,
	              inner.h + m.top + m.bottom };
}


Sponge::Wm_protocol::Rect
Sponge::Wm_protocol::inner_from_outer(Rect outer, Margins const &m)
{
	return Rect { outer.x + m.left, outer.y + m.top,
	              outer.w - m.left - m.right,
	              outer.h - m.top - m.bottom };
}


/* ============== frozen chrome layout + hit testing ============== */

namespace {

using namespace Sponge::Wm_protocol;

bool in_rect(Rect const &r, int px, int py)
{
	return px >= r.x && py >= r.y
	    && px <  r.x + r.w && py < r.y + r.h;
}

int grip_depth(int margin)
{
	return margin < CHROME_GRIP_MAX ? margin : CHROME_GRIP_MAX;
}

}  /* anonymous namespace */


Rect Sponge::Wm_protocol::title_band(Rect outer, Margins const &m)
{
	return Rect { outer.x, outer.y, outer.w, m.top };
}


Rect Sponge::Wm_protocol::button_rect(Rect outer, Margins const &m,
                                      Button b, bool resizeable)
{
	int slot = -1;
	switch (b) {
	case Button::CLOSE:      slot = 0;                      break;
	case Button::FULLSCREEN: slot = resizeable ? 1 : -1;   break;
	case Button::MINIMIZE:   slot = resizeable ? 2 : 1;    break;
	}

	if (slot < 0)
		return Rect { };

	long const right = outer.x + outer.w;
	int const x = (int)(right - (slot + 1)
	                    * ((long)CHROME_BUTTON_W + CHROME_BUTTON_PAD));
	int const y = outer.y + (m.top - CHROME_BUTTON_H) / 2;

	return Rect { x, y, CHROME_BUTTON_W, CHROME_BUTTON_H };
}


Sizer_zone Sponge::Wm_protocol::sizer_zone(int px, int py, Rect outer,
                                           Margins const &m)
{
	long const gx = (long)px - outer.x;
	long const gy = (long)py - outer.y;

	if (gx < 0 || gy < 0 || gx >= outer.w || gy >= outer.h)
		return Sizer_zone::NONE;

	/* 16x16 corner squares override the edge grips */
	if (gx < CHROME_CORNER_PX && gy < CHROME_CORNER_PX)
		return Sizer_zone::NW;
	if (gx >= outer.w - CHROME_CORNER_PX && gy < CHROME_CORNER_PX)
		return Sizer_zone::NE;
	if (gx < CHROME_CORNER_PX && gy >= outer.h - CHROME_CORNER_PX)
		return Sizer_zone::SW;
	if (gx >= outer.w - CHROME_CORNER_PX && gy >= outer.h - CHROME_CORNER_PX)
		return Sizer_zone::SE;

	/* edge grips: outermost min(margin, 8) px of each edge */
	if (gy < grip_depth(m.top))
		return Sizer_zone::N;
	if (gy >= outer.h - grip_depth(m.bottom))
		return Sizer_zone::S;
	if (gx < grip_depth(m.left))
		return Sizer_zone::W;
	if (gx >= outer.w - grip_depth(m.right))
		return Sizer_zone::E;

	return Sizer_zone::NONE;
}


Hit Sponge::Wm_protocol::chrome_hit(int px, int py, Rect outer,
                                    Margins const &m, bool resizeable)
{
	/* precedence: corner > edge grip > button > title/move */
	switch (sizer_zone(px, py, outer, m)) {
	case Sizer_zone::NONE: break;
	case Sizer_zone::N:  return Hit::SIZER_N;
	case Sizer_zone::S:  return Hit::SIZER_S;
	case Sizer_zone::E:  return Hit::SIZER_E;
	case Sizer_zone::W:  return Hit::SIZER_W;
	case Sizer_zone::NE: return Hit::SIZER_NE;
	case Sizer_zone::NW: return Hit::SIZER_NW;
	case Sizer_zone::SE: return Hit::SIZER_SE;
	case Sizer_zone::SW: return Hit::SIZER_SW;
	}

	if (in_rect(button_rect(outer, m, Button::CLOSE, resizeable), px, py))
		return Hit::BTN_CLOSE;

	if (resizeable
	 && in_rect(button_rect(outer, m, Button::FULLSCREEN, true), px, py))
		return Hit::BTN_FULLSCREEN;

	if (in_rect(button_rect(outer, m, Button::MINIMIZE, resizeable), px, py))
		return Hit::BTN_MINIMIZE;

	if (in_rect(title_band(outer, m), px, py))
		return Hit::TITLE_MOVE;

	return Hit::NONE;
}


