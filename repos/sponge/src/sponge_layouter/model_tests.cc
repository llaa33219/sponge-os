/* SPDX-License-Identifier: Apache-2.0
 *
 * model_tests.cc — deterministic sponge_layouter model cases.
 *
 * See model_tests.h. Pure and deterministic: no Env, no sessions, no
 * timers, no allocation, no globals (all state is local per case).
 * Every case drives the production 'Model' and the production
 * sponge_wm_protocol generators/parsers; expected tiling/chrome
 * geometry comes from independent reference helpers transcribed from
 * the committed acceptance probes.
 */

#include "model_tests.h"
#include "layout_model.h"

#include <sponge/wm_protocol.h>

namespace Sponge::Layout_model {

namespace {

using namespace Sponge::Wm_protocol;

struct Ctx
{
	unsigned checks   = 0;
	unsigned failures = 0;
	Model_check_fn report;

	void check(bool ok, char const *what)
	{
		checks++;
		if (!ok)
			failures++;
		report(ok, what);
	}

	void check_rect(bool ok, char const *what, Rect const &got, Rect const &want)
	{
		checks++;
		if (ok) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<256> msg(
			what, ": got (", got.x, ",", got.y, ") ", got.w, "x", got.h,
			" want (", want.x, ",", want.y, ") ", want.w, "x", want.h);
		report(false, msg.string());
	}
};


template <typename FN>
char const *gen_xml(char *buf, Genode::size_t buf_len,
                    char const *type, FN const &fn)
{
	auto const res = Genode::Xml_generator::generate({ buf, buf_len }, type, fn);
	if (res.failed()) {
		buf[0] = '\0';
		return buf;
	}
	return buf;
}


Genode::Node node_of(char const *bytes)
{
	return Genode::Node(Genode::Const_byte_range_ptr(bytes,
	                                                Genode::strlen(bytes)));
}


/* ================ production-code round-trip I/O ================= */

void feed_window_list(Model &model, Window_list_entry const *entries,
                      unsigned n)
{
	char buf[8192];
	gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
		for (unsigned i = 0; i < n; i++)
			gen_window_list_entry(g, entries[i]);
	});

	Window_list_entry parsed[MAX_WINDOWS] { };
	unsigned count = 0;
	parse_window_list(node_of(buf), [&] (Window_list_entry const &e) {
		if (count < MAX_WINDOWS)
			parsed[count++] = e;
	});

	model.window_list(parsed, count);
}


struct Layout_win
{
	Genode::String<256> title { };
	int  x { 0 }, y { 0 };
	long w { 0 }, h { 0 };
	bool focused { false };
};

struct Layout_out
{
	Boundary   boundary { };
	bool       boundary_seen { false };
	Layout_win wins[MAX_WINDOWS] { };
	unsigned   n { 0 };
};

Layout_out read_layout(Model const &model)
{
	char buf[16384];
	gen_xml(buf, sizeof(buf), "window_layout",
		[&] (Genode::Xml_generator &g) { model.gen_window_layout(g); });

	Layout_out out;
	parse_window_layout(node_of(buf),
		[&] (Boundary const &b, Layout_window const &w) {
			if (!out.boundary_seen) {
				out.boundary = b;
				out.boundary_seen = true;
			}
			if (out.n < MAX_WINDOWS) {
				Layout_win &lw = out.wins[out.n++];
				lw.title   = w.title;
				lw.x       = w.xpos;
				lw.y       = w.ypos;
				lw.w       = w.width;
				lw.h       = w.height;
				lw.focused = w.focused;
			}
		});
	return out;
}

Layout_win const *find_layout(Layout_out const &out, char const *title)
{
	for (unsigned i = 0; i < out.n; i++)
		if (Genode::strcmp(out.wins[i].title.string(), title) == 0)
			return &out.wins[i];
	return nullptr;
}

Rect rect_of(Layout_win const &w) { return Rect { w.x, w.y, w.w, w.h }; }


struct Req_out
{
	Window_request entries[MAX_WINDOWS] { };
	unsigned n { 0 };
	bool     malformed { false };
};

Req_out read_request(Model const &model)
{
	char buf[16384];
	gen_xml(buf, sizeof(buf), "window_request",
		[&] (Genode::Xml_generator &g) { model.gen_window_request(g); });

	Req_out out;
	out.malformed = !parse_window_request(node_of(buf),
		[&] (Window_request const &r) {
			if (out.n < MAX_WINDOWS)
				out.entries[out.n++] = r;
		});
	return out;
}

Window_request const *find_req(Req_out const &out, unsigned long wid)
{
	for (unsigned i = 0; i < out.n; i++)
		if (out.entries[i].wid == wid)
			return &out.entries[i];
	return nullptr;
}

bool read_focus(Model const &model, Focus_target &out)
{
	char buf[1024];
	gen_xml(buf, sizeof(buf), "focus",
		[&] (Genode::Xml_generator &g) { model.gen_focus(g); });

	Focus_target t;
	if (!parse_focus(node_of(buf), t))
		return false;
	out = t;
	return true;
}


/* ============ reference (probe-copy) geometry oracle ============= */

/*
 * Independent copies of the committed acceptance probe math (test/
 * native_wm_tiling_probe/main.cc '_slot_content'/'_maximized_content',
 * test/native_wm_common/native_wm_probe_util.h). The fixtures match
 * the native run topology: 1024x768 screen, 28 px panel.
 */
enum : int { SCREEN_W = 1024, SCREEN_H = 768, PANEL_H = 28 };

struct Geom { int x, y; long w, h; };

Rect rect_of(Geom const &g) { return Rect { g.x, g.y, g.w, g.h }; }

Geom ref_slot_content(int slot_index, int eligible_total, Margins const &m)
{
	int const work_h = SCREEN_H - PANEL_H;
	int const work_y = PANEL_H;
	int const half_w = SCREEN_W / 2;

	int ox, oy, ow, oh;
	if (slot_index == 0) {
		ox = 0;      oy = work_y;
		ow = half_w; oh = work_h;
	} else {
		int const bands = eligible_total - 1;
		int const band  = slot_index - 1;
		int const y0 = work_y + (int)((work_h * band) / bands);
		int const y1 = work_y + (int)((work_h * (band + 1)) / bands);
		ox = half_w; oy = y0;
		ow = SCREEN_W - half_w; oh = y1 - y0;
	}

	return Geom { ox + (int)m.left, oy + (int)m.top,
	              ow - (int)m.left - (int)m.right,
	              oh - (int)m.top - (int)m.bottom };
}

Geom ref_maximized_content(Margins const &m)
{
	return Geom { (int)m.left, PANEL_H + (int)m.top,
	              SCREEN_W - (int)m.left - (int)m.right,
	              SCREEN_H - PANEL_H - (int)m.top - (int)m.bottom };
}

Geom ref_title_center(Geom const &content, Margins const &m)
{
	return Geom { content.x + (int)(content.w / 2),
	              content.y - (int)m.top / 2, 0, 0 };
}

Geom ref_button_center(Geom const &content, Margins const &m, Button which)
{
	int const outer_right = content.x + (int)content.w + (int)m.right;
	int const title_mid_y = content.y - (int)m.top / 2;

	int x = outer_right - CHROME_BUTTON_PAD - CHROME_BUTTON_W / 2;
	switch (which) {
	case Button::CLOSE:      break;
	case Button::FULLSCREEN: x -= CHROME_BUTTON_W; break;
	case Button::MINIMIZE:   x -= 2 * CHROME_BUTTON_W; break;
	}
	return Geom { x, title_mid_y, 0, 0 };
}

int ref_grip_inset(unsigned margin)
{
	unsigned const half = margin / 2;
	if (half < 1) return 1;
	if (half > 3) return 3;
	return (int)half;
}

struct Zone_points { int px, py, rx, ry; };

Zone_points ref_zone_points(int index /* 1=N .. 8=SE */,
                            Geom const &content, Margins const &m)
{
	int const ox = content.x - (int)m.left;
	int const oy = content.y - (int)m.top;
	int const ow = (int)content.w + (int)m.left + (int)m.right;
	int const oh = (int)content.h + (int)m.top + (int)m.bottom;

	int const it = ref_grip_inset(m.top);
	int const ib = ref_grip_inset(m.bottom);
	int const il = ref_grip_inset(m.left);
	int const ir = ref_grip_inset(m.right);

	int const D = 30;
	switch (index) {
	case 1:  return { ox + ow / 2, oy + it,      ox + ow / 2, oy + it + D };
	case 2:  return { ox + ow / 2, oy + oh - ib, ox + ow / 2, oy + oh - ib - D };
	case 3:  return { ox + ow - ir, oy + oh / 2, ox + ow - ir - D, oy + oh / 2 };
	case 4:  return { ox + il, oy + oh / 2,      ox + il + D, oy + oh / 2 };
	case 5:  return { ox + il, oy + it,          ox + il + D, oy + it + D };
	case 6:  return { ox + ow - ir, oy + it,     ox + ow - ir - D, oy + it + D };
	case 7:  return { ox + il, oy + oh - ib,     ox + il + D, oy + oh - ib - D };
	default: return { ox + ow - ir, oy + oh - ib,
	                  ox + ow - ir - D, oy + oh - ib - D };
	}
}

struct Zone_delta { int dx, dy, dw, dh; };

Zone_delta ref_zone_delta(int index)
{
	int const D = 30;
	switch (index) {
	case 1:  return {  0, D,  0, -D };
	case 2:  return {  0, 0,  0, -D };
	case 3:  return {  0, 0, -D,  0 };
	case 4:  return {  D, 0, -D,  0 };
	case 5:  return {  D, D, -D, -D };
	case 6:  return {  0, D, -D, -D };
	case 7:  return {  D, 0, -D, -D };
	default: return {  0, 0, -D, -D };
	}
}


/* ============================ fixtures ========================== */

Margins const M { 28, 2, 2, 2 };   /* top bottom left right */
Work_area const WORK { 0, 28, 1024, 740 };

char const *const DEMO_LABEL = "sponge-de -> Sponge DE Demo";
char const *const DEMO_TITLE = "Sponge DE Demo";
char const *const PKG_LABEL  = "pkg_runtime -> pkg_gui_demo";
char const *const PKG_TITLE  = "Sponge Pkg GUI Demo";

char const *const DEMO_LT = "sponge-de -> Sponge DE Demo Sponge DE Demo";
char const *const PKG_LT  = "pkg_runtime -> pkg_gui_demo Sponge Pkg GUI Demo";

Rule_assign assign_of(int x, int y, long w, long h, bool maximized = false)
{
	Rule_assign a;
	a.has_position = true;
	a.xpos = x;
	a.ypos = y;
	a.has_size = true;
	a.width  = w;
	a.height = h;
	a.maximized = maximized;
	return a;
}

Rule_assign assign_label(char const *label, int x, int y, long w, long h,
                         bool maximized = false)
{
	Rule_assign a = assign_of(x, y, w, h, maximized);
	a.label = label;
	return a;
}

Rule_assign assign_prefix(char const *prefix, int x, int y, long w, long h)
{
	Rule_assign a = assign_of(x, y, w, h);
	a.label_prefix = prefix;
	return a;
}

Rule_assign assign_parked(char const *label)
{
	return assign_label(label, PARKED_COORD, PARKED_COORD, 320, 240);
}

Rules_doc doc_of(Rule_frame frame, Rule_assign const *assigns, unsigned n)
{
	Rules_doc d;
	d.frame = frame;
	for (unsigned i = 0; i < n && i < MAX_ASSIGNS; i++)
		d.assigns[d.num_assigns++] = assigns[i];
	return d;
}

Window_list_entry wl_of(unsigned long wid, char const *label,
                        char const *title, long w, long h,
                        bool resizeable = true)
{
	Window_list_entry e;
	e.id = wid;
	e.label = label;
	e.title = title;
	e.width = w;
	e.height = h;
	e.resizeable = resizeable;
	return e;
}

void click(Ctx &c, Model &model, Geom const &button)
{
	bool const p = model.press(button.x, button.y);
	c.check(p, "click: press landed on chrome");
	model.release(button.x, button.y);
	c.check(!model.drag_active(), "click: grab cleared after release");
}

void drag(Model &model, int px, int py, int rx, int ry)
{
	model.press(px, py);
	model.motion(rx, ry);
	model.release(rx, ry);
}


/* ============================== cases =========================== */

void case_create_placement(Ctx &c)
{
	Rule_assign assigns[2] {
		assign_label(DEMO_LABEL, 192, 172, 640, 480),
		assign_prefix("pkg_runtime", 50, 320, 320, 240) };

	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 2));

	Window_list_entry entries[2] {
		wl_of(1, DEMO_LABEL, DEMO_TITLE, 640, 480),
		wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 2);

	Layout_out const l = read_layout(model);
	c.check(l.n == 2, "create: both windows published in window_layout");

	Layout_win const *demo = find_layout(l, DEMO_LT);
	Layout_win const *pkg  = find_layout(l, PKG_LT);
	c.check(demo != nullptr, "create: demo identity (label+title) present");
	c.check(pkg  != nullptr, "create: pkg identity (label+title) present");

	if (demo)
		c.check_rect(demo->x == 192 && demo->y == 172
		          && demo->w == 640 && demo->h == 480,
		             "create: demo at configured content geometry",
		             rect_of(*demo), Rect { 192, 172, 640, 480 });
	if (pkg)
		c.check_rect(pkg->x == 50 && pkg->y == 320
		          && pkg->w == 320 && pkg->h == 240,
		             "create: pkg at configured content geometry",
		             rect_of(*pkg), Rect { 50, 320, 320, 240 });

	c.check(l.boundary_seen, "create: boundary published");
	c.check_rect(l.boundary.xpos == 0 && l.boundary.ypos == 28
	          && l.boundary.width == 1024 && l.boundary.height == 740,
	             "create: boundary IS the work area (0,28,1024,740)",
	             Rect { l.boundary.xpos, l.boundary.ypos,
	                    l.boundary.width, l.boundary.height },
	             Rect { 0, 28, 1024, 740 });

	Req_out const r = read_request(model);
	c.check(!r.malformed, "create: window_request parses");
	Window_request const *r1 = find_req(r, 1), *r2 = find_req(r, 2);
	c.check(r1 && !r1->close && r1->x == 192 && r1->y == 172
	     && r1->w == 640 && r1->h == 480,
	        "create: window_request carries INNER geometry (wid 1)");
	c.check(r2 && !r2->close && r2->x == 50 && r2->y == 320
	     && r2->w == 320 && r2->h == 240,
	        "create: window_request carries INNER geometry (wid 2)");

	c.check(demo && !demo->focused && pkg && !pkg->focused,
	        "create: no focus steal at creation (row 1)");
}


void case_creation_order_cascade(Ctx &c)
{
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, nullptr, 0));

	Window_list_entry entries[2] {
		wl_of(1, "app/one", "One", 200, 100),
		wl_of(2, "app/two", "Two", 300, 150) };
	feed_window_list(model, entries, 2);

	Layout_out const l = read_layout(model);
	Layout_win const *one = find_layout(l, "app/one One");
	Layout_win const *two = find_layout(l, "app/two Two");

	c.check_rect(one && one->x == 2 && one->y == 56
	          && one->w == 200 && one->h == 100,
	             "cascade: first window at work origin + margins",
	             rect_of(one ? *one : Layout_win { }),
	             Rect { 2, 56, 200, 100 });
	c.check_rect(two && two->x == 34 && two->y == 88
	          && two->w == 300 && two->h == 150,
	             "cascade: second window offset by 32,32",
	             rect_of(two ? *two : Layout_win { }),
	             Rect { 34, 88, 300, 150 });

	c.check(model.find(1) && model.find(2)
	     && model.find(1)->create_seq < model.find(2)->create_seq,
	        "creation order: wid 1 created before wid 2");

	Req_out const r = read_request(model);
	c.check(r.n == 2 && r.entries[0].wid == 1 && r.entries[1].wid == 2,
	        "creation order: window_request snapshot is creation-ordered");

	Window_list_entry flipped[2] {
		wl_of(2, "app/two", "Two", 300, 150),
		wl_of(1, "app/one", "One", 200, 100) };
	feed_window_list(model, flipped, 2);

	c.check(model.num_windows() == 2,
	        "creation order: flipped snapshot keeps both windows");
	c.check(model.find(1)->create_seq == 1 && model.find(2)->create_seq == 2,
	        "creation order: sequence numbers survive snapshot reorder");

	Req_out const r2 = read_request(model);
	c.check(r2.n == 2 && r2.entries[0].wid == 1 && r2.entries[1].wid == 2,
	        "creation order: request order stays creation order");
}


void case_destroy(Ctx &c)
{
	Rule_assign assigns[1] { assign_prefix("app", 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[2] {
		wl_of(1, "app/one", "One", 200, 100),
		wl_of(2, "app/two", "Two", 300, 150) };
	feed_window_list(model, entries, 2);

	Window_list_entry rest[1] { wl_of(2, "app/two", "Two", 300, 150) };
	feed_window_list(model, rest, 1);

	c.check(model.num_windows() == 1 && !model.find(1) && model.find(2),
	        "destroy: vanished wid removed from the model");

	Layout_out const l = read_layout(model);
	c.check(l.n == 1 && find_layout(l, "app/one One") == nullptr,
	        "destroy: gone from window_layout");

	Req_out const r = read_request(model);
	c.check(r.n == 1 && find_req(r, 1) == nullptr,
	        "destroy: gone from window_request");

	feed_window_list(model, rest, 0);
	c.check(model.num_windows() == 0, "destroy: empty snapshot clears all");
}


void case_title_move(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Geom const content { 50, 320, 320, 240 };
	Geom const tc = ref_title_center(content, M);

	c.check(model.press(tc.x, tc.y), "move: title press hit chrome");
	c.check(model.drag_active(), "move: title drag grabs");
	model.motion(tc.x + 40, tc.y + 40);
	model.release(tc.x + 40, tc.y + 40);
	c.check(!model.drag_active(), "move: grab released");

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 90 && p->y == 360 && p->w == 320 && p->h == 240,
	             "move: exact drag delta applied, size unchanged",
	             rect_of(p ? *p : Layout_win { }), Rect { 90, 360, 320, 240 });

	Focus_target t;
	c.check(read_focus(model, t) && t.set && t.wid == 2,
	        "move: chrome press steers focus to the window");

	c.check(!model.press(210, 440), "move: client-area press is not chrome");
	model.motion(300, 500);
	model.release(300, 500);
	Layout_win const *p2 = find_layout(read_layout(model), PKG_LT);
	c.check(p2 && p2->x == 90 && p2->y == 360,
	        "move: client-area drag does not move the window");
}


void case_resize_zones(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 400, 300) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[2] {
		wl_of(2, PKG_LABEL, PKG_TITLE, 400, 300),
		wl_of(3, "app/fixed", "Fixed", 100, 100, false) };
	feed_window_list(model, entries, 2);

	static char const *const zone_names[8] =
		{ "north", "south", "east", "west", "nw", "ne", "sw", "se" };

	for (int z = 1; z <= 8; ++z) {
		Layout_win const *p = find_layout(read_layout(model), PKG_LT);
		if (!p) {
			c.check(false, "zone: window missing from window_layout");
			return;
		}
		Geom const before { p->x, p->y, p->w, p->h };
		Zone_points const pts = ref_zone_points(z, before, M);
		Zone_delta  const dz  = ref_zone_delta(z);

		drag(model, pts.px, pts.py, pts.rx, pts.ry);

		Layout_win const *q = find_layout(read_layout(model), PKG_LT);
		if (!q) {
			c.check(false, "zone: window missing after drag");
			return;
		}
		int const got_dx = q->x - before.x;
		int const got_dy = q->y - before.y;
		int const got_dw = (int)q->w - (int)before.w;
		int const got_dh = (int)q->h - (int)before.h;

		bool const moved_ok = got_dx == dz.dx && got_dy == dz.dy
		                   && got_dw == dz.dw && got_dh == dz.dh;
		bool const untouched_ok =
			   (dz.dx != 0 || got_dx == 0)
			&& (dz.dy != 0 || got_dy == 0)
			&& (dz.dw != 0 || got_dw == 0)
			&& (dz.dh != 0 || got_dh == 0);

		Genode::String<128> what("zone ", zone_names[z - 1],
		                         ": expected delta applied");
		c.check(moved_ok && untouched_ok, what.string());
		if (!(moved_ok && untouched_ok))
			c.check_rect(false, "zone delta detail", rect_of(*q),
			             Rect { before.x + dz.dx, before.y + dz.dy,
			                    before.w + dz.dw, before.h + dz.dh });
	}

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	if (p) {
		Geom const before { p->x, p->y, p->w, p->h };
		Zone_points const pts = ref_zone_points(2, before, M);   /* south */
		drag(model, pts.px, pts.py, pts.rx, pts.ry - 500);

		Layout_win const *q = find_layout(read_layout(model), PKG_LT);
		c.check_rect(q && q->h == 64 && q->y == before.y,
		             "zone: resize clamps at 64x64 inner minimum",
		             rect_of(q ? *q : Layout_win { }),
		             Rect { before.x, before.y, before.w, 64 });
	}

	Layout_win const *f0 = find_layout(read_layout(model), "app/fixed Fixed");
	if (f0) {
		Geom const before { f0->x, f0->y, f0->w, f0->h };
		Zone_points const pts = ref_zone_points(4, before, M);   /* west */
		drag(model, pts.px, pts.py, pts.rx, pts.ry);

		Layout_win const *f1 = find_layout(read_layout(model), "app/fixed Fixed");
		c.check_rect(f1 && f1->x == before.x && f1->w == before.w,
		             "zone: non-resizeable resize drag is a no-op",
		             rect_of(f1 ? *f1 : Layout_win { }), rect_of(before));
	} else {
		c.check(false, "zone: non-resizeable fixture present");
	}
}


void case_focus_steering_truth(Ctx &c)
{
	Rule_assign assigns[2] {
		assign_label(DEMO_LABEL, 192, 172, 640, 480),
		assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 2));

	Window_list_entry entries[2] {
		wl_of(1, DEMO_LABEL, DEMO_TITLE, 640, 480),
		wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 2);

	Focus_request req;
	req.id = 10;
	req.label = PKG_LABEL;
	model.focus_request(req);

	Focus_target t;
	c.check(read_focus(model, t) && t.set && t.wid == 2,
	        "focus: focus_request steers the `focus` report");

	Layout_out l = read_layout(model);
	c.check(l.n == 2 && !find_layout(l, PKG_LT)->focused
	                 && !find_layout(l, DEMO_LT)->focused,
	        "focus: steering alone does NOT set focused=\"yes\"");

	Focus_target truth;
	truth.set = true;
	truth.wid = 2;
	model.focus_state(truth);

	l = read_layout(model);
	c.check(find_layout(l, PKG_LT)->focused && !find_layout(l, DEMO_LT)->focused,
	        "focus: focus_state truth alone sets focused=\"yes\"");

	truth.wid = 1;
	model.focus_state(truth);
	l = read_layout(model);
	c.check(!find_layout(l, PKG_LT)->focused && find_layout(l, DEMO_LT)->focused,
	        "focus: focus_state flips the focused attribute");
	c.check(read_focus(model, t) && t.set && t.wid == 1,
	        "focus: changed truth replaces stale steering");

	truth.set = false;
	model.focus_state(truth);
	l = read_layout(model);
	c.check(!find_layout(l, PKG_LT)->focused && !find_layout(l, DEMO_LT)->focused,
	        "focus: empty focus_state clears focused=\"yes\"");
	c.check(read_focus(model, t) && !t.set,
	        "focus: changed empty truth clears stale steering");

	req.id = 10;
	req.label = DEMO_LABEL;
	model.focus_request(req);
	c.check(read_focus(model, t) && !t.set,
	        "focus: same-id focus_request is deduplicated (negative)");

	req.id = 11;
	req.label = DEMO_LABEL;
	model.focus_request(req);
	c.check(read_focus(model, t) && t.set && t.wid == 1,
	        "focus: a new id re-steers focus");

	req.id = 12;
	req.label = "no/such/label";
	model.focus_request(req);
	c.check(read_focus(model, t) && t.set && t.wid == 1,
	        "focus: unknown label does not steal steering (negative)");

	Rule_assign park_doc[1] { assign_parked(PKG_LABEL) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, park_doc, 1));
	{
		Layout_win const *pk = find_layout(read_layout(model), PKG_LT);
		c.check(pk && pk->x == PARKED_COORD && pk->y == PARKED_COORD,
		        "focus: fixture parked");
	}

	req.id = 13;
	req.label = PKG_LABEL;
	model.focus_request(req);

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 50 && p->y == 320 && p->w == 320 && p->h == 240,
	             "focus: focus_request restores a minimized window",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });
	c.check(read_focus(model, t) && t.set && t.wid == 2,
	        "focus: restore-with-focus lands steered (U3)");
}


void case_content_click_focus(Ctx &c)
{
	Rule_assign assigns[2] {
		assign_label(DEMO_LABEL, 192, 172, 640, 480),
		assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 2));

	Window_list_entry entries[2] {
		wl_of(1, DEMO_LABEL, DEMO_TITLE, 640, 480),
		wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 2);

	Focus_target truth { };
	Focus_target echo  { };
	truth.set = true;
	truth.wid = 2;
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 2,
	        "content focus: external truth fills empty steering");

	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 2,
	        "content focus: first identical report is stable");
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 2,
	        "content focus: second identical report is stable");

	Focus_request req { };
	req.id = 20;
	req.label = DEMO_LABEL;
	model.focus_request(req);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 1,
	        "content focus: fresh request leads core truth");
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 1,
	        "content focus: unchanged old truth preserves fresh request");

	truth.wid = 1;
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 1,
	        "content focus: requested target confirmation is adopted");
	Layout_out const l = read_layout(model);
	c.check(find_layout(l, DEMO_LT)->focused && !find_layout(l, PKG_LT)->focused,
	        "content focus: layout truth follows requested confirmation");

	truth.wid = 2;
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 2,
	        "content focus: changed truth replaces a different stale target");

	truth.set = false;
	truth.wid = 77;
	model.focus_state(truth);
	c.check(read_focus(model, echo) && !echo.set,
	        "content focus: no-focus truth clears steering regardless of wid");

	req.id = 21;
	req.label = PKG_LABEL;
	model.focus_request(req);
	truth.wid = 0;
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 2,
	        "content focus: unchanged no-focus truth preserves fresh request");

	truth.set = true;
	truth.wid = 1;
	model.focus_state(truth);
	model.focus_state(truth);
	c.check(read_focus(model, echo) && echo.set && echo.wid == 1,
	        "content focus: re-adopted selection survives another repeat");
}


void case_close_sticky(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Geom const content { 50, 320, 320, 240 };
	Geom const closer = ref_button_center(content, M, Button::CLOSE);
	click(c, model, closer);

	c.check(model.close_pending(2), "close: chrome closer arms the request");

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check(p && p->x == 50 && p->y == 320,
	        "close: window stays published until window_list removal");

	Req_out r = read_request(model);
	Window_request const *e2 = find_req(r, 2);
	c.check(r.n == 1 && e2 && e2->close,
	        "close: window_request carries action=\"close\"");

	r = read_request(model);
	e2 = find_req(r, 2);
	c.check(e2 && e2->close,
	        "close: close node kept in every window_request snapshot");

	Rule_assign assigns2[1] { assign_prefix("app", 50, 320, 320, 240) };
	Model m2;
	m2.configure(WORK, M, Mode::FLOATING,
	             doc_of(Rule_frame::ABSOLUTE, assigns2, 1));
	Window_list_entry e3[1] { wl_of(3, "app/three", "Three", 200, 100) };
	feed_window_list(m2, e3, 1);

	Geom const c3 { 50, 320, 320, 240 };
	Geom const b3 = ref_button_center(c3, M, Button::CLOSE);
	m2.press(b3.x, b3.y);
	m2.release(b3.x + 10, b3.y);
	c.check(!m2.close_pending(3),
	        "close: sloppy button release fires no close (negative)");
	Req_out const r3 = read_request(m2);
	Window_request const *e3r = find_req(r3, 3);
	c.check(e3r && !e3r->close && e3r->w == 320 && e3r->x == 50,
	        "close: sloppy release keeps the geometry entry");

	feed_window_list(model, entries, 0);
	c.check(model.num_windows() == 0, "close: destroyed window dropped");
	r = read_request(model);
	c.check(r.n == 0, "close: close node gone once the wid disappears");

	Window_list_entry recreated[1] { wl_of(6, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, recreated, 1);
	c.check(!model.close_pending(6),
	        "close: recreated window does not inherit close pending");
	r = read_request(model);
	Window_request const *e6 = find_req(r, 6);
	c.check(r.n == 1 && e6 && !e6->close,
	        "close: recreated window starts without action=close");
}


void case_minimize_restore_focus(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Geom const content { 50, 320, 320, 240 };
	Geom const minimizer = ref_button_center(content, M, Button::MINIMIZE);
	click(c, model, minimizer);

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && parked(p->x, p->y) && p->x == PARKED_COORD
	          && p->y == PARKED_COORD && p->w == 320 && p->h == 240,
	             "minimize: parked at (-32000,-32000), size kept",
	             rect_of(p ? *p : Layout_win { }),
	             Rect { PARKED_COORD, PARKED_COORD, 320, 240 });

	Req_out r = read_request(model);
	Window_request const *e2 = find_req(r, 2);
	c.check(e2 && !e2->close && e2->x == PARKED_COORD && e2->y == PARKED_COORD,
	        "minimize: window_request parks with x=y=-32000");

	Rule_assign restore[1] {
		assign_label(PKG_LABEL, 50, 320 - 28, 320, 240) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, restore, 1));

	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 50 && p->y == 320 && p->w == 320 && p->h == 240,
	             "restore: boundary-origin assign lands at the exact geometry",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	Rule_assign parked_again[1] { assign_parked(PKG_LABEL) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, parked_again, 1));
	{
		Layout_win const *pk = find_layout(read_layout(model), PKG_LT);
		c.check(pk && pk->y == PARKED_COORD,
		        "restore: parked again via rules");
	}

	Focus_request req;
	req.id = 20;
	req.label = PKG_LABEL;
	model.focus_request(req);

	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 50 && p->y == 320 && p->w == 320 && p->h == 240,
	             "restore: focus_request restores the saved geometry",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	Focus_target t;
	c.check(read_focus(model, t) && t.set && t.wid == 2,
	        "restore: restore-with-focus steers focus (U3)");

	Rule_assign half_sentinel[1] {
		assign_label(PKG_LABEL, PARKED_COORD, 100, 320, 240) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, half_sentinel, 1));

	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && !parked(p->x, p->y) && p->x == PARKED_COORD
	          && p->y == 128,
	             "restore: single sentinel coordinate does not park (negative)",
	             rect_of(p ? *p : Layout_win { }),
	             Rect { PARKED_COORD, 128, 320, 240 });
}


void case_maximize_restore(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Geom const content { 50, 320, 320, 240 };
	Geom const want = ref_maximized_content(M);
	Geom const fs = ref_button_center(content, M, Button::FULLSCREEN);
	click(c, model, fs);

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == want.x && p->y == want.y
	          && p->w == want.w && p->h == want.h,
	             "maximize: OUTER rect fills the work area (inner by margins)",
	             rect_of(p ? *p : Layout_win { }), rect_of(want));

	Rect const outer = outer_from_inner(rect_of(want), M);
	c.check_rect(outer.x == 0 && outer.y == 28 && outer.w == 1024
	          && outer.h == 740,
	             "maximize: outer geometry equals the work area exactly",
	             outer, Rect { 0, 28, 1024, 740 });

	Geom const fs2 = ref_button_center(want, M, Button::FULLSCREEN);
	click(c, model, fs2);

	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 50 && p->y == 320 && p->w == 320 && p->h == 240,
	             "maximize: toggle restores the previous geometry exactly",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	Rule_assign mx[1] {
		assign_label(PKG_LABEL, 50, 320 - 28, 320, 240, true) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, mx, 1));
	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == want.x && p->y == want.y
	          && p->w == want.w && p->h == want.h,
	             "maximize: rules maximized=\"yes\" fills the work area",
	             rect_of(p ? *p : Layout_win { }), rect_of(want));

	Rule_assign reassert[1] {
		assign_label(PKG_LABEL, want.x, want.y - 28, want.w, want.h, true) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, reassert, 1));

	Geom const fs3 = ref_button_center(want, M, Button::FULLSCREEN);
	click(c, model, fs3);
	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 50 && p->y == 320 && p->w == 320 && p->h == 240,
	             "maximize: maximized-rect republish does not clobber the "
	             "restore target (negative)",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	Rule_assign mx2[1] {
		assign_label(PKG_LABEL, 10, 10 - 28, 200, 150, true) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, mx2, 1));
	Rule_assign un[1] {
		assign_label(PKG_LABEL, 300, 100, 320, 240, false) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, un, 1));

	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 300 && p->y == 128 && p->w == 320 && p->h == 240,
	             "maximize: rules maximized=\"no\" restores the assign geometry",
	             rect_of(p ? *p : Layout_win { }), Rect { 300, 128, 320, 240 });
}


void case_tiling_arrangement(Ctx &c)
{
	Rule_assign assigns[2] {
		assign_label(DEMO_LABEL, 420, 60, 560, 220),
		assign_prefix("pkg_runtime", 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 2));

	Window_list_entry entries[2] {
		wl_of(1, DEMO_LABEL, DEMO_TITLE, 560, 220),
		wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 2);

	Layout_win const *d = find_layout(read_layout(model), DEMO_LT);
	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(d && d->x == 420 && d->y == 60,
	             "tiling: initial floating placement kept (demo)",
	             rect_of(d ? *d : Layout_win { }), Rect { 420, 60, 560, 220 });
	c.check_rect(p && p->x == 50 && p->y == 320,
	             "tiling: initial floating placement kept (pkg)",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	auto assert_slots = [&] (int n, char const *what) {
		Layout_out const l = read_layout(model);
		Geom const m_slot = ref_slot_content(0, n, M);
		Geom const s_slot = n > 1 ? ref_slot_content(1, n, M) : Geom { };
		Layout_win const *dw = find_layout(l, DEMO_LT);
		Layout_win const *pw = find_layout(l, PKG_LT);
		bool ok = dw && pw;
		if (ok)
			ok = dw->x == m_slot.x && dw->y == m_slot.y
			  && dw->w == m_slot.w && dw->h == m_slot.h;
		if (ok && n > 1)
			ok = pw->x == s_slot.x && pw->y == s_slot.y
			  && pw->w == s_slot.w && pw->h == s_slot.h;
		c.check_rect(ok, what,
		             rect_of(dw ? *dw : Layout_win { }), rect_of(m_slot));
	};

	model.mode(Mode::TILING);
	assert_slots(2, "tiling: mode switch reflows master + stack");

	Window_list_entry three[3] {
		wl_of(1, DEMO_LABEL, DEMO_TITLE, 560, 220),
		wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240),
		wl_of(3, "app/three", "Three", 100, 100) };
	feed_window_list(model, three, 3);

	{
		Layout_out const l = read_layout(model);
		Geom const s0 = ref_slot_content(0, 3, M);
		Geom const s1 = ref_slot_content(1, 3, M);
		Geom const s2 = ref_slot_content(2, 3, M);
		Layout_win const *w1 = find_layout(l, DEMO_LT);
		Layout_win const *w2 = find_layout(l, PKG_LT);
		Layout_win const *w3 = find_layout(l, "app/three Three");
		c.check_rect(w1 && w1->x == s0.x && w1->y == s0.y
		          && w1->w == s0.w && w1->h == s0.h,
		             "tiling: 3 windows -> master at slot 0",
		             rect_of(w1 ? *w1 : Layout_win { }), rect_of(s0));
		c.check_rect(w2 && w2->x == s1.x && w2->y == s1.y
		          && w2->w == s1.w && w2->h == s1.h,
		             "tiling: 3 windows -> stack band 0",
		             rect_of(w2 ? *w2 : Layout_win { }), rect_of(s1));
		c.check_rect(w3 && w3->x == s2.x && w3->y == s2.y
		          && w3->w == s2.w && w3->h == s2.h,
		             "tiling: 3 windows -> stack band 1",
		             rect_of(w3 ? *w3 : Layout_win { }), rect_of(s2));
	}

	feed_window_list(model, three, 2);
	assert_slots(2, "tiling: close reflow drops the band");

	Rule_assign parked_doc[1] { assign_parked(PKG_LABEL) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, parked_doc, 1));
	{
		Layout_out const l = read_layout(model);
		Geom const m_slot = ref_slot_content(0, 1, M);
		Layout_win const *dw = find_layout(l, DEMO_LT);
		Layout_win const *pw = find_layout(l, PKG_LT);
		c.check_rect(dw && dw->x == m_slot.x && dw->y == m_slot.y
		          && dw->w == m_slot.w && dw->h == m_slot.h,
		             "tiling: minimize leaves the arrangement (master left-half)",
		             rect_of(dw ? *dw : Layout_win { }), rect_of(m_slot));
		c.check(pw && parked(pw->x, pw->y),
		        "tiling: minimized window parked (-32000,-32000)");
	}

	Rule_assign restore_doc[1] {
		assign_label(PKG_LABEL, 50, 320 - 28, 320, 240) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, restore_doc, 1));
	assert_slots(2, "tiling: restore re-enters the arrangement");

	Rule_assign parked_demo[1] { assign_parked(DEMO_LABEL) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, parked_demo, 1));
	{
		Layout_out const l = read_layout(model);
		Geom const m_slot = ref_slot_content(0, 1, M);
		Layout_win const *pw = find_layout(l, PKG_LT);
		c.check_rect(pw && pw->x == m_slot.x && pw->y == m_slot.y
		          && pw->w == m_slot.w && pw->h == m_slot.h,
		             "tiling: master slot passes to the next eligible window",
		             rect_of(pw ? *pw : Layout_win { }), rect_of(m_slot));
	}

	Rule_assign restore_demo[1] {
		assign_label(DEMO_LABEL, 420, 60 - 28, 560, 220) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, restore_demo, 1));
	assert_slots(2, "tiling: restored master reclaims the master slot");

	{
		Layout_out const l = read_layout(model);
		Layout_win const *pw = find_layout(l, PKG_LT);
		Geom const band { pw->x, pw->y, pw->w, pw->h };
		Geom const fs = ref_button_center(band, M, Button::FULLSCREEN);
		click(c, model, fs);

		Layout_out const l2 = read_layout(model);
		Geom const want = ref_maximized_content(M);
		Layout_win const *pw2 = find_layout(l2, PKG_LT);
		Geom const m_slot = ref_slot_content(0, 1, M);
		Layout_win const *dw2 = find_layout(l2, DEMO_LT);
		c.check_rect(pw2 && pw2->x == want.x && pw2->y == want.y
		          && pw2->w == want.w && pw2->h == want.h,
		             "tiling: maximize fills the work area, leaves the "
		             "arrangement",
		             rect_of(pw2 ? *pw2 : Layout_win { }), rect_of(want));
		c.check_rect(dw2 && dw2->x == m_slot.x && dw2->y == m_slot.y
		          && dw2->w == m_slot.w && dw2->h == m_slot.h,
		             "tiling: remaining window reflows after maximize",
		             rect_of(dw2 ? *dw2 : Layout_win { }), rect_of(m_slot));

		Geom const fs2 = ref_button_center(want, M, Button::FULLSCREEN);
		click(c, model, fs2);
	}
	assert_slots(2, "tiling: maximize-restore returns the arrangement slot");

	Layout_out const before = read_layout(model);
	model.mode(Mode::FLOATING);
	{
		Layout_out const after = read_layout(model);
		Layout_win const *dw = find_layout(after, DEMO_LT);
		Layout_win const *pw = find_layout(after, PKG_LT);
		Layout_win const *db = find_layout(before, DEMO_LT);
		Layout_win const *pb = find_layout(before, PKG_LT);
		c.check_rect(dw && db && dw->x == db->x && dw->y == db->y
		          && dw->w == db->w && dw->h == db->h,
		             "tiling->floating: demo keeps its current geometry",
		             rect_of(dw ? *dw : Layout_win { }),
		             rect_of(db ? *db : Layout_win { }));
		c.check_rect(pw && pb && pw->x == pb->x && pw->y == pb->y
		          && pw->w == pb->w && pw->h == pb->h,
		             "tiling->floating: pkg keeps its current geometry",
		             rect_of(pw ? *pw : Layout_win { }),
		             rect_of(pb ? *pb : Layout_win { }));
	}

	model.mode(Mode::TILING);
	assert_slots(2, "floating->tiling: arrangement reflows again");

	{
		Layout_out const l = read_layout(model);
		Layout_win const *dw = find_layout(l, DEMO_LT);
		Geom const dm { dw->x, dw->y, dw->w, dw->h };
		Geom const closer = ref_button_center(dm, M, Button::CLOSE);
		click(c, model, closer);
		c.check(model.close_pending(1),
		        "tiling: master close request armed (sticky)");
	}
	Window_list_entry only_pkg[1] {
		wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, only_pkg, 1);
	{
		Layout_out const l = read_layout(model);
		Geom const m_slot = ref_slot_content(0, 1, M);
		Layout_win const *pw = find_layout(l, PKG_LT);
		c.check_rect(pw && pw->x == m_slot.x && pw->y == m_slot.y
		          && pw->w == m_slot.w && pw->h == m_slot.h,
		             "tiling: master closed -> remaining window becomes master",
		             rect_of(pw ? *pw : Layout_win { }), rect_of(m_slot));
	}
}


void case_non_resizeable_float(Ctx &c)
{
	Rule_assign assigns[3] {
		assign_label(PKG_LABEL, 50, 320, 320, 240),
		assign_label("app/tool", 100, 400, 200, 150),
		assign_label("app/two", 0, 0, 100, 100) };

	Model model;
	model.configure(WORK, M, Mode::TILING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 3));

	Window_list_entry entries[3] {
		wl_of(1, PKG_LABEL, PKG_TITLE, 320, 240),
		wl_of(2, "app/tool", "Tool", 200, 150, false),
		wl_of(3, "app/two", "Two", 100, 100) };
	feed_window_list(model, entries, 3);

	Layout_out const l = read_layout(model);
	Geom const s0 = ref_slot_content(0, 2, M);
	Geom const s1 = ref_slot_content(1, 2, M);
	Layout_win const *w1 = find_layout(l, PKG_LT);
	Layout_win const *w2 = find_layout(l, "app/tool Tool");
	Layout_win const *w3 = find_layout(l, "app/two Two");

	c.check_rect(w1 && w1->x == s0.x && w1->y == s0.y
	          && w1->w == s0.w && w1->h == s0.h,
	             "non-resizeable: resizeable window 1 is the master",
	             rect_of(w1 ? *w1 : Layout_win { }), rect_of(s0));
	c.check_rect(w3 && w3->x == s1.x && w3->y == s1.y
	          && w3->w == s1.w && w3->h == s1.h,
	             "non-resizeable: resizeable window 2 is the stack band",
	             rect_of(w3 ? *w3 : Layout_win { }), rect_of(s1));
	c.check_rect(w2 && w2->x == 100 && w2->y == 400
	          && w2->w == 200 && w2->h == 150,
	             "non-resizeable: floats at its placed geometry",
	             rect_of(w2 ? *w2 : Layout_win { }), Rect { 100, 400, 200, 150 });

	Geom const t { 100, 400, 200, 150 };
	Zone_points const zp = ref_zone_points(4, t, M);   /* west grip */
	drag(model, zp.px, zp.py, zp.rx, zp.ry);
	Layout_win const *w2b = find_layout(read_layout(model), "app/tool Tool");
	c.check_rect(w2b && w2b->x == 100 && w2b->w == 200,
	             "non-resizeable: resize drag is a no-op (negative)",
	             rect_of(w2b ? *w2b : Layout_win { }), Rect { 100, 400, 200, 150 });

	Geom const tc = ref_title_center(t, M);
	drag(model, tc.x, tc.y, tc.x + 10, tc.y + 10);
	Layout_win const *w2c = find_layout(read_layout(model), "app/tool Tool");
	c.check_rect(w2c && w2c->x == 110 && w2c->y == 410,
	             "non-resizeable: title move works (always floats)",
	             rect_of(w2c ? *w2c : Layout_win { }), Rect { 110, 410, 200, 150 });

	Geom const m1 { (int)s0.x, (int)s0.y, s0.w, s0.h };
	Geom const mtc = ref_title_center(m1, M);
	drag(model, mtc.x, mtc.y, mtc.x + 40, mtc.y + 40);
	Layout_win const *w1b = find_layout(read_layout(model), PKG_LT);
	c.check_rect(w1b && w1b->x == s0.x && w1b->y == s0.y,
	             "tiled: title drag is a no-op in tiling mode (negative)",
	             rect_of(w1b ? *w1b : Layout_win { }), rect_of(s0));

	Zone_points const mz = ref_zone_points(3, m1, M);   /* east grip */
	drag(model, mz.px, mz.py, mz.rx, mz.ry);
	Layout_win const *w1c = find_layout(read_layout(model), PKG_LT);
	c.check_rect(w1c && w1c->w == s0.w && w1c->x == s0.x,
	             "tiled: resize drag is a no-op in tiling mode (negative)",
	             rect_of(w1c ? *w1c : Layout_win { }), rect_of(s0));
}


void case_rules_drift_soak(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	char baseline[16384];
	gen_xml(baseline, sizeof(baseline), "window_layout",
		[&] (Genode::Xml_generator &g) { model.gen_window_layout(g); });

	bool stable = true;
	for (unsigned cycle = 1; cycle <= 25; cycle++) {
		Layout_win const *p = find_layout(read_layout(model), PKG_LT);
		if (!p || p->x != 50 || p->y != 320 || p->w != 320 || p->h != 240) {
			stable = false;
			break;
		}

		Position const rel = assign_position(p->x, p->y, WORK.x, WORK.y);
		Rule_assign republish[2] {
			assign_label(PKG_LABEL, rel.xpos, rel.ypos, p->w, p->h),
			assign_label("decoy/never", (int)cycle, 0, 1, 1) };
		model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, republish, 2));
		model.consume_dirty();
	}

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(stable && p && p->x == 50 && p->y == 320
	          && p->w == 320 && p->h == 240,
	             "soak: 25 rules republishes drift by 0 px",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	char after[16384];
	gen_xml(after, sizeof(after), "window_layout",
		[&] (Genode::Xml_generator &g) { model.gen_window_layout(g); });
	c.check(Genode::strcmp(baseline, after) == 0,
	        "soak: window_layout bytes identical after 25 republishes");

	bool mode_stable = true;
	for (unsigned cycle = 0; cycle < 20 && mode_stable; cycle++) {
		model.mode(Mode::TILING);
		Geom const s0 = ref_slot_content(0, 1, M);
		Layout_win const *w = find_layout(read_layout(model), PKG_LT);
		if (!w || w->x != s0.x || w->y != s0.y
		 || w->w != s0.w || w->h != s0.h) {
			mode_stable = false;
			break;
		}
		model.mode(Mode::FLOATING);
		w = find_layout(read_layout(model), PKG_LT);
		if (!w || w->x != s0.x || w->y != s0.y
		 || w->w != s0.w || w->h != s0.h)
			mode_stable = false;
	}
	c.check(mode_stable,
	        "soak: 20 tiling/floating cycles keep the exact geometry");
}


void case_rules_semantics(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Rule_assign first[2] {
		assign_label(PKG_LABEL, 10, 20 - 28, 100, 50),
		assign_prefix("pkg_runtime", 99, 99, 1, 1) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, first, 2));

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 10 && p->y == 20 && p->w == 100 && p->h == 50,
	             "rules: first matching assign wins (exact before prefix)",
	             rect_of(p ? *p : Layout_win { }), Rect { 10, 20, 100, 50 });

	Rule_assign second[1] {
		assign_label(PKG_LABEL, 40, 40 - 28, 200, 200) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, second, 1));
	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 40 && p->y == 40,
	             "rules: publication is last-wins (second document)",
	             rect_of(p ? *p : Layout_win { }), Rect { 40, 40, 200, 200 });

	Rule_assign third[1] {
		assign_label(PKG_LABEL, 60, 60 - 28, 300, 300) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, third, 1));
	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 60 && p->y == 60,
	             "rules: publication is last-wins (third document)",
	             rect_of(p ? *p : Layout_win { }), Rect { 60, 60, 300, 300 });

	model.rules_invalid();
	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 50 && p->y == 320 && p->w == 320 && p->h == 240,
	             "rules: invalid ROM falls back to the static document",
	             rect_of(p ? *p : Layout_win { }), Rect { 50, 320, 320, 240 });

	Rule_assign hide[1] { };
	hide[0].label = PKG_LABEL;
	hide[0].visible = false;
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, hide, 1));
	Layout_out const l = read_layout(model);
	c.check(find_layout(l, PKG_LT) == nullptr,
	        "rules: visible=\"no\" hides the window from window_layout");

	Rule_assign show[1] { };
	show[0].label = PKG_LABEL;
	show[0].visible = true;
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, show, 1));
	c.check(find_layout(read_layout(model), PKG_LT) != nullptr,
	        "rules: visible=\"yes\" shows the window again");

	/* negative control: an identical publication is not a new
	 * publication — it must NOT snap a user-moved window back */
	Rule_assign place[1] { assign_label(PKG_LABEL, 50, 292, 320, 240) };
	Rules_doc const doc_a = doc_of(Rule_frame::BOUNDARY_RELATIVE, place, 1);
	model.rules(doc_a);

	Geom const tc = ref_title_center(Geom { 50, 320, 320, 240 }, M);
	drag(model, tc.x, tc.y, tc.x + 40, tc.y + 40);
	model.rules(doc_a);

	p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == 90 && p->y == 360,
	             "rules: identical re-feed does not snap back (negative)",
	             rect_of(p ? *p : Layout_win { }), Rect { 90, 360, 320, 240 });
}


void case_mode_work_area_parse(Ctx &c)
{
	c.check(parse_layout_mode(node_of("<config/>")) == Mode::FLOATING,
	        "parse: absent wm.layout.mode reads floating");

	char const *tiling_doc =
		"<config><key name=\"wm.layout.mode\" value=\"tiling\"/></config>";
	c.check(parse_layout_mode(node_of(tiling_doc)) == Mode::TILING,
	        "parse: wm.layout.mode=tiling reads tiling");

	char const *floating_doc =
		"<config><key name=\"wm.layout.mode\" value=\"floating\"/></config>";
	c.check(parse_layout_mode(node_of(floating_doc)) == Mode::FLOATING,
	        "parse: wm.layout.mode=floating reads floating");

	char const *garbage_doc =
		"<config><key name=\"wm.layout.mode\" value=\"banana\"/></config>";
	c.check(parse_layout_mode(node_of(garbage_doc)) == Mode::FLOATING,
	        "parse: unrecognized mode value reads floating (negative)");

	char const *other_key =
		"<config><key name=\"panel.height\" value=\"64\"/></config>";
	c.check(parse_layout_mode(node_of(other_key)) == Mode::FLOATING,
	        "parse: unrelated keys do not switch the mode (negative)");

	Work_area wa;
	char const *wa_doc =
		"<config><work_area x=\"0\" y=\"28\" width=\"1024\" height=\"740\"/></config>";
	c.check(work_area_from_node(node_of(wa_doc), wa)
	     && wa.x == 0 && wa.y == 28 && wa.w == 1024 && wa.h == 740,
	        "parse: explicit work_area config node is read");

	c.check(!work_area_from_node(node_of("<config/>"), wa),
	        "parse: missing work_area falls back (negative)");

	char const *degenerate =
		"<config><work_area x=\"0\" y=\"28\" width=\"0\" height=\"740\"/></config>";
	c.check(!work_area_from_node(node_of(degenerate), wa),
	        "parse: degenerate work_area falls back (negative)");

	char const *rules_doc =
		"<rules><screen name=\"screen\"/>"
		"<assign label=\"x\" target=\"screen\" xpos=\"1\" ypos=\"2\""
		" width=\"3\" height=\"4\" maximized=\"no\"/></rules>";
	Rules_doc doc;
	c.check(rules_doc_from_node(node_of(rules_doc), Rule_frame::ABSOLUTE, doc)
	     && doc.num_assigns == 1
	     && doc.assigns[0].has_position && doc.assigns[0].xpos == 1
	     && doc.assigns[0].ypos == 2 && doc.assigns[0].has_size
	     && doc.assigns[0].width == 3 && doc.assigns[0].height == 4,
	        "parse: rules document round-trips through parse_rules");

	c.check(!rules_doc_from_node(node_of("<other/>"),
	                             Rule_frame::ABSOLUTE, doc),
	        "parse: non-rules node is rejected (negative)");
}


void case_button_click_slop(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Geom const content { 50, 320, 320, 240 };
	Geom const minimizer = ref_button_center(content, M, Button::MINIMIZE);

	model.press(minimizer.x, minimizer.y);
	model.release(minimizer.x + 3, minimizer.y + 3);
	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check(p && parked(p->x, p->y),
	        "click: release within 5 px completes the click (positive)");

	Rule_assign restore[1] {
		assign_label(PKG_LABEL, 50, 320 - 28, 320, 240) };
	model.rules(doc_of(Rule_frame::BOUNDARY_RELATIVE, restore, 1));

	Geom const closer = ref_button_center(content, M, Button::CLOSE);
	model.press(closer.x, closer.y);
	model.release(closer.x + 10, closer.y);
	c.check(!model.close_pending(2),
	        "click: release 10 px away fires no action (negative)");

	Req_out const r = read_request(model);
	Window_request const *e2 = find_req(r, 2);
	c.check(e2 && !e2->close && e2->x == 50 && e2->y == 320,
	        "click: sloppy release leaves geometry untouched");
}


void case_maximized_drag_noop(Ctx &c)
{
	Rule_assign assigns[1] { assign_label(PKG_LABEL, 50, 320, 320, 240) };
	Model model;
	model.configure(WORK, M, Mode::FLOATING,
	                doc_of(Rule_frame::ABSOLUTE, assigns, 1));

	Window_list_entry entries[1] { wl_of(2, PKG_LABEL, PKG_TITLE, 320, 240) };
	feed_window_list(model, entries, 1);

	Geom const content { 50, 320, 320, 240 };
	Geom const fs = ref_button_center(content, M, Button::FULLSCREEN);
	click(c, model, fs);

	Geom const want = ref_maximized_content(M);
	Geom const tc = ref_title_center(want, M);
	c.check(model.press(tc.x, tc.y), "maximized drag: chrome press hit");
	c.check(!model.drag_active(), "maximized drag: no grab on maximized");
	model.motion(tc.x + 40, tc.y + 40);
	model.release(tc.x + 40, tc.y + 40);

	Layout_win const *p = find_layout(read_layout(model), PKG_LT);
	c.check_rect(p && p->x == want.x && p->y == want.y
	          && p->w == want.w && p->h == want.h,
	             "maximized drag: geometry unchanged (negative)",
	             rect_of(p ? *p : Layout_win { }), rect_of(want));
}


unsigned run_all(Model_check_fn report)
{
	struct Case { char const *name; void (*fn)(Ctx &); };
	static Case const cases[] = {
		{ "create_placement",       case_create_placement       },
		{ "creation_order_cascade", case_creation_order_cascade },
		{ "destroy",                case_destroy                },
		{ "title_move",             case_title_move             },
		{ "resize_zones",           case_resize_zones           },
		{ "focus_steering_truth",   case_focus_steering_truth   },
		{ "content_click_focus",    case_content_click_focus    },
		{ "close_sticky",           case_close_sticky           },
		{ "minimize_restore_focus", case_minimize_restore_focus },
		{ "maximize_restore",       case_maximize_restore       },
		{ "tiling_arrangement",     case_tiling_arrangement     },
		{ "non_resizeable_float",   case_non_resizeable_float   },
		{ "rules_drift_soak",       case_rules_drift_soak       },
		{ "rules_semantics",        case_rules_semantics        },
		{ "mode_work_area_parse",   case_mode_work_area_parse   },
		{ "button_click_slop",      case_button_click_slop      },
		{ "maximized_drag_noop",    case_maximized_drag_noop    },
	};

	Ctx c;
	c.report = report;

	for (Case const &cs : cases)
		cs.fn(c);

	return c.failures;
}

}  /* anonymous namespace */


unsigned run_model_tests(Model_check_fn report)
{
	return run_all(report);
}


}  /* namespace Sponge::Layout_model */
