/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Entry point for the sponge_files component (Phase 7 todo 15).
 *
 * Qt on Genode requires the libc component model
 * (Libc::Component::construct) plus qpa_init() before QApplication.
 *
 * The theme source is chosen by the component config — the same scheme
 * sponge-de uses (ThemeController::config_asks_for_themed):
 *   - <theme source="themed"/>  -> open the live "theme" ROM
 *   - absent / "default"        -> read default.theme once (fallback)
 *
 * For Alpha, the focused run/sponge-files.run uses the fallback path
 * (default.theme ROM) so the component works without sponge_themed, but
 * the structure supports the live themed ROM when the alpha scenario
 * mounts it (reusing sponge-de's ThemeLoader pattern verbatim).
 */

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <libc/component.h>
#include <rom_session/connection.h>
#include <util/hid.h>
#include <util/xml_node.h>

#include <QApplication>
#include <QFont>

#include <qt6_component/qpa_init.h>

#include "files_window.h"
#include "theme/theme_loader.h"

using namespace Sponge::Sponge_Files;


namespace {

/*
 * True when this component's <config> carries <theme source="themed"/>.
 * The probe scenario uses <theme source="default"/> (or omits the node),
 * so we fall back to default.theme. HID or XML, same shape sponge-de's
 * ThemeController accepts.
 */
bool config_asks_for_themed(Genode::Attached_rom_dataspace &config)
{
	config.update();
	if (!config.valid())
		return false;

	char const *const base = config.local_addr<char>();
	Genode::size_t  const sz  = config.size();

	bool live = false;

	bool const is_xml = (sz > 0 && base[0] == '<');
	if (is_xml) {
		try {
			Genode::Xml_node const root(base, sz);
			root.for_each_sub_node("theme", [&](Genode::Xml_node const &t) {
				if (!live)
					live = t.attribute_value("source",
					         Genode::String<32>()) ==
					       Genode::String<32>("themed");
			});
		}
		catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		root.for_each_sub_node([&](Genode::Hid_node const &n) {
			if (!live && n.has_type("theme"))
				live = n.attribute_value("source",
				         Genode::String<32>()) ==
				       Genode::String<32>("themed");
		});
	}

	return live;
}

} /* namespace */


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] {

		qpa_init(env);

		int argc = 1;
		char const *argv[] = { "sponge_files", nullptr };

		QApplication app(argc, const_cast<char **>(argv));

		/*
		 * Resolve the initial theme.
		 *  - live "theme" ROM: parse it once at startup. (Live reload
		 *    is intentionally NOT wired here — Alpha scope is "structure
		 *    it so the live themed ROM works in the alpha scenario",
		 *    not deliver the live reload; sponge_files has no
		 *    ThemeController equivalent and the alpha scenario uses the
		 *    default-theme fallback anyway.)
		 *  - default.theme ROM (fallback): parse once.
		 *  - else: built-in defaults.
		 */
		Theme::Theme theme { };
		bool const live = [&] {
			Genode::Attached_rom_dataspace config(env, "config");
			return config_asks_for_themed(config);
		}();

		bool parsed_any { false };

		if (live) {
			try {
				Genode::Attached_rom_dataspace theme_rom(env, "theme");
				theme_rom.update();
				if (theme_rom.valid()) {
					try {
						Genode::Xml_node const xml = theme_rom.xml();
						if (xml.has_type("theme")) {
							Genode::String<4096> const content =
								xml.decoded_content<Genode::String<4096>>();
							Theme::ThemeLoader loader;
							loader.load(content.string(),
							            content.length(), theme);
							parsed_any = true;
							Genode::log("sponge_files: live theme loaded");
						}
					}
					catch (Genode::Xml_node::Invalid_syntax) { }
				}
			}
			catch (Genode::Rom_connection::Rom_connection_failed) {
				Genode::warning("sponge_files: live theme ROM unavailable, "
				                "trying default.theme");
			}
		}

		if (!parsed_any) {
			try {
				Genode::Attached_rom_dataspace rom(env, "default.theme");
				if (rom.valid()) {
					Theme::ThemeLoader loader;
					loader.load(rom.local_addr<char const>(),
					            rom.size(), theme);
					parsed_any = true;
					Genode::log("sponge_files: theme loaded: default.theme");
				}
			}
			catch (Genode::Rom_connection::Rom_connection_failed) { }
		}

		if (!parsed_any)
			Genode::warning("sponge_files: no theme parsed, using built-in defaults");

		app.setFont(QFont(theme.default_font().family.string(),
		                  (int)theme.default_font().size));

		Files_window window(env, theme);
		window.show();

		app.connect(&app, SIGNAL(lastWindowClosed()), SLOT(quit()));

		exit(app.exec());
	});
}
