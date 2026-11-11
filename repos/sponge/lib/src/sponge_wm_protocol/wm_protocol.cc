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


