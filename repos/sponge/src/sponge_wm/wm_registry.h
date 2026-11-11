/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_registry — sponge_wm window registry and protocol-state model.
 *
 * The PURE model behind the sponge_wm core (D16W.1): window identity
 * (monotonic wid), the frozen protocol-v1 'window_list' attributes,
 * the v1.1 'window_chrome' / 'focus_state' snapshots, the v1.1
 * 'window_request' application state (inner placement + sticky close),
 * 'focus' steering truth, and 'decorator_margins' inner<->outer frame
 * math. The transport layer (main.cc / wm_session.cc) owns all
 * sessions and IPC; everything here is deterministic state.
 *
 * Contract references (binding):
 *
 *   - protocol v1 (frozen): docs/05-sponge-de.md §4.6 — window_list
 *     shape, omission rules, parking sentinel, identity pairs.
 *   - protocol v1.1: docs/plans/phase16-sponge-wm.md §11 +
 *     repos/sponge/include/sponge/wm_protocol.h — channel shapes and
 *     semantics (snapshots not events, sticky close, outer frame).
 *
 * PURITY CONTRACT (same hard rules as <sponge/wm_protocol.h>):
 * no Genode::Env, no sessions, no hardware access, no exceptions, no
 * mutable global state, no dynamic allocation. The window table is a
 * fixed-capacity array; every mutation reports which published
 * snapshot(s) it dirties through the 'Dirty' flags so the transport
 * layer can republish exactly the affected reports (and never a
 * move-only 'window_chrome' — v1.1 forbids move publication).
 *
 * Placement semantics: the v1.1 'window_request' is the authority for
 * the INNER screen-absolute geometry. Until a window is placed by the
 * layouter, its inner rect is the client's own top-level-view rect
 * (the transient initial placement). The window_list 'width'/'height'
 * always report the CLIENT size (the top-level view area), never the
 * layouter's placement (frozen pass-through field).
 *
 * 'hidden' is part of the frozen shape but has no v1.1 setter: the
 * native minimize convention is the <=-32000 parking sentinel in
 * 'window_layout' (recorded approval 8), so the attribute stays false
 * and is omitted from the report exactly as the generator contract
 * requires.
 */

#pragma once

#include <sponge/wm_protocol.h>

namespace Sponge::Wm_registry {


using Wm_protocol::Focus_target;
using Wm_protocol::Margins;
using Wm_protocol::Rect;

/* Fixed window-table capacity (no allocation in the model). */
enum : unsigned { MAX_WINDOWS = 64 };

/*
 * Snapshot-dirty flags returned by every mutator. The transport
 * layer ORs the result of a batch and republishes exactly the named
 * reports.
 */
enum class Dirty : unsigned {
	NONE         = 0,
	WINDOW_LIST  = 1u << 0,
	CHROME       = 1u << 1,
	FOCUS        = 1u << 2
};

inline Dirty operator| (Dirty a, Dirty b)
{
	return Dirty((unsigned)a | (unsigned)b);
}

inline Dirty &operator|= (Dirty &a, Dirty b)
{
	a = a | b;
	return a;
}

inline bool dirty_any(Dirty d, Dirty mask)
{
	return ((unsigned)d & (unsigned)mask) != 0;
}


/* One registered window (a content Gui session). */
struct Window
{
	bool                used { false };

	/* monotonic core-assigned identity (frozen window_list 'id') */
	unsigned long       wid  { 0 };

	/* frozen window_list identity pair (full session label / client title) */
	Genode::String<256> label { };
	Genode::String<256> title { };

	/* frozen window_list pass-through/consumed attributes */
	long                client_w  { 0 };   /* top-level view size */
	long                client_h  { 0 };
	bool                has_alpha { false };
	bool                hidden    { false };  /* always false in v1.1 (see header) */
	bool                resizeable { false };

	/*
	 * Client top-level-view origin, in the client's own coordinate
	 * space. Input translation for this window is
	 * 'client = screen - inner_origin + (virt_x, virt_y)'.
	 */
	int                 virt_x { 0 }, virt_y { 0 };

	/* inner screen-absolute placement (window_request authority) */
	bool                placed  { false };
	int                 inner_x { 0 }, inner_y { 0 };
	long                inner_w { 0 }, inner_h { 0 };

	/* sticky close (window_request action="close") */
	bool                close_requested { false };
	bool                close_applied   { false };

	/* chrome-view association (decorator view title == decimal wid) */
	bool                chrome_present { false };

	bool sized() const { return client_w > 0 && client_h > 0; }
};


class Registry
{
	private:

		Window       _windows[MAX_WINDOWS] { };
		unsigned long _next_wid { 1 };
		unsigned     _count { 0 };

		Focus_target _focus   { };
		Margins      _margins { };

		Window *_slot(unsigned long wid)
		{
			for (unsigned i = 0; i < MAX_WINDOWS; i++)
				if (_windows[i].used && _windows[i].wid == wid)
					return &_windows[i];
			return nullptr;
		}

		Window const *_slot(unsigned long wid) const
		{
			for (unsigned i = 0; i < MAX_WINDOWS; i++)
				if (_windows[i].used && _windows[i].wid == wid)
					return &_windows[i];
			return nullptr;
		}

	public:

		/* ---- lifecycle ------------------------------------------- */

		/*
		 * Register a new window under a fresh monotonic wid. Returns
		 * the wid, or 0 when the table is full.
		 */
		unsigned long create_window(Genode::String<256> const &label);

		/*
		 * Remove a window. Returns the dirty flags: every removal
		 * touches window_list and window_chrome (destroy = absence)
		 * and focus_state when the removed window held focus.
		 */
		Dirty destroy_window(unsigned long wid);

		unsigned count() const { return _count; }

		Window const *find(unsigned long wid) const { return _slot(wid); }
		Window       *find(unsigned long wid)       { return _slot(wid); }

		template <typename FN>
		void for_each_window(FN const &fn)
		{
			for (unsigned i = 0; i < MAX_WINDOWS; i++)
				if (_windows[i].used)
					fn(_windows[i]);
		}

		template <typename FN>
		void for_each_window(FN const &fn) const
		{
			for (unsigned i = 0; i < MAX_WINDOWS; i++)
				if (_windows[i].used)
					fn(_windows[i]);
		}

		/*
		 * Look up a window by its frozen window_list label identity.
		 */
		Window const *by_label(char const *label) const;

		/* ---- attributes (client truth) ---------------------------- */

		/* top-level-view title -> window_list 'title'; WINDOW_LIST + CHROME on change */
		Dirty set_title(unsigned long wid, char const *title);

		/*
		 * Top-level-view rect (client coordinate space): records the
		 * virtual origin and the client size. WINDOW_LIST on size
		 * change; CHROME as well while the window is unplaced (the
		 * transient inner rect follows the client rect).
		 */
		Dirty set_client_geometry(unsigned long wid, int virt_x, int virt_y,
		                         long w, long h);

		/* WINDOW_LIST on change */
		Dirty set_has_alpha(unsigned long wid, bool value);

		/* WINDOW_LIST + CHROME on change (chrome publishes resizeable) */
		Dirty set_resizeable(unsigned long wid, bool value);

		/* ---- placement (window_request authority) ----------------- */

		/*
		 * Apply the inner screen-absolute geometry. CHROME is dirtied
		 * only when the SIZE changes (v1.1: "never move").
		 */
		Dirty place(unsigned long wid, int x, int y, long w, long h);

		/*
		 * Record the sticky close request. Returns true exactly on
		 * the FIRST application (the transport owes the client the
		 * app-visible 0x0 close); re-processing the sticky node is a
		 * no-op until the wid vanishes.
		 */
		bool request_close(unsigned long wid);

		/* ---- focus truth ------------------------------------------ */

		/*
		 * Set the focus truth ('focus' steering or a real content
		 * click). FOCUS on change. The empty target clears focus.
		 */
		Dirty set_focus(Focus_target target);

		Focus_target focus() const { return _focus; }

		/* ---- margins (decorator_margins) -------------------------- */

		/* CHROME on change (every outer frame size depends on it) */
		Dirty set_margins(Margins const &m);

		Margins margins() const { return _margins; }

		/* ---- chrome-view association ------------------------------ */

		bool attach_chrome(unsigned long wid);
		bool detach_chrome(unsigned long wid);

		/* ---- geometry --------------------------------------------- */

		/*
		 * INNER screen-absolute rect: the placement when present,
		 * else the client rect (transient initial placement).
		 */
		Rect inner_rect(unsigned long wid) const;

		/* OUTER frame rect: inner expanded by the margins. */
		Rect outer_rect(unsigned long wid) const;

		/*
		 * Size the client is asked to resize to (the v1.1
		 * app-visible resize request): the placement size once
		 * placed, else the client size. Zero size with close pending
		 * is expressed by 'close_requested'.
		 */
		void requested_size(unsigned long wid, long &w, long &h) const;

		/* ---- snapshots -------------------------------------------- */

		void gen_window_list(Genode::Xml_generator &g) const;
		void gen_focus_state(Genode::Xml_generator &g) const;
		void gen_window_chrome(Genode::Xml_generator &g) const;
};


}  /* namespace Sponge::Wm_registry */
