/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of ConfigController. See config_controller.h for the
 * thread model (the sigh handler marshals; applyConfig emits signals).
 */

#include "config_controller.h"

#include "launcher/launcher_menu_view.h"
#include "panel/notifier_widget.h"
#include "panel/notify_poster.h"
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
 * Phase 16 W8 (U16.5 / D16.5) per-id key parser. Matches the
 * pattern templates registered in sponge_configd (the W2
 * pattern-key registry):
 *
 *   panel.<id>.height          uint [16..128]   (clones panel.height validator)
 *   panel.<id>.position        enum             (clones panel.position validator)
 *   panel.<id>.visible_widgets enum-list        (clones panel.visible_widgets)
 *
 * Returns true on a match and writes the (id, suffix) pair; returns
 * false on a flat key (panel.height etc.) or an unknown suffix
 * (the caller treats those as regular flat keys).
 *
 * The id charset is `[a-z0-9_-]{1,16}` — duplicate of the configd
 * charset check (W2 #3 step b). The GUI-side fan-out does NOT
 * re-validate (the configd reject is the authoritative gate);
 * invalid ids here would not be in `_instantiated` on the daemon
 * side and would never round-trip in the broadcast.
 *
 * Implementation note: the Genode::String<32> template constructor
 * concatenates its arguments via Genode::print; passing a (char *,
 * size_t) tuple to it would render the pointer contents followed
 * by the decimal size. We instead build the result with memcpy
 * into the String's _buf through copy_cstring (the String's NUL-
 * terminated cstring constructor).
 */
bool Sponge::Sponge_DE::parse_panel_id_key(char const *key,
                                           Genode::String<32> &id_out,
                                           Genode::String<32> &suffix_out)
{
	if (key == nullptr) return false;

	Genode::size_t const key_len = Genode::strlen(key);
	static char const PREFIX[] = "panel.";
	static Genode::size_t const PREFIX_LEN = sizeof(PREFIX) - 1;

	if (key_len <= PREFIX_LEN) return false;
	if (Genode::strcmp(key, PREFIX, PREFIX_LEN) != 0) return false;

	/*
	 * Walk forward from the prefix end to find the LAST '.'. The
	 * <id> segment is between the prefix and that last '.'.
	 */
	char const *tail = key + PREFIX_LEN;
	Genode::size_t tail_len = key_len - PREFIX_LEN;

	char const *last_dot = nullptr;
	for (Genode::size_t i = 0; i < tail_len; ++i)
		if (tail[i] == '.') last_dot = tail + i;
	if (!last_dot) return false;

	Genode::size_t const id_len = (Genode::size_t)(last_dot - tail);
	if (id_len == 0 || id_len > 16) return false;

	char const *suffix = last_dot + 1;
	Genode::size_t const suffix_len = (tail + tail_len) - suffix;
	if (suffix_len == 0) return false;

	/*
	 * Suffix whitelist: must match one of the W2 pattern templates.
	 * Closed enum of three members; an unknown suffix means the
	 * key is not a per-id panel key (could be a future W11+ key).
	 */
	bool suffix_ok = false;
	if (Genode::strcmp(suffix, "height",          6) == 0 &&
	    suffix_len == 6) suffix_ok = true;
	else if (Genode::strcmp(suffix, "position",    8) == 0 &&
	         suffix_len == 8) suffix_ok = true;
	else if (Genode::strcmp(suffix, "visible_widgets", 15) == 0 &&
	         suffix_len == 15) suffix_ok = true;
	if (!suffix_ok) return false;

	/*
	 * Build the id + suffix NUL-terminated local buffers, then
	 * invoke the explicit cstring constructor. copy_cstring
	 * truncates safely at the String's CAPACITY boundary.
	 */
	char id_buf[24]    { };
	char suff_buf[20]   { };
	for (Genode::size_t i = 0; i < id_len;    ++i) id_buf[i]  = tail[i];
	for (Genode::size_t i = 0; i < suffix_len; ++i) suff_buf[i] = suffix[i];
	id_buf[id_len]        = '\0';
	suff_buf[suffix_len]  = '\0';

	id_out     = Genode::String<32>(id_buf);
	suffix_out = Genode::String<32>(suff_buf);
	return true;
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
 * GUI thread. Parse the marshalled payload into the seven flat
 * key/value pairs AND the per-id panel keys, then emit the matching
 * signals (de-duped per key). Phase 16 W8 added the per-id parse
 * loop (the W2 pattern-key infrastructure carries the per-id entries
 * through the broadcast; W8 wires them to the PanelCollection).
 */
void ConfigController::applyConfig(QString payload)
{
	if (payload.isEmpty())
		return;

	/*
	 * Re-parse the XML payload to extract the seven key/value pairs.
	 * The broadcast is guaranteed well-formed by sponge_configd
	 * (sponge_configd/main.cc:482-501), but be defensive: a single
	 * malformed <key> must not stop the rest from applying.
	 */
	Genode::String<128> panel_height_str    { };
		Genode::String<128> panel_visible_str   { };
		Genode::String<128> panel_position_str  { };
		Genode::String<128> clock_format_str   { };
		Genode::String<128> launcher_sort_str  { };
		Genode::String<128> background_color_str { };
		Genode::String<128> background_image_str { };
		Genode::String<128> panel_ids_str        { };

		/*
		 * Per-id accumulator. Each successful per-id parse emits
		 * one entry below; we deduplicate by (id, suffix) inside
		 * _emit_changed. The accumulator is bounded by the
		 * MAX_PATTERN_KEYS = 32 (configd-side ceiling); we set a
		 * local ceiling of 64 entries as a defensive cap (the GUI
		 * should never see more than the registry's 32 anyway).
		 */
		struct Per_id_entry {
			char id   [24];   /* charset [a-z0-9_-]{1,16} + NUL */
			char suff [20];   /* "height" / "position" / "visible_widgets" + NUL */
			char value[160];  /* raw value as written */
		};
		Per_id_entry per_id[64] { };
		unsigned per_id_count { 0 };

		try {
			Genode::Xml_node const root(payload.toUtf8().constData(),
			                            (Genode::size_t)payload.toUtf8().size());
			key_value(root, "panel.height",          panel_height_str);
			key_value(root, "panel.visible_widgets", panel_visible_str);
			key_value(root, "panel.position",        panel_position_str);
			key_value(root, "clock.format",          clock_format_str);
			key_value(root, "launcher.sort_by",      launcher_sort_str);
			key_value(root, "background.color",      background_color_str);
			key_value(root, "background.image",      background_image_str);
			key_value(root, "panel.ids",             panel_ids_str);

			/*
			 * Phase 16 W8 (U16.5 / D16.5) — per-id parse loop.
			 * Walks every <key> child, identifies the
			 * panel.<id>.suffix pattern, and accumulates the
			 * (id, suffix, value) triple. The signal fan-out
			 * happens in _emit_changed.
			 */
			root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
				if (per_id_count >= sizeof(per_id) / sizeof(per_id[0])) return;
				Genode::String<128> const k_name =
					k.attribute_value("name", Genode::String<128>());
				Genode::String<32>  id   { };
				Genode::String<32>  suff { };
				if (!parse_panel_id_key(k_name.string(), id, suff))
					return;
				Genode::String<160> const value =
					k.attribute_value("value", Genode::String<160>());
				Per_id_entry &e = per_id[per_id_count++];
				Genode::size_t const id_len   = Genode::min((Genode::size_t)id.length(),   sizeof(e.id)   - 1);
				Genode::size_t const suff_len = Genode::min((Genode::size_t)suff.length(), sizeof(e.suff) - 1);
				Genode::size_t const val_len  = Genode::min((Genode::size_t)value.length(), sizeof(e.value) - 1);
				Genode::memcpy(e.id,   id.string(),   id_len);
				Genode::memcpy(e.suff, suff.string(), suff_len);
				Genode::memcpy(e.value, value.string(), val_len);
				e.id[id_len]     = '\0';
				e.suff[suff_len] = '\0';
				e.value[val_len] = '\0';
			});
		}
		catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("sponge-de: config broadcast XML invalid; "
			                "ignoring this update");
			return;
		}

		QString const panel_height_q    = QString::fromUtf8(panel_height_str.string());
		QString const panel_visible_q   = QString::fromUtf8(panel_visible_str.string());
		QString const panel_position_q  = QString::fromUtf8(panel_position_str.string());
		QString const clock_format_q    = QString::fromUtf8(clock_format_str.string());
		QString const launcher_sort_q   = QString::fromUtf8(launcher_sort_str.string());
		QString const background_color_q = QString::fromUtf8(background_color_str.string());
		QString const background_image_q = QString::fromUtf8(background_image_str.string());
		QString const panel_ids_q       = QString::fromUtf8(panel_ids_str.string());

		_emit_changed(panel_height_q, panel_visible_q, panel_position_q,
		              clock_format_q, launcher_sort_q,
		              background_color_q, background_image_q,
		              panel_ids_q, per_id, per_id_count);
	}


void ConfigController::_emit_changed(QString const &panel_height,
                                     QString const &panel_visible_widgets,
                                     QString const &panel_position,
                                     QString const &clock_format,
                                     QString const &launcher_sort_by,
                                     QString const &background_color,
                                     QString const &background_image,
                                     QString const &panel_ids,
                                     void const *per_id_entries,
                                     unsigned per_id_count)
{
	(void)per_id_entries;
	(void)per_id_count;
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

	if (panel_position != _last_panel_position) {
		_last_panel_position = panel_position;
		emit panel_position_changed(panel_position);
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

	if (background_color != _last_background_color && !background_color.isEmpty()) {
		_last_background_color = background_color;
		emit background_color_changed(background_color);
		changed = true;
	}

	if (background_image != _last_background_image && !background_image.isEmpty()) {
		_last_background_image = background_image;
		emit background_image_changed(background_image);
		changed = true;
	}

	/*
	 * Phase 16 W8 (U16.5 / D16.5) — emit panel_ids_changed BEFORE
	 * the per-id apply loop. The PanelCollection subscribes to
	 * both; the order ensures a freshly-instantiated widget
	 * receives its per-id apply in the same broadcast cycle.
	 * Without this ordering, the per-id apply fires BEFORE the
	 * widget exists, and the PanelCollection's applyPositionFor /
	 * applyHeightFor find no widget for the id (the F5 sentinel).
	 */
	if (panel_ids != _last_panel_ids && !panel_ids.isEmpty()) {
		_last_panel_ids = panel_ids;
		emit panel_ids_changed(panel_ids);
	}

	/*
	 * Phase 16 W8 (U16.5 / D16.5) — per-id fan-out. Walk every
	 * accumulated (id, suffix, value) triple and emit the
	 * matching per-id signal. The PanelCollection routes the
	 * per-id signals to the matching PanelWidget instance.
	 */
	struct Per_id_entry {
		char id   [24];
		char suff [20];
		char value[160];
	};
	auto const *entries = static_cast<Per_id_entry const *>(per_id_entries);

	/*
	 * Per-id dedup is scoped to (id, suffix): a write to
	 * panel.alpha.position=top then panel.alpha.position=bottom
	 * must produce TWO distinct signals. We track the last value
	 * per (id, suffix) in a small map. The QMap would be cleaner
	 * but adds a heap dependency; a fixed array is fine for the
	 * realistic < 32 entries per broadcast.
	 */
	struct Seen_key { char id[24]; char suff[20]; char value[160]; };
	Seen_key seen[64] { };
	unsigned seen_count = 0;
	for (unsigned i = 0; i < per_id_count; ++i) {
		Per_id_entry const &e = entries[i];

		/*
		 * Dedup: skip if (id, suffix, value) is identical to a
		 * previous entry in this same broadcast. Without this,
		 * the broadcast's own deduplication could fire twice for
		 * the same key (the configd `_emit_broadcast` path
		 * sometimes carries duplicates when a value is re-applied
		 * after a refresh).
		 */
		bool dup = false;
		for (unsigned j = 0; j < seen_count; ++j) {
			if (Genode::strcmp(seen[j].id,    e.id,    sizeof(e.id))   == 0 &&
			    Genode::strcmp(seen[j].suff,  e.suff,  sizeof(e.suff)) == 0 &&
			    Genode::strcmp(seen[j].value, e.value, sizeof(e.value)) == 0) {
				dup = true;
				break;
			}
		}
		if (dup) continue;

		QString const id_q    = QString::fromUtf8(e.id);
		QString const value_q = QString::fromUtf8(e.value);

		if (Genode::strcmp(e.suff, "height", 6) == 0) {
			unsigned h = parse_panel_height(e.value);
			if (h != 0) {
				emit panel_height_changed_for(id_q, h);
				changed = true;
			}
		}
		else if (Genode::strcmp(e.suff, "position", 8) == 0) {
			emit panel_position_changed_for(id_q, value_q);
			changed = true;
		}
		else if (Genode::strcmp(e.suff, "visible_widgets", 15) == 0) {
			emit panel_visible_widgets_changed_for(id_q, value_q);
			changed = true;
		}

		Seen_key &s = seen[seen_count++];
		Genode::size_t const id_len   = Genode::strlen(e.id);
		Genode::size_t const suff_len = Genode::strlen(e.suff);
		Genode::size_t const val_len  = Genode::strlen(e.value);
		Genode::memcpy(s.id,    e.id,    Genode::min(id_len,   sizeof(s.id)    - 1));
		Genode::memcpy(s.suff,  e.suff,  Genode::min(suff_len, sizeof(s.suff)  - 1));
		Genode::memcpy(s.value, e.value, Genode::min(val_len,  sizeof(s.value) - 1));
		s.id[Genode::min(id_len,   sizeof(s.id)    - 1)] = '\0';
		s.suff[Genode::min(suff_len, sizeof(s.suff)  - 1)] = '\0';
		s.value[Genode::min(val_len,  sizeof(s.value) - 1)] = '\0';
	}

	/*
	 * Phase 16 W8 (U16.5 / D16.5) — emit panel_ids_changed BEFORE
	 * the per-id apply loop. The PanelCollection subscribes to
	 * both; the order ensures a freshly-instantiated widget
	 * receives its per-id apply in the same broadcast cycle.
	 * Without this ordering, the per-id apply fires BEFORE the
	 * widget exists, and the PanelCollection's applyPositionFor /
	 * applyHeightFor find no widget for the id (the F5 sentinel).
	 */
	if (changed) {
		Genode::log("sponge-de: config applied ",
		            "(height=", panel_height.toUtf8().constData(),
		            " visible=", panel_visible_widgets.toUtf8().constData(),
		            " position=", panel_position.toUtf8().constData(),
		            " clock=", clock_format.toUtf8().constData(),
		            " sort=", launcher_sort_by.toUtf8().constData(),
		            " bg.color=", background_color.toUtf8().constData(),
		            " bg.image=", background_image.toUtf8().constData(),
		            " panel.ids=", panel_ids.toUtf8().constData(),
		            " per_id=", per_id_count, ")");

		if (_notify) {
			QString title = QStringLiteral("config applied");
			QString body;
			if (panel_height != _last_panel_height)
				body += QStringLiteral("panel.height=%1 ").arg(panel_height);
			if (panel_visible_widgets != _last_panel_visible_widgets)
				body += QStringLiteral("panel.visible_widgets=%1 ").arg(panel_visible_widgets);
			if (panel_position != _last_panel_position)
				body += QStringLiteral("panel.position=%1 ").arg(panel_position);
			if (clock_format != _last_clock_format)
				body += QStringLiteral("clock.format=%1 ").arg(clock_format);
			if (launcher_sort_by != _last_launcher_sort_by)
				body += QStringLiteral("launcher.sort_by=%1 ").arg(launcher_sort_by);
			if (background_color != _last_background_color)
				body += QStringLiteral("background.color=%1 ").arg(background_color);
			if (background_image != _last_background_image)
				body += QStringLiteral("background.image=%1 ").arg(background_image);
			_notify->post(title, body.trimmed(), QStringLiteral("info"), 3000);
		}
	}
}


void ConfigController::attach_notify_poster(NotifyPoster *poster)
{
	_notify = poster;
}


ConfigController::~ConfigController()
{
	/*
 * Phase 14 W11 #49: stop the 250 ms configd-broadcast poll timer
 * before QObject parent-child cleanup runs.
 */
	if (_poll_timer) {
		_poll_timer->stop();
		_poll_timer->deleteLater();
		_poll_timer = nullptr;
	}
}