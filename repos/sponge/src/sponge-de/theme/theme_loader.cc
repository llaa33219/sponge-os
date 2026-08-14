/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of the Sponge DE theme parser.
 *
 * This is a best-effort INI-style parser. It depends only on Genode base
 * headers and avoids std::string, exceptions, and heap allocation. It is
 * intentionally a skeleton: values are parsed into a Theme struct but are
 * not yet applied to any Qt widget. Rendering integration is Phase 5/6 work.
 */

#include "theme_loader.h"

#include <base/log.h>

using namespace Sponge::Sponge_DE::Theme;


namespace {

/* True if `c` is ASCII horizontal whitespace. */
constexpr bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}


/* Copy `src` of length `len` into `dst` of capacity `dst_cap`, ensuring a
 * null terminator. Returns the number of characters copied (excluding the
 * terminator). */
Genode::size_t copy_with_nul(char *dst, Genode::size_t dst_cap,
                             char const *src, Genode::size_t len)
{
	Genode::size_t const n = len < dst_cap - 1 ? len : dst_cap - 1;
	for (Genode::size_t i = 0; i < n; i++) dst[i] = src[i];
	dst[n] = '\0';
	return n;
}


/* Trim leading and trailing space from `[start, end)`. Returns the
 * new length; `*out_start` is updated to the first non-space character. */
Genode::size_t trim(char const *line, Genode::size_t len,
                    Genode::size_t &out_start)
{
	Genode::size_t start = 0;
	while (start < len && is_space(line[start])) start++;

	Genode::size_t end = len;
	while (end > start && is_space(line[end - 1])) end--;

	out_start = start;
	return end - start;
}

} /* namespace */


bool ThemeLoader::load(char const *data, Genode::size_t len, Theme &out)
{
	_theme = &out;
	out = Theme();
	_ok = true;
	_section = Genode::String<32>();

	/*
	 * ROM dataspaces are page-aligned, so a theme file loaded from a ROM
	 * module arrives padded with NUL bytes. Trailing NULs are never valid
	 * theme content; stop at the first padding byte.
	 */
	while (len > 0 && data[len - 1] == '\0') len--;

	Genode::size_t line_no = 1;
	Genode::size_t pos = 0;
	while (pos < len) {
		Genode::size_t line_start = pos;
		while (pos < len && data[pos] != '\n') pos++;
		Genode::size_t line_len = pos - line_start;
		if (pos < len && data[pos] == '\n') pos++;

		_parse_line(data + line_start, line_len, line_no);
		line_no++;
	}

	return _ok;
}


bool ThemeLoader::_parse_line(char const *line, Genode::size_t len,
                              Genode::size_t line_no)
{
	Genode::size_t start = 0;
	Genode::size_t const trimmed_len = trim(line, len, start);

	if (trimmed_len == 0) return true;
	if (line[start] == '#') return true;

	if (line[start] == '[' && line[start + trimmed_len - 1] == ']') {
		Genode::size_t sec_offset = 0;
		Genode::size_t const sec_len = trim(line + start + 1, trimmed_len - 2, sec_offset);
		Genode::size_t const sec_start = start + 1 + sec_offset;

		if (sec_len == 0 || sec_len >= 32) {
			Genode::warning("theme: parse error at line ", line_no,
			                ": invalid section header");
			_ok = false;
			return false;
		}

		char buf[33];
		copy_with_nul(buf, sizeof(buf), line + sec_start, sec_len);
		_section = Genode::String<32>(buf);
		return true;
	}

	Genode::size_t eq = start;
	while (eq < start + trimmed_len && line[eq] != '=') eq++;
	if (eq >= start + trimmed_len) {
		Genode::warning("theme: parse error at line ", line_no,
		                ": expected '='");
		_ok = false;
		return false;
	}

	Genode::size_t key_offset = 0;
	Genode::size_t const key_len = trim(line + start, eq - start, key_offset);
	Genode::size_t const key_start = start + key_offset;

	Genode::size_t val_offset = 0;
	Genode::size_t const val_len = trim(line + eq + 1,
	                                    (start + trimmed_len) - (eq + 1), val_offset);
	Genode::size_t const val_start = eq + 1 + val_offset;

	if (key_len == 0) {
		Genode::warning("theme: parse error at line ", line_no,
		                ": empty key");
		_ok = false;
		return false;
	}

	return _apply_key(line + key_start, key_len,
	                  line + val_start, val_len, line_no);
}


bool ThemeLoader::_apply_key(char const *key, Genode::size_t key_len,
                             char const *value, Genode::size_t value_len,
                             Genode::size_t line_no)
{
	char key_buf[64];
	copy_with_nul(key_buf, sizeof(key_buf), key, key_len);
	char const *const section = _section.string();

	if (Genode::strcmp(section, "meta") == 0) {
		if (Genode::strcmp(key_buf, "version") == 0) {
			unsigned v = 0;
			if (!_parse_uint(value, value_len, v)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": version must be an integer");
				_ok = false;
				return false;
			}
			if (v != 1) {
				Genode::warning("theme: unsupported format version ", v,
				                " at line ", line_no, "; ignoring theme");
				_ok = false;
				return false;
			}
			_theme->_format_version = v;
			return true;
		}
	}
	else if (Genode::strcmp(section, "colors") == 0) {
		Color *target = nullptr;
		if      (Genode::strcmp(key_buf, "panel_bg")      == 0) target = &_theme->_panel_bg;
		else if (Genode::strcmp(key_buf, "panel_text")    == 0) target = &_theme->_panel_text;
		else if (Genode::strcmp(key_buf, "accent")        == 0) target = &_theme->_accent;
		else if (Genode::strcmp(key_buf, "separator")     == 0) target = &_theme->_separator;
		else if (Genode::strcmp(key_buf, "window_bg")     == 0) target = &_theme->_window_bg;
		else if (Genode::strcmp(key_buf, "window_border") == 0) target = &_theme->_window_border;
		else if (Genode::strcmp(key_buf, "title_text")    == 0) target = &_theme->_title_text;
		else if (Genode::strcmp(key_buf, "error_bg")      == 0) target = &_theme->_error_bg;
		else if (Genode::strcmp(key_buf, "error_text")    == 0) target = &_theme->_error_text;
		else if (Genode::strcmp(key_buf, "success_bg")    == 0) target = &_theme->_success_bg;
		else if (Genode::strcmp(key_buf, "success_text")  == 0) target = &_theme->_success_text;
		else if (Genode::strcmp(key_buf, "warning_bg")    == 0) target = &_theme->_warning_bg;
		else if (Genode::strcmp(key_buf, "warning_text")  == 0) target = &_theme->_warning_text;

		if (target) {
			if (!_parse_color(value, value_len, *target)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid color");
				_ok = false;
				return false;
			}
			return true;
		}
	}
	else if (Genode::strcmp(section, "fonts") == 0) {
		if (Genode::strcmp(key_buf, "default_family") == 0) {
			return _parse_string(value, value_len, _theme->_default_font.family);
		}
		if (Genode::strcmp(key_buf, "default_size") == 0) {
			if (!_parse_uint(value, value_len, _theme->_default_font.size)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid font size");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "title_size") == 0) {
			if (!_parse_uint(value, value_len, _theme->_title_font.size)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid font size");
				_ok = false;
				return false;
			}
			return true;
		}
	}
	else if (Genode::strcmp(section, "layout") == 0) {
		if (Genode::strcmp(key_buf, "panel_position") == 0) {
			if (!_parse_position(value, value_len, _theme->_panel_position)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid panel_position");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "panel_height") == 0) {
			if (!_parse_uint(value, value_len, _theme->_panel_height)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid panel_height");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "padding") == 0) {
			if (!_parse_uint(value, value_len, _theme->_padding)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid padding");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "margin") == 0) {
			if (!_parse_uint(value, value_len, _theme->_margin)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid margin");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "launcher_width") == 0) {
			if (!_parse_uint(value, value_len, _theme->_launcher_width)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid launcher_width");
				_ok = false;
				return false;
			}
			return true;
		}
		/*
		 * Phase 14 W11 #18: parsed-but-unused keys
		 * (`icon_size`, `panel.popup_width`, `panel.popup_entry_min_height`)
		 * have been removed. The fallback "unknown key" warning at the
		 * bottom of this function is the documented behavior for any
		 * theme file that still carries them — surfaces the cleanup
		 * as a one-shot reminder rather than a silent drop.
		 */
	}
	else if (Genode::strcmp(section, "window") == 0) {
		if (Genode::strcmp(key_buf, "border_radius") == 0) {
			if (!_parse_uint(value, value_len, _theme->_border_radius)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid border_radius");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "border_width") == 0) {
			if (!_parse_uint(value, value_len, _theme->_border_width)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid border_width");
				_ok = false;
				return false;
			}
			return true;
		}
		if (Genode::strcmp(key_buf, "shadow_radius") == 0) {
			if (!_parse_uint(value, value_len, _theme->_shadow_radius)) {
				Genode::warning("theme: parse error at line ", line_no,
				                ": invalid shadow_radius");
				_ok = false;
				return false;
			}
			return true;
		}
	}

	Genode::warning("theme: unknown key '", (char const *)key_buf, "' at line ", line_no);
	return true;
}


bool ThemeLoader::_parse_color(char const *value, Genode::size_t len, Color &out)
{
	if (len > 0 && value[0] == '#') {
		if (len != 7) return false;
		Genode::uint32_t raw = 0;
		for (Genode::size_t i = 1; i < len; i++) {
			char const c = value[i];
			unsigned digit = 0;
			if      (c >= '0' && c <= '9') digit = c - '0';
			else if (c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
			else if (c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
			else return false;
			raw = (raw << 4) | digit;
		}
		out = Color(raw);
		return true;
	}

	char buf[32];
	copy_with_nul(buf, sizeof(buf), value, len);

	if      (Genode::strcmp(buf, "black")   == 0) { out = Color{(Genode::uint32_t)0x000000}; return true; }
	else if (Genode::strcmp(buf, "white")   == 0) { out = Color{(Genode::uint32_t)0xffffff}; return true; }
	else if (Genode::strcmp(buf, "red")     == 0) { out = Color{(Genode::uint32_t)0xff0000}; return true; }
	else if (Genode::strcmp(buf, "green")   == 0) { out = Color{(Genode::uint32_t)0x00ff00}; return true; }
	else if (Genode::strcmp(buf, "blue")    == 0) { out = Color{(Genode::uint32_t)0x0000ff}; return true; }
	else if (Genode::strcmp(buf, "yellow")  == 0) { out = Color{(Genode::uint32_t)0xffff00}; return true; }
	else if (Genode::strcmp(buf, "cyan")    == 0) { out = Color{(Genode::uint32_t)0x00ffff}; return true; }
	else if (Genode::strcmp(buf, "magenta") == 0) { out = Color{(Genode::uint32_t)0xff00ff}; return true; }
	else if (Genode::strcmp(buf, "gray")    == 0) { out = Color{(Genode::uint32_t)0x808080}; return true; }

	return false;
}


bool ThemeLoader::_parse_uint(char const *value, Genode::size_t len, unsigned &out)
{
	if (len == 0) return false;
	unsigned result = 0;
	for (Genode::size_t i = 0; i < len; i++) {
		char const c = value[i];
		if (c < '0' || c > '9') return false;
		result = result * 10 + static_cast<unsigned>(c - '0');
	}
	out = result;
	return true;
}


bool ThemeLoader::_parse_string(char const *value, Genode::size_t len,
                                Genode::String<64> &out)
{
	Genode::size_t start = 0;
	Genode::size_t const trimmed_len = trim(value, len, start);
	Genode::size_t end = start + trimmed_len;

	/* Strip a single pair of surrounding double quotes. */
	if (trimmed_len >= 2 && value[start] == '"' && value[end - 1] == '"') {
		start++;
		end--;
	}

	Genode::size_t const final_len = end - start;
	char buf[65];
	copy_with_nul(buf, sizeof(buf), value + start, final_len);
	out = Genode::String<64>(buf);
	return true;
}


bool ThemeLoader::_parse_position(char const *value, Genode::size_t len,
                                  Theme::PanelPosition &out)
{
	char buf[16];
	copy_with_nul(buf, sizeof(buf), value, len);

	if      (Genode::strcmp(buf, "top")    == 0) out = Theme::PanelPosition::TOP;
	else if (Genode::strcmp(buf, "bottom") == 0) out = Theme::PanelPosition::BOTTOM;
	else if (Genode::strcmp(buf, "left")   == 0) out = Theme::PanelPosition::LEFT;
	else if (Genode::strcmp(buf, "right")  == 0) out = Theme::PanelPosition::RIGHT;
	else return false;

	return true;
}
