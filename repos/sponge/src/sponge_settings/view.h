/* SPDX-License-Identifier: Apache-2.0
 *
 * view.h — native listing painting for sponge_settings.
 *
 * Lightweight nitpicker_gfx surface painting (Box_painter +
 * Text_painter + Tff_font), the sponge_decorator precedent. No Qt,
 * no OpenGL, no libc: "no GL-heavy dependency" (docs/05-sponge-de.md
 * §5.4 privileges paragraph).
 *
 * THEME NOTE (deliberate deviation from AGENTS.md §3.4 "separate
 * themes"): the capability contract for this component allows only
 * the installed-metadata ROM, the two settings launch labels, and
 * Gui/Timer (docs/05-sponge-de.md §5.4) — there is no spare ROM
 * session for a theme file. The palette is therefore a single
 * compile-time constant block right here (never scattered through the
 * painters), and every paint entry point takes its colors from it.
 * If the contract ever grants a theme ROM, only this block moves.
 */

#pragma once

#include <nitpicker_gfx/text_painter.h>
#include <os/pixel_rgb888.h>
#include <os/surface.h>

#include "model.h"

namespace Sponge::Settings {

namespace Palette {

inline constexpr Genode::Color WINDOW_BG   = Genode::Color::rgb(0x1e, 0x1e, 0x2e);
inline constexpr Genode::Color HEADER_BG   = Genode::Color::rgb(0x27, 0x27, 0x3a);
inline constexpr Genode::Color HEADER_TEXT = Genode::Color::rgb(0x9c, 0xd1, 0xf2);
inline constexpr Genode::Color ENTRY_TITLE = Genode::Color::rgb(0xe8, 0xe8, 0xf0);
inline constexpr Genode::Color ENTRY_HINT  = Genode::Color::rgb(0x8a, 0x8a, 0x9c);
inline constexpr Genode::Color PENDING_BG  = Genode::Color::rgb(0x2c, 0x3a, 0x4e);
inline constexpr Genode::Color STATUS_BG   = Genode::Color::rgb(0x16, 0x16, 0x22);
inline constexpr Genode::Color STATUS_TEXT = Genode::Color::rgb(0xd8, 0xd8, 0xe2);
inline constexpr Genode::Color ICON_MARK   = Genode::Color::rgb(0x6c, 0x8e, 0xc8);
inline constexpr Genode::Color DEFAULT_MARK = Genode::Color::rgb(0x55, 0x55, 0x68);
inline constexpr Genode::Color EMPTY_TEXT  = Genode::Color::rgb(0x70, 0x70, 0x84);
inline constexpr Genode::Color DIVIDER     = Genode::Color::rgb(0x33, 0x33, 0x46);

} /* namespace Palette */


/*
 * Paint one full frame: the grouped listing (closed-enum category
 * order, title / icon-marker / keywords per entry), and the bounded
 * single-line status surface at the bottom.
 *
 * 'highlight' is the model index of the entry with an open request in
 * flight (-1 for none). 'status_line' is caller-bounded English.
 */
void paint_listing(Genode::Surface<Genode::Pixel_rgb888> &surface,
                   Text_painter::Font const              &font,
                   Model const                           &model,
                   int                                    scroll,
                   char const                            *status_line,
                   int                                    highlight);

} /* namespace Sponge::Settings */
