/* SPDX-License-Identifier: Apache-2.0
 *
 * theme.h — pure theme model and INI parsing for sponge_decorator.
 *
 * Consumes the existing Sponge INI theme format
 * (docs/10-theme-format.md semantics: full-line '#' comments, trimmed
 * '[section]' headers, first-'=' key/value split, case-sensitive
 * identifiers, '#RRGGBB' or nine lower-case named colors, base-10
 * unsigned integers, best-effort application with a warning flag).
 *
 * The decorator consumes a documented SUBSET:
 *
 *   [decor]  (optional; the decorator's own documented key mechanism)
 *     margin_top / margin_bottom / margin_left / margin_right
 *     title_bg / title_bg_unfocused
 *     title_text / title_text_unfocused
 *     frame_border
 *     button_bg / button_bg_unfocused
 *     button_glyph / button_glyph_unfocused
 *     button_close_glyph
 *     grip_bg
 *
 *   [colors] (canonical aliases, applied when no [decor] key exists)
 *     window_bg, panel_bg, window_border, title_text, separator,
 *     error_bg
 *
 * Every field resolves as: [decor] key > [colors] alias > documented
 * fallback constant (the fallback palette equals the shipped
 * default.theme values so chrome stays coherent with the DE when a
 * theme carries no [decor] section). Unknown keys inside [decor] are
 * parse warnings (typo guard); all other canonical sections/keys are
 * valid theme content and are ignored silently.
 *
 * PURITY: no Env, no sessions, no allocation, no exceptions, no
 * mutable global state. Only public <util/color.h> and
 * <sponge/wm_protocol.h> types.
 */

#pragma once

#include <base/stdint.h>
#include <util/color.h>
#include <sponge/wm_protocol.h>


namespace Sponge::Decorator {


/* Resolved chrome colors for one visual state. */
struct Theme_colors
{
	Genode::Color title_bg             { };
	Genode::Color title_bg_unfocused   { };
	Genode::Color title_text           { };
	Genode::Color title_text_unfocused { };
	Genode::Color frame_border         { };
	Genode::Color button_bg            { };
	Genode::Color button_bg_unfocused  { };
	Genode::Color button_glyph         { };
	Genode::Color button_glyph_unfocused { };
	Genode::Color button_close_glyph   { };
	Genode::Color grip_bg              { };   /* a == 0: grips not painted */
};

struct Theme
{
	Wm_protocol::Margins margins { };
	Theme_colors         colors  { };
};

/* Resolved colors for one focus state ('focused' selects the tint). */
struct Chrome_style
{
	Genode::Color title_bg, title_text, frame_border,
	              button_bg, button_glyph, button_close_glyph, grip_bg;
};

Chrome_style chrome_style(Theme const &theme, bool focused);


/*
 * Documented fallback palette and metrics:
 *
 *   margins      top=28 bottom=8 left=8 right=8
 *   title_bg             = #313244 (= [colors] window_bg alias)
 *   title_bg_unfocused   = #1e1e2e (= [colors] panel_bg alias)
 *   title_text           = #cdd6f4 (= [colors] title_text alias)
 *   title_text_unfocused = #7f849c
 *   frame_border         = #45475a (= [colors] window_border alias)
 *   button_bg            = #45475a (= [colors] separator alias)
 *   button_bg_unfocused  = #313244
 *   button_glyph         = #cdd6f4 (= [colors] title_text alias)
 *   button_glyph_unfocused = #7f849c
 *   button_close_glyph   = #f38ba8 (= [colors] error_bg alias)
 *   grip_bg              = transparent (resize affordances not painted)
 */
Theme default_theme();

/*
 * Margin validation for the published decorator_margins geometry:
 * 'top' must fit the frozen 24x16 buttons ([16, 255]), the other
 * margins must be in [0, 255].
 */
bool valid_margins(Wm_protocol::Margins const &margins);


/*
 * Parse theme INI bytes (NUL-terminated or NUL-padded ROM content is
 * handled: a NUL byte ends the text). 'out' is reset to
 * 'default_theme()' first, then every valid key overrides its field
 * ([decor] > [colors] alias > fallback). Returns true when every line
 * parsed cleanly; false when any line produced a warning (invalid
 * value, malformed line, unknown key in [decor]) — such lines are
 * skipped and the previous value is kept, mirroring the canonical
 * sponge-de loader's best-effort semantics.
 */
bool parse_theme(char const *text, Genode::size_t len, Theme &out);


}  /* namespace Sponge::Decorator */
