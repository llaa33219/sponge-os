/* SPDX-License-Identifier: Apache-2.0
 *
 * native_wm_fixture_client — fixed-size native Gui fixture for the WM
 * acceptance probes. It deliberately never opens or signals the Gui info
 * ROM, so the native core reports resizeable="no" for this window.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/signal.h>
#include <gui_session/connection.h>
#include <os/pixel_rgb888.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

using Genode::String;


/*
 * Inverse of Sponge::Wm_protocol::escape_attr for the fixture_cmd
 * channel (run/sponge-wm-native.run fixture contract): the probe
 * escapes attribute text, so a quote-bearing test title reaches the
 * Gui title raw — the title-injection case of native_wm_probe. Text
 * outside the five named entities passes through unchanged.
 */
String<64> decode_attr(char const *raw)
{
	char out[64];
	unsigned n = 0;

	for (char const *p = raw; *p != '\0' && n + 1 < sizeof(out); ) {

		char const *suffix = nullptr;
		char        plain  = '\0';

		if      (Genode::strcmp(p, "&amp;",  5) == 0) { plain = '&';  suffix = p + 5; }
		else if (Genode::strcmp(p, "&quot;", 6) == 0) { plain = '"';  suffix = p + 6; }
		else if (Genode::strcmp(p, "&lt;",   4) == 0) { plain = '<';  suffix = p + 4; }
		else if (Genode::strcmp(p, "&gt;",   4) == 0) { plain = '>';  suffix = p + 4; }
		else if (Genode::strcmp(p, "&apos;", 6) == 0) { plain = '\''; suffix = p + 6; }

		if (suffix) {
			out[n++] = plain;
			p = suffix;
		} else {
			out[n++] = *p++;
		}
	}

	out[n] = '\0';
	return String<64>(Genode::Cstring(out, n));
}


struct Fixture
{
	enum : int { X = 850, Y = 560, W = 160, H = 160 };

	Genode::Env &_env;

	Genode::Attached_rom_dataspace _cmd { _env, "fixture_cmd" };
	Genode::Signal_handler<Fixture> _cmd_handler {
		_env.ep(), *this, &Fixture::_handle_cmd };

	Gui::Connection _gui { _env, "native_wm_fixture_client" };
	Genode::Constructible<Genode::Attached_dataspace> _fb_ds { };

	Gui::View_id const _view_id { 1 };
	String<64> _title { "WM Fixture" };
	bool _view_live { false };

	Fixture(Genode::Env &env) : _env(env)
	{
		_gui.buffer(Framebuffer::Mode { .area = Gui::Area(W, H),
		                                .alpha = false });
		_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());
		_create_view();
		_cmd.sigh(_cmd_handler);
		_handle_cmd();
	}

	void _paint()
	{
		Genode::Pixel_rgb888 *pixels =
			_fb_ds->local_addr<Genode::Pixel_rgb888>();
		Genode::Pixel_rgb888 const color { 0x33, 0x66, 0x99 };
		for (unsigned i = 0; i < W * H; ++i)
			pixels[i] = color;
		_gui.framebuffer.refresh(Gui::Rect(Gui::Point(0, 0),
		                                   Gui::Area(W, H)));
	}

	void _create_view()
	{
		if (_view_live) {
			Genode::warning("fixture: create_view ignored; view already exists");
			return;
		}
		Gui::Session::View_attr const attr {
			.title = _title,
			.rect  = Gui::Rect(Gui::Point(X, Y), Gui::Area(W, H)),
			.front = true
		};
		_gui.view(_view_id, attr);
		_gui.execute();
		_view_live = true;
		_paint();
		Genode::log("fixture: created view 1 title='", _title, "'");
	}

	void _destroy_view()
	{
		if (!_view_live) return;
		_gui.destroy_view(_view_id);
		_gui.execute();
		_view_live = false;
		Genode::log("fixture: destroyed view 1");
	}

	void _handle_cmd()
	{
		_cmd.update();
		if (!_cmd.valid()) return;

		_cmd.node().for_each_sub_node([&](Genode::Node const &node) {
			if (node.has_type("title")) {
				_title = decode_attr(
					node.attribute_value("text", String<512>("WM Fixture"))
						.string());
				if (_view_live) {
					_gui.enqueue<Gui::Session::Command::Title>(_view_id, _title);
					_gui.execute();
				}
				Genode::log("fixture: title view 1 '", _title, "'");
			} else if (node.has_type("destroy_view")) {
				_destroy_view();
			} else if (node.has_type("create_view")) {
				_title = decode_attr(
					node.attribute_value("title", String<512>(_title.string()))
						.string());
				_create_view();
			}
		});
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Fixture fixture { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
