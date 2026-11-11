/* SPDX-License-Identifier: Apache-2.0
 *
 * chrome_view.h — one decorated window's chrome view.
 *
 * Owns exactly one Gui session and one top-level view per window
 * (protocol v1.1 association: the view title is the decimal 'wid',
 * 'Wm_protocol::view_title'), plus the alpha-capable framebuffer the
 * renderer paints. The core (sponge_wm) associates the view with the
 * client window by its title and positions it; this component never
 * moves views and never reads input (v1.1 publishes no move updates,
 * and the decorator performs no hit-testing or policy).
 */

#pragma once

#include <base/attached_dataspace.h>
#include <gui_session/connection.h>
#include <nitpicker_gfx/tff_font.h>
#include <sponge/wm_protocol.h>

#include "renderer.h"
#include "theme.h"


namespace Sponge::Decorator {


struct Chrome_view
{
	using Window_chrome = Wm_protocol::Window_chrome;

	private:

		Genode::Env &_env;

		Window_chrome _chrome { };
		bool          _focused { true };

		Gui::Connection _gui;

		Gui::View_ref          _view_ref { };
		Gui::View_ids::Element _view_id { _view_ref, _gui.view_ids };

		Genode::Constructible<Genode::Attached_dataspace> _fb { };

		void _allocate_buffer();
		void _create_view();
		void _draw(Theme const &theme, Text_painter::Font const &font);

	public:

		Chrome_view(Genode::Env &env, Window_chrome const &chrome,
		            Theme const &theme, Text_painter::Font const &font);

		~Chrome_view();

		unsigned long wid() const { return _chrome.wid; }

		/*
		 * Apply a window_chrome snapshot entry. Size changes rebuild
		 * the buffer and the view (the core re-associates by title);
		 * title/resizeable changes repaint in place.
		 */
		void update(Window_chrome const &chrome, Theme const &theme,
		            Text_painter::Font const &font);

		void repaint(Theme const &theme, Text_painter::Font const &font);

		/*
		 * Focused/unfocused visual state. v1.1 delivers no focus signal
		 * to the decorator (see README); the state is renderable and
		 * switchable here so a future protocol revision only has to
		 * route it.
		 */
		void focused(bool value, Theme const &theme,
		             Text_painter::Font const &font);
};


}  /* namespace Sponge::Decorator */
