/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Theme parser for sponge_files.
 *
 * Reuses the Sponge DE theme format (docs/10-theme-format.md) so that
 * sponge_files adopts the same system theme as the desktop. The parser
 * is intentionally Qt-free (it depends only on Genode base) so the
 * theme model can be exercised independently of the widget toolkit.
 *
 * The implementation mirrors repos/sponge/src/sponge-de/theme/theme_loader.{h,cc}
 * but lives inside its own namespace (Sponge::Sponge_Files::Theme) so
 * the component stays self-contained (AGENTS.md §3.4 component
 * isolation; no Sponge_DE header dependency).
 */

#pragma once

#include <base/component.h>
#include <util/string.h>

namespace Sponge::Sponge_Files::Theme {

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

/* Parsed theme values. Fields mirror the Sponge DE model so the same
 * theme files apply. */
struct Theme
{
	public:

		Theme() = default;

		Color window_bg()     const { return _window_bg; }
		Color window_border() const { return _window_border; }
		Color title_text()    const { return _title_text; }
		Color accent()        const { return _accent; }
		Color list_alt()      const { return _list_alt; }
		Color error()         const { return _error; }
		Color success()       const { return _success; }
		Color warning()       const { return _warning; }

		Font const &default_font() const { return _default_font; }

		unsigned border_radius() const { return _border_radius; }
		unsigned border_width()  const { return _border_width; }
		unsigned padding()       const { return _padding; }

		unsigned format_version() const { return _format_version; }

	private:

		friend class ThemeLoader;

		Color _window_bg     { 0x313244 };
		Color _window_border { 0x45475a };
		Color _title_text    { 0xcdd6f4 };
		Color _accent        { 0x89b4fa };
		Color _list_alt      { 0x3b3d50 };
		Color _error         { 0xf38ba8 };
		Color _success       { 0xa6e3a1 };
		Color _warning       { 0xf9e2af };

		Font _default_font { Genode::String<64>("DejaVu Sans"), 11 };

		unsigned _border_radius { 6 };
		unsigned _border_width  { 1 };
		unsigned _padding       { 8 };

		unsigned _format_version { 1 };
};

/* Best-effort parser for the Sponge DE theme format. */
class ThemeLoader
{
	public:

		ThemeLoader() = default;

		bool load(char const *data, Genode::size_t len, Theme &out);

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
};

} /* namespace Sponge::Sponge_Files::Theme */
