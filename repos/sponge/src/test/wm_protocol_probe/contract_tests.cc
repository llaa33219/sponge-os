/* SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic protocol-v1 contract cases for sponge_wm_protocol
 * (docs/05-sponge-de.md §4.6, frozen 2026-11-12). See contract_tests.h.
 *
 * The golden fixtures are transcribed from the delivered
 * producers/consumers — never captured from this library's own output:
 *
 *   - window_list entry attribute names/order/values:
 *     genode/repos/gems/src/server/wm/window_registry.h
 *     ('generate_window_list_entry' + 'gen_window_attr');
 *   - window_layout boundary/window attribute names/order/values and
 *     the pass-through set (id/highlight/has_alpha/maximizer/closer):
 *     genode/repos/gems/src/app/window_layouter/target_list.h,
 *     window.h ('generate');
 *   - focus_request/rules emission format and assign semantics:
 *     repos/sponge/src/sponge-de/panel/tasklist_controller.cc
 *     ('_publish_focus_request', '_compose_rules',
 *     '_append_assign_for', '_emit_static_rules') and the upstream
 *     assign vocabulary (window_layouter/assign.h 'update');
 *   - byte layout of generated documents: the Genode Xml_generator
 *     format rules (util/xml_generator.h + src/lib/base/xml_generator.cc).
 *
 * Gen byte-shape checks compare the generated XML document EXACTLY
 * (attribute order included, upstream order preserved). Parse checks
 * feed golden documents through Genode::Node, which is the same
 * syntax-agnostic reader the delivered TasklistController uses, so
 * HID-encoded payloads (the upstream producers' default wire format,
 * see base/src/lib/ldso/main.cc '_generate_xml') are covered as well.
 *
 * Pure and deterministic: no libc, no Env, no sessions, no timers, no
 * allocation, no globals (all state is in the local 'Ctx').
 */

#include "contract_tests.h"

#include <sponge/wm_protocol.h>
#include <util/hid.h>

namespace {

using namespace Sponge::Wm_protocol;

struct Ctx
{
	unsigned checks   = 0;
	unsigned failures = 0;
	Check_fn report;

	void check(bool ok, char const *what)
	{
		checks++;
		if (!ok)
			failures++;
		report(ok, what);
	}

	void check_xml(char const *what, char const *expected,
	               char const *generated)
	{
		bool const ok = Genode::strcmp(expected, generated) == 0;
		checks++;
		if (ok) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<512> const msg(what, ": expected <", expected,
		                              "> got <", generated, ">");
		report(false, msg.string());
	}
};


/* Generate an XML document into 'buf' (the Xml_generator appends a
 * trailing newline and terminates the buffer with '\0'). */
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


Genode::Node node_of(char const *bytes, Genode::size_t len)
{
	return Genode::Node(Genode::Const_byte_range_ptr(bytes, len));
}


Genode::Node node_of(char const *nul_terminated)
{
	return node_of(nul_terminated, Genode::strlen(nul_terminated));
}


bool str_eq(char const *a, char const *b)
{
	return Genode::strcmp(a, b) == 0;
}


/* ==================== 1. window_list shape ===================== */

void case_window_list(Ctx &c)
{
	/* full entry: upstream attribute order and 'yes' booleans */
	Window_list_entry full;
	full.id         = 7;
	full.label      = "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo";
	full.title      = "Hello World";
	full.width      = 320;
	full.height     = 240;
	full.has_alpha  = true;
	full.hidden     = true;
	full.resizeable = true;

	char buf[1024];

	c.check_xml("window_list gen: full entry byte shape (upstream attribute order)",
	            "<window_list>\n"
	            "\t<window id=\"7\" label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
	            " title=\"Hello World\" width=\"320\" height=\"240\""
	            " has_alpha=\"yes\" hidden=\"yes\" resizeable=\"yes\"/>\n"
	            "</window_list>\n",
	            gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
		            gen_window_list_entry(g, full);
	            }));

	/* false booleans omitted (upstream form), empty title emitted */
	Window_list_entry plain;
	plain.id     = 8;
	plain.label  = "pkg_runtime -> other";
	plain.width  = 100;
	plain.height = 50;

	c.check_xml("window_list gen: booleans omitted when false",
	            "<window_list>\n"
	            "\t<window id=\"8\" label=\"pkg_runtime -> other\" title=\"\""
	            " width=\"100\" height=\"50\"/>\n"
	            "</window_list>\n",
	            gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
		            gen_window_list_entry(g, plain);
	            }));

	/* omission rules: unsized and label-less entries are never emitted */
	Window_list_entry unsized = full;
	unsized.width  = 0;
	unsized.height = 240;

	Window_list_entry no_label = full;
	no_label.label = "";

	bool emitted = true;
	c.check_xml("window_list gen: unsized entry omitted (omitted until first sized)",
	            "<window_list/>\n",
	            gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
		            emitted = gen_window_list_entry(g, unsized);
	            }));
	c.check(!emitted, "window_list gen: unsized entry reports no emission");

	c.check_xml("window_list gen: label-less entry omitted (no identity)",
	            "<window_list/>\n",
	            gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
		            emitted = gen_window_list_entry(g, no_label);
	            }));
	c.check(!emitted, "window_list gen: label-less entry reports no emission");

	c.check_xml("window_list gen: full entry reports emission",
	            "<window_list>\n"
	            "\t<window id=\"7\" label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
	            " title=\"Hello World\" width=\"320\" height=\"240\""
	            " has_alpha=\"yes\" hidden=\"yes\" resizeable=\"yes\"/>\n"
	            "</window_list>\n",
	            gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
		            emitted = gen_window_list_entry(g, full);
	            }));
	c.check(emitted, "window_list gen: full entry reports emission");

	/* parse: upstream-shaped golden (pass-through id/width/height +
	 * 'yes' booleans), consumed fields exact */
	Window_list_entry got { };
	unsigned num = 0;

	bool ok = parse_window_list(
		node_of("<window_list>"
		        "<window id=\"3\""
		        " label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
		        " title=\"Hello World\" width=\"320\" height=\"240\""
		        " has_alpha=\"yes\" hidden=\"yes\" resizeable=\"yes\"/>"
		        "</window_list>"),
		[&] (Window_list_entry const &e) { got = e; num++; });

	c.check(ok, "window_list parse: upstream-shaped document accepted");
	c.check(num == 1, "window_list parse: one entry visited");
	c.check(got.id == 3 && got.width == 320 && got.height == 240,
	        "window_list parse: id/width/height preserved");
	c.check(str_eq(got.label.string(),
	               "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo"),
	        "window_list parse: label (with spaces and '->') preserved verbatim");
	c.check(str_eq(got.title.string(), "Hello World"),
	        "window_list parse: title preserved");
	c.check(got.has_alpha && got.hidden && got.resizeable,
	        "window_list parse: 'yes' booleans read as true");

	/* parse: missing optional data takes the defaults */
	num = 0;
	ok = parse_window_list(
		node_of("<window_list><window label=\"a b\" title=\"t\"/>"
		        "</window_list>"),
		[&] (Window_list_entry const &e) {
			got = e;
			num++;
		});

	c.check(ok, "window_list parse: minimal entry document accepted");
	c.check(num == 1, "window_list parse: minimal entry visited");
	c.check(got.id == 0 && got.width == 0 && got.height == 0,
	        "window_list parse: missing id/width/height default to 0");
	c.check(!got.has_alpha && !got.hidden && !got.resizeable,
	        "window_list parse: missing booleans default to false");

	/* parse: label-less entries are ignored, empty and missing alike */
	num = 0;
	ok = parse_window_list(
		node_of("<window_list>"
		        "<window id=\"1\" title=\"x\" width=\"10\" height=\"10\"/>"
		        "<window id=\"2\" label=\"\" title=\"x\" width=\"10\" height=\"10\"/>"
		        "<window id=\"3\" label=\"kept\" title=\"t\" width=\"10\" height=\"10\"/>"
		        "</window_list>"),
		[&] (Window_list_entry const &e) {
			got = e;
			num++;
		});

	c.check(ok, "window_list parse: document with label-less entries accepted");
	c.check(num == 1, "window_list parse: label-less entries ignored (missing and empty)");
	c.check(str_eq(got.label.string(), "kept"),
	        "window_list parse: only the labeled entry visited");

	/* parse: malformed / wrong root rejected */
	num = 0;
	ok = parse_window_list(
		node_of("<window_list><window label=\"a\""),
		[&] (Window_list_entry const &) { num++; });
	c.check(!ok && num == 0, "window_list parse: malformed document rejected");

	ok = parse_window_list(
		node_of("<windows><window label=\"a\"/></windows>"),
		[&] (Window_list_entry const &) { num++; });
	c.check(!ok, "window_list parse: wrong root type rejected");

	/* round trip: generated document parses back identically */
	num = 0;
	ok = parse_window_list(
		node_of(gen_xml(buf, sizeof(buf), "window_list", [&] (Genode::Xml_generator &g) {
			gen_window_list_entry(g, full);
			gen_window_list_entry(g, plain);
		})),
		[&] (Window_list_entry const &e) {
			if (num == 0)
				c.check(str_eq(e.label.string(), full.label.string())
				     && e.id == full.id && e.width == full.width
				     && e.has_alpha && e.hidden && e.resizeable,
				        "window_list round trip: first entry fields");
			if (num == 1)
				c.check(str_eq(e.label.string(), plain.label.string())
				     && e.id == plain.id && e.height == plain.height
				     && !e.has_alpha && !e.hidden && !e.resizeable,
				        "window_list round trip: second entry fields");
			num++;
		});
	c.check(ok && num == 2, "window_list round trip: document order preserved");
}


/* =================== 2. window_layout shape ==================== */

void case_window_layout(Ctx &c)
{
	Layout_window w;
	w.title   = "pkg_runtime -> pkg_gui_demo Hello";
	w.xpos    = 50;
	w.ypos    = 320;
	w.width   = 320;
	w.height  = 240;
	w.focused = true;

	Boundary b;
	b.name   = "screen";
	b.width  = 1024;
	b.height = 768;

	char buf[1024];

	c.check_xml("window_layout gen: focused window byte shape (pass-throughs omitted)",
	            "<window_layout>\n"
	            "\t<boundary name=\"screen\" xpos=\"0\" ypos=\"0\""
	            " width=\"1024\" height=\"768\">\n"
	            "\t\t<window title=\"pkg_runtime -> pkg_gui_demo Hello\""
	            " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\""
	            " focused=\"yes\"/>\n"
	            "\t</boundary>\n"
	            "</window_layout>\n",
	            gen_xml(buf, sizeof(buf), "window_layout", [&] (Genode::Xml_generator &g) {
		            gen_boundary(g, b, &w, 1);
	            }));

	Layout_window unfocused = w;
	unfocused.focused = false;
	unfocused.title   = "pkg_runtime -> other";

	c.check_xml("window_layout gen: unfocused window omits 'focused'",
	            "<window_layout>\n"
	            "\t<boundary name=\"screen\" xpos=\"0\" ypos=\"0\""
	            " width=\"1024\" height=\"768\">\n"
	            "\t\t<window title=\"pkg_runtime -> other\""
	            " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\"/>\n"
	            "\t\t<window title=\"pkg_runtime -> pkg_gui_demo Hello\""
	            " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\""
	            " focused=\"yes\"/>\n"
	            "\t</boundary>\n"
	            "</window_layout>\n",
	            gen_xml(buf, sizeof(buf), "window_layout", [&] (Genode::Xml_generator &g) {
		            Layout_window pair[2] { unfocused, w };
		            gen_boundary(g, b, pair, 2);
	            }));

	/* title-less windows are never emitted */
	Layout_window no_title = w;
	no_title.title = "";

	bool emitted = true;
	c.check_xml("window_layout gen: title-less window omitted inside boundary",
	            "<window_layout>\n"
	            "\t<boundary name=\"screen\" xpos=\"0\" ypos=\"0\""
	            " width=\"1024\" height=\"768\"/>\n"
	            "</window_layout>\n",
	            gen_xml(buf, sizeof(buf), "window_layout", [&] (Genode::Xml_generator &g) {
		            gen_boundary(g, b, &no_title, 1);
	            }));

	c.check_xml("window_layout gen: title-less window reports no emission",
	            "<window_layout/>\n",
	            gen_xml(buf, sizeof(buf), "window_layout", [&] (Genode::Xml_generator &g) {
		            emitted = gen_window_layout_window(g, no_title);
	            }));
	c.check(!emitted, "window_layout gen: title-less window emission flag false");

	/* parse: upstream-shaped golden with the full pass-through set
	 * (id, <highlight>, has_alpha, maximizer, closer) — consumed
	 * fields identical to the minimal shape */
	Layout_window got { };
	Boundary got_b { };
	unsigned num = 0;

	bool ok = parse_window_layout(
		node_of("<window_layout>"
		        "<boundary name=\"screen\" xpos=\"0\" ypos=\"0\""
		        " width=\"1024\" height=\"768\">"
		        "<window id=\"3\" title=\"pkg_runtime -> pkg_gui_demo Hello\""
		        " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\""
		        " focused=\"yes\">"
		        "<highlight><title-bar pressed=\"yes\"/></highlight>"
		        "</window>"
		        "</boundary>"
		        "</window_layout>"),
		[&] (Boundary const &boundary, Layout_window const &win) {
			got_b = boundary;
			got = win;
			num++;
		});

	c.check(ok, "window_layout parse: upstream-shaped document accepted");
	c.check(num == 1, "window_layout parse: one window visited");
	c.check(str_eq(got_b.name.string(), "screen")
	     && got_b.xpos == 0 && got_b.ypos == 0
	     && got_b.width == 1024 && got_b.height == 768,
	        "window_layout parse: boundary attrs consumed");
	c.check(str_eq(got.title.string(), "pkg_runtime -> pkg_gui_demo Hello")
	     && got.xpos == 50 && got.ypos == 320
	     && got.width == 320 && got.height == 240,
	        "window_layout parse: window attrs consumed (screen-absolute)");
	c.check(got.focused,
	        "window_layout parse: focused=\"yes\" read as true");

	ok = parse_window_layout(
		node_of("<window_layout>"
		        "<boundary name=\"screen\" xpos=\"10\" ypos=\"20\""
		        " width=\"1024\" height=\"768\">"
		        "<window id=\"3\" title=\"pkg_runtime -> pkg_gui_demo Hello\""
		        " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\""
		        " has_alpha=\"yes\" maximizer=\"yes\" closer=\"yes\"/>"
		        "</boundary>"
		        "</window_layout>"),
		[&] (Boundary const &boundary, Layout_window const &win) {
			got_b = boundary;
			got = win;
			num++;
		});

	c.check(ok, "window_layout parse: pass-through-only attrs accepted");
	c.check(got_b.xpos == 10 && got_b.ypos == 20,
	        "window_layout parse: boundary origin (non-zero) consumed");
	c.check(!got.focused,
	        "window_layout parse: missing focused defaults to false");

	/* parse: title-less windows ignored; missing optional data defaults */
	num = 0;
	ok = parse_window_layout(
		node_of("<window_layout>"
		        "<boundary name=\"screen\">"
		        "<window title=\"\" xpos=\"1\" ypos=\"1\" width=\"1\" height=\"1\"/>"
		        "<window title=\"kept\"/>"
		        "</boundary>"
		        "</window_layout>"),
		[&] (Boundary const &, Layout_window const &win) {
			got = win;
			num++;
		});

	c.check(ok, "window_layout parse: minimal document accepted");
	c.check(num == 1, "window_layout parse: title-less window ignored");
	c.check(str_eq(got.title.string(), "kept")
	     && got.xpos == 0 && got.ypos == 0
	     && got.width == 0 && got.height == 0,
	        "window_layout parse: missing geometry defaults to 0");

	/* parse: multi-boundary pairing */
	num = 0;
	unsigned paired = 0;
	ok = parse_window_layout(
		node_of("<window_layout>"
		        "<boundary name=\"a\" xpos=\"1\" ypos=\"2\" width=\"3\" height=\"4\">"
		        "<window title=\"wa\" xpos=\"9\" ypos=\"9\" width=\"9\" height=\"9\"/>"
		        "</boundary>"
		        "<boundary name=\"b\" xpos=\"5\" ypos=\"6\" width=\"7\" height=\"8\"/>"
		        "</window_layout>"),
		[&] (Boundary const &boundary, Layout_window const &win) {
			if (str_eq(boundary.name.string(), "a")
			 && str_eq(win.title.string(), "wa"))
				paired++;
			num++;
		});
	c.check(ok && num == 1 && paired == 1,
	        "window_layout parse: window paired with its owning boundary");

	/* parse: malformed / wrong root rejected */
	num = 0;
	ok = parse_window_layout(
		node_of("<window_layout><boundary name=\"s\">"),
		[&] (Boundary const &, Layout_window const &) { num++; });
	c.check(!ok && num == 0, "window_layout parse: malformed document rejected");

	ok = parse_window_layout(
		node_of("<layout/>"),
		[&] (Boundary const &, Layout_window const &) { num++; });
	c.check(!ok, "window_layout parse: wrong root type rejected");

	/* round trip through the generator */
	num = 0;
	ok = parse_window_layout(
		node_of(gen_xml(buf, sizeof(buf), "window_layout", [&] (Genode::Xml_generator &g) {
			Layout_window pair[2] { unfocused, w };
			gen_boundary(g, b, pair, 2);
		})),
		[&] (Boundary const &boundary, Layout_window const &win) {
			if (num == 1)
				c.check(win.focused
				     && str_eq(win.title.string(), w.title.string())
				     && win.xpos == w.xpos && win.ypos == w.ypos,
				        "window_layout round trip: focused window fields");
			c.check(str_eq(boundary.name.string(), "screen"),
			        "window_layout round trip: boundary carried to each window");
			num++;
		});
	c.check(ok && num == 2, "window_layout round trip: two windows in order");
}


/* ================== 3. geometry semantics ====================== */

void case_geometry(Ctx &c)
{
	c.check(parked(-32000, -32000), "geometry: (-32000,-32000) reads as parked");
	c.check(parked(-32001, -32000), "geometry: both below sentinel parks");
	c.check(parked(-32000, -32001), "geometry: both below sentinel parks (2)");
	c.check(!parked(-32000, -31999),
	        "geometry: single sentinel coordinate does not park (y)");
	c.check(!parked(-31999, -32000),
	        "geometry: single sentinel coordinate does not park (x)");
	c.check(!parked(-32000, 320),
	        "geometry: sentinel x alone does not park");
	c.check(!parked(50, 320), "geometry: on-screen position not parked");
	c.check(!parked(0, 0), "geometry: origin not parked");

	Position const p = parked_position();
	c.check(p.xpos == -32000 && p.ypos == -32000,
	        "geometry: parked_position is the established (-32000,-32000) write value");

	Position const zero_origin = assign_position(50, 320, 0, 0);
	c.check(zero_origin.xpos == 50 && zero_origin.ypos == 320,
	        "geometry: boundary-origin conversion is identity at origin (0,0)");

	Position const rel = assign_position(50, 320, 10, 20);
	c.check(rel.xpos == 40 && rel.ypos == 300,
	        "geometry: assign coords are target-relative (boundary origin subtracted)");

	Position const neg = assign_position(5, 5, 10, 20);
	c.check(neg.xpos == -5 && neg.ypos == -15,
	        "geometry: conversion may yield negative target-relative coords");

	/* no-drift identity: feeding the converted value back through the
	 * boundary origin reproduces the absolute position exactly (the
	 * controller comment's +28px/cycle runaway is what happens when
	 * the subtraction is skipped) */
	Position const back { 10 + rel.xpos, 20 + rel.ypos };
	c.check(back.xpos == 50 && back.ypos == 320,
	        "geometry: origin + assign_position(abs) == abs (republish is stable)");
}


/* ============ 4. focus_request shape + id preservation ========== */

void case_focus_request(Ctx &c)
{
	Focus_request req;

	bool ok = parse_focus_request(
		node_of("<focus_request id=\"1\" label=\"pkg_runtime -> pkg_gui_demo\"/>"),
		req);
	c.check(ok, "focus_request parse: controller-shaped document accepted");
	c.check(req.id == 1, "focus_request parse: id preserved (first counter value)");
	c.check(str_eq(req.label.string(), "pkg_runtime -> pkg_gui_demo"),
	        "focus_request parse: label preserved verbatim");

	/* id preservation across the counter's full unsigned range
	 * (the controller's counter is an 'unsigned') */
	ok = parse_focus_request(
		node_of("<focus_request id=\"4294967295\""
		        " label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\"/>"),
		req);
	c.check(ok, "focus_request parse: unsigned-max id accepted");
	c.check(req.id == 4294967295UL,
	        "focus_request parse: unsigned-max id preserved (no wrap/truncation)");

	ok = parse_focus_request(
		node_of("<focus_request id=\"3748291021\" label=\"pkg_runtime -> a\"/>"),
		req);
	c.check(ok && req.id == 3748291021UL,
	        "focus_request parse: arbitrary 10-digit id preserved");

	/* strictness: missing/empty required data and malformed input are
	 * rejected and leave the output untouched */
	req.id    = 123;
	req.label = "sentinel";

	ok = parse_focus_request(node_of("<focus_request label=\"x\"/>"), req);
	c.check(!ok && req.id == 123 && str_eq(req.label.string(), "sentinel"),
	        "focus_request parse: missing id rejected (output untouched)");

	ok = parse_focus_request(node_of("<focus_request id=\"1\"/>"), req);
	c.check(!ok && req.id == 123,
	        "focus_request parse: missing label rejected");

	ok = parse_focus_request(node_of("<focus_request id=\"1\" label=\"\"/>"), req);
	c.check(!ok && req.id == 123,
	        "focus_request parse: empty label rejected (nothing to focus)");

	ok = parse_focus_request(node_of("<focus id=\"1\" label=\"x\"/>"), req);
	c.check(!ok, "focus_request parse: wrong root type rejected");

	ok = parse_focus_request(node_of("<focus_request id=\"1\" label=\"x\""), req);
	c.check(!ok, "focus_request parse: malformed document rejected");

	ok = parse_focus_request(node_of(""), req);
	c.check(!ok, "focus_request parse: empty input rejected");
}


/* ================== 5. rules shape (sole writer) ================ */

void case_rules(Ctx &c)
{
	/* the controller's publication format (_compose_rules +
	 * _append_assign_for + _emit_static_rules): screen first, then the
	 * tracked assigns frontmost first, then static fragments, then the
	 * key-sequence nodes */
	static char const *PUB =
		"<rules>"
		"<screen name=\"screen\"/>"
		"<assign label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
		" target=\"screen\" xpos=\"40\" ypos=\"300\""
		" width=\"320\" height=\"240\" maximized=\"yes\"/>"
		"<assign label=\"pkg_runtime -> other\""
		" target=\"screen\" xpos=\"-32000\" ypos=\"-32000\""
		" width=\"320\" height=\"240\" maximized=\"no\"/>"
		"<assign label_prefix=\"pkg_runtime\" target=\"screen\""
		" xpos=\"any\" ypos=\"any\" width=\"512\" height=\"384\"/>"
		"<press key=\"BTN_LEFT\" action=\"drag\"/>"
		"<release key=\"BTN_LEFT\" action=\"drop\"/>"
		"</rules>";

	Rules info;
	Assign assigns[8];
	unsigned num = 0;

	bool ok = parse_rules(node_of(PUB), info,
		[&] (Assign const &a) {
			if (num < 8)
				assigns[num] = a;
			num++;
		});

	c.check(ok, "rules parse: publication-shaped document accepted");
	c.check(info.num_screens == 1
	     && str_eq(info.screen_name.string(), "screen"),
	        "rules parse: <screen name=\"screen\"/> consumed");
	c.check(num == 3 && info.num_assigns == 3,
	        "rules parse: three assigns visited (tracked + static)");

	/* first assign (frontmost) carries the controller's exact attrs */
	c.check(str_eq(assigns[0].label.string(),
	               "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo")
	     && str_eq(assigns[0].target.string(), "screen"),
	        "rules parse: frontmost assign label/target exact");
	c.check(assigns[0].has_position && assigns[0].xpos == 40
	     && assigns[0].ypos == 300,
	        "rules parse: assign target-relative xpos/ypos consumed");
	c.check(assigns[0].has_size && assigns[0].width == 320
	     && assigns[0].height == 240,
	        "rules parse: assign width/height consumed");
	c.check(assigns[0].maximized,
	        "rules parse: maximized=\"yes\" reads as true");
	c.check(assigns[0].visible,
	        "rules parse: missing visible defaults to true (upstream default)");

	/* second assign: the parking sentinel round-trips through rules */
	c.check(assigns[1].xpos == -32000 && assigns[1].ypos == -32000
	     && parked(assigns[1].xpos, assigns[1].ypos),
	        "rules parse: parking sentinel preserved in the assign");
	c.check(!assigns[1].maximized,
	        "rules parse: maximized=\"no\" reads as false");

	/* third assign: static-fragment vocabulary (label_prefix, any) */
	c.check(str_eq(assigns[2].label.string(), "")
	     && str_eq(assigns[2].label_prefix.string(), "pkg_runtime")
	     && str_eq(assigns[2].label_suffix.string(), ""),
	        "rules parse: static fragment label_prefix captured");
	c.check(assigns[2].has_position && assigns[2].xpos_any
	     && assigns[2].ypos_any && assigns[2].xpos == 0
	     && assigns[2].ypos == 0,
	        "rules parse: xpos/ypos=\"any\" captured (value unparsable -> 0)");
	c.check(assigns[2].has_size && assigns[2].width == 512
	     && assigns[2].height == 384,
	        "rules parse: static fragment size consumed");

	/* key-sequence nodes (contract item 4, kept in every publication) */
	c.check(info.has_press && str_eq(info.press.key.string(), "BTN_LEFT")
	     && str_eq(info.press.action.string(), "drag"),
	        "rules parse: <press key=\"BTN_LEFT\" action=\"drag\"/> consumed");
	c.check(info.has_release && str_eq(info.release.key.string(), "BTN_LEFT")
	     && str_eq(info.release.action.string(), "drop"),
	        "rules parse: <release key=\"BTN_LEFT\" action=\"drop\"/> consumed");
	c.check(info.num_press == 1 && info.num_release == 1,
	        "rules parse: one press and one release node counted");

	/* document order is semantic (frontmost first) */
	c.check(str_eq(assigns[0].label.string(),
	               "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo")
	     && str_eq(assigns[1].label.string(), "pkg_runtime -> other"),
	        "rules parse: assign document order preserved (frontmost first)");

	/* partial/missing optional data handling */
	num = 0;
	ok = parse_rules(
		node_of("<rules><assign label=\"a\" target=\"screen\""
		        " xpos=\"1\" width=\"2\"/></rules>"),
		info, [&] (Assign const &a) {
			if (num < 8)
				assigns[num] = a;
			num++;
		});
	c.check(ok && num == 1,
	        "rules parse: partial assign accepted");
	c.check(!assigns[0].has_position && !assigns[0].has_size,
	        "rules parse: xpos without ypos / width without height not a position/size");
	c.check(!assigns[0].maximized && assigns[0].visible,
	        "rules parse: missing maximized=false, missing visible=true");

	num = 0;
	ok = parse_rules(
		node_of("<rules>"
		        "<assign label=\"a\" visible=\"no\" maximized=\"no\"/>"
		        "<assign label=\"b\" visible=\"yes\" maximized=\"yes\"/>"
		        "</rules>"),
		info, [&] (Assign const &a) {
			if (num < 8)
				assigns[num] = a;
			num++;
		});
	c.check(ok && !assigns[0].visible && assigns[1].visible,
	        "rules parse: visible=\"no\"/\"yes\" lexemes read");
	c.check(!assigns[0].maximized && assigns[1].maximized,
	        "rules parse: maximized lexemes read");

	num = 0;
	ok = parse_rules(
		node_of("<rules><assign label=\"a\" xpos=\"any\"/></rules>"),
		info, [&] (Assign const &a) {
			if (num < 8)
				assigns[num] = a;
			num++;
		});
	c.check(ok && assigns[0].xpos_any && !assigns[0].ypos_any
	     && !assigns[0].has_position,
	        "rules parse: lone xpos=\"any\" is not a position (upstream vocabulary)");

	num = 0;
	ok = parse_rules(
		node_of("<rules><assign/></rules>"),
		info, [&] (Assign const &a) {
			if (num < 8)
				assigns[num] = a;
			num++;
		});
	c.check(ok && num == 1
	     && str_eq(assigns[0].label.string(), "")
	     && str_eq(assigns[0].target.string(), ""),
	        "rules parse: attribute-less assign tolerated (all fields default)");

	/* missing screen / press / release tolerated and counted */
	ok = parse_rules(
		node_of("<rules><assign label=\"a\"/></rules>"), info,
		[&] (Assign const &) { });
	c.check(ok && info.num_screens == 0 && !info.has_press
	     && !info.has_release,
	        "rules parse: missing screen/press/release tolerated");

	/* unknown extra children ignored */
	num = 0;
	ok = parse_rules(
		node_of("<rules><foo bar=\"1\"/><assign label=\"a\"/></rules>"),
		info, [&] (Assign const &) { num++; });
	c.check(ok && num == 1 && info.num_assigns == 1,
	        "rules parse: unknown children ignored");

	/* malformed / wrong root rejected */
	ok = parse_rules(node_of("<rules><assign label=\"a\""), info,
		[&] (Assign const &) { });
	c.check(!ok, "rules parse: malformed document rejected");

	ok = parse_rules(node_of("<rulez/>"), info,
		[&] (Assign const &) { });
	c.check(!ok, "rules parse: wrong root type rejected");
}


/* ============ 6. identity chain (TasklistController match) ====== */

void case_identity(Ctx &c)
{
	/* the controller matches window_layout titles against
	 * 'label + " " + title' or the bare 'label', never by splitting on
	 * spaces (tasklist_controller.cc _recompute_tracked) */
	Genode::String<256> const composed =
		layout_title("pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo",
		             "Hello World");

	c.check(str_eq(composed.string(),
	               "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo Hello World"),
	        "identity: title = label + \" \" + client title (label with spaces kept whole)");

	Genode::String<256> const bare =
		layout_title("pkg_runtime -> pkg_gui_demo", "");

	c.check(str_eq(bare.string(), "pkg_runtime -> pkg_gui_demo"),
	        "identity: empty client title -> bare label (no trailing space)");
	c.check(bare.length() == Genode::strlen("pkg_runtime -> pkg_gui_demo") + 1,
	        "identity: bare label has no hidden trailing characters");

	c.check(str_eq(layout_title("pkg_runtime -> pkg_gui_demo", "Hello").string(),
	               "pkg_runtime -> pkg_gui_demo Hello"),
	        "identity: non-empty title gets exactly one separating space");

	/* full identity chain: window_list entry + window_layout title */
	Window_list_entry e;
	unsigned n = 0;
	parse_window_list(
		node_of("<window_list>"
		        "<window id=\"3\""
		        " label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
		        " title=\"Hello World\" width=\"320\" height=\"240\"/>"
		        "</window_list>"),
		[&] (Window_list_entry const &entry) { e = entry; n++; });

	Layout_window lw { };
	unsigned m = 0;
	parse_window_layout(
		node_of("<window_layout>"
		        "<boundary name=\"screen\" xpos=\"0\" ypos=\"0\""
		        " width=\"1024\" height=\"768\">"
		        "<window title=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo Hello World\""
		        " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\"/>"
		        "</boundary>"
		        "</window_layout>"),
		[&] (Boundary const &, Layout_window const &win) { lw = win; m++; });

	c.check(n == 1 && m == 1
	     && str_eq(layout_title(e.label.string(), e.title.string()).string(),
	               lw.title.string()),
	        "identity: layout_title(list label, list title) matches window_layout title");

	/* empty-title identity: the layout title is the bare label and
	 * matches the controller's 'qt == e.label' branch */
	e = Window_list_entry { };
	n = 0;
	parse_window_list(
		node_of("<window_list>"
		        "<window id=\"3\" label=\"pkg_runtime -> pkg_gui_demo\""
		        " title=\"\" width=\"320\" height=\"240\"/>"
		        "</window_list>"),
		[&] (Window_list_entry const &entry) { e = entry; n++; });

	lw = Layout_window { };
	m = 0;
	parse_window_layout(
		node_of("<window_layout>"
		        "<boundary name=\"screen\" xpos=\"0\" ypos=\"0\""
		        " width=\"1024\" height=\"768\">"
		        "<window title=\"pkg_runtime -> pkg_gui_demo\""
		        " xpos=\"50\" ypos=\"320\" width=\"320\" height=\"240\"/>"
		        "</boundary>"
		        "</window_layout>"),
		[&] (Boundary const &, Layout_window const &win) { lw = win; m++; });

	c.check(n == 1 && m == 1
	     && str_eq(layout_title(e.label.string(), e.title.string()).string(),
	               lw.title.string())
	     && str_eq(lw.title.string(), e.label.string()),
	        "identity: empty-title layout title equals the bare window_list label");
}


/* ============ 7. wire-encoding independence (HID) ============== */

void case_hid_encoding(Ctx &c)
{
	/*
	 * The upstream producers serialize through Genode::Generator,
	 * which defaults to the HID encoding (base/src/lib/ldso/main.cc
	 * '_generate_xml' = false). The delivered TasklistController
	 * consumes both encodings through Genode::Node; the parse side of
	 * this library must do the same. The fixture below mirrors the
	 * upstream window_list emission calls (window_registry.h) through
	 * Hid_generator — the same node/attribute API the Generator facade
	 * forwards to.
	 */
	char buf[512];
	Genode::size_t len = 0;

	Genode::Hid_generator::generate({ buf, sizeof(buf) }, "window_list",
		[&] (Genode::Hid_generator &g) {
			g.node("window", [&] {
				g.attribute("id", 3UL);
				g.attribute("label", "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo");
				g.attribute("title", "Hello World");
				g.attribute("width", 320L);
				g.attribute("height", 240L);
				g.attribute("has_alpha", "yes");
			});
		}).with_result(
			[&] (Genode::size_t n) { len = n; },
			[&] (Genode::Buffer_error) { len = 0; });

	Window_list_entry got { };
	unsigned num = 0;
	bool const ok = parse_window_list(node_of(buf, len),
		[&] (Window_list_entry const &e) { got = e; num++; });

	c.check(ok && num == 1,
	        "hid: window_list HID payload accepted (encoding-independent parse)");
	c.check(str_eq(got.label.string(),
	               "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo")
	     && str_eq(got.title.string(), "Hello World")
	     && got.id == 3 && got.width == 320 && got.height == 240
	     && got.has_alpha,
	        "hid: HID window_list entry fields consumed exactly");

	/* window_layout through HID (the layouter's production encoding) */
	Genode::Hid_generator::generate({ buf, sizeof(buf) }, "window_layout",
		[&] (Genode::Hid_generator &g) {
			g.node("boundary", [&] {
				g.attribute("name", "screen");
				g.attribute("xpos", 10);
				g.attribute("ypos", 20);
				g.attribute("width", 1024L);
				g.attribute("height", 768L);
				g.node("window", [&] {
					g.attribute("title", "pkg_runtime -> pkg_gui_demo Hello");
					g.attribute("xpos", 50);
					g.attribute("ypos", 320);
					g.attribute("width", 320L);
					g.attribute("height", 240L);
					g.attribute("focused", "yes");
				});
			});
		}).with_result(
			[&] (Genode::size_t n) { len = n; },
			[&] (Genode::Buffer_error) { len = 0; });

	Layout_window got_w { };
	Boundary got_b { };
	num = 0;
	bool const ok2 = parse_window_layout(node_of(buf, len),
		[&] (Boundary const &b, Layout_window const &w) {
			got_b = b;
			got_w = w;
			num++;
		});

	c.check(ok2 && num == 1,
	        "hid: window_layout HID payload accepted");
	c.check(got_b.xpos == 10 && got_b.ypos == 20
	     && str_eq(got_w.title.string(), "pkg_runtime -> pkg_gui_demo Hello")
	     && got_w.xpos == 50 && got_w.ypos == 320 && got_w.focused,
	        "hid: HID window_layout fields consumed exactly");

	/* malformed HID degrades to an empty node and is rejected */
	c.check(!parse_window_list(node_of("\x01\x02\x03"),
		[&] (Window_list_entry const &) { }),
	        "hid: malformed HID payload rejected");
}


/* ============ 8. v1.1 focus_state / focus (steering) ============ */

void case_v11_focus(Ctx &c)
{
	Focus_target t;
	t.set = true;
	t.wid = 5;

	char buf[512];

	c.check_xml("v1.1 gen: focus_state focused byte shape",
	            "<focus_state>\n\t<window wid=\"5\"/>\n</focus_state>\n",
	            gen_xml(buf, sizeof(buf), "focus_state", [&] (Genode::Xml_generator &g) {
		            gen_focus_state(g, t);
	            }));

	Focus_target const none { };
	c.check_xml("v1.1 gen: empty focus_state (no focus)",
	            "<focus_state/>\n",
	            gen_xml(buf, sizeof(buf), "focus_state", [&] (Genode::Xml_generator &g) {
		            gen_focus_state(g, none);
	            }));

	c.check_xml("v1.1 gen: focus steering byte shape",
	            "<focus>\n\t<window wid=\"5\"/>\n</focus>\n",
	            gen_xml(buf, sizeof(buf), "focus", [&] (Genode::Xml_generator &g) {
		            gen_focus(g, t);
	            }));

	Focus_target out;
	bool ok = parse_focus_state(
		node_of("<focus_state><window wid=\"5\"/></focus_state>"), out);
	c.check(ok && out.set && out.wid == 5,
	        "v1.1 parse: focus_state wid preserved (truth)");

	ok = parse_focus_state(node_of("<focus_state/>"), out);
	c.check(ok && !out.set && out.wid == 0,
	        "v1.1 parse: empty focus_state = no focus");

	ok = parse_focus(node_of("<focus><window wid=\"5\"/></focus>"), out);
	c.check(ok && out.set && out.wid == 5,
	        "v1.1 parse: focus steering wid preserved");

	ok = parse_focus_state(node_of("<focus_state><window/></focus_state>"), out);
	c.check(!ok, "v1.1 parse: focus_state window without wid is malformed");

	ok = parse_focus_state(
		node_of("<focus_state><window label=\"x\"/></focus_state>"), out);
	c.check(!ok, "v1.1 parse: focus_state window without wid (other attrs) malformed");

	out.set = true;
	out.wid = 99;
	ok = parse_focus_state(node_of("<fs/>"), out);
	c.check(!ok && out.wid == 99,
	        "v1.1 parse: focus_state wrong root rejected (out untouched)");

	ok = parse_focus_state(node_of("<focus_state><window wid=\"5\""), out);
	c.check(!ok, "v1.1 parse: focus_state malformed document rejected");

	ok = parse_focus_state(
		node_of("<focus_state><window wid=\"5\"/><window wid=\"6\"/></focus_state>"),
		out);
	c.check(ok && out.set && out.wid == 5,
	        "v1.1 parse: focus_state first window wins (extras ignored)");

	out = Focus_target { };
	ok = parse_focus_state(
		node_of(gen_xml(buf, sizeof(buf), "focus_state", [&] (Genode::Xml_generator &g) {
			gen_focus_state(g, t);
		})), out);
	c.check(ok && out.set && out.wid == 5,
	        "v1.1 round trip: focus_state");
}


/* ============ 9. v1.1 window_request (desired state) ============ */

void case_v11_window_request(Ctx &c)
{
	Window_request geom;
	geom.wid = 5;
	geom.x = 192;
	geom.y = 172;
	geom.w = 640;
	geom.h = 480;

	Window_request close;
	close.wid   = 6;
	close.close = true;

	char buf[512];

	c.check_xml("v1.1 gen: window_request geometry byte shape",
	            "<window_request>\n"
	            "\t<window wid=\"5\" x=\"192\" y=\"172\" w=\"640\" h=\"480\"/>\n"
	            "</window_request>\n",
	            gen_xml(buf, sizeof(buf), "window_request", [&] (Genode::Xml_generator &g) {
		            gen_window_request_entry(g, geom);
	            }));

	c.check_xml("v1.1 gen: window_request sticky-close byte shape",
	            "<window_request>\n\t<window wid=\"6\" action=\"close\"/>\n</window_request>\n",
	            gen_xml(buf, sizeof(buf), "window_request", [&] (Genode::Xml_generator &g) {
		            gen_window_request_entry(g, close);
	            }));

	c.check_xml("v1.1 gen: window_request mixed snapshot",
	            "<window_request>\n"
	            "\t<window wid=\"5\" x=\"192\" y=\"172\" w=\"640\" h=\"480\"/>\n"
	            "\t<window wid=\"6\" action=\"close\"/>\n"
	            "</window_request>\n",
	            gen_xml(buf, sizeof(buf), "window_request", [&] (Genode::Xml_generator &g) {
		            gen_window_request_entry(g, geom);
		            gen_window_request_entry(g, close);
	            }));

	Window_request got[4];
	unsigned num = 0;
	bool ok = parse_window_request(
		node_of("<window_request>"
		        "<window wid=\"5\" x=\"192\" y=\"172\" w=\"640\" h=\"480\"/>"
		        "<window wid=\"6\" action=\"close\"/>"
		        "</window_request>"),
		[&] (Window_request const &r) {
			if (num < 4)
				got[num] = r;
			num++;
		});
	c.check(ok && num == 2, "v1.1 parse: window_request entries in order");
	c.check(!got[0].close && got[0].wid == 5
	     && got[0].x == 192 && got[0].y == 172
	     && got[0].w == 640 && got[0].h == 480,
	        "v1.1 parse: geometry entry (inner screen-absolute)");
	c.check(got[1].close && got[1].wid == 6,
	        "v1.1 parse: sticky-close representation (action=\"close\")");

	num = 0;
	ok = parse_window_request(
		node_of("<window_request>"
		        "<window wid=\"7\" x=\"-32000\" y=\"-32000\" w=\"320\" h=\"240\"/>"
		        "</window_request>"),
		[&] (Window_request const &r) {
			if (num < 4)
				got[num] = r;
			num++;
		});
	c.check(ok && num == 1 && got[0].x == -32000 && got[0].y == -32000
	     && parked(got[0].x, got[0].y),
	        "v1.1 parse: x=y=-32000 geometry parks");

	num = 0;
	ok = parse_window_request(
		node_of("<window_request>"
		        "<window wid=\"8\" action=\"close\" x=\"1\" y=\"2\" w=\"3\" h=\"4\"/>"
		        "</window_request>"),
		[&] (Window_request const &r) {
			if (num < 4)
				got[num] = r;
			num++;
		});
	c.check(ok && got[0].close && got[0].wid == 8 && got[0].x == 0,
	        "v1.1 parse: action=\"close\" wins over geometry attrs");

	num = 0;
	ok = parse_window_request(
		node_of("<window_request><window wid=\"5\" x=\"1\" y=\"2\" w=\"3\"/></window_request>"),
		[&] (Window_request const &) { num++; });
	c.check(!ok, "v1.1 parse: geometry entry missing h is malformed");

	ok = parse_window_request(
		node_of("<window_request><window wid=\"5\" action=\"focus\"/></window_request>"),
		[&] (Window_request const &) { num++; });
	c.check(!ok, "v1.1 parse: unknown action value is malformed");

	ok = parse_window_request(
		node_of("<window_request><window x=\"1\" y=\"2\" w=\"3\" h=\"4\"/></window_request>"),
		[&] (Window_request const &) { num++; });
	c.check(!ok, "v1.1 parse: entry without wid is malformed");

	ok = parse_window_request(
		node_of("<window_request><window wid=\"5\""),
		[&] (Window_request const &) { num++; });
	c.check(!ok, "v1.1 parse: malformed window_request rejected");

	ok = parse_window_request(
		node_of("<requests/>"),
		[&] (Window_request const &) { num++; });
	c.check(!ok, "v1.1 parse: window_request wrong root rejected");

	num = 0;
	ok = parse_window_request(
		node_of(gen_xml(buf, sizeof(buf), "window_request", [&] (Genode::Xml_generator &g) {
			gen_window_request_entry(g, geom);
			gen_window_request_entry(g, close);
		})),
		[&] (Window_request const &r) {
			if (num < 4)
				got[num] = r;
			num++;
		});
	c.check(ok && num == 2 && !got[0].close && got[1].close && got[1].wid == 6,
	        "v1.1 round trip: window_request geometry + sticky close");
}


/* ============ 10. v1.1 window_chrome (core -> decorator) ======== */

void case_v11_window_chrome(Ctx &c)
{
	Window_chrome ch;
	ch.wid        = 5;
	ch.label      = "pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo";
	ch.title      = "Hello World";
	ch.resizeable = true;
	ch.focused    = true;
	ch.fw         = 648;
	ch.fh         = 488;

	char buf[512];

	c.check_xml("v1.1 gen: window_chrome byte shape (resizeable)",
	            "<window_chrome>\n"
	            "\t<window wid=\"5\""
	            " label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
	            " title=\"Hello World\" resizeable=\"yes\" focused=\"yes\" fw=\"648\" fh=\"488\"/>\n"
	            "</window_chrome>\n",
	            gen_xml(buf, sizeof(buf), "window_chrome", [&] (Genode::Xml_generator &g) {
		            gen_window_chrome_entry(g, ch);
	            }));

	Window_chrome fixed = ch;
	fixed.resizeable = false;

	c.check_xml("v1.1 gen: window_chrome omits resizeable when false",
	            "<window_chrome>\n"
	            "\t<window wid=\"5\""
	            " label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
	            " title=\"Hello World\" focused=\"yes\" fw=\"648\" fh=\"488\"/>\n"
	            "</window_chrome>\n",
	            gen_xml(buf, sizeof(buf), "window_chrome", [&] (Genode::Xml_generator &g) {
		            gen_window_chrome_entry(g, fixed);
	            }));

	Window_chrome plain = ch;
	plain.focused = false;

	c.check_xml("v1.1 gen: window_chrome carries focused=\"no\" when unfocused",
	            "<window_chrome>\n"
	            "\t<window wid=\"5\""
	            " label=\"pkg_runtime -> pkg_gui_demo -> Sponge Pkg GUI Demo\""
	            " title=\"Hello World\" resizeable=\"yes\" focused=\"no\" fw=\"648\" fh=\"488\"/>\n"
	            "</window_chrome>\n",
	            gen_xml(buf, sizeof(buf), "window_chrome", [&] (Genode::Xml_generator &g) {
		            gen_window_chrome_entry(g, plain);
	            }));

	Window_chrome got { };
	unsigned num = 0;
	bool ok = parse_window_chrome(
		node_of("<window_chrome>"
		        "<window wid=\"5\" label=\"a b\" title=\"t\""
		        " resizeable=\"yes\" fw=\"648\" fh=\"488\"/>"
		        "</window_chrome>"),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1, "v1.1 parse: window_chrome accepted");
	c.check(got.wid == 5 && got.fw == 648 && got.fh == 488,
	        "v1.1 parse: wid + OUTER frame size consumed");
	c.check(str_eq(got.label.string(), "a b")
	     && str_eq(got.title.string(), "t") && got.resizeable,
	        "v1.1 parse: label/title/resizeable consumed");

	num = 0;
	ok = parse_window_chrome(
		node_of("<window_chrome>"
		        "<window wid=\"5\" label=\"a\" title=\"\" fw=\"640\" fh=\"480\"/>"
		        "</window_chrome>"),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && !got.resizeable && str_eq(got.title.string(), ""),
	        "v1.1 parse: missing resizeable defaults false, empty title ok");

	num = 0;
	ok = parse_window_chrome(
		node_of("<window_chrome><window wid=\"5\" fw=\"640\"/></window_chrome>"),
		[&] (Window_chrome const &) { num++; });
	c.check(!ok, "v1.1 parse: window_chrome missing fh is malformed");

	ok = parse_window_chrome(
		node_of("<window_chrome><window label=\"a\" fw=\"1\" fh=\"1\"/></window_chrome>"),
		[&] (Window_chrome const &) { num++; });
	c.check(!ok, "v1.1 parse: window_chrome missing wid is malformed");

	ok = parse_window_chrome(node_of("<chrome/>"),
		[&] (Window_chrome const &) { num++; });
	c.check(!ok, "v1.1 parse: window_chrome wrong root rejected");

	ok = parse_window_chrome(node_of("<window_chrome><window wid=\"5\""),
		[&] (Window_chrome const &) { num++; });
	c.check(!ok, "v1.1 parse: window_chrome malformed rejected");

	num = 0;
	unsigned long first_wid = 0;
	ok = parse_window_chrome(
		node_of("<window_chrome>"
		        "<window wid=\"5\" label=\"a\" title=\"t\" fw=\"648\" fh=\"488\"/>"
		        "<window wid=\"6\" label=\"b\" title=\"u\" resizeable=\"yes\""
		        " fw=\"320\" fh=\"240\"/>"
		        "</window_chrome>"),
		[&] (Window_chrome const &x) {
			if (num == 0)
				first_wid = x.wid;
			num++;
		});
	c.check(ok && num == 2 && first_wid == 5,
	        "v1.1 parse: window_chrome snapshot order (destroy = absence)");

	num = 0;
	ok = parse_window_chrome(
		node_of(gen_xml(buf, sizeof(buf), "window_chrome", [&] (Genode::Xml_generator &g) {
			gen_window_chrome_entry(g, ch);
		})),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && got.wid == 5 && got.resizeable
	     && got.fw == 648 && got.fh == 488,
	        "v1.1 round trip: window_chrome");

	/* focused: explicit yes/no round trip, absent stays compatible */
	num = 0;
	ok = parse_window_chrome(
		node_of("<window_chrome>"
		        "<window wid=\"5\" label=\"a\" title=\"t\" focused=\"yes\""
		        " fw=\"640\" fh=\"480\"/>"
		        "</window_chrome>"),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && got.focused,
	        "v1.1 parse: focused=\"yes\" read as true");

	num = 0;
	ok = parse_window_chrome(
		node_of("<window_chrome>"
		        "<window wid=\"5\" label=\"a\" title=\"t\" focused=\"no\""
		        " fw=\"640\" fh=\"480\"/>"
		        "</window_chrome>"),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && !got.focused,
	        "v1.1 parse: focused=\"no\" read as false");

	num = 0;
	ok = parse_window_chrome(
		node_of("<window_chrome>"
		        "<window wid=\"5\" label=\"a\" title=\"t\" fw=\"640\" fh=\"480\"/>"
		        "</window_chrome>"),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && !got.focused,
	        "v1.1 parse: absent focused defaults false (backward compatible)");

	num = 0;
	ok = parse_window_chrome(
		node_of(gen_xml(buf, sizeof(buf), "window_chrome", [&] (Genode::Xml_generator &g) {
			gen_window_chrome_entry(g, ch);
		})),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && got.focused,
	        "v1.1 round trip: window_chrome focused=\"yes\"");

	num = 0;
	ok = parse_window_chrome(
		node_of(gen_xml(buf, sizeof(buf), "window_chrome", [&] (Genode::Xml_generator &g) {
			gen_window_chrome_entry(g, plain);
		})),
		[&] (Window_chrome const &x) {
			got = x;
			num++;
		});
	c.check(ok && num == 1 && !got.focused,
	        "v1.1 round trip: window_chrome focused=\"no\"");
}


/* ============ 11. v1.1 decorator_margins ======================== */

void case_v11_margins(Ctx &c)
{
	Margins m;
	m.top    = 28;
	m.bottom = 2;
	m.left   = 2;
	m.right  = 2;

	char buf[256];

	c.check_xml("v1.1 gen: decorator_margins byte shape",
	            "<decorator_margins>\n"
	            "\t<floating top=\"28\" bottom=\"2\" left=\"2\" right=\"2\"/>\n"
	            "</decorator_margins>\n",
	            gen_xml(buf, sizeof(buf), "decorator_margins", [&] (Genode::Xml_generator &g) {
		            gen_decorator_margins(g, m);
	            }));

	Margins out;
	bool ok = parse_decorator_margins(
		node_of("<decorator_margins>"
		        "<floating top=\"28\" bottom=\"2\" left=\"2\" right=\"2\"/>"
		        "</decorator_margins>"), out);
	c.check(ok && out.top == 28 && out.bottom == 2
	     && out.left == 2 && out.right == 2,
	        "v1.1 parse: decorator_margins values consumed");

	ok = parse_decorator_margins(node_of("<decorator_margins/>"), out);
	c.check(!ok, "v1.1 parse: decorator_margins without <floating> malformed");

	ok = parse_decorator_margins(
		node_of("<decorator_margins>"
		        "<floating top=\"28\" bottom=\"2\" left=\"2\"/>"
		        "</decorator_margins>"), out);
	c.check(!ok, "v1.1 parse: decorator_margins missing right malformed");

	ok = parse_decorator_margins(node_of("<margins/>"), out);
	c.check(!ok, "v1.1 parse: decorator_margins wrong root rejected");

	ok = parse_decorator_margins(node_of("<decorator_margins><floating"), out);
	c.check(!ok, "v1.1 parse: decorator_margins malformed rejected");

	out = Margins { };
	ok = parse_decorator_margins(
		node_of(gen_xml(buf, sizeof(buf), "decorator_margins", [&] (Genode::Xml_generator &g) {
			gen_decorator_margins(g, m);
		})), out);
	c.check(ok && out.top == 28 && out.right == 2,
	        "v1.1 round trip: decorator_margins");
}


/* ============ 12. v1.1 chrome geometry + buttons ================= */

void case_chrome_geometry(Ctx &c)
{
	Margins const m { 28, 2, 2, 2 };

	Rect const inner { 192, 172, 640, 480 };
	Rect const outer = outer_from_inner(inner, m);

	c.check(outer.x == 190 && outer.y == 144
	     && outer.w == 644 && outer.h == 510,
	        "v1.1 geometry: outer = inner expanded by margins");

	Rect const back = inner_from_outer(outer, m);
	c.check(back.x == 192 && back.y == 172 && back.w == 640 && back.h == 480,
	        "v1.1 geometry: inner_from_outer(outer_from_inner(x)) == x");

	Margins const zero { 0, 0, 0, 0 };
	Rect const same = outer_from_inner(inner, zero);
	c.check(same.x == 192 && same.y == 172 && same.w == 640 && same.h == 480,
	        "v1.1 geometry: zero margins keep inner == outer");

	Rect const band = title_band(outer, m);
	c.check(band.x == 190 && band.y == 144 && band.w == 644 && band.h == 28,
	        "v1.1 chrome: title band = top margin strip of outer frame");

	Rect const close_r = button_rect(outer, m, Button::CLOSE, true);
	Rect const full_r  = button_rect(outer, m, Button::FULLSCREEN, true);
	Rect const min_r   = button_rect(outer, m, Button::MINIMIZE, true);

	c.check(close_r.w == 24 && close_r.h == 16,
	        "v1.1 chrome: button size 24x16");
	c.check(close_r.x == 806 && close_r.y == 150,
	        "v1.1 chrome: closer right-aligned (pad 4), vertically centered");
	c.check(full_r.x == close_r.x - 28,
	        "v1.1 chrome: fullscreen 4px pad left of closer");
	c.check(min_r.x == full_r.x - 28,
	        "v1.1 chrome: minimizer 4px pad left of fullscreen");
	c.check(close_r.x > full_r.x && full_r.x > min_r.x,
	        "v1.1 chrome: right-to-left order CLOSE, FULLSCREEN, MINIMIZE");

	Rect const absent = button_rect(outer, m, Button::FULLSCREEN, false);
	c.check(absent.w <= 0 && absent.h <= 0,
	        "v1.1 chrome: fullscreen affordance absent when not resizeable");

	Rect const min_fixed = button_rect(outer, m, Button::MINIMIZE, false);
	c.check(min_fixed.x == close_r.x - 28 && min_fixed.x > close_r.x - 56,
	        "v1.1 chrome: minimizer hugs the right group without fullscreen");
}


/* ============ 13. v1.1 chrome hit testing ======================== */

void case_chrome_hit(Ctx &c)
{
	Margins const m { 28, 2, 2, 2 };
	Rect const outer { 0, 0, 648, 510 };

	c.check(chrome_hit(0, 0, outer, m, true) == Hit::SIZER_NW,
	        "v1.1 hit: NW corner");
	c.check(chrome_hit(15, 15, outer, m, true) == Hit::SIZER_NW,
	        "v1.1 hit: NW corner reaches 16x16 (beyond the 8px grips)");
	c.check(chrome_hit(647, 0, outer, m, true) == Hit::SIZER_NE,
	        "v1.1 hit: NE corner");
	c.check(chrome_hit(0, 509, outer, m, true) == Hit::SIZER_SW,
	        "v1.1 hit: SW corner");
	c.check(chrome_hit(647, 509, outer, m, true) == Hit::SIZER_SE,
	        "v1.1 hit: SE corner");

	c.check(chrome_hit(100, 7, outer, m, true) == Hit::SIZER_N,
	        "v1.1 hit: N grip = outermost min(top 28, 8) px");
	c.check(chrome_hit(100, 8, outer, m, true) == Hit::TITLE_MOVE,
	        "v1.1 hit: below the N grip is the title band");
	c.check(chrome_hit(100, 508, outer, m, true) == Hit::SIZER_S,
	        "v1.1 hit: S grip = bottom margin 2 px");
	c.check(chrome_hit(1, 100, outer, m, true) == Hit::SIZER_W,
	        "v1.1 hit: W grip = left margin 2 px");
	c.check(chrome_hit(647, 100, outer, m, true) == Hit::SIZER_E,
	        "v1.1 hit: E grip = right margin 2 px");

	c.check(chrome_hit(630, 14, outer, m, true) == Hit::BTN_CLOSE,
	        "v1.1 hit: closer button");
	c.check(chrome_hit(600, 14, outer, m, true) == Hit::BTN_FULLSCREEN,
	        "v1.1 hit: fullscreen button (resizeable)");
	c.check(chrome_hit(570, 14, outer, m, true) == Hit::BTN_MINIMIZE,
	        "v1.1 hit: minimizer button");

	c.check(chrome_hit(600, 14, outer, m, false) == Hit::BTN_MINIMIZE,
	        "v1.1 hit: minimizer occupies the fullscreen slot when not resizeable");
	c.check(chrome_hit(570, 14, outer, m, false) == Hit::TITLE_MOVE,
	        "v1.1 hit: leftmost slot is title when not resizeable");

	c.check(chrome_hit(2, 2, outer, m, true) == Hit::SIZER_NW,
	        "v1.1 hit precedence: corner beats edge grip");
	c.check(chrome_hit(630, 7, outer, m, true) == Hit::SIZER_N,
	        "v1.1 hit precedence: edge grip beats button");
	c.check(chrome_hit(630, 14, outer, m, true) == Hit::BTN_CLOSE,
	        "v1.1 hit precedence: button beats title/move");

	c.check(chrome_hit(100, 14, outer, m, true) == Hit::TITLE_MOVE,
	        "v1.1 hit: title band is move");
	c.check(chrome_hit(300, 300, outer, m, true) == Hit::NONE,
	        "v1.1 hit: client area is NONE");
	c.check(chrome_hit(-1, 300, outer, m, true) == Hit::NONE,
	        "v1.1 hit: outside the frame is NONE");

	c.check(sizer_zone(100, 7, outer, m) == Sizer_zone::N,
	        "v1.1 sizer: N zone");
	c.check(sizer_zone(647, 509, outer, m) == Sizer_zone::SE,
	        "v1.1 sizer: SE zone");
	c.check(sizer_zone(300, 300, outer, m) == Sizer_zone::NONE,
	        "v1.1 sizer: NONE in client area");
}


/* ============ 14. v1.1 wid view-title conversion ================ */

void case_view_title(Ctx &c)
{
	c.check(str_eq(view_title(5).string(), "5"),
	        "v1.1 wid: view title is the decimal wid");
	c.check(str_eq(view_title(0).string(), "0"),
	        "v1.1 wid: view title of wid 0");

	unsigned long wid = 0;
	c.check(parse_view_title(view_title(5).string(), wid) && wid == 5,
	        "v1.1 wid: round trip 5");
	c.check(parse_view_title(view_title(4294967295UL).string(), wid)
	     && wid == 4294967295UL,
	        "v1.1 wid: round trip unsigned max");
	c.check(parse_view_title("0", wid) && wid == 0,
	        "v1.1 wid: round trip 0");

	wid = 99;
	c.check(!parse_view_title("007", wid) && wid == 99,
	        "v1.1 wid: leading zeros rejected (canonical decimal only)");
	c.check(!parse_view_title("5x", wid), "v1.1 wid: non-digit rejected");
	c.check(!parse_view_title("-1", wid), "v1.1 wid: sign rejected");
	c.check(!parse_view_title("", wid), "v1.1 wid: empty rejected");
	c.check(!parse_view_title("99999999999999999999999", wid),
	        "v1.1 wid: overflow rejected");
}

}  /* anonymous namespace */


unsigned Sponge::Wm_protocol::run_contract_tests(Check_fn report)
{
	Ctx c { };
	c.report = report;

	case_window_list(c);
	case_window_layout(c);
	case_geometry(c);
	case_focus_request(c);
	case_rules(c);
	case_identity(c);
	case_hid_encoding(c);
	case_v11_focus(c);
	case_v11_window_request(c);
	case_v11_window_chrome(c);
	case_v11_margins(c);
	case_chrome_geometry(c);
	case_chrome_hit(c);
	case_view_title(c);

	return c.failures;
}
