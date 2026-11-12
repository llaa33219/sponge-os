/* SPDX-License-Identifier: Apache-2.0
 *
 * layout_model.cc — sponge_layouter pure layout/state policy model.
 *
 * See layout_model.h for the full contract. Pure and deterministic:
 * no Env, no sessions, no allocation, no exceptions, no mutable
 * global state. The deterministic tests (model_tests.cc) exercise
 * this exact code.
 */

#include "layout_model.h"

namespace Sponge::Layout_model {

using Wm_protocol::PARKED_COORD;
using Wm_protocol::Rect;
using Wm_protocol::Margins;
using Wm_protocol::Assign;
using Wm_protocol::Hit;
using Wm_protocol::Sizer_zone;
using Wm_protocol::Button;


/* ======================== free helpers ========================= */

namespace {

bool empty(Genode::String<256> const &s)
{
	return s.string()[0] == '\0';
}

bool in_rect(Rect const &r, int px, int py)
{
	return px >= r.x && py >= r.y
	    && px <  r.x + r.w && py < r.y + r.h;
}

/*
 * Assign matching (upstream window_layouter README semantics): the
 * present fields must all match — exact 'label', 'label_prefix'
 * (starts-with), 'label_suffix' (ends-with). An assign with no match
 * fields at all matches every window.
 */
bool matches(Rule_assign const &a, Genode::String<256> const &label)
{
	char const *l = label.string();

	if (!empty(a.label) && Genode::strcmp(a.label.string(), l) != 0)
		return false;

	if (!empty(a.label_prefix)) {
		Genode::size_t const n = a.label_prefix.length() - 1;
		if (Genode::strcmp(l, a.label_prefix.string(), n) != 0)
			return false;
	}

	if (!empty(a.label_suffix)) {
		Genode::size_t const sl = Genode::strlen(l);
		Genode::size_t const n = a.label_suffix.length() - 1;
		if (sl < n || Genode::strcmp(l + (sl - n), a.label_suffix.string()) != 0)
			return false;
	}

	return true;
}

}  /* anonymous namespace */


void add_assign(Rules_doc &doc, Assign const &a)
{
	if (doc.num_assigns >= MAX_ASSIGNS)
		return;

	Rule_assign &r = doc.assigns[doc.num_assigns++];
	r.label        = a.label;
	r.label_prefix = a.label_prefix;
	r.label_suffix = a.label_suffix;
	r.has_position = a.has_position;
	r.xpos_any     = a.xpos_any;
	r.ypos_any     = a.ypos_any;
	r.xpos         = a.xpos;
	r.ypos         = a.ypos;
	r.has_size     = a.has_size;
	r.width        = a.width;
	r.height       = a.height;
	r.maximized    = a.maximized;
	r.visible      = a.visible;
}


bool rules_doc_from_node(Genode::Node const &node, Rule_frame frame,
                         Rules_doc &doc)
{
	Rules_doc out { };
	out.frame = frame;

	Wm_protocol::Rules info { };
	bool const ok = Wm_protocol::parse_rules(node, info,
		[&] (Assign const &a) { add_assign(out, a); });

	if (!ok)
		return false;

	doc = out;
	return true;
}


bool same_rules_doc(Rules_doc const &a, Rules_doc const &b)
{
	if (a.frame != b.frame || a.num_assigns != b.num_assigns)
		return false;

	for (unsigned i = 0; i < a.num_assigns; i++) {
		Rule_assign const &x = a.assigns[i], &y = b.assigns[i];
		if (x.label != y.label
		 || x.label_prefix != y.label_prefix
		 || x.label_suffix != y.label_suffix
		 || x.has_position != y.has_position
		 || x.xpos_any != y.xpos_any
		 || x.ypos_any != y.ypos_any
		 || x.xpos != y.xpos
		 || x.ypos != y.ypos
		 || x.has_size != y.has_size
		 || x.width != y.width
		 || x.height != y.height
		 || x.maximized != y.maximized
		 || x.visible != y.visible)
			return false;
	}

	return true;
}


Mode parse_layout_mode(Genode::Node const &configd_broadcast)
{
	/* default floating when the key (or the broadcast) is absent */
	Mode mode = Mode::FLOATING;

	configd_broadcast.for_each_sub_node("key", [&] (Genode::Node const &k) {
		if (k.attribute_value("name", Genode::String<128>())
		 != Genode::String<128>("wm.layout.mode"))
			return;

		Genode::String<32> const v =
			k.attribute_value("value", Genode::String<32>());

		/* unrecognized values read as the registered default */
		mode = (v == Genode::String<32>("tiling")) ? Mode::TILING
		                                           : Mode::FLOATING;
	});

	return mode;
}


bool work_area_from_node(Genode::Node const &config, Work_area &out)
{
	bool ok = false;

	config.with_sub_node("work_area",
		[&] (Genode::Node const &n) {
			Work_area wa;
			wa.x = n.attribute_value("x", 0);
			wa.y = n.attribute_value("y", 0);
			wa.w = n.attribute_value("width",  0L);
			wa.h = n.attribute_value("height", 0L);
			if (wa.w > 0 && wa.h > 0) {
				out = wa;
				ok  = true;
			}
		},
		[&] { });

	return ok;
}


/* ======================== model internals ====================== */

Window *Model::_by_wid(unsigned long wid)
{
	for (unsigned i = 0; i < _num_windows; i++)
		if (_windows[i].wid == wid)
			return &_windows[i];
	return nullptr;
}

Window const *Model::_by_wid(unsigned long wid) const
{
	for (unsigned i = 0; i < _num_windows; i++)
		if (_windows[i].wid == wid)
			return &_windows[i];
	return nullptr;
}

int Model::_stack_pos(unsigned long wid) const
{
	for (unsigned i = 0; i < _stack_size; i++)
		if (_stack[i] == wid)
			return (int)i;
	return -1;
}

void Model::_raise(unsigned long wid)
{
	int const pos = _stack_pos(wid);
	if (pos < 0)
		return;
	unsigned long const w = _stack[pos];
	for (unsigned i = (unsigned)pos; i > 0; i--)
		_stack[i] = _stack[i - 1];
	_stack[0] = w;
}

bool Model::_arrangement_eligible(Window const &w) const
{
	return w.resizeable && !w.minimized && !w.maximized && !w.hidden;
}

bool Model::_gesture_eligible(Window const &w) const
{
	if (w.minimized || w.maximized)
		return false;

	/* move/resize only floating; tiled geometry is layouter-owned
	 * (non-resizeable windows float in both modes) */
	return _mode == Mode::FLOATING || !w.resizeable;
}

bool Model::_resize_eligible(Window const &w) const
{
	return _gesture_eligible(w) && w.resizeable;
}

Wm_protocol::Hit Model::_hit_test(Window const &w, int px, int py) const
{
	Rect const outer = Wm_protocol::outer_from_inner(w.inner, _margins);

	struct { Button button; Hit hit; } const buttons[3] {
		{ Button::CLOSE,      Hit::BTN_CLOSE      },
		{ Button::FULLSCREEN, Hit::BTN_FULLSCREEN },
		{ Button::MINIMIZE,   Hit::BTN_MINIMIZE   } };

	for (auto const &b : buttons) {
		Rect const r =
			Wm_protocol::button_rect(outer, _margins, b.button, w.resizeable);
		if (r.w > 0 && r.h > 0 && in_rect(r, px, py))
			return b.hit;
	}

	return Wm_protocol::chrome_hit(px, py, outer, _margins, w.resizeable);
}

Rect Model::_maximized_inner() const
{
	/* work-area maximize: OUTER == work area, INNER derived by the
	 * margins (the frozen 64 px tasklist inference keeps working) */
	return Wm_protocol::inner_from_outer(
		Rect { _work.x, _work.y, _work.w, _work.h }, _margins);
}

Rect Model::arrangement_slot_outer(unsigned slot_index,
                                   unsigned eligible_total) const
{
	/* Master-Stack over OUTER rects (the tiling acceptance contract,
	 * test/native_wm_tiling_probe/main.cc '_slot_content'): slot 0
	 * is the left-half master, the remaining eligible windows split
	 * the right half into equal bands top to bottom; integer band
	 * math is reproduced exactly. */
	long const wx = _work.x, wy = _work.y;
	long const ww = _work.w, wh = _work.h;
	long const half = ww / 2;

	if (slot_index == 0)
		return Rect { (int)wx, (int)wy, half, wh };

	unsigned const bands = eligible_total - 1;
	unsigned const band  = slot_index - 1;

	long const y0 = wy + (wh * (long)band)     / (long)bands;
	long const y1 = wy + (wh * (long)(band + 1)) / (long)bands;

	return Rect { (int)(wx + half), (int)y0, ww - half, y1 - y0 };
}

void Model::_place_arrangement()
{
	unsigned order[MAX_WINDOWS] { };
	unsigned n = 0;

	for (unsigned i = 0; i < _num_windows; i++)
		if (_arrangement_eligible(_windows[i]))
			order[n++] = i;

	if (n == 0)
		return;

	for (unsigned k = 0; k < n; k++) {
		Rect const outer = arrangement_slot_outer(k, n);
		_windows[order[k]].inner =
			Wm_protocol::inner_from_outer(outer, _margins);
	}
}

void Model::_cascade_place(Window &w)
{
	/* deterministic fallback placement when no static rule matches */
	unsigned const k = _num_windows > 0 ? _num_windows - 1 : 0;
	int const off = (int)(32 * (k % 8));
	w.inner.x = _work.x + _margins.left + off;
	w.inner.y = _work.y + _margins.top  + off;
	w.restore = w.inner;
}

void Model::_minimize(Window &w)
{
	if (w.minimized)
		return;

	/* a maximized window keeps its pre-maximize restore target */
	if (!w.maximized)
		w.restore = w.inner;

	w.minimized = true;
	w.maximized = false;

	/* park at the established sentinel (keep the size) */
	w.inner.x = PARKED_COORD;
	w.inner.y = PARKED_COORD;

	_dirty = true;
}

void Model::_maximize(Window &w)
{
	w.maximized = true;
	w.minimized = false;
	w.inner = _maximized_inner();
	_dirty = true;
}

void Model::_unmaximize(Window &w)
{
	if (!w.maximized)
		return;

	w.maximized = false;

	/* tiling: the arrangement slot is recomputed by the caller's
	 * reflow; floating (and always-floating non-resizeable) windows
	 * restore the saved pre-maximize geometry */
	if (!(_mode == Mode::TILING && _arrangement_eligible(w)))
		w.inner = w.restore;

	_dirty = true;
}

void Model::_restore(Window &w)
{
	if (!w.minimized)
		return;

	w.minimized = false;

	/* tiling: re-enters the arrangement (reflow by the caller);
	 * floating: the saved pre-minimize geometry */
	if (!(_mode == Mode::TILING && _arrangement_eligible(w)))
		w.inner = w.restore;

	_dirty = true;
}

void Model::_destroy_at(unsigned idx)
{
	unsigned long const gone = _windows[idx].wid;

	for (unsigned i = idx; i + 1 < _num_windows; i++)
		_windows[i] = _windows[i + 1];
	_num_windows--;

	/* stack bookkeeping */
	int const pos = _stack_pos(gone);
	if (pos >= 0) {
		for (unsigned i = (unsigned)pos; i + 1 < _stack_size; i++)
			_stack[i] = _stack[i + 1];
		_stack_size--;
	}

	/* a grabbed or focused window that disappears drops its role */
	if (_grab_wid == gone)
		_grab_kind = Grab_kind::NONE;

	if (_have_desired_focus && _desired_focus == gone)
		_have_desired_focus = false;

	if (_have_focus_truth && _focus_truth == gone)
		_have_focus_truth = false;

	_dirty = true;
}


/* ========================= assign semantics ==================== */

void Model::_apply_assign(Window &w, Rule_assign const &a, Rule_frame frame)
{
	if (!a.visible) {
		w.hidden = true;
		_dirty = true;
		return;
	}
	if (w.hidden) {
		w.hidden = false;
		_dirty = true;
	}

	/* parked position: BOTH sentinel coordinates (minimize) */
	bool const parked_pos =
		a.has_position && !a.xpos_any && !a.ypos_any
	 && a.xpos <= PARKED_COORD && a.ypos <= PARKED_COORD;

	if (parked_pos) {
		_minimize(w);
		return;
	}

	bool const tiled = _mode == Mode::TILING && _arrangement_eligible(w);
	bool const has_geom = a.has_position || a.has_size;

	/* frame-converted target geometry (defaults to the current one
	 * for unconstrained axes) */
	Rect g = w.inner;
	if (a.has_position) {
		int x = a.xpos_any ? w.inner.x : a.xpos;
		int y = a.ypos_any ? w.inner.y : a.ypos;
		if (frame == Rule_frame::BOUNDARY_RELATIVE) {
			x += _work.x;
			y += _work.y;
		}
		g.x = x;
		g.y = y;
	}
	if (a.has_size) {
		g.w = a.width;
		g.h = a.height;
	}

	if (a.maximized && !w.maximized) {
		/* maximize: the supplied geometry is the pre-maximize
		 * restore target */
		if (has_geom)
			w.restore = g;
		else if (!w.minimized)
			w.restore = w.inner;
		_maximize(w);
		return;
	}

	if (!a.maximized && w.maximized) {
		/* unmaximize: the supplied geometry is the restore target */
		w.maximized = false;
		if (has_geom)
			w.restore = g;
		if (!(_mode == Mode::TILING && _arrangement_eligible(w)))
			w.inner = w.restore;
		_dirty = true;
		return;
	}

	if (w.maximized) {
		/* maximized re-assert: keep the saved restore target */
		w.inner = _maximized_inner();
		_dirty = true;
		return;
	}

	if (w.minimized) {
		/* non-parked assign on a parked window: restore */
		w.minimized = false;
		if (has_geom)
			w.restore = g;
		if (!tiled)
			w.inner = w.restore;
		_dirty = true;
		return;
	}

	/* ordinary geometry update (layouter-owned in tiling mode: the
	 * values are state hints only and never fight the arrangement) */
	if (has_geom && !tiled) {
		w.inner = g;
		_dirty = true;
	}
}


/* ======================= consumed channels ===================== */

void Model::configure(Work_area const &work, Margins const &margins,
                      Mode mode, Rules_doc const &static_rules)
{
	_work       = work;
	_margins    = margins;
	_mode       = mode;
	_static_doc = static_rules;
	_rom_valid  = false;
	_dirty      = true;
}


void Model::window_list(Window_list_entry const *entries, Genode::size_t n)
{
	bool seen[MAX_WINDOWS] { };

	for (Genode::size_t e = 0; e < n; e++) {
		Window_list_entry const &entry = entries[e];

		/* no label identity -> ignored (frozen contract) */
		if (empty(entry.label))
			continue;

		Window *w = _by_wid(entry.id);
		if (!w) {
			if (_num_windows >= MAX_WINDOWS)
				continue;

			unsigned const idx = _num_windows++;
			w = &_windows[idx];
			*w = Window { };

			w->wid        = entry.id;
			w->create_seq = _next_create_seq++;
			w->label      = entry.label;
			w->title      = entry.title;
			w->resizeable = entry.resizeable;

			/* deterministic default: the client size at a
			 * cascaded position, then the static rules may
			 * override (initial placement) */
			w->inner.w = entry.width  > 0 ? entry.width  : 640;
			w->inner.h = entry.height > 0 ? entry.height : 480;
			_cascade_place(*w);

			/* initial placement from the static inline rules */
			Rules_doc const &doc = _static_doc;
			bool matched = false;
			for (unsigned i = 0; i < doc.num_assigns && !matched; i++) {
				if (!matches(doc.assigns[i], w->label))
					continue;
				_apply_assign(*w, doc.assigns[i], doc.frame);
				matched = true;
			}

			/* stack: newest in front until raised */
			for (unsigned i = _stack_size; i > 0; i--)
				_stack[i] = _stack[i - 1];
			_stack[0] = w->wid;
			_stack_size++;

			_dirty = true;
		} else {
			/* complete-snapshot last-state-wins refresh */
			w->label      = entry.label;
			w->title      = entry.title;
			w->resizeable = entry.resizeable;
		}

		/* mark seen (creation order preserved across snapshots) */
		for (unsigned i = 0; i < _num_windows; i++)
			if (_windows[i].wid == entry.id)
				seen[i] = true;
	}

	/* destroy every window absent from the complete snapshot */
	for (unsigned i = _num_windows; i-- > 0; )
		if (!seen[i])
			_destroy_at(i);

	if (_mode == Mode::TILING)
		_place_arrangement();

	_dirty = true;
}


void Model::focus_request(Focus_request const &req)
{
	/* dedup on id: a repeated id is processed at most once */
	if (_have_focus_id && _focus_id == req.id)
		return;
	_have_focus_id = true;
	_focus_id      = req.id;

	Window *w = nullptr;
	for (unsigned i = 0; i < _num_windows && !w; i++)
		if (Genode::strcmp(_windows[i].label.string(), req.label.string()) == 0)
			w = &_windows[i];

	if (!w)
		return;

	/* restore-with-focus (U3): a minimized window comes back in the
	 * same step that steers focus to it */
	if (w->minimized)
		_restore(*w);

	_raise(w->wid);
	_have_desired_focus = true;
	_desired_focus      = w->wid;

	if (_mode == Mode::TILING)
		_place_arrangement();

	_dirty = true;
}


void Model::rules(Rules_doc const &doc)
{
	/* last-wins is per DISTINCT publication: an identical document
	 * is not a new publication and must not snap user-moved windows
	 * back to the echoed geometry */
	if (_rom_valid && same_rules_doc(_rom_doc, doc))
		return;

	_rom_doc   = doc;
	_rom_valid = true;

	for (unsigned i = 0; i < _num_windows; i++) {
		Window &w = _windows[i];
		for (unsigned a = 0; a < doc.num_assigns; a++) {
			if (!matches(doc.assigns[a], w.label))
				continue;
			_apply_assign(w, doc.assigns[a], doc.frame);
			break;   /* first matching assign takes effect */
		}
	}

	if (_mode == Mode::TILING)
		_place_arrangement();

	_dirty = true;
}


void Model::rules_invalid()
{
	/* upstream semantics: without valid ROM rules the static inline
	 * document comes back into effect (once per transition) */
	if (!_rom_valid)
		return;

	_rom_valid = false;

	for (unsigned i = 0; i < _num_windows; i++) {
		Window &w = _windows[i];
		for (unsigned a = 0; a < _static_doc.num_assigns; a++) {
			if (!matches(_static_doc.assigns[a], w.label))
				continue;
			_apply_assign(w, _static_doc.assigns[a], _static_doc.frame);
			break;
		}
	}

	if (_mode == Mode::TILING)
		_place_arrangement();

	_dirty = true;
}


void Model::focus_state(Focus_target const &t)
{
	/* the sole truth for window_layout's consumed focused="yes" */
	_have_focus_truth = t.set;
	_focus_truth      = t.wid;
	_dirty            = true;
}


void Model::margins(Margins const &m)
{
	_margins = m;

	/* maximized geometry and the arrangement derive from margins */
	for (unsigned i = 0; i < _num_windows; i++)
		if (_windows[i].maximized)
			_windows[i].inner = _maximized_inner();

	if (_mode == Mode::TILING)
		_place_arrangement();

	_dirty = true;
}


void Model::mode(Mode m)
{
	_mode = m;

	/* floating -> tiling reflows; tiling -> floating keeps every
	 * window's current geometry as its floating geometry (no hidden
	 * pre-tile history is kept) */
	if (_mode == Mode::TILING)
		_place_arrangement();

	_dirty = true;
}


/* ========================= input events ======================== */

bool Model::press(int px, int py)
{
	if (_grab_kind != Grab_kind::NONE)
		return false;

	/* hit-test the topmost window owning the point (stack order) */
	for (unsigned i = 0; i < _stack_size; i++) {
		Window *w = _by_wid(_stack[i]);
		if (!w || w->hidden || w->minimized)
			continue;

		Rect const outer =
			Wm_protocol::outer_from_inner(w->inner, _margins);

		if (!in_rect(outer, px, py))
			continue;

		Hit const hit = _hit_test(*w, px, py);

		if (hit == Hit::NONE)
			return false;   /* client area of the topmost window */

		/* any chrome press focuses+raises */
		_raise(w->wid);
		_have_desired_focus = true;
		_desired_focus      = w->wid;
		_dirty              = true;

		switch (hit) {
		case Hit::BTN_CLOSE:
		case Hit::BTN_FULLSCREEN:
		case Hit::BTN_MINIMIZE:
			_grab_kind = Grab_kind::BUTTON;
			_grab_hit  = hit;
			_grab_wid  = w->wid;
			_grab_px   = px;
			_grab_py   = py;
			return true;

		case Hit::TITLE_MOVE:
			if (_gesture_eligible(*w)) {
				_grab_kind   = Grab_kind::MOVE;
				_grab_wid    = w->wid;
				_grab_px     = px;
				_grab_py     = py;
				_grab_inner  = w->inner;
			}
			return true;

		default:
			/* 8-zone resize grip */
			if (_resize_eligible(*w)) {
				_grab_kind  = Grab_kind::RESIZE;
				_grab_wid   = w->wid;
				_grab_zone  = [&] () -> Sizer_zone {
					switch (hit) {
					case Hit::SIZER_N:  return Sizer_zone::N;
					case Hit::SIZER_S:  return Sizer_zone::S;
					case Hit::SIZER_E:  return Sizer_zone::E;
					case Hit::SIZER_W:  return Sizer_zone::W;
					case Hit::SIZER_NE: return Sizer_zone::NE;
					case Hit::SIZER_NW: return Sizer_zone::NW;
					case Hit::SIZER_SE: return Sizer_zone::SE;
					default:            return Sizer_zone::SW;
					}
				}();
				_grab_px    = px;
				_grab_py    = py;
				_grab_inner = w->inner;
			}
			return true;
		}
	}

	return false;
}


void Model::motion(int px, int py)
{
	Window *w = _by_wid(_grab_wid);
	if (!w) {
		_grab_kind = Grab_kind::NONE;
		return;
	}

	if (_grab_kind == Grab_kind::MOVE) {
		/* exact pointer delta, size unchanged */
		w->inner.x = _grab_inner.x + (px - _grab_px);
		w->inner.y = _grab_inner.y + (py - _grab_py);
		_dirty = true;
		return;
	}

	if (_grab_kind != Grab_kind::RESIZE)
		return;

	int const dx = px - _grab_px;
	int const dy = py - _grab_py;

	Rect const s = _grab_inner;
	Rect r = s;

	bool const n_edge = _grab_zone == Sizer_zone::N
	                 || _grab_zone == Sizer_zone::NE
	                 || _grab_zone == Sizer_zone::NW;
	bool const s_edge = _grab_zone == Sizer_zone::S
	                 || _grab_zone == Sizer_zone::SE
	                 || _grab_zone == Sizer_zone::SW;
	bool const w_edge = _grab_zone == Sizer_zone::W
	                 || _grab_zone == Sizer_zone::NW
	                 || _grab_zone == Sizer_zone::SW;
	bool const e_edge = _grab_zone == Sizer_zone::E
	                 || _grab_zone == Sizer_zone::NE
	                 || _grab_zone == Sizer_zone::SE;

	if (n_edge) {
		r.y = s.y + dy;
		r.h = s.h - dy;
		if (r.h < MIN_INNER_H) {
			r.h = MIN_INNER_H;
			r.y = s.y + (int)s.h - (int)MIN_INNER_H;
		}
	}
	if (s_edge) {
		r.h = s.h + dy;
		if (r.h < MIN_INNER_H)
			r.h = MIN_INNER_H;
	}
	if (w_edge) {
		r.x = s.x + dx;
		r.w = s.w - dx;
		if (r.w < MIN_INNER_W) {
			r.w = MIN_INNER_W;
			r.x = s.x + (int)s.w - (int)MIN_INNER_W;
		}
	}
	if (e_edge) {
		r.w = s.w + dx;
		if (r.w < MIN_INNER_W)
			r.w = MIN_INNER_W;
	}

	w->inner = r;
	_dirty = true;
}


bool Model::release(int px, int py)
{
	bool const was_grab = _grab_kind != Grab_kind::NONE;

	if (_grab_kind == Grab_kind::BUTTON) {
		Window *w = _by_wid(_grab_wid);

		/* a click is press + release within CLICK_SLOP_PX landing
		 * in the SAME button rect; a sloppy release fires nothing */
		bool click = false;
		if (w) {
			int const ddx = px - _grab_px;
			int const ddy = py - _grab_py;
			bool const near =
				(ddx * ddx + ddy * ddy)
				 < (int)(CLICK_SLOP_PX * CLICK_SLOP_PX);

			if (near) {
				Hit const hit = _hit_test(*w, px, py);
				click = (hit == _grab_hit);
			}
		}

		if (click && w) {
			switch (_grab_hit) {
			case Hit::BTN_CLOSE:
				/* sticky close request: kept in every
				 * window_request snapshot until the wid
				 * disappears from window_list */
				w->close_pending = true;
				break;

			case Hit::BTN_MINIMIZE:
				_minimize(*w);
				break;

			case Hit::BTN_FULLSCREEN:
				if (w->resizeable) {
					if (w->maximized) {
						_unmaximize(*w);
					} else {
						if (!w->minimized)
							w->restore = w->inner;
						_maximize(*w);
					}
				}
				break;

			default:
				break;
			}

			if (_mode == Mode::TILING)
				_place_arrangement();

			_dirty = true;
		}
	}

	_grab_kind = Grab_kind::NONE;
	_grab_wid  = 0;

	return was_grab;
}


bool Model::drag_active() const
{
	return _grab_kind == Grab_kind::MOVE || _grab_kind == Grab_kind::RESIZE;
}


/* ========================= produced channels =================== */

void Model::gen_window_layout(Genode::Xml_generator &g) const
{
	Wm_protocol::Boundary boundary;
	boundary.name   = Genode::String<64>("screen");
	boundary.xpos   = _work.x;
	boundary.ypos   = _work.y;
	boundary.width  = _work.w;
	boundary.height = _work.h;

	Wm_protocol::Layout_window windows[MAX_WINDOWS] { };
	Genode::size_t n = 0;

	/* stack order, front first */
	for (unsigned i = 0; i < _stack_size && n < MAX_WINDOWS; i++) {
		Window const *w = _by_wid(_stack[i]);
		if (!w || w->hidden)
			continue;

		Wm_protocol::Layout_window &lw = windows[n++];
		lw.title   = Wm_protocol::layout_title(w->label.string(),
		                                      w->title.string());
		lw.xpos    = w->inner.x;
		lw.ypos    = w->inner.y;
		lw.width   = w->inner.w;
		lw.height  = w->inner.h;
		lw.focused = _have_focus_truth && _focus_truth == w->wid;
	}

	Wm_protocol::gen_boundary(g, boundary, windows, n);
}


void Model::gen_window_request(Genode::Xml_generator &g) const
{
	/* complete desired state in creation order */
	for (unsigned i = 0; i < _num_windows; i++) {
		Window const &w = _windows[i];

		Wm_protocol::Window_request req;
		req.wid = w.wid;

		if (w.close_pending) {
			req.close = true;
			Wm_protocol::gen_window_request_entry(g, req);
			continue;
		}

		req.x = w.inner.x;
		req.y = w.inner.y;
		req.w = w.inner.w;
		req.h = w.inner.h;
		Wm_protocol::gen_window_request_entry(g, req);
	}
}


void Model::gen_focus(Genode::Xml_generator &g) const
{
	/* steering snapshot: the desired focus (empty root = none) */
	Focus_target t;
	t.set = _have_desired_focus;
	t.wid = _desired_focus;
	Wm_protocol::gen_focus(g, t);
}


bool Model::consume_dirty()
{
	bool const d = _dirty;
	_dirty = false;
	return d;
}


bool Model::close_pending(unsigned long wid) const
{
	Window const *w = _by_wid(wid);
	return w && w->close_pending;
}


}  /* namespace Sponge::Layout_model */
