/* SPDX-License-Identifier: Apache-2.0
 *
 * test_cases.cc — deterministic pure test cases for sponge_decorator
 * (see test_cases.h).
 */

#include "test_cases.h"

#include <util/string.h>
#include <util/xml_generator.h>

#include "../chrome_geometry.h"
#include "../theme.h"

using namespace Sponge::Decorator;
namespace Wm_protocol = Sponge::Wm_protocol;
using Sponge::Wm_protocol::Margins;
using Sponge::Wm_protocol::Rect;


namespace {


struct Checker
{
	unsigned checks = 0, failures = 0;
	Check_fn report;

	void check(bool ok, char const *what)
	{
		checks++;
		if (!ok) failures++;
		report(ok, what);
	}

	void check_eq(long got, long expected, char const *what)
	{
		checks++;
		if (got == expected) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<256> const msg(what, ": expected ", expected,
		                              " got ", got);
		report(false, msg.string());
	}

	void check_rect(Rect got, Rect expected, char const *what)
	{
		checks++;
		if (got.x == expected.x && got.y == expected.y
		 && got.w == expected.w && got.h == expected.h) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<256> const msg(
			what, ": expected (", expected.x, ",", expected.y, " ",
			expected.w, "x", expected.h, ") got (", got.x, ",",
			got.y, " ", got.w, "x", got.h, ")");
		report(false, msg.string());
	}

	void check_color(Genode::Color got, Genode::Color expected, char const *what)
	{
		checks++;
		if (got.r == expected.r && got.g == expected.g
		 && got.b == expected.b) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<256> const msg(
			what, ": expected rgb(", (unsigned)expected.r, ",",
			(unsigned)expected.g, ",", (unsigned)expected.b,
			") got rgb(", (unsigned)got.r, ",", (unsigned)got.g,
			",", (unsigned)got.b, ")");
		report(false, msg.string());
	}
};


/* Deterministic font stand-in: 6 px per codepoint, height 10. */
struct Stub_font : Text_painter::Font
{
	using Codepoint = Text_painter::Codepoint;
	using Glyph = Text_painter::Glyph;
	using Fixpoint_number = Text_painter::Fixpoint_number;

	void _apply_glyph(Codepoint, Apply_fn const &fn) const override
	{
		fn.apply(Glyph { 0, 0, 0, Fixpoint_number(0), nullptr });
	}

	Advance_info advance_info(Codepoint) const override
	{
		return Advance_info { 6u, Fixpoint_number(6) };
	}

	unsigned baseline() const override { return 8; }
	unsigned height() const override { return 10; }
	Text_painter::Area bounding_box() const override
	{
		return Text_painter::Area(10, 12);
	}
};


bool parse_str(char const *s, Theme &t)
{
	return parse_theme(s, Genode::strlen(s), t);
}

bool str_contains(char const *haystack, char const *needle)
{
	for (char const *h = haystack; *h; h++) {
		char const *a = h, *b = needle;
		while (*a && *b && *a == *b) { a++; b++; }
		if (*b == '\0')
			return true;
	}
	return false;
}

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


/* ========================= theme parsing ======================== */

void test_theme(Checker &c)
{
	Theme t;

	c.check(parse_theme("", 0, t), "empty theme parses cleanly");
	c.check_eq(t.margins.top, 28, "fallback margin top is 28");
c.check_eq(t.margins.bottom, 8, "fallback margin bottom is 8");
c.check_eq(t.margins.left, 8, "fallback margin left is 8");
c.check_eq(t.margins.right, 8, "fallback margin right is 8");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0x31, 0x32, 0x44),
	              "fallback title_bg is #313244");
	c.check_color(t.colors.button_close_glyph,
	              Genode::Color::rgb(0xf3, 0x8b, 0xa8),
	              "fallback button_close_glyph is #f38ba8");
	c.check(t.colors.grip_bg.transparent(),
	        "fallback grip_bg is not painted");

	char const *decor_margins =
		"[decor]\nmargin_top = 20\nmargin_bottom = 4\n"
		"margin_left = 10\nmargin_right = 6\n";
	c.check(parse_str(decor_margins, t), "[decor] margins parse cleanly");
	c.check_eq(t.margins.top, 20, "[decor] margin_top applied");
	c.check_eq(t.margins.bottom, 4, "[decor] margin_bottom applied");
	c.check_eq(t.margins.left, 10, "[decor] margin_left applied");
	c.check_eq(t.margins.right, 6, "[decor] margin_right applied");

	char const *bad_margins =
		"[decor]\nmargin_top = 15\nmargin_bottom = 300\n"
		"margin_left = -1\nmargin_right = 6\n";
	c.check(!parse_str(bad_margins, t), "invalid margins warn");
	c.check_eq(t.margins.top, 28, "invalid margin_top keeps fallback");
c.check_eq(t.margins.bottom, 8, "invalid margin_bottom keeps fallback");
c.check_eq(t.margins.left, 8, "invalid margin_left keeps fallback");
	c.check_eq(t.margins.right, 6, "valid margin_right still applied");

	char const *colors =
		"[decor]\ntitle_bg = #112233\ntitle_bg_unfocused = gray\n"
		"button_bg = blue\n";
	c.check(parse_str(colors, t), "[decor] colors parse cleanly");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0x11, 0x22, 0x33),
	              "#RRGGBB color applied");
	c.check_color(t.colors.title_bg_unfocused,
	              Genode::Color::rgb(0x80, 0x80, 0x80),
	              "named color 'gray' applied");
	c.check_color(t.colors.button_bg, Genode::Color::rgb(0x00, 0x00, 0xff),
	              "named color 'blue' applied");

	char const *bad_color = "[decor]\ntitle_bg = #1122\n";
	c.check(!parse_str(bad_color, t), "invalid color warns");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0x31, 0x32, 0x44),
	              "invalid color keeps fallback");

	char const *alias = "[colors]\nwindow_bg = #123456\naccent = #89b4fa\n";
	c.check(parse_str(alias, t), "canonical [colors] parse cleanly");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0x12, 0x34, 0x56),
	              "[colors] window_bg aliases title_bg");
	c.check_color(t.colors.title_bg_unfocused,
	              Genode::Color::rgb(0x1e, 0x1e, 0x2e),
	              "panel_bg fallback kept when alias absent");

	char const *override_order =
		"[decor]\ntitle_bg = #aabbcc\n[colors]\nwindow_bg = #123456\n";
	c.check(parse_str(override_order, t),
	        "decor over alias (decor first) parses cleanly");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0xaa, 0xbb, 0xcc),
	              "[decor] wins over later [colors] alias");

	char const *override_order2 =
		"[colors]\nwindow_bg = #123456\n[decor]\ntitle_bg = #aabbcc\n";
	c.check(parse_str(override_order2, t),
	        "decor over alias (colors first) parses cleanly");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0xaa, 0xbb, 0xcc),
	              "[decor] wins over earlier [colors] alias");

	char const *unknown_decor = "[decor]\nmargin_top = 40\ntitle_bgg = #fff\n";
	c.check(!parse_str(unknown_decor, t), "unknown [decor] key warns");
	c.check_eq(t.margins.top, 40, "valid keys applied despite warning");

	char const *canonical =
		"# comment\n[meta]\nversion = 1\n[fonts]\ndefault_size = 11\n"
		"[layout]\npanel_height = 28\n[window]\nborder_width = 1\n";
	c.check(parse_str(canonical, t),
	        "canonical sections ignored silently");
	c.check_eq(t.margins.top, 28, "canonical sections do not touch margins");

	char const *inline_comment = "[decor]\ntitle_bg = #112233 # nope\n";
	c.check(!parse_str(inline_comment, t),
	        "inline comments are not stripped (value kept verbatim)");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0x31, 0x32, 0x44),
	              "inline-comment value rejected, fallback kept");

	char const *first_eq = "[decor]\ntitle_bg = #112233 = x\n";
	c.check(!parse_str(first_eq, t),
	        "first '=' splits; rest stays in the value");
	c.check_color(t.colors.title_bg, Genode::Color::rgb(0x31, 0x32, 0x44),
	              "value with trailing junk rejected");

	char const *trim = "  [decor]  \r\n  margin_top  =  40  \r\n";
	c.check(parse_str(trim, t), "whitespace trimming parses cleanly");
	c.check_eq(t.margins.top, 40, "trimmed value applied");

	char const *cased = "[decor]\nMargin_Top = 40\n";
	c.check(!parse_str(cased, t), "keys are case-sensitive");
	c.check_eq(t.margins.top, 28, "wrong-case key not applied");

	char const *outside = "margin_top = 40\n";
	c.check(!parse_str(outside, t), "key outside a section warns");
	c.check_eq(t.margins.top, 28, "key outside a section not applied");

	char const *bad_header = "[decor\nmargin_top = 40\n";
	c.check(!parse_str(bad_header, t), "unterminated section warns");
	c.check_eq(t.margins.top, 28, "unterminated section not applied");

	char const padded[] = "[decor]\nmargin_top = 40";
	c.check(parse_theme(padded, sizeof(padded), t),
	        "NUL-padded ROM content parses cleanly");
	c.check_eq(t.margins.top, 40, "padded content value applied");

	char const *named_bad = "[decor]\ntitle_bg = crimson\n";
	c.check(!parse_str(named_bad, t), "unknown named color warns");
}


/* ====================== margins validation ====================== */

void test_margins(Checker &c)
{
	c.check(valid_margins(Margins { 28, 2, 2, 2 }), "default margins valid");
	c.check(valid_margins(Margins { 16, 0, 0, 0 }), "boundary margins valid");
	c.check(!valid_margins(Margins { 15, 2, 2, 2 }), "top < 16 invalid");
	c.check(!valid_margins(Margins { 0, 2, 2, 2 }), "top 0 invalid");
	c.check(!valid_margins(Margins { 256, 2, 2, 2 }), "top > 255 invalid");
	c.check(!valid_margins(Margins { 28, -1, 2, 2 }), "negative invalid");
	c.check(!valid_margins(Margins { 28, 2, 2, 300 }), "side > 255 invalid");

	char buf[256];
	char const *doc = gen_xml(buf, sizeof(buf), "decorator_margins",
		[&] (Genode::Xml_generator &g) {
			Wm_protocol::gen_decorator_margins(
				g, default_theme().margins);
		});

	c.check(str_contains(doc, "<floating top=\"28\" bottom=\"8\" left=\"8\" right=\"8\"/>"),
	        "decorator_margins publishes the v1.1 default shape");

	Margins parsed { };
	Genode::Node const root { Genode::Const_byte_range_ptr(doc, Genode::strlen(doc)) };
	c.check(Wm_protocol::parse_decorator_margins(root, parsed),
	        "published shape parses via the frozen parser");
	c.check_eq(parsed.top, 28, "published top round-trips");
c.check_eq(parsed.bottom, 8, "published bottom round-trips");
c.check_eq(parsed.left, 8, "published left round-trips");
c.check_eq(parsed.right, 8, "published right round-trips");
}


/* ======================== view geometry ========================= */

void test_geometry(Checker &c)
{
	Margins const m { 28, 2, 2, 2 };
	Chrome_layout const l = chrome_layout(648, 488, m, true);

	c.check_rect(l.outer, Rect { 0, 0, 648, 488 }, "outer frame at origin");
	c.check_rect(l.inner, Rect { 2, 28, 644, 458 }, "inner cutout");
	c.check_rect(l.title, Rect { 0, 0, 648, 28 }, "title band is the top margin");
	c.check_rect(l.strips[0], Rect { 0, 28, 2, 460 }, "left strip");
	c.check_rect(l.strips[1], Rect { 646, 28, 2, 460 }, "right strip");
	c.check_rect(l.strips[2], Rect { 0, 486, 648, 2 }, "bottom strip");

	c.check_rect(l.buttons[0], Rect { 620, 6, 24, 16 }, "CLOSE at 620,6");
	c.check_rect(l.buttons[1], Rect { 592, 6, 24, 16 }, "FULLSCREEN at 592,6");
	c.check_rect(l.buttons[2], Rect { 564, 6, 24, 16 }, "MINIMIZE at 564,6");

	for (unsigned i = 0; i < 3; i++) {
		Wm_protocol::Button const b = (i == 0) ? Wm_protocol::Button::CLOSE
		           : (i == 1) ? Wm_protocol::Button::FULLSCREEN
		                      : Wm_protocol::Button::MINIMIZE;
		Rect const shared = Wm_protocol::button_rect(l.outer, m, b, true);
		c.check_rect(l.buttons[i], shared, "button matches shared button_rect");
	}

	Chrome_layout const fixed = chrome_layout(648, 488, m, false);
	c.check_rect(fixed.buttons[0], Rect { 620, 6, 24, 16 },
	             "non-resizeable CLOSE unchanged");
	c.check_rect(fixed.buttons[1], Rect { },
	             "non-resizeable FULLSCREEN absent");
	c.check_rect(fixed.buttons[2], Rect { 592, 6, 24, 16 },
	             "non-resizeable MINIMIZE hugs the right edge");
	c.check_rect(fixed.buttons[2],
	             Wm_protocol::button_rect(fixed.outer, m,
	                                      Wm_protocol::Button::MINIMIZE, false),
	             "non-resizeable MINIMIZE matches shared button_rect");

	c.check_rect(l.corners[0], Rect { 0, 0, 16, 16 }, "NW corner 16x16");
	c.check_rect(l.corners[1], Rect { 632, 0, 16, 16 }, "NE corner 16x16");
	c.check_rect(l.corners[2], Rect { 0, 472, 16, 16 }, "SW corner 16x16");
	c.check_rect(l.corners[3], Rect { 632, 472, 16, 16 }, "SE corner 16x16");

	c.check_rect(l.edge_bands[0], Rect { 0, 0, 648, 8 }, "N grip band 8px");
	c.check_rect(l.edge_bands[1], Rect { 0, 486, 648, 2 }, "S grip band is min(bottom,8)");
	c.check_rect(l.edge_bands[2], Rect { 0, 0, 2, 488 }, "W grip band 2px");
	c.check_rect(l.edge_bands[3], Rect { 646, 0, 2, 488 }, "E grip band 2px");

	Chrome_layout const custom = chrome_layout(100, 80, { 20, 4, 10, 6 }, true);
	c.check_rect(custom.inner, Rect { 10, 20, 84, 56 }, "custom inner cutout");
	c.check_rect(custom.buttons[0], Rect { 72, 2, 24, 16 },
	             "custom margins shift the button group");
	c.check_rect(custom.edge_bands[2], Rect { 0, 0, 8, 80 },
	             "grip depth capped at 8 for large margins");

	/* affordance reach: the frozen corner zones override the client cutout */
	Rect const sw = l.corners[2];
	bool const in_inner =
		(4 >= l.inner.x) && (4 < l.inner.x + l.inner.w)
	 && (484 >= l.inner.y) && (484 < l.inner.y + l.inner.h);
	bool const in_corner =
		(4 >= sw.x) && (4 < sw.x + sw.w)
	 && (484 >= sw.y) && (484 < sw.y + sw.h);
	c.check(in_inner && in_corner,
	        "corner affordance overlaps the client cutout");
	c.check(Wm_protocol::sizer_zone(4, 484, l.outer, m)
	        == Wm_protocol::Sizer_zone::SW,
	        "overlapping point classifies as SW per the frozen helper");
}


/* ========================= glyph geometry ======================= */

void test_glyphs(Checker &c)
{
	Glyph_bitmap const close = button_glyph_bitmap(Wm_protocol::Button::CLOSE);
	Glyph_bitmap const mini = button_glyph_bitmap(Wm_protocol::Button::MINIMIZE);
	Glyph_bitmap const full = button_glyph_bitmap(Wm_protocol::Button::FULLSCREEN);

	c.check_eq(close.w, 9, "close glyph is 9 wide");
	c.check_eq(close.h, 9, "close glyph is 9 high");
	c.check_eq(mini.w, 9, "minimize glyph is 9 wide");
	c.check_eq(full.w, 9, "fullscreen glyph is 9 wide");

	bool x_mirror = true, y_mirror = true;
	for (unsigned y = 0; y < close.h; y++)
		for (unsigned x = 0; x < close.w; x++) {
			x_mirror &= glyph_pixel(close, x, y) == glyph_pixel(close, 8 - x, y);
			y_mirror &= glyph_pixel(close, x, y) == glyph_pixel(close, x, 8 - y);
		}
	c.check(x_mirror, "close glyph is x-mirrored");
	c.check(y_mirror, "close glyph is y-mirrored");

	bool bars_only = true;
	for (unsigned y = 0; y < mini.h; y++)
		for (unsigned x = 0; x < mini.w; x++)
			if (glyph_pixel(mini, x, y))
				bars_only &= (y == 6 || y == 7);
	c.check(bars_only, "minimize glyph is the lower bar only");
	c.check(glyph_pixel(mini, 4, 6) && glyph_pixel(mini, 4, 7),
	        "minimize bar is 2px thick at the center");

	c.check(glyph_pixel(full, 0, 0) && glyph_pixel(full, 8, 8),
	        "fullscreen glyph fills its corners");
	c.check(!glyph_pixel(full, 4, 4), "fullscreen glyph has a hollow center");
	c.check(glyph_pixel(full, 4, 0) && glyph_pixel(full, 0, 4),
	        "fullscreen glyph edges are drawn");
	c.check(!glyph_pixel(close, 10, 0) && !glyph_pixel(close, 0, 9),
	        "out-of-range glyph pixels are off");

	Rect const btn { 620, 6, 24, 16 };
	c.check_rect(glyph_rect(btn), Rect { 627, 9, 9, 9 },
	             "glyph centered in the button rect");
	c.check_rect(glyph_rect(Rect { }), Rect { },
	             "absent button yields no glyph rect");
}


/* ========================== title text ========================== */

void test_title(Checker &c)
{
	Stub_font const font;

	c.check_rect(title_region(Rect { 0, 0, 648, 28 }, 556),
	             Rect { 8, 0, 548, 28 }, "title region is left-padded");
	c.check_rect(title_region(Rect { 0, 0, 648, 28 }, 8), Rect { },
	             "zero-width title region when the limit is tight");

	c.check(Genode::strcmp(fit_title(font, "Hello", "app",
	                                 Rect { 8, 0, 100, 28 }).string(),
	                       "Hello") == 0,
	        "title text used when present");
	c.check(Genode::strcmp(fit_title(font, "", "app",
	                                 Rect { 8, 0, 100, 28 }).string(),
	                       "app") == 0,
	        "label fallback when the title is empty");
	c.check(fit_title(font, "", "", Rect { 8, 0, 100, 28 }).string()[0] == '\0',
	        "empty title and label yield empty text");

	c.check(Genode::strcmp(fit_title(font, "ABCDEFGHIJ", "app",
	                                 Rect { 8, 0, 59, 28 }).string(),
	                       "ABCDEF...") == 0,
	        "truncation at the codepoint boundary with ellipsis");
	c.check(Genode::strcmp(fit_title(font, "ABCDEFGHIJ", "app",
	                                 Rect { 8, 0, 60, 28 }).string(),
	                       "ABCDEFGHIJ") == 0,
	        "exact fit is not truncated");
	c.check(Genode::strcmp(fit_title(font, "ABCDEFGHIJ", "app",
	                                 Rect { 8, 0, 18, 28 }).string(),
	                       "...") == 0,
	        "ellipsis alone when only it fits");
	c.check(fit_title(font, "ABCDEFGHIJ", "app",
	                  Rect { 8, 0, 17, 28 }).string()[0] == '\0',
	        "nothing fits below the ellipsis width");
	c.check(fit_title(font, "Hello", "app", Rect { }).string()[0] == '\0',
	        "no text in a zero-size region");

	Text_origin const origin = title_text_origin(Rect { 8, 0, 548, 28 }, font);
	c.check_eq(origin.x, 8, "text origin x is the region x");
	c.check_eq(origin.y, 9, "text origin vertically centers the font");
	Text_origin const tight = title_text_origin(Rect { 8, 0, 548, 6 }, font);
	c.check_eq(tight.y, 0, "text origin clamps when the band is tight");
}


}  /* anonymous namespace */


unsigned Sponge::Decorator::run_test_cases(Check_fn report)
{
	Checker c { 0, 0, report };

	test_theme(c);
	test_margins(c);
	test_geometry(c);
	test_glyphs(c);
	test_title(c);

	return c.failures;
}
