/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_protocol.h — protocol v1 parsers and assertions shared by
 * the native-WM acceptance probes (Criterion 16,
 * docs/plans/phase16-sponge-wm.md D16W.6).
 *
 * The shapes parsed here are FROZEN protocol v1, verbatim from
 * docs/05-sponge-de.md §4.6 ("Protocol v1 report/ROM contract"):
 *
 *   window_list    (sponge_wm      -> TasklistController)  <window ...>
 *   window_layout  (sponge_layouter-> TasklistController)  <boundary><window>
 *   focus_request  (TasklistController -> sponge_layouter)  <focus_request>
 *   rules          (TasklistController -> sponge_layouter)  <rules>
 *
 * The native producers must reproduce the consumed attributes exactly;
 * the helpers below are the reusable acceptance assertions every native
 * probe shares. Nothing here talks to the upstream wm/window_layouter/
 * decorator trio — that fallback keeps its own scenarios
 * (run/sponge-wm{,-qmp,-tasks}.run) untouched.
 *
 * Header-only on purpose: the probes are separate build targets and the
 * shared code must not need a cross-target library. Inline everything.
 *
 * Deliberately independent of the production `sponge_wm_protocol`
 * parser (repos/sponge/include/sponge/wm_protocol.h): this probe-side
 * parser is the acceptance ORACLE for protocol v1. Sharing one parser
 * between the producers and the checks would let a parser/generator
 * misunderstanding self-certify; the duplication is kept on purpose.
 * Per the frozen contract's empty-string convention (and the production
 * library's own idiom), emptiness is tested via string()[0] == '\0',
 * not String::length().
 */

#pragma once

#include <base/log.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace Native_wm {


/*
 * Parking sentinel (docs/05-sponge-de.md §4.6). A reported window whose
 * xpos and ypos are BOTH at or below -32000 reads as parked/minimized —
 * the delivered TasklistController rule the native layouter must keep.
 */
inline constexpr int PARK_X = -32000;
inline constexpr int PARK_Y = -32000;

/*
 * Maximize-inference tolerance (frozen consumer rule): the controller
 * infers Maximized when a non-parked window's inner geometry fills the
 * boundary area within a 64 px decorator-margin tolerance. Native
 * producers must keep this geometry-only truth working — v1 adds no
 * maximized attribute to window_layout (that would be a protocol bump).
 */
inline constexpr unsigned MAXIMIZE_FILL_TOL = 64;


struct Geom {
	int      x { 0 };
	int      y { 0 };
	unsigned w { 0 };
	unsigned h { 0 };
	bool     valid { false };
};

inline bool same_geom(Geom const &a, Geom const &b)
{
	return a.valid && b.valid && a.x == b.x && a.y == b.y
	    && a.w == b.w && a.h == b.h;
}

inline void log_geom(char const *tag, char const *what, Geom const &g)
{
	if (!g.valid) {
		Genode::log(tag, ": ", what, " <invalid>");
		return;
	}
	Genode::log(tag, ": ", what, " (", g.x, ",", g.y, ") ", g.w, "x", g.h);
}


/* Boundary rect of window_layout's single <boundary> target ("screen"). */
struct Boundary {
	int      x { 0 };
	int      y { 0 };
	unsigned w { 0 };
	unsigned h { 0 };
	bool     valid { false };
};


inline bool parked(Geom const &g)
{
	return g.valid && g.x <= PARK_X && g.y <= PARK_Y;
}


/*
 * The frozen maximize inference: inner geometry fills the boundary area
 * within MAXIMIZE_FILL_TOL. Kept identical to
 * TasklistController::_recompute_tracked so the native producers and the
 * frozen consumer agree by construction.
 */
inline bool fills_boundary(Geom const &g, Boundary const &b)
{
	return g.valid && b.valid
	    && b.w > 0 && b.h > 0
	    && g.w >= (b.w - MAXIMIZE_FILL_TOL)
	    && g.h >= (b.h - MAXIMIZE_FILL_TOL);
}


/*
 * window_layout's window identity: its `title` attribute is the
 * concatenation `label + " " + qt title` (bare `label` when the client
 * title is empty). The delivered TasklistController matches the pair
 * against window_list — never by splitting on spaces. This helper is
 * the same match, for probe use.
 */
inline bool title_matches(char const *label, char const *qt_title,
                          char const *layout_title)
{
	if (!label || !layout_title) return false;

	/* exact: bare label (empty qt title) */
	if (!qt_title || !*qt_title)
		return Genode::strcmp(label, layout_title) == 0;

	/* label + " " + qt_title */
	Genode::size_t const ll = Genode::strlen(label);
	if (Genode::strcmp(layout_title, label, ll) != 0) return false;
	if (layout_title[ll] != ' ') return false;
	return Genode::strcmp(layout_title + ll + 1, qt_title) == 0;
}


/*
 * window_list shape check (protocol v1). Root is <window_list> with
 * <window> sub-nodes. Required per frozen spec: `label` (wm session
 * label, may contain spaces and "->") and `title` (client title).
 * Entries with an empty label are ignored by the consumer (frozen
 * behavior) and skipped here too. `has_alpha` / `hidden` / `resizeable`
 * are consumed booleans — when present they must parse (the Node API
 * bool reader already coerces "yes"/"no").
 *
 * Returns the number of usable <window> entries, or -1 on a shape
 * violation (a FAIL line names the reason; the caller exits).
 */
inline int check_window_list_shape(Genode::Node root, char const *tag)
{
	if (!root.has_type("window_list")) {
		Genode::error(tag, ": window_list root node is '", root.type(),
		              "', expected 'window_list'");
		return -1;
	}

	int count = 0;
	bool ok = true;

	root.for_each_sub_node("window", [&](Genode::Node const &w) {
		if (!ok) return;

		Genode::String<256> const label =
			w.attribute_value("label", Genode::String<256>());

		/* empty label: ignored by the frozen consumer - skip */
		if (label.string()[0] == '\0') return;

		if (!w.has_attribute("title")) {
			Genode::error(tag, ": window_list <window label='",
			              label.string(), "'> has no 'title' attribute");
			ok = false;
			return;
		}

		/* consumed booleans must be present-or-defaultable */
		(void)w.attribute_value("has_alpha",  false);
		(void)w.attribute_value("hidden",     false);
		(void)w.attribute_value("resizeable", false);

		Genode::log(tag, ": window_list entry label='", label.string(),
		            "' title='",
		            w.attribute_value("title", Genode::String<256>()).string(),
		            "' has_alpha=", w.attribute_value("has_alpha",  false) ? "yes" : "no",
		            " hidden=",     w.attribute_value("hidden",     false) ? "yes" : "no",
		            " resizeable=", w.attribute_value("resizeable", false) ? "yes" : "no");
		++count;
	});

	if (!ok) return -1;
	Genode::log(tag, ": window_list shape OK (", count, " window(s))");
	return count;
}


/*
 * window_list lookup by LABEL PREFIX. The wm session label may carry
 * the client-title suffix ("pkg_runtime -> pkg_gui_demo -> Sponge Pkg
 * GUI Demo"), so probes match with the delivered consumer idiom
 * (strncmp on the prefix length — wm_tasks_probe /
 * TasklistController) and read the FULL label back from the entry for
 * exact-label assertions (focus_request carries the verbatim label).
 */
struct Wl_entry {
	Genode::String<256> label { };
	Genode::String<256> title { };
	bool has_alpha  { false };
	bool hidden     { false };
	bool resizeable { false };
	bool found      { false };
};

inline Wl_entry window_list_find_prefix(Genode::Node root,
                                        char const *label_prefix)
{
	Wl_entry e;
	Genode::size_t const n = Genode::strlen(label_prefix);
	root.for_each_sub_node("window", [&](Genode::Node const &w) {
		if (e.found) return;
		Genode::String<256> const l =
			w.attribute_value("label", Genode::String<256>());
		if (Genode::strcmp(l.string(), label_prefix, n) != 0) return;

		e.label      = l;
		e.title      = w.attribute_value("title", Genode::String<256>());
		e.has_alpha  = w.attribute_value("has_alpha",  false);
		e.hidden     = w.attribute_value("hidden",     false);
		e.resizeable = w.attribute_value("resizeable", false);
		e.found      = true;
	});
	return e;
}


/* window_list presence by exact label (protocol identity). */
inline bool window_list_has(Genode::Node root, char const *label)
{
	bool found = false;
	root.for_each_sub_node("window", [&](Genode::Node const &w) {
		if (found) return;
		Genode::String<256> const l =
			w.attribute_value("label", Genode::String<256>());
		if (Genode::strcmp(l.string(), label) == 0) found = true;
	});
	return found;
}


inline bool window_list_resizeable(Genode::Node root, char const *label)
{
	bool found = false;
	root.for_each_sub_node("window", [&](Genode::Node const &w) {
		if (found) return;
		Genode::String<256> const l =
			w.attribute_value("label", Genode::String<256>());
		if (Genode::strcmp(l.string(), label) != 0) return;
		found = w.attribute_value("resizeable", false);
	});
	return found;
}


/*
 * window_layout boundary extraction. The boundary's xpos/ypos/width/
 * height are consumed: xpos/ypos is the coordinate-origin reference
 * (assign coordinates are target-relative) and the size drives the
 * maximize inference.
 */
inline Boundary layout_boundary(Genode::Node root)
{
	Boundary b;
	root.with_sub_node("boundary",
		[&] (Genode::Node const &n) {
			b.x = n.attribute_value("xpos",  0);
			b.y = n.attribute_value("ypos",  0);
			b.w = n.attribute_value("width",  0u);
			b.h = n.attribute_value("height", 0u);
			b.valid = true;
		},
		[&] { });
	return b;
}


/*
 * Find a window in window_layout by its window_list identity pair
 * (label, qt title) using the frozen title-matching rule. Fills geom
 * (screen-absolute inner geometry) and optionally `focused`.
 */
inline bool find_layout_window(Genode::Node root,
                               char const *label, char const *qt_title,
                               Geom &out, bool *focused = nullptr)
{
	bool found = false;
	root.for_each_sub_node("boundary", [&](Genode::Node const &boundary) {
		boundary.for_each_sub_node("window", [&](Genode::Node const &w) {
			if (found) return;
			Genode::String<256> const t =
				w.attribute_value("title", Genode::String<256>());
			if (!title_matches(label, qt_title, t.string())) return;

			out.x = w.attribute_value("xpos",  0);
			out.y = w.attribute_value("ypos",  0);
			out.w = w.attribute_value("width",  0u);
			out.h = w.attribute_value("height", 0u);
			out.valid = true;
			if (focused) *focused = w.attribute_value("focused", false);
			found = true;
		});
	});
	return found;
}


/*
 * Find a window in window_layout whose `title` CONTAINS label_prefix.
 * The delivered consumer idiom (wm_tasks_probe._geom_by_label): the
 * label may carry the client-title suffix, so a substring scan on the
 * stable prefix is the robust probe-side lookup. Fills screen-absolute
 * inner geometry and optionally `focused`.
 */
inline bool find_layout_window_by_prefix(Genode::Node root,
                                         char const *label_prefix,
                                         Geom &out, bool *focused = nullptr)
{
	bool found = false;
	Genode::size_t const n = Genode::strlen(label_prefix);
	root.for_each_sub_node("boundary", [&](Genode::Node const &boundary) {
		boundary.for_each_sub_node("window", [&](Genode::Node const &w) {
			if (found) return;
			Genode::String<256> const t =
				w.attribute_value("title", Genode::String<256>());
			bool hit = false;
			for (char const *p = t.string(); *p && !hit; ++p)
				hit = (Genode::strcmp(p, label_prefix, n) == 0);
			if (!hit) return;

			out.x = w.attribute_value("xpos",  0);
			out.y = w.attribute_value("ypos",  0);
			out.w = w.attribute_value("width",  0u);
			out.h = w.attribute_value("height", 0u);
			out.valid = true;
			if (focused) *focused = w.attribute_value("focused", false);
			found = true;
		});
	});
	return found;
}


/*
 * The TasklistController's tracked order is window_layout iteration
 * order (its _recompute_tracked walks <boundary>/<window> and appends).
 * Probes that click a tasklist entry derive the entry index with the
 * same walk, so they stay correct whatever the layouter's node order
 * is. `seen` counts layout windows matching ANY known identity before
 * the wanted one; the caller passes its identity matcher.
 */
template <typename MATCH_FN>
inline int layout_tracked_index(Genode::Node root, MATCH_FN const &is_wanted)
{
	int index = 0;
	bool found = false;
	root.for_each_sub_node("boundary", [&](Genode::Node const &boundary) {
		boundary.for_each_sub_node("window", [&](Genode::Node const &w) {
			if (found) return;
			Genode::String<256> const t =
				w.attribute_value("title", Genode::String<256>());
			if (t.string()[0] == '\0') return;
			if (is_wanted(t.string())) { found = true; return; }
			++index;
		});
	});
	return found ? index : -1;
}


/* ============ rules (TasklistController -> sponge_layouter) ============ */

/*
 * rules document shape (frozen, docs/05-sponge-de.md §4.6):
 *   root <rules>, replaced wholesale per publication (last wins);
 *   child order is semantic: >=1 <screen>, then one <assign> per
 *   tracked window frontmost-first, then static <assign> fragments,
 *   then <press>/<release> key-sequence nodes kept in every publication.
 * Each <assign> carries one label identity (label, label_prefix, or
 * label_suffix), target, xpos, ypos, width, height and optionally
 * maximized. Assign coordinates are TARGET-RELATIVE (the
 * controller subtracts the boundary origin).
 */
inline bool check_rules_shape(Genode::Node root, char const *tag)
{
	if (!root.has_type("rules")) {
		Genode::error(tag, ": rules root node is '", root.type(),
		              "', expected 'rules'");
		return false;
	}

	bool ok = true;
	unsigned screens = 0, assigns = 0, press_nodes = 0, release_nodes = 0;

	root.for_each_sub_node([&](Genode::Node const &n) {
		if (!ok) return;

		if (n.has_type("screen")) {
			++screens;
			return;
		}

		if (n.has_type("assign")) {
			++assigns;
			static char const *const required[] = {
				"target", "xpos", "ypos", "width", "height" };
			for (char const *attr : required) {
				if (n.has_attribute(attr)) continue;
				Genode::error(tag, ": rules <assign> missing '", attr,
				              "' attribute");
				ok = false;
				return;
			}
			if (!n.has_attribute("label")
			 && !n.has_attribute("label_prefix")
			 && !n.has_attribute("label_suffix")) {
				Genode::error(tag, ": rules <assign> has no label identity");
				ok = false;
				return;
			}
			/* maximized is optional; when present it is a boolean */
			(void)n.attribute_value("maximized", false);
			return;
		}

		if (n.has_type("press"))   { ++press_nodes;   return; }
		if (n.has_type("release")) { ++release_nodes; return; }
	});

	if (!ok) return false;

	if (screens == 0) {
		Genode::error(tag, ": rules document has no <screen> node");
		return false;
	}
	if (press_nodes == 0 || release_nodes == 0) {
		Genode::error(tag, ": rules document lacks <press>/<release> nodes");
		return false;
	}

	Genode::log(tag, ": rules shape OK (", screens, " screen, ", assigns,
	            " assign, press/release present)");
	return true;
}


/*
 * The first <assign> whose `label` matches `label` with the probe-side
 * prefix rule (the controller copies the window_list label verbatim,
 * which carries the Qt-title suffix; a bare label_prefix compare is the
 * delivered consumer idiom). Fills target-relative geometry + maximized.
 */
inline bool rules_assign_for(Genode::Node root, char const *label,
                             Geom &out, bool *maximized = nullptr)
{
	bool found = false;
	root.for_each_sub_node("assign", [&](Genode::Node const &a) {
		if (found) return;
		Genode::String<256> const l =
			a.attribute_value("label", Genode::String<256>());
		if (Genode::strcmp(l.string(), label, Genode::strlen(label)) != 0)
			return;

		out.x = a.attribute_value("xpos",  0);
		out.y = a.attribute_value("ypos",  0);
		out.w = a.attribute_value("width",  0u);
		out.h = a.attribute_value("height", 0u);
		out.valid = true;
		if (maximized) *maximized = a.attribute_value("maximized", false);
		found = true;
	});
	return found;
}


/* ============ focus_request (TasklistController -> sponge_layouter) ============ */

/*
 * Root <focus_request> with attributes `id` (monotonic counter) and
 * `label` (the full window_list label). Sole writer: the tasklist.
 */
inline bool check_focus_request(Genode::Node root, char const *tag,
                                char const *want_label = nullptr)
{
	if (!root.has_type("focus_request")) {
		Genode::error(tag, ": focus_request root node is '", root.type(),
		              "', expected 'focus_request'");
		return false;
	}
	if (!root.has_attribute("id") || !root.has_attribute("label")) {
		Genode::error(tag, ": focus_request lacks id/label attributes");
		return false;
	}

	Genode::String<256> const label =
		root.attribute_value("label", Genode::String<256>());

	if (want_label && Genode::strcmp(label.string(), want_label) != 0) {
		Genode::error(tag, ": focus_request label='", label.string(),
		              "' != expected '", want_label, "'");
		return false;
	}

	Genode::log(tag, ": focus_request shape OK (id=",
	            root.attribute_value("id", 0U), " label='",
	            label.string(), "')");
	return true;
}


} /* namespace Native_wm */
