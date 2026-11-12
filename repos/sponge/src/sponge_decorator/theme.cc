/* SPDX-License-Identifier: Apache-2.0
 *
 * theme.cc — pure theme model and INI parsing for sponge_decorator.
 *
 * See theme.h for the contract. INI semantics mirror the canonical
 * sponge-de ThemeLoader (src/sponge-de/theme/theme_loader.cc) line by
 * line: full-line '#' comments only (no inline comment stripping),
 * '[section]' headers trimmed and bounded at 31 chars, first-'=' key/
 * value split with both sides trimmed of ' ', '\t', '\r', exact
 * case-sensitive key matching, '#RRGGBB' (7 chars) or the nine
 * lower-case named colors, base-10 digit-only unsigned values, and
 * best-effort continuation after a warning.
 */

#include "theme.h"

using namespace Sponge::Decorator;


/* ======================= value parsing ========================== */

namespace {

bool char_eq(char a, char b) { return a == b; }

bool str_eq(char const *a, char const *b)
{
	while (*a && char_eq(*a, *b)) { a++; b++; }
	return *a == *b;
}

bool hex_digit(char c, unsigned &out)
{
	if (c >= '0' && c <= '9') { out = (unsigned)(c - '0'); return true; }
	if (c >= 'a' && c <= 'f') { out = (unsigned)(c - 'a') + 10; return true; }
	if (c >= 'A' && c <= 'F') { out = (unsigned)(c - 'A') + 10; return true; }
	return false;
}

/*
 * '#RRGGBB' (exactly 7 chars) or one of the nine lower-case named
 * colors of the canonical format. Returns false on anything else.
 */
bool parse_color(char const *v, Genode::Color &out)
{
	if (v[0] == '#') {
		unsigned digits[6];
		for (unsigned i = 0; i < 6; i++)
			if (!hex_digit(v[1 + i], digits[i]))
				return false;
		if (v[7] != '\0')
			return false;

		out = Genode::Color::clamped_rgb(
			digits[0]*16 + digits[1],
			digits[2]*16 + digits[3],
			digits[4]*16 + digits[5]);
		return true;
	}

	struct Named { char const *name; Genode::Color color; };

	static Named const named[] = {
		{ "black",   Genode::Color::rgb(0x00, 0x00, 0x00) },
		{ "white",   Genode::Color::rgb(0xff, 0xff, 0xff) },
		{ "red",     Genode::Color::rgb(0xff, 0x00, 0x00) },
		{ "green",   Genode::Color::rgb(0x00, 0xff, 0x00) },
		{ "blue",    Genode::Color::rgb(0x00, 0x00, 0xff) },
		{ "yellow",  Genode::Color::rgb(0xff, 0xff, 0x00) },
		{ "cyan",    Genode::Color::rgb(0x00, 0xff, 0xff) },
		{ "magenta", Genode::Color::rgb(0xff, 0x00, 0xff) },
		{ "gray",    Genode::Color::rgb(0x80, 0x80, 0x80) },
	};

	for (Named const &n : named)
		if (str_eq(v, n.name)) {
			out = n.color;
			return true;
		}

	return false;
}

/* base-10 digits only (no sign, no '+', no whitespace) */
bool parse_uint(char const *v, unsigned long &out)
{
	if (v[0] == '\0')
		return false;

	unsigned long result = 0;
	for (char const *p = v; *p; p++) {
		if (*p < '0' || *p > '9')
			return false;
		unsigned const d = (unsigned)(*p - '0');
		if (result > (~0UL - d) / 10)
			return false;
		result = result * 10 + d;
	}

	out = result;
	return true;
}


/* ======================== parse state =========================== */

struct Parse_state
{
	/* [colors] alias slots */
	Genode::Color window_bg { }, panel_bg { }, window_border { },
	              alias_title_text { }, separator { }, error_bg { };
	bool seen_window_bg = false, seen_panel_bg = false,
	     seen_window_border = false, seen_alias_title_text = false,
	     seen_separator = false, seen_error_bg = false;

	/* [decor] slots */
	unsigned long margin_top = 0, margin_bottom = 0,
	              margin_left = 0, margin_right = 0;
	bool seen_margin_top = false, seen_margin_bottom = false,
	     seen_margin_left = false, seen_margin_right = false;

	Genode::Color title_bg { }, title_bg_unfocused { },
	              title_text_decor { }, title_text_unfocused { },
	              frame_border { }, button_bg { }, button_bg_unfocused { },
	              button_glyph { }, button_glyph_unfocused { },
	              button_close_glyph { }, grip_bg { };
	bool seen_title_bg = false, seen_title_bg_unfocused = false,
	     seen_title_text = false, seen_title_text_unfocused = false,
	     seen_frame_border = false, seen_button_bg = false,
	     seen_button_bg_unfocused = false, seen_button_glyph = false,
	     seen_button_glyph_unfocused = false,
	     seen_button_close_glyph = false, seen_grip_bg = false;

	bool warned = false;
};


/* Apply one '[decor]' key; returns false (and flags a warning) on an
 * unknown key or an invalid value. */
bool apply_decor_key(Parse_state &s, char const *key, char const *value)
{
	/* integer margin keys with range validation */
	struct Margin_key {
		char const *key;
		unsigned long *slot;
		bool *seen;
		unsigned long min, max;
	};

	Margin_key const margins[] = {
		{ "margin_top",    &s.margin_top,    &s.seen_margin_top,    16, 255 },
		{ "margin_bottom", &s.margin_bottom, &s.seen_margin_bottom,  0, 255 },
		{ "margin_left",   &s.margin_left,   &s.seen_margin_left,    0, 255 },
		{ "margin_right",  &s.margin_right,  &s.seen_margin_right,   0, 255 },
	};

	for (Margin_key const &m : margins)
		if (str_eq(key, m.key)) {
			unsigned long v = 0;
			if (!parse_uint(value, v) || v < m.min || v > m.max)
				return false;
			*m.slot  = v;
			*m.seen  = true;
			return true;
		}

	/* color keys */
	struct Color_key {
		char const *key;
		Genode::Color *slot;
		bool *seen;
	};

	Color_key const colors[] = {
		{ "title_bg",               &s.title_bg,               &s.seen_title_bg },
		{ "title_bg_unfocused",     &s.title_bg_unfocused,     &s.seen_title_bg_unfocused },
		{ "title_text",             &s.title_text_decor,       &s.seen_title_text },
		{ "title_text_unfocused",   &s.title_text_unfocused,   &s.seen_title_text_unfocused },
		{ "frame_border",           &s.frame_border,           &s.seen_frame_border },
		{ "button_bg",              &s.button_bg,              &s.seen_button_bg },
		{ "button_bg_unfocused",    &s.button_bg_unfocused,    &s.seen_button_bg_unfocused },
		{ "button_glyph",           &s.button_glyph,           &s.seen_button_glyph },
		{ "button_glyph_unfocused", &s.button_glyph_unfocused, &s.seen_button_glyph_unfocused },
		{ "button_close_glyph",     &s.button_close_glyph,     &s.seen_button_close_glyph },
		{ "grip_bg",                &s.grip_bg,                &s.seen_grip_bg },
	};

	for (Color_key const &c : colors)
		if (str_eq(key, c.key)) {
			if (!parse_color(value, *c.slot))
				return false;
			*c.seen = true;
			return true;
		}

	/* unknown key in OUR section: warning (typo guard) */
	return false;
}

/* Apply one '[colors]' key to its alias slot. Canonical keys the
 * decorator does not alias are valid theme content: accepted silently.
 */
void apply_colors_key(Parse_state &s, char const *key, char const *value)
{
	struct Alias_key {
		char const *key;
		Genode::Color *slot;
		bool *seen;
	};

	Alias_key const aliases[] = {
		{ "window_bg",     &s.window_bg,     &s.seen_window_bg },
		{ "panel_bg",      &s.panel_bg,      &s.seen_panel_bg },
		{ "window_border", &s.window_border, &s.seen_window_border },
		{ "title_text",    &s.alias_title_text, &s.seen_alias_title_text },
		{ "separator",     &s.separator,     &s.seen_separator },
		{ "error_bg",      &s.error_bg,      &s.seen_error_bg },
	};

	for (Alias_key const &a : aliases)
		if (str_eq(key, a.key)) {
			/* invalid color: warn, keep previous */
			if (!parse_color(value, *a.slot)) {
				s.warned = true;
				return;
			}
			*a.seen = true;
			return;
		}

	/* any other canonical [colors] key: ignored silently */
}


/* Combine the seen flags into the final theme. */
Theme finalize(Parse_state const &s)
{
	Theme t = default_theme();

	/* margins: [decor] key > fallback (already in 't') */
	if (s.seen_margin_top)    t.margins.top    = (int)s.margin_top;
	if (s.seen_margin_bottom) t.margins.bottom = (int)s.margin_bottom;
	if (s.seen_margin_left)   t.margins.left   = (int)s.margin_left;
	if (s.seen_margin_right)  t.margins.right  = (int)s.margin_right;

	/* colors: [decor] key > [colors] alias > fallback */
	auto resolve = [] (bool seen_decor, Genode::Color decor,
	                   bool seen_alias, Genode::Color alias,
	                   Genode::Color fallback) {
		if (seen_decor) return decor;
		if (seen_alias) return alias;
		return fallback;
	};

	t.colors.title_bg = resolve(s.seen_title_bg, s.title_bg,
	                            s.seen_window_bg, s.window_bg,
	                            Genode::Color::rgb(0x31, 0x32, 0x44));

	t.colors.title_bg_unfocused =
		resolve(s.seen_title_bg_unfocused, s.title_bg_unfocused,
		        s.seen_panel_bg, s.panel_bg,
		        Genode::Color::rgb(0x1e, 0x1e, 0x2e));

	t.colors.title_text = resolve(s.seen_title_text, s.title_text_decor,
	                              s.seen_alias_title_text, s.alias_title_text,
	                              Genode::Color::rgb(0xcd, 0xd6, 0xf4));

	t.colors.title_text_unfocused =
		resolve(s.seen_title_text_unfocused, s.title_text_unfocused,
		        false, Genode::Color { },
		        Genode::Color::rgb(0x7f, 0x84, 0x9c));

	t.colors.frame_border = resolve(s.seen_frame_border, s.frame_border,
	                                s.seen_window_border, s.window_border,
	                                Genode::Color::rgb(0x45, 0x47, 0x5a));

	t.colors.button_bg = resolve(s.seen_button_bg, s.button_bg,
	                             s.seen_separator, s.separator,
	                             Genode::Color::rgb(0x45, 0x47, 0x5a));

	t.colors.button_bg_unfocused =
		resolve(s.seen_button_bg_unfocused, s.button_bg_unfocused,
		        false, Genode::Color { },
		        Genode::Color::rgb(0x31, 0x32, 0x44));

	t.colors.button_glyph = resolve(s.seen_button_glyph, s.button_glyph,
	                                s.seen_alias_title_text, s.alias_title_text,
	                                Genode::Color::rgb(0xcd, 0xd6, 0xf4));

	t.colors.button_glyph_unfocused =
		resolve(s.seen_button_glyph_unfocused, s.button_glyph_unfocused,
		        false, Genode::Color { },
		        Genode::Color::rgb(0x7f, 0x84, 0x9c));

	t.colors.button_close_glyph =
		resolve(s.seen_button_close_glyph, s.button_close_glyph,
		        s.seen_error_bg, s.error_bg,
		        Genode::Color::rgb(0xf3, 0x8b, 0xa8));

	/* grip_bg has no alias; default stays fully transparent */
	if (s.seen_grip_bg)
		t.colors.grip_bg = s.grip_bg;

	return t;
}

}  /* anonymous namespace */


/* ========================== public API ========================== */

Theme Sponge::Decorator::default_theme()
{
	Theme t;
	t.margins = Wm_protocol::Margins { 28, 8, 8, 8 };

	t.colors.title_bg               = Genode::Color::rgb(0x31, 0x32, 0x44);
	t.colors.title_bg_unfocused     = Genode::Color::rgb(0x1e, 0x1e, 0x2e);
	t.colors.title_text             = Genode::Color::rgb(0xcd, 0xd6, 0xf4);
	t.colors.title_text_unfocused   = Genode::Color::rgb(0x7f, 0x84, 0x9c);
	t.colors.frame_border           = Genode::Color::rgb(0x45, 0x47, 0x5a);
	t.colors.button_bg              = Genode::Color::rgb(0x45, 0x47, 0x5a);
	t.colors.button_bg_unfocused    = Genode::Color::rgb(0x31, 0x32, 0x44);
	t.colors.button_glyph           = Genode::Color::rgb(0xcd, 0xd6, 0xf4);
	t.colors.button_glyph_unfocused = Genode::Color::rgb(0x7f, 0x84, 0x9c);
	t.colors.button_close_glyph     = Genode::Color::rgb(0xf3, 0x8b, 0xa8);
	t.colors.grip_bg                = Genode::Color { };  /* a == 0 */

	return t;
}


bool Sponge::Decorator::valid_margins(Wm_protocol::Margins const &m)
{
	if (m.top < 16 || m.top > 255)
		return false;
	if (m.bottom < 0 || m.bottom > 255)
		return false;
	if (m.left < 0 || m.left > 255)
		return false;
	if (m.right < 0 || m.right > 255)
		return false;
	return true;
}


Chrome_style Sponge::Decorator::chrome_style(Theme const &theme, bool focused)
{
	Theme_colors const &c = theme.colors;

	Chrome_style style { };

	if (focused) {
		style.title_bg           = c.title_bg;
		style.title_text         = c.title_text;
		style.frame_border       = c.frame_border;
		style.button_bg          = c.button_bg;
		style.button_glyph       = c.button_glyph;
		style.button_close_glyph = c.button_close_glyph;
	} else {
		/* unfocused: dimmed band/text/buttons; the close glyph dims too */
		style.title_bg           = c.title_bg_unfocused;
		style.title_text         = c.title_text_unfocused;
		style.frame_border       = c.frame_border;
		style.button_bg          = c.button_bg_unfocused;
		style.button_glyph       = c.button_glyph_unfocused;
		style.button_close_glyph = c.button_glyph_unfocused;
	}

	style.grip_bg = c.grip_bg;
	return style;
}


bool Sponge::Decorator::parse_theme(char const *text, Genode::size_t len,
                                    Theme &out)
{
	out = default_theme();

	Parse_state s;
	char section[32] = "";

	char key_buf[64], value_buf[64];

	Genode::size_t pos = 0;

	while (pos < len && text[pos] != '\0') {

		/* line extent (exclusive of the '\n') */
		Genode::size_t const start = pos;
		while (pos < len && text[pos] != '\n' && text[pos] != '\0')
			pos++;
		Genode::size_t end = pos;
		if (pos < len && text[pos] == '\n')
			pos++;

		/* trim ' ', '\t', '\r' */
		Genode::size_t b = start, e = end;
		while (b < e && (text[b] == ' ' || text[b] == '\t' || text[b] == '\r'))
			b++;
		while (e > b && (text[e-1] == ' ' || text[e-1] == '\t' || text[e-1] == '\r'))
			e--;

		if (b == e)
			continue;                       /* blank line */
		if (text[b] == '#')
			continue;                       /* full-line comment */

		/* section header */
		if (text[b] == '[') {
			Genode::size_t close = b + 1;
			while (close < e && text[close] != ']')
				close++;
			if (close >= e) {
				s.warned = true;            /* no closing bracket */
				continue;
			}

			/* only whitespace allowed after ']' */
			bool clean = true;
			for (Genode::size_t i = close + 1; i < e; i++)
				if (text[i] != ' ' && text[i] != '\t' && text[i] != '\r')
					clean = false;

			Genode::size_t const name_len = close - (b + 1);
			if (!clean || name_len == 0 || name_len >= sizeof(section)) {
				s.warned = true;
				continue;
			}

			for (Genode::size_t i = 0; i < name_len; i++)
				section[i] = text[b + 1 + i];
			section[name_len] = '\0';
			continue;
		}

		/* key = value */
		Genode::size_t eq = b;
		while (eq < e && text[eq] != '=')
			eq++;
		if (eq >= e) {
			s.warned = true;                /* no '=' */
			continue;
		}

		/* key: [b, eq) trimmed */
		Genode::size_t kb = b, ke = eq;
		while (ke > kb && (text[ke-1] == ' ' || text[ke-1] == '\t' || text[ke-1] == '\r'))
			ke--;
		if (kb == ke || ke - kb >= sizeof(key_buf)) {
			s.warned = true;
			continue;
		}
		for (Genode::size_t i = 0; i < ke - kb; i++)
			key_buf[i] = text[kb + i];
		key_buf[ke - kb] = '\0';

		/* value: (eq, e) trimmed */
		Genode::size_t vb = eq + 1, ve = e;
		while (vb < ve && (text[vb] == ' ' || text[vb] == '\t' || text[vb] == '\r'))
			vb++;
		while (ve > vb && (text[ve-1] == ' ' || text[ve-1] == '\t' || text[ve-1] == '\r'))
			ve--;
		if (ve - vb >= sizeof(value_buf)) {
			s.warned = true;
			continue;
		}
		for (Genode::size_t i = 0; i < ve - vb; i++)
			value_buf[i] = text[vb + i];
		value_buf[ve - vb] = '\0';

		if (section[0] == '\0') {
			s.warned = true;                /* key outside any section */
			continue;
		}

		if (str_eq(section, "decor")) {
			if (!apply_decor_key(s, key_buf, value_buf))
				s.warned = true;
			continue;
		}

		if (str_eq(section, "colors")) {
			apply_colors_key(s, key_buf, value_buf);
			continue;
		}

		/* other canonical sections ([meta]/[fonts]/[layout]/[window])
		 * and unknown sections: valid theme content, ignored here */
	}

	out = finalize(s);
	return !s.warned;
}
