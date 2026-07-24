/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Theme parser skeleton for Sponge DE.
 *
 * This header defines the data model and loader for the INI-style theme
 * format described in docs/10-theme-format.md. The parser is intentionally
 * self-contained: it depends only on Genode base and does not link Qt,
 * rendering, or widget code. Rendering integration is handled in a later
 * phase and is marked with TODO(theme) comments per AGENTS.md §5.3.
 */

#pragma once

#include <base/component.h>
#include <util/string.h>

namespace Sponge::Sponge_DE::Theme {

/* Color stored as 0xRRGGBB with byte accessors. */
struct Color
{
	Genode::uint32_t raw { 0 };

	constexpr Color(Genode::uint32_t raw = 0) : raw(raw) { }

	Genode::uint8_t r() const { return (raw >> 16) & 0xffu; }
	Genode::uint8_t g() const { return (raw >> 8)  & 0xffu; }
	Genode::uint8_t b() const { return raw         & 0xffu; }

	bool operator == (Color const &other) const { return raw == other.raw; }
	bool operator != (Color const &other) const { return raw != other.raw; }
};

/* Font family and size. */
struct Font
{
	Genode::String<64> family { "DejaVu Sans" };
	unsigned size { 11 };
};

/* Parsed theme values. All fields are public so the loader can write them
 * directly; inline accessors provide read-only access for consumers. */
struct Theme
{
	public:

		enum class PanelPosition { TOP, BOTTOM, LEFT, RIGHT };

		/* Initialize all fields to the system default values (member
		 * initializers). */
		Theme() = default;

		/* colors */
		Color panel_bg()      const { return _panel_bg; }
		Color panel_text()    const { return _panel_text; }
		Color accent()        const { return _accent; }
		Color separator()     const { return _separator; }
		Color window_bg()     const { return _window_bg; }
		Color window_border() const { return _window_border; }
		Color title_text()    const { return _title_text; }
		Color error()         const { return _error; }
		Color success()       const { return _success; }
		Color warning()       const { return _warning; }

		/* fonts */
		Font const &default_font() const { return _default_font; }
		Font const &title_font()     const { return _title_font; }

		/* layout */
		PanelPosition panel_position() const { return _panel_position; }
		unsigned panel_height()        const { return _panel_height; }
		unsigned padding()             const { return _padding; }
		unsigned margin()              const { return _margin; }
		unsigned launcher_width()      const { return _launcher_width; }
		unsigned icon_size()           const { return _icon_size; }

		/* window */
		unsigned border_radius() const { return _border_radius; }
		unsigned border_width()  const { return _border_width; }
		unsigned shadow_radius() const { return _shadow_radius; }

		/* meta */
		unsigned format_version() const { return _format_version; }

		/* TODO(theme): add rendering-application hooks in Phase 5/6 when
		 * Qt widget styling is wired. */

	private:

		friend class ThemeLoader;

		Color _panel_bg      { 0x1e1e2e };
		Color _panel_text    { 0xcdd6f4 };
		Color _accent        { 0x89b4fa };
		Color _separator     { 0x45475a };
		Color _window_bg     { 0x313244 };
		Color _window_border { 0x45475a };
		Color _title_text    { 0xcdd6f4 };
		Color _error         { 0xf38ba8 };
		Color _success       { 0xa6e3a1 };
		Color _warning       { 0xf9e2af };

		Font _default_font { Genode::String<64>("DejaVu Sans"), 11 };
		Font _title_font   { Genode::String<64>("DejaVu Sans"), 12 };

		PanelPosition _panel_position { PanelPosition::TOP };
		unsigned _panel_height        { 28 };
		unsigned _padding             { 8 };
		unsigned _margin              { 4 };
		unsigned _launcher_width        { 48 };
		unsigned _icon_size           { 24 };

		unsigned _border_radius { 6 };
		unsigned _border_width  { 1 };
		unsigned _shadow_radius { 8 };

		unsigned _format_version { 1 };
};

/* Best-effort parser for the Sponge DE theme format. */
class ThemeLoader
{
	public:

		ThemeLoader() = default;

		/* Parse `data` of length `len` into `out`. Returns true if the
		 * file was parsed without any warnings; false if one or more
		 * lines were malformed or unknown. */
		bool load(char const *data, Genode::size_t len, Theme &out);

		/* Convenience overload for Genode::String. */
		template <Genode::size_t N>
		bool load(Genode::String<N> const &content, Theme &out)
		{
			return load(content.string(), content.length(), out);
		}

	private:

		Theme *_theme { nullptr };
		Genode::String<32> _section;
		bool _ok { true };

		bool _parse_line(char const *line, Genode::size_t len, Genode::size_t line_no);
		bool _apply_key(char const *key, Genode::size_t key_len,
		                char const *value, Genode::size_t value_len,
		                Genode::size_t line_no);

		bool _parse_color(char const *value, Genode::size_t len, Color &out);
		bool _parse_uint(char const *value, Genode::size_t len, unsigned &out);
		bool _parse_string(char const *value, Genode::size_t len, Genode::String<64> &out);
		bool _parse_position(char const *value, Genode::size_t len, Theme::PanelPosition &out);
};

} /* namespace Sponge::Sponge_DE::Theme */
