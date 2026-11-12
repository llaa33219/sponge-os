/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_decorator — lightweight native theme-driven window chrome.
 *
 * Protocol v1.1 (repos/sponge/include/sponge/wm_protocol.h, internal
 * section; docs/plans/phase16-sponge-wm.md §11):
 *
 *   consumes   window_chrome (core -> decorator, complete snapshot;
 *              create/destroy/title/resizeable/size/focus only,
 *              no moves)
 *   consumes   the run-provided theme ROM ('default.theme' raw INI,
 *              or the 'theme' report ROM for <theme source="themed"/>)
 *   publishes  decorator_margins at startup and on every theme change
 *
 * For each snapshot entry it creates one Gui session and one view
 * titled with the decimal 'wid' (the core associates chrome views
 * with client windows by that title and positions them), paints the
 * title band / native close-minimize-fullscreen buttons / title text
 * at the frozen geometry, and exposes the eight resize zones as
 * input-mask affordances. The decorator performs NO hit-testing, NO
 * input reading, and NO policy: the layouter owns all of that.
 *
 * Component entry; pure logic lives in theme.{h,cc} and
 * chrome_geometry.{h,cc}, painting in renderer.{h,cc}, per-window
 * session plumbing in chrome_view.{h,cc}.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <util/xml_generator.h>

#include "chrome_view.h"
#include "theme.h"

extern "C" char _binary_font_tff_start[];


namespace Sponge::Decorator {


struct Main
{
	private:

		enum { MAX_VIEWS = 32 };

		Genode::Env &_env;

		Genode::Attached_rom_dataspace _config_rom { _env, "config" };

		bool _live_theme { false };

		Genode::Constructible<Genode::Attached_rom_dataspace> _theme_rom { };
		Genode::Attached_rom_dataspace _chrome_rom { _env, "window_chrome" };

		/*
		 * Glyph scratch for Tff_font. 'data/font.tff' needs
		 * glyph_buffer_size == 1020 bytes; 2048 leaves headroom (see
		 * README "swapping the font asset").
		 */
		Tff_font::Static_glyph_buffer<2048> _glyphs { };

		Tff_font _font { _binary_font_tff_start, _glyphs };

		Theme _theme { default_theme() };

		Genode::Expanding_reporter _margins_reporter {
			_env, "decorator_margins", "decorator_margins" };

		Genode::Constructible<Chrome_view> _views[MAX_VIEWS];

		Genode::Signal_handler<Main> _theme_handler {
			_env.ep(), *this, &Main::_handle_theme };

		Genode::Signal_handler<Main> _chrome_handler {
			_env.ep(), *this, &Main::_handle_chrome };

		void _read_config()
		{
			_config_rom.update();

			_live_theme = false;
			Genode::Xml_node const config = _config_rom.xml();

			config.for_each_sub_node("theme", [&] (Genode::Xml_node const &t) {
				Genode::String<16> const source =
					t.attribute_value("source", Genode::String<16>());
				_live_theme = (Genode::strcmp(source.string(), "themed") == 0);
			});
		}

		void _publish_margins()
		{
			Wm_protocol::Margins const m = _theme.margins;

			_margins_reporter.generate_xml([&] (Genode::Xml_generator &g) {
				Wm_protocol::gen_decorator_margins(g, m);
			});
		}

		void _load_theme()
		{
			if (!_theme_rom.constructed())
				return;

			_theme_rom->update();

			Theme parsed;
			bool clean = false;

			if (_live_theme) {
				/*
				 * Live pipeline: sponge_themed's report wraps the raw
				 * INI bytes as '<theme name="...">...</theme>' text
				 * content (same extraction as the sponge-de
				 * ThemeController).
				 */
				Genode::Xml_node const xml = _theme_rom->xml();
				if (xml.has_type("theme")) {
					Genode::String<8192> const content =
						xml.decoded_content<Genode::String<8192>>();
					clean = parse_theme(content.string(), content.length(),
					                    parsed);
				} else {
					Genode::warning("sponge_decorator: theme ROM is not a "
					                "<theme> document; keeping defaults");
				}
			} else {
				clean = parse_theme(_theme_rom->local_addr<char>(),
				                    _theme_rom->size(), parsed);
			}

			if (!clean)
				Genode::warning("sponge_decorator: theme parsed with "
				                "warnings (invalid keys ignored)");

			_theme = parsed;
			_publish_margins();

			for (auto &slot : _views)
				if (slot.constructed())
					slot->repaint(_theme, _font);
		}

		void _sync_chrome()
		{
			_chrome_rom.update();

			Wm_protocol::Window_chrome snap[MAX_VIEWS];
			unsigned num = 0;
			bool overflow = false;

			if (_chrome_rom.size() > 0) {
				bool ok = Wm_protocol::parse_window_chrome(
					_chrome_rom.node(),
					[&] (Wm_protocol::Window_chrome const &c) {
						if (num < MAX_VIEWS) snap[num++] = c;
						else overflow = true;
					});

				if (!ok) {
					Genode::warning("sponge_decorator: malformed "
					                "window_chrome snapshot (dropped)");
					return;
				}
			}

			if (overflow)
				Genode::warning("sponge_decorator: window_chrome snapshot "
				                "exceeds ", (unsigned)MAX_VIEWS, " windows");

			for (auto &slot : _views) {
				if (!slot.constructed())
					continue;

				bool present = false;
				for (unsigned i = 0; i < num; i++)
					present |= (snap[i].wid == slot->wid());

				if (!present) {
					Genode::log("sponge_decorator: destroy chrome view ",
					            slot->wid());
					slot.destruct();
				}
			}

			for (unsigned i = 0; i < num; i++) {

				Chrome_view *view = nullptr;
				for (auto &slot : _views)
					if (slot.constructed() && slot->wid() == snap[i].wid)
						view = &*slot;

				if (view) {
					view->update(snap[i], _theme, _font);
					continue;
				}

			for (auto &slot : _views) {
				if (slot.constructed())
					continue;
				Genode::log("sponge_decorator: create chrome view ",
				            snap[i].wid, " (", snap[i].fw, "x",
				            snap[i].fh, ")");
				slot.construct(_env, snap[i], _theme, _font);
				break;
			}
			}
		}

		void _handle_theme()  { _load_theme(); }
		void _handle_chrome() { _sync_chrome(); }

	public:

		Main(Genode::Env &env) : _env(env)
		{
			_read_config();

			_theme_rom.construct(
				_env, _live_theme ? "theme" : "default.theme");
			_theme_rom->sigh(_theme_handler);

			_load_theme();

			_chrome_rom.sigh(_chrome_handler);
			_sync_chrome();

			Genode::log("sponge_decorator: ready (theme source=",
			            _live_theme ? "themed" : "default",
			            ", margins top=", _theme.margins.top,
			            " bottom=", _theme.margins.bottom,
			            " left=", _theme.margins.left,
			            " right=", _theme.margins.right, ")");
		}
};


}  /* namespace Sponge::Decorator */


void Component::construct(Genode::Env &env)
{
	static Sponge::Decorator::Main main { env };
}


Genode::size_t Component::stack_size()
{
	return 32 * 1024 * sizeof(Genode::addr_t);
}
