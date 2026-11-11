/* SPDX-License-Identifier: Apache-2.0
 *
 * chrome_view.cc — one decorated window's chrome view (see
 * chrome_view.h).
 */

#include "chrome_view.h"

using namespace Sponge::Decorator;

using Window_chrome = Sponge::Wm_protocol::Window_chrome;


namespace {

Gui::Area view_area(Window_chrome const &chrome)
{
	return Gui::Area { (Genode::uint32_t)chrome.fw,
	                   (Genode::uint32_t)chrome.fh };
}

Gui::Rect view_rect(Window_chrome const &chrome)
{
	return Gui::Rect { Gui::Point { 0, 0 }, view_area(chrome) };
}

}  /* anonymous namespace */


Chrome_view::Chrome_view(Genode::Env &env, Window_chrome const &chrome,
                         Theme const &theme, Text_painter::Font const &font)
:
	_env(env),
	_chrome(chrome),
	_gui(env, Wm_protocol::view_title(chrome.wid).string())
{
	_allocate_buffer();
	_create_view();
	_draw(theme, font);
	_gui.framebuffer.refresh(view_rect(_chrome));
}


Chrome_view::~Chrome_view()
{
	_gui.destroy_view(_view_id.id());
	_gui.release_view_id(_view_id.id());
	_fb.destruct();
}


void Chrome_view::_allocate_buffer()
{
	_gui.buffer(Framebuffer::Mode { view_area(_chrome), true });
	_fb.construct(_env.rm(), _gui.framebuffer.dataspace());
}


void Chrome_view::_create_view()
{
	Gui::Title const title { Wm_protocol::view_title(_chrome.wid).string() };

	_gui.view(_view_id.id(),
	          Gui::Session::View_attr { title, view_rect(_chrome), true });
}


void Chrome_view::_draw(Theme const &theme, Text_painter::Font const &font)
{
	Framebuffer::Mode const mode { view_area(_chrome), true };

	Chrome_layout const layout =
		chrome_layout(_chrome.fw, _chrome.fh, theme.margins, _chrome.resizeable);

	mode.with_pixel_surface(*_fb, [&] (Genode::Surface<Genode::Pixel_rgb888> &pixels) {
		mode.with_alpha_surface(*_fb, [&] (Genode::Surface<Genode::Pixel_alpha8> &alpha) {
			mode.with_input_surface(*_fb, [&] (Genode::Surface<Genode::Pixel_input8> &mask) {

				Chrome_renderer::draw(
					pixels, alpha, mask, layout, theme, font,
					_chrome.title.string(), _chrome.label.string(),
					_focused, _chrome.resizeable);
			});
		});
	});
}


void Chrome_view::update(Window_chrome const &chrome, Theme const &theme,
                         Text_painter::Font const &font)
{
	bool const resized =
		(chrome.fw != _chrome.fw) || (chrome.fh != _chrome.fh);

	_chrome = chrome;

	if (resized) {
		_gui.destroy_view(_view_id.id());
		_fb.destruct();
		_allocate_buffer();
		_create_view();
	}

	_draw(theme, font);
	_gui.framebuffer.refresh(view_rect(_chrome));
}


void Chrome_view::repaint(Theme const &theme, Text_painter::Font const &font)
{
	_draw(theme, font);
	_gui.framebuffer.refresh(view_rect(_chrome));
}


void Chrome_view::focused(bool value, Theme const &theme,
                          Text_painter::Font const &font)
{
	if (_focused == value)
		return;

	_focused = value;
	repaint(theme, font);
}
