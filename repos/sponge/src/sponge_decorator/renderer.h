/* SPDX-License-Identifier: Apache-2.0
 *
 * renderer.h — chrome painting for sponge_decorator.
 *
 * Paints one window's chrome into the three buffer planes of a
 * Gui framebuffer Mode { alpha = true }:
 *
 *   pixels     — title band, frame strips, buttons, glyphs, title text
 *   alpha      — opaque (255) exactly over the painted frame regions,
 *                zero over the client cutout (the client content shows
 *                through the chrome view)
 *   input mask — 1 over the whole outer frame except the client
 *                cutout, plus the four 16x16 corner squares (the eight
 *                frozen resize affordances reach into the client area);
 *                0 over the client cutout so clicks pass to the client
 *
 * Session-free and side-effect-free apart from the surfaces it is
 * given: no Env, no hit-testing, no input, no policy. Button and
 * title placement come exclusively from the shared frozen helpers
 * (via chrome_geometry), so the pixels land exactly where the
 * layouter hit-tests.
 */

#pragma once

#include <os/pixel_alpha8.h>
#include <os/pixel_rgb888.h>
#include <os/surface.h>

#include "chrome_geometry.h"
#include "theme.h"


namespace Sponge::Decorator {


struct Chrome_renderer
{
	static void draw(Genode::Surface<Genode::Pixel_rgb888> &pixels,
	                 Genode::Surface<Genode::Pixel_alpha8> &alpha,
	                 Genode::Surface<Genode::Pixel_input8> &input_mask,
	                 Chrome_layout const &layout, Theme const &theme,
	                 Text_painter::Font const &font,
	                 char const *title, char const *label,
	                 bool focused, bool resizeable);
};


}  /* namespace Sponge::Decorator */
