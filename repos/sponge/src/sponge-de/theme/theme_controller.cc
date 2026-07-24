/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of ThemeController. See theme_controller.h for the
 * thread model (the sigh handler marshals; applyTheme re-styles).
 */

#include "theme_controller.h"

#include "panel/panel_widget.h"
#include "sponge_de_main.h"
#include "theme/theme_loader.h"

#include <base/log.h>
#include <util/hid.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

#include <QApplication>
#include <QFont>
#include <QTimer>

using namespace Sponge::Sponge_DE;


namespace {

/*
 * Extract the <theme source="..."/> value from the component config, which
 * init may deliver in either HID (the run scripts' install_config form) or
 * XML. Returns "themed" only when explicitly configured; anything else
 * (including a missing theme node) yields the safe default-theme fallback.
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

}  /* namespace */


ThemeController::ThemeController(Genode::Env &env, QObject *parent)
:
	QObject(parent),
	_env(env)
{
	_applied_report.enabled(true);

	/*
	 * The theme SOURCE is chosen by the component config, not by probing
	 * a ROM: opening an unrouted "theme" ROM is a fatal parent denial
	 * (not a catchable Rom_connection_failed), so the config must tell us
	 * whether the live sponge_themed path is wired. <theme source="themed"/>
	 * -> watch the "theme" ROM; absent (or source="default") -> read
	 * default.theme once (the run/sponge-de-test.run fallback). This keeps
	 * the choice explicit and inspectable (AGENTS.md §1.2).
	 */
	bool const live = [&] {
		Genode::Attached_rom_dataspace config(_env, "config");
		return config_asks_for_themed(config);
	}();

	if (live) {
		/*
		 * Live mode. The ROM signal (push) is wired for correctness, but
		 * because Qt's event loop does not drive the Genode entrypoint,
		 * a QTimer (pull) on the GUI thread is the reliable re-check.
		 * Both funnel into applyTheme(), deduped by name.
		 */
		_live = true;
		_theme_rom.construct(_env, "theme");
		_theme_rom->update();
		_sigh.construct(_env.ep(), *this, &ThemeController::_on_rom);
		_theme_rom->sigh(*_sigh);
		_on_rom();

		_poll_timer = new QTimer(this);
		_poll_timer->start(250);
		QObject::connect(_poll_timer, &QTimer::timeout, this, &ThemeController::_poll);
	} else {
		Genode::log("sponge-de: theme source=default.theme (fallback; no live reload)");
		_fallback_default_theme();
	}
}


void ThemeController::attach_panel(PanelWidget *panel) { _panel = panel; }
void ThemeController::attach_main(Main *main)         { _main  = main; }


/*
 * Genode entrypoint dispatcher thread. Reads the ROM and marshals to the
 * GUI thread. NEVER touches a QWidget or QApplication here. (When the
 * entrypoint is not driven during app.exec, the QTimer _poll is the path
 * that actually applies updates; this handler is correct for whenever the
 * entrypoint does dispatch.)
 */
void ThemeController::_on_rom()
{
	QString name, ini;
	if (_read_theme(name, ini))
		QMetaObject::invokeMethod(this, "applyTheme",
		                          Qt::QueuedConnection,
		                          Q_ARG(QString, name),
		                          Q_ARG(QString, ini));
}


bool ThemeController::_read_theme(QString &name, QString &ini)
{
	_theme_rom->update();
	if (!_theme_rom->valid())
		return false;

	try {
		Genode::Xml_node const xml = _theme_rom->xml();
		if (!xml.has_type("theme"))
			return false;

		name = QString::fromUtf8(
			xml.attribute_value("name", Genode::String<64>()).string());

		Genode::String<2048> const content =
			xml.decoded_content<Genode::String<2048>>();
		ini = QString::fromUtf8(content.string(), (int)content.length());

		return !name.isEmpty();
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}
}


/* GUI thread: re-check the ROM and apply any new theme directly. */
void ThemeController::_poll()
{
	QString name, ini;
	if (_read_theme(name, ini))
		applyTheme(name, ini);
}


/*
 * GUI thread. Parses the marshalled INI with the same Qt-free
 * ThemeLoader used for the default.theme fallback, then re-styles every
 * attached widget and republishes the applied-theme name. De-duplicates
 * by name so the push (signal) and pull (timer) paths never double-apply.
 */
void ThemeController::applyTheme(QString name, QString ini)
{
	if (name.isEmpty() || name == _last_applied)
		return;

	QByteArray const ini_utf8 = ini.toUtf8();

	Sponge_DE::Theme::Theme parsed { };
	bool const clean = [&] {
		Sponge_DE::Theme::ThemeLoader loader;
		return loader.load(ini_utf8.constData(),
		                   (Genode::size_t)ini_utf8.size(), parsed);
	}();

	/*
	 * sponge_themed guarantees it only ships a resolved theme, so adopt
	 * the parsed values. The "keep previous on unknown theme" rule is
	 * enforced upstream in sponge_themed (it never republishes on an
	 * unknown name); here we just apply what arrived.
	 */
	_theme = parsed;
	_last_applied = name;

	Genode::log("sponge-de: applying theme '",
	            name.toUtf8().constData(), "'",
	            clean ? "" : " (with parse warnings)");

	qApp->setFont(QFont(_theme.default_font().family.string(),
	                    (int)_theme.default_font().size));

	if (_panel) _panel->restyle(_theme);
	if (_main)  _main->restyle(_theme);

	_live_applied = true;
	_publish_applied(name.toUtf8().constData());
}


void ThemeController::_publish_applied(char const *name)
{
	(void)_applied_report.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("name", name);
	});
}


void ThemeController::_fallback_default_theme()
{
	try {
		Genode::Attached_rom_dataspace rom(_env, "default.theme");
		if (rom.valid()) {
			Sponge_DE::Theme::ThemeLoader loader;
			loader.load(rom.local_addr<char const>(),
			            rom.size(), _theme);
			Genode::log("sponge-de: theme loaded: default.theme (fallback)");
			_last_applied = QStringLiteral("default");
		} else {
			Genode::warning("sponge-de: default.theme invalid, using built-in defaults");
		}
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::warning("sponge-de: default.theme ROM unavailable, using built-in defaults");
	}

	_publish_applied("default");
}
