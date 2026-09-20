/* SPDX-License-Identifier: Apache-2.0
 *
 * PanelCollection implementation — Phase 16 W8 (U16.5 / D16.5).
 *
 * The collection owns N PanelWidget instances keyed by a per-
 * instance id. The "default" panel is always present (the
 * backward-compat path for the regression scenarios that boot
 * with `panel.ids=""` empty). On every `panel.ids` broadcast the
 * collection creates / destroys the corresponding entries.
 *
 * Per-panel reporters (W8 F5 defense) are constructed alongside
 * each widget and shared with the widget via set_click_reporter.
 * The reporter's label is "panel_<id>" (the F5 trap defense: two
 * same-label Report sessions would collide on the single-writer
 * rule from AGENTS.md §1.2).
 */

#include "panel_collection.h"

#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_node.h>

#include "config/config_controller.h"
#include "config/settings_controller.h"
#include "launcher/launcher_menu_view.h"
#include "panel/panel_widget.h"
#include "panel/tasklist_widget.h"
#include "theme/theme_loader.h"

#include <utility>

#include <QGuiApplication>
#include <QWidget>

using namespace Sponge::Sponge_DE;


namespace {

/*
 * Parse the <panels source="..."/> gate from the child config.
 * Mirrors the HID+XML dual-parser pattern used by config_asks_for
 * _configd (config_controller.cc:37) and de_config_asks_for_
 * controller (settings_controller.cc:32).
 */
bool parse_panels_asks_for_collection(Genode::Attached_rom_dataspace &config)
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
			root.for_each_sub_node("panels", [&](Genode::Xml_node const &n) {
				if (!live)
					live = n.attribute_value("source",
					         Genode::String<32>()) ==
					       Genode::String<32>("collection");
			});
		}
		catch (Genode::Xml_node::Invalid_syntax) { }
	} else {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		root.for_each_sub_node([&](Genode::Hid_node const &n) {
			if (!live && n.has_type("panels"))
				live = n.attribute_value("source",
				         Genode::String<32>()) ==
				       Genode::String<32>("collection");
		});
	}

	return live;
}

}  /* namespace */


bool Sponge::Sponge_DE::panels_asks_for_collection(Genode::Env &env)
{
	Genode::Attached_rom_dataspace config(env, "config");
	return parse_panels_asks_for_collection(config);
}


/*
 * Validate a single character against the per-id charset
 * `[a-z0-9_-]{1,16}`. Same charset as the configd pattern-key
 * registry (sponge_configd/main.cc, the W2 charset check). We
 * duplicate the check here because the GUI-side panel-add UI
 * generates the id client-side and must NOT rely on the daemon
 * to reject (the validator parity contract D16.9 expects the
 * GUI to never produce an invalid id; the configd reject is the
 * defense-in-depth fallback).
 */
static bool _id_char_ok(QChar c)
{
	QChar::Category cat = c.category();
	(void)cat;
	char const ascii = c.toLatin1();
	if (ascii >= 'a' && ascii <= 'z') return true;
	if (ascii >= '0' && ascii <= '9') return true;
	if (ascii == '_' || ascii == '-') return true;
	return false;
}


PanelCollection::PanelCollection(Genode::Env &env, Theme::Theme const &theme,
                                 SettingsController *settings, QObject *parent)
:
	QObject(parent),
	_env(env),
	_theme(theme),
	_settings(settings)
{
	/*
	 * The default panel is created lazily on the first applyPanelIds
	 * call that lists it (or that has no ids at all). For the
	 * backward-compat path (`panel.ids=""`), the default panel is
	 * the only widget shown. For multi-panel mode (`panel.ids=
	 * alpha,beta`), the default panel is NOT created — its absence
	 * keeps the per-id panels from being hidden under a Singleton
	 * default widget at y=0 (which would mask the panel_alpha
	 * top-edge from the W8 F5 cross-panel click assertion).
	 */
}


PanelCollection::~PanelCollection()
{
	/*
	 * Destroys every per-panel widget + reporter in the
	 * collection. The unique_ptr destructors free the Entry
	 * objects, whose Constructible destructors call the QObject
	 * and Genode::Expanding_reporter destructors in the right
	 * order.
	 */
	_panels.clear();
}


void PanelCollection::attach_config_controller(ConfigController *config)
{
	if (!config) return;

	/*
	 * Route the flat panel.* keys to the default panel. The
	 * default panel is the legacy single-panel regression path
	 * — its writes use the flat keys (the W5 panel-config
	 * scenarios never write `panel.<id>.*` keys, they write
	 * `panel.height` etc.).
	 */
	if (_default_panel) {
		QObject::connect(config, &ConfigController::panel_height_changed,
		                 _default_panel, &PanelWidget::applyHeight);
		QObject::connect(config, &ConfigController::panel_visible_widgets_changed,
		                 _default_panel, &PanelWidget::applyVisibleWidgets);
		QObject::connect(config, &ConfigController::panel_position_changed,
		                 _default_panel, &PanelWidget::applyPosition);
		QObject::connect(config, &ConfigController::clock_format_changed,
		                 _default_panel, &PanelWidget::applyClockFormat);
	}

	/*
	 * Route the per-id keys to the matching widget. The
	 * ConfigController parses `panel.<id>.{height,position,
	 * visible_widgets}` entries on every broadcast and emits the
	 * per-id signal carrying the id + value. The collection
	 * forwards to the matching widget's apply* slot.
	 *
	 * The per-id signal fan-out uses lambdas because
	 * QObject::connect cannot bind a function pointer to a
	 * method that takes a runtime id (Qt's signal/slot type
	 * system needs a known signature). The lambda routes
	 * through PanelCollection's per-id fan-out methods.
	 */
	QObject::connect(config, &ConfigController::panel_height_changed_for,
	                 this, &PanelCollection::applyHeightFor);
	QObject::connect(config, &ConfigController::panel_position_changed_for,
	                 this, &PanelCollection::applyPositionFor);
	QObject::connect(config, &ConfigController::panel_visible_widgets_changed_for,
	                 this, &PanelCollection::applyVisibleWidgetsFor);
}


void PanelCollection::set_launcher_view(LauncherMenuView *view)
{
	_launcher_view = view;
	for (auto const &kv : _panels)
		if (kv.second && kv.second->widget.constructed())
			kv.second->widget->set_launcher_view(view);
}


void PanelCollection::attach_tasklist(TasklistWidget *widget)
{
	if (_default_panel && widget)
		_default_panel->attach_tasklist(widget);
}


PanelWidget *PanelCollection::panel_for_id(QString const &id) const
{
	auto it = _panels.find(id);
	if (it != _panels.end() && it->second && it->second->widget.constructed())
		return &*it->second->widget;
	return nullptr;
}


QStringList PanelCollection::panel_ids() const
{
	QStringList out;
	out.reserve(_panels.size());
	for (auto const &kv : _panels)
		out << kv.first;
	return out;
}


QStringList PanelCollection::_parse_csv(QString const &csv) const
{
	QStringList out;
	QString current;
	for (QChar c : csv) {
		if (c == QLatin1Char(',') || c == QLatin1Char(';')) {
			QString const tok = current.trimmed();
			if (!tok.isEmpty()) out << tok;
			current.clear();
		} else {
			current.append(c);
		}
	}
	QString const tok = current.trimmed();
	if (!tok.isEmpty()) out << tok;

	/*
	 * Validate each id against the charset (the configd
	 * pattern-key charset `[a-z0-9_-]{1,16}`). Invalid ids are
	 * dropped client-side — the configd defense-in-depth
	 * validator will also reject them, but the GUI-side check
	 * keeps the collection in a consistent state.
	 */
	QStringList filtered;
	for (QString const &id : out) {
		if (id.isEmpty() || id.length() > 16) continue;
		bool ok = true;
		for (QChar c : id) {
			if (!_id_char_ok(c)) { ok = false; break; }
		}
		if (ok) filtered << id;
	}
	return filtered;
}


void PanelCollection::_create_panel(QString const &id)
{
	if (_panels.count(id) > 0) return;

	auto entry = std::make_unique<Entry>();

	/*
	 * Per-panel Expanding_reporter: label "panel_<id>", node
	 * type "panel". Heap-allocated so it survives QHash
	 * reallocation. The widget gets a pointer via
	 * set_click_reporter. Expanding_reporter is implicitly
	 * enabled at construction time.
	 */
	QString const label = QStringLiteral("panel_") + id;
	entry->reporter.construct(_env, "panel", label.toUtf8().constData());

	/*
	 * The widget. Window title doubles as the Gui session label
	 * (F5 defense). For the default id the title is "Sponge
	 * Panel" (the legacy title); for explicit ids the title is
	 * "Sponge Panel <id>".
	 */
	QString const title = (id == PanelWidget::default_id())
	    ? QStringLiteral("Sponge Panel")
	    : (QStringLiteral("Sponge Panel ") + id);

	entry->widget.construct(_theme, id, title);
	entry->widget->set_click_reporter(&*entry->reporter);
	if (_settings) entry->widget->set_settings_controller(_settings);
	if (_launcher_view) entry->widget->set_launcher_view(_launcher_view);

	PanelWidget *widget_ptr = &*entry->widget;

	_panels[id] = std::move(entry);

	if (id == PanelWidget::default_id())
		_default_panel = widget_ptr;

	Genode::log("sponge-de: panel collection: created id=", id.toUtf8().constData());
	emit panel_added(id, widget_ptr);
}


void PanelCollection::_destroy_panel(QString const &id)
{
	if (_panels.count(id) == 0) return;

	/*
	 * Phase 16 W8 (U16.5 / D16.5): the "default" id CAN be
	 * destroyed in multi-panel mode (panel.ids != ""). In
	 * backward-compat mode (panel.ids == ""), applyPanelIds only
	 * ever wants the default, so the destroy loop never picks it.
	 * The legacy regression scenarios don't use the
	 * PanelCollection at all (the `<panels source="collection"/>`
	 * gate is absent), so this code path doesn't fire there.
	 */

	auto it = _panels.find(id);
	if (it == _panels.end()) return;

	emit panel_removed(id);

	/*
	 * The unique_ptr's destructor frees the Entry; the Entry's
	 * Constructible destructors run first via the ~Entry()
	 * chain, destroying the widget + reporter in the right
	 * order.
	 */
	_panels.erase(it);

	Genode::log("sponge-de: panel collection: destroyed id=", id.toUtf8().constData());
}


PanelWidget *PanelCollection::_find_or_default(QString const &id) const
{
	if (auto *p = panel_for_id(id)) return p;
	return _default_panel;
}


void PanelCollection::applyPanelIds(QString comma_list)
{
	QStringList const new_ids = _parse_csv(comma_list);

	/*
	 * The wanted set:
	 *
	 *   panel.ids=""          -> just the default panel (legacy
	 *                            single-panel mode).
	 *   panel.ids="alpha,beta" -> alpha + beta only (the default
	 *                            is NOT created in multi-panel
	 *                            mode; it would mask the top-edge
	 *                            panel_alpha with a Singleton
	 *                            default widget at y=0, breaking
	 *                            the W8 F5 cross-panel click
	 *                            assertion).
	 *
	 * The "always default" rule holds for the empty case (the
	 * backward-compat path); once explicit ids are listed, the
	 * default is removed.
	 */
	QStringList wanted;
	if (new_ids.isEmpty()) {
		wanted << PanelWidget::default_id();
	} else {
		wanted = new_ids;
	}

	/*
	 * Create any missing widgets.
	 */
	for (QString const &id : wanted)
		if (!_panels.count(id))
			_create_panel(id);

	/*
	 * Destroy any widgets no longer in the wanted set (the
	 * default is destroyable in multi-panel mode — only the
	 * legacy default is created via _create_panel in the
	 * constructor; this applyPanelIds path may either keep or
	 * destroy it).
	 */
	QStringList to_remove;
	for (auto it = _panels.begin(); it != _panels.end(); ++it)
		if (!wanted.contains(it->first))
			to_remove << it->first;
	for (QString const &id : to_remove)
		_destroy_panel(id);

	Genode::log("sponge-de: panel collection: ids now ",
	            panel_ids().join(",").toUtf8().constData());
}


void PanelCollection::applyHeightFor(QString id, unsigned h)
{
	if (auto *p = _find_or_default(id))
		p->applyHeight(h);
}


void PanelCollection::applyPositionFor(QString id, QString position)
{
	if (auto *p = _find_or_default(id)) {
		/*
		 * Phase 16 W8 — derive the PanelWidget Role from the
		 * per-id position. Each panel's role determines its screen
		 * y coordinate (Top → y=0, Bottom → y=740, Singleton →
		 * y=0). Without this mapping all panels overlap at y=0
		 * (both alpha and beta would land on the top edge — the
		 * F5 click trap signature).
		 */
		if (position == QStringLiteral("top"))
			p->set_role(PanelWidget::Role::Top);
		else if (position == QStringLiteral("bottom")
		      || position == QStringLiteral("left")
		      || position == QStringLiteral("right"))
			p->set_role(PanelWidget::Role::Bottom);
		p->applyPosition(position);
	}
}


void PanelCollection::applyVisibleWidgetsFor(QString id, QString list)
{
	if (auto *p = _find_or_default(id))
		p->applyVisibleWidgets(list);
}