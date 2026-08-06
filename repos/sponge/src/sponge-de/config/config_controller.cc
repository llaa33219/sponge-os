/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of ConfigController. See config_controller.h for the
 * thread model (the sigh handler marshals; applyConfig emits signals).
 */

#include "config_controller.h"

#include "launcher/launcher_menu_view.h"
#include "panel/panel_widget.h"

#include <base/log.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_node.h>

#include <QApplication>
#include <QTimer>

using namespace Sponge::Sponge_DE;


namespace {

/*
 * Parse the child config (HID or XML). Returns true only when the
 * child explicitly opts in via <config source="configd"/>; absent (or
 * any other value) leaves the controller in fallback mode. Mirrors
 * ThemeController's config_asks_for_themed at theme_controller.cc:34.
 *
 * HID is the framework default since the format became default; we
 * accept either so scenarios delivering the legacy XML form still
 * boot cleanly.
 */
bool parse_config_asks_for_configd(Genode::Attached_rom_dataspace &config)
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
			root.for_each_sub_node("config", [&](Genode::Xml_node const &c) {
				if (!live)
					live = c.attribute_value("source",
					         Genode::String<32>()) ==
					       Genode::String<32>("configd");
			});
		}
		catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		root.for_each_sub_node([&](Genode::Hid_node const &n) {
			if (!live && n.has_type("config"))
				live = n.attribute_value("source",
				         Genode::String<32>()) ==
				       Genode::String<32>("configd");
		});
	}

	return live;
}

}  /* namespace */


bool Sponge::Sponge_DE::config_asks_for_configd(Genode::Env &env)
{
	Genode::Attached_rom_dataspace config(env, "config");
	return parse_config_asks_for_configd(config);
}


/*
 * Extract one <key name="..." value="..."/> child from the broadcast
 * XML. Returns true and writes `out` on a match; returns false (and
 * leaves `out` unchanged) if the key is missing or the XML is
 * malformed. Name comparison is case-sensitive to match the configd
 * registry (sponge_configd/main.cc:177-194).
 */
bool key_value(Genode::Xml_node const &root, char const *name,
               Genode::String<128> &out)
{
	try {
		bool found = false;
		root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
			if (found) return;
			Genode::String<64> const k_name =
				k.attribute_value("name", Genode::String<64>());
			if (Genode::strcmp(k_name.string(), name) == 0) {
				out = k.attribute_value("value", Genode::String<128>());
				found = true;
			}
		});
		return found;
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}
}


/*
 * Parse the panel.height string (uint [16..128]). On invalid input
 * (should not happen — configd rejects out-of-range before broadcast)
 * we return 0 and the controller logs a warning. The panel falls back
 * to its theme-derived height in that case (the height never reaches
 * the widget).
 */
unsigned parse_panel_height(char const *v)
{
	if (v == nullptr || *v == '\0') return 0;
	unsigned parsed = 0;
	for (char const *p = v; *p; ++p) {
		char const c = *p;
		if (c < '0' || c > '9') return 0;
		parsed = parsed * 10U + (unsigned)(c - '0');
	}
	return parsed;
}


ConfigController::ConfigController(Genode::Env &env, QObject *parent)
:
	QObject(parent),
	_env(env)
{
	bool const live = config_asks_for_configd(env);

	if (!live) {
		Genode::log("sponge-de: config source=none (no configd wiring; "
		            "panel/launcher/clock keep their ctor-time defaults)");
		return;
	}

	Genode::log("sponge-de: config source=configd (live)");

	/*
	 * Live mode. The ROM signal (push) is wired for correctness, but
	 * because Qt's event loop does not drive the Genode entrypoint,
	 * a QTimer (pull) on the GUI thread is the reliable re-check.
	 * Both funnel into applyConfig(), deduped per key.
	 *
	 * The ROM session is labeled "configd" (NOT "config") to avoid
	 * the init-inline-config collision: the "config" label is
	 * reserved by init for the child's inline <config> block
	 * (genode/repos/os/src/lib/sandbox/child.cc:510-524), and
	 * routing the configd broadcast to "config" would shadow the
	 * activation gate. The run script's report_rom policy maps
	 * "sponge-de -> configd" to "sponge_configd -> config".
	 */
	try {
		_config_rom.construct(_env, "configd");
		_config_rom->update();
		_sigh.construct(_env.ep(), *this, &ConfigController::_on_rom);
		_config_rom->sigh(*_sigh);
		_on_rom();

		_poll_timer = new QTimer(this);
		_poll_timer->start(250);
		QObject::connect(_poll_timer, &QTimer::timeout, this, &ConfigController::_poll);
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		/*
		 * report_rom has no policy for "config" → sponge_configd's
		 * "config" broadcast in this scenario. This is the fallback
		 * path (mirrors ThemeController's fallback at
		 * theme_controller.cc:113). No signals, no restyles.
		 */
		Genode::warning("sponge-de: config ROM unavailable "
		                "(no report_rom policy for "
		                "sponge-de -> config <- sponge_configd -> config)");
		_config_rom.destruct();
		_sigh.destruct();
	}
}


void ConfigController::attach_panel(PanelWidget *panel)
{
	_panel = panel;

	/* Panel-side apply slots live in panel_widget.{h,cc} as private
	 * slots invoked via QMetaObject::invokeMethod (the GUI-thread
	 * marshal rule, failure-point 2). Connecting them here keeps the
	 * marshalling in one place. */
	if (_panel)
		QObject::connect(this, &ConfigController::panel_height_changed,
		                 _panel, &PanelWidget::applyHeight);
	if (_panel)
		QObject::connect(this, &ConfigController::panel_visible_widgets_changed,
		                 _panel, &PanelWidget::applyVisibleWidgets);
	if (_panel)
		QObject::connect(this, &ConfigController::clock_format_changed,
		                 _panel, &PanelWidget::applyClockFormat);
}


void ConfigController::attach_launcher(LauncherMenuView *launcher)
{
	_launcher = launcher;

	if (_launcher)
		QObject::connect(this, &ConfigController::launcher_sort_by_changed,
		                 _launcher, &LauncherMenuView::applySortBy);
}


/*
 * Genode entrypoint dispatcher thread. Reads the ROM and marshals to the
 * GUI thread. NEVER touches a QWidget or QApplication here. (When the
 * entrypoint is not driven during app.exec, the QTimer _poll is the path
 * that actually applies updates; this handler is correct for whenever
 * the entrypoint does dispatch.)
 */
void ConfigController::_on_rom()
{
	QString payload;
	if (_read_payload(payload))
		QMetaObject::invokeMethod(this, "applyConfig",
		                          Qt::QueuedConnection,
		                          Q_ARG(QString, payload));
}


bool ConfigController::_read_payload(QString &payload)
{
	if (!_config_rom.constructed())
		return false;

	_config_rom->update();
	if (!_config_rom->valid())
		return false;

	/*
	 * The configd broadcast ROM is the full XML document
	 * <config><key name="..." value="..."/>...</config>
	 * (sponge_configd/main.cc:482-501). We cannot use decoded_content
	 * here — that returns only the inner text, NOT the root tags,
	 * and the inner text alone is not well-formed XML. Read the full
	 * <config>...</config> bytes from the ROM dataspace instead.
	 *
	 * The 8192-byte cap matches the W1 theme transport cap
	 * (theme_controller.cc:155) — sponge_configd's broadcast is
	 * bounded by its `String<128>` values, so 8192 is comfortably
	 * above the realistic max. ROM dataspaces are page-aligned, so
	 * the bytes after `size()` may be NUL padding that must NOT be
	 * included.
	 */
	char const *const base = _config_rom->local_addr<char>();
	Genode::size_t const sz  = _config_rom->size();

	payload = QString::fromUtf8(base, (int)sz);
	return !payload.isEmpty();
}


/* GUI thread: re-check the ROM and apply any new broadcast directly. */
void ConfigController::_poll()
{
	QString payload;
	if (_read_payload(payload))
		applyConfig(payload);
}


/*
 * GUI thread. Parse the marshalled payload into the four key/value
 * pairs, then emit the matching signals (de-duped per key).
 */
void ConfigController::applyConfig(QString payload)
{
	if (payload.isEmpty())
		return;

	/*
	 * Re-parse the XML payload to extract the four key/value pairs.
	 * The broadcast is guaranteed well-formed by sponge_configd
	 * (sponge_configd/main.cc:482-501), but be defensive: a single
	 * malformed <key> must not stop the rest from applying.
	 */
	Genode::String<128> panel_height_str { };
	Genode::String<128> panel_visible_str { };
	Genode::String<128> clock_format_str   { };
	Genode::String<128> launcher_sort_str  { };

	try {
		Genode::Xml_node const root(payload.toUtf8().constData(),
		                            (Genode::size_t)payload.toUtf8().size());
		key_value(root, "panel.height",          panel_height_str);
		key_value(root, "panel.visible_widgets", panel_visible_str);
		key_value(root, "clock.format",          clock_format_str);
		key_value(root, "launcher.sort_by",      launcher_sort_str);
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge-de: config broadcast XML invalid; "
		                "ignoring this update");
		return;
	}

	QString const panel_height_q    = QString::fromUtf8(panel_height_str.string());
	QString const panel_visible_q   = QString::fromUtf8(panel_visible_str.string());
	QString const clock_format_q    = QString::fromUtf8(clock_format_str.string());
	QString const launcher_sort_q   = QString::fromUtf8(launcher_sort_str.string());

	_emit_changed(panel_height_q, panel_visible_q, clock_format_q, launcher_sort_q);
}


void ConfigController::_emit_changed(QString const &panel_height,
                                     QString const &panel_visible_widgets,
                                     QString const &clock_format,
                                     QString const &launcher_sort_by)
{
	/*
	 * De-dup: skip the signal when the broadcast value is byte-
	 * identical to the last-applied value (avoids re-entrant restyle
	 * loops from the push + pull double-funneling the same payload).
	 */
	bool changed = false;

	if (panel_height != _last_panel_height) {
		unsigned h = parse_panel_height(panel_height.toUtf8().constData());
		if (h != 0) {
			_last_panel_height = panel_height;
			emit panel_height_changed(h);
			changed = true;
		}
	}

	if (panel_visible_widgets != _last_panel_visible_widgets) {
		_last_panel_visible_widgets = panel_visible_widgets;
		emit panel_visible_widgets_changed(panel_visible_widgets);
		changed = true;
	}

	if (clock_format != _last_clock_format) {
		_last_clock_format = clock_format;
		emit clock_format_changed(clock_format);
		changed = true;
	}

	if (launcher_sort_by != _last_launcher_sort_by) {
		_last_launcher_sort_by = launcher_sort_by;
		emit launcher_sort_by_changed(launcher_sort_by);
		changed = true;
	}

	if (changed) {
		Genode::log("sponge-de: config applied ",
		            "(height=", panel_height.toUtf8().constData(),
		            " visible=", panel_visible_widgets.toUtf8().constData(),
		            " clock=", clock_format.toUtf8().constData(),
		            " sort=", launcher_sort_by.toUtf8().constData(), ")");
	}
}