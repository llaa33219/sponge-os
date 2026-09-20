/* SPDX-License-Identifier: Apache-2.0
 *
 * PanelCollection — Phase 16 W8 (U16.5 / D16.5).
 *
 * Owns N PanelWidget instances keyed by a per-instance id
 * (e.g. "default", "alpha", "beta"). Each instance has a distinct
 * Gui session label suffix (the F5 nitpicker label_prefix trap
 * defense) and a unique per-panel click_count Expanding_reporter
 * (label "panel_<id>", node type "panel").
 *
 * Subscribes to the configd `panel.ids` broadcast (comma-list of
 * active ids). On every add / remove the collection creates /
 * destroys the corresponding PanelWidget (and its per-panel
 * reporter). The default panel id "default" is ALWAYS present;
 * `panel.ids=""` (empty) leaves only the default panel visible,
 * which keeps every regression scenario byte-compatible.
 *
 * Wires the per-id configd keys to the matching widget: the
 * ConfigController emits
 *
 *   panel_height_changed_for(QString id, unsigned h)
 *   panel_position_changed_for(QString id, QString position)
 *   panel_visible_widgets_changed_for(QString id, QString list)
 *
 * for each <key name="panel.<id>.{height,position,visible_widgets}"/>
 * in the broadcast. The collection routes those to the matching
 * PanelWidget's apply* slots (matching by id).
 *
 * RAM budget per U16.5: ~128 MiB Qt6 instance weight per panel;
 * the multi-panel scenario runs at -m 4G (two panels + wm +
 * layouter + decorator + configd + themed + pkgd fit comfortably).
 */

#pragma once

#include <base/component.h>
#include <base/heap.h>
#include <os/reporter.h>
#include <util/reconstructible.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <unordered_map>

#include "panel_widget.h"

class QTimer;

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }
class ConfigController;
class LauncherMenuView;
class SettingsController;
class TasklistWidget;

/*
 * Read `<panels source="collection"/>` from the component config.
 * Mirrors the existing activation gates (config_asks_for_configd,
 * de_config_asks_for_controller, etc.). Absent the gate, the
 * PanelCollection stays in single-panel mode (one "default"
 * PanelWidget; no per-id apply slots; no per-panel reporters).
 *
 * Returns true ONLY when explicitly opted in. Absent (or any other
 * value) leaves the legacy single-panel mode active. Regression
 * scenarios that do not opt in keep the Phase 14 / W5 byte-
 * compatible behavior.
 */
bool panels_asks_for_collection(Genode::Env &env);


class PanelCollection : public QObject
{
	Q_OBJECT

	public:

		/*
		 * The collection is constructed BEFORE the panel widgets
		 * (so the very first applyConfig() can fan out). The Env
		 * reference is required for the per-panel click_count
		 * Expanding_reporter construction.
		 *
		 * `theme` is the initial theme; it is captured by the
		 * widgets at construction time and reused on every restyle.
		 *
		 * `settings` is the SettingsController used by the per-
		 * panel context menu's Add panel / Remove panel entries.
		 * May be nullptr in regression scenarios that wire no
		 * DE-side writer.
		 *
		 * `legacy_panels` is the vector of pre-existing PanelWidget
		 * pointers (Phase 14 / W5 regression scenarios construct
		 * these directly with role=Singleton / Top / Bottom). When
		 * panels_asks_for_collection(env) is false, the collection
		 * is bypassed entirely and the legacy widgets are used
		 * directly by main.cc — this method's body in that case
		 * never runs.
		 */
		PanelCollection(Genode::Env &env, Theme::Theme const &theme,
		                SettingsController *settings,
		                QObject *parent = nullptr);
		~PanelCollection() override;

		/*
		 * Attach the ConfigController so the per-id apply slots
		 * are routed to the matching PanelWidget instance. The
		 * connection is established here (not at construction)
		 * because ConfigController is constructed BEFORE the
		 * collection but the signal-slot wiring must happen AFTER
		 * the collection's widgets exist.
		 */
		void attach_config_controller(ConfigController *config);

		/*
		 * Apply the `panel.ids` comma-list value. Creates any new
		 * PanelWidget instances (the corresponding per-id keys
		 * were already instantiated by the configd pattern-key
		 * mechanism). Destroys widgets whose id is no longer in
		 * the list. The "default" id is special: it is ALWAYS
		 * present even when absent from the comma-list (the
		 * backward-compat path).
		 *
		 * GUI thread ONLY.
		 */
		void applyPanelIds(QString comma_list);

		/*
		 * Apply per-id configd key values. Each parameter is a
		 * complete (id, value) pair; the collection finds the
		 * matching widget (or the "default" widget when the
		 * id is empty, the W5 backward-compat path) and forwards
		 * to its apply* slot. Unknown ids (no widget, no
		 * default) are ignored — the configd pattern-key
		 * instantiation order means the per-id widget exists
		 * before the per-id apply reaches it.
		 *
		 * GUI thread ONLY.
		 */
		void applyHeightFor(QString id, unsigned h);
		void applyPositionFor(QString id, QString position);
		void applyVisibleWidgetsFor(QString id, QString list);

		/*
		 * Look up a panel widget by id. Returns nullptr if no
		 * widget matches (the caller can decide whether to fall
		 * back to the default widget).
		 */
		PanelWidget *panel_for_id(QString const &id) const;

		/*
		 * The current set of panel ids. Always contains at least
		 * "default" (the backward-compat path). The SettingsController
		 * uses this to populate Add panel / Remove panel menu
		 * entries.
		 */
		QStringList panel_ids() const;

		/*
		 * Inject a launcher view (shared across all panels — the
		 * same LauncherMenuView instance opens on any panel's
		 * launcher-toggle click). Optional; legacy scenarios that
		 * do not opt into the collection bypass this entirely.
		 */
		void set_launcher_view(LauncherMenuView *view);

		/*
		 * Inject a tasklist widget. The widget is attached to
		 * the default panel (the W7 regression path). Multi-panel
		 * scenarios that opt into the collection share the same
		 * tasklist widget on the default panel; per-panel tasklist
		 * attachment is Phase 17+ scope.
		 */
		void attach_tasklist(TasklistWidget *widget);

	signals:

		/*
		 * Emitted on the GUI thread when a panel is added (via
		 * applyPanelIds). Carries the new widget pointer so
		 * main.cc can wire additional context (launcher view,
		 * settings controller, ...). The QObject::connect must
		 * use Qt::QueuedConnection because the slot may live on
		 * a different QObject thread context.
		 */
		void panel_added(QString id, PanelWidget *widget);

		/*
		 * Emitted on the GUI thread when a panel is removed.
		 * After this signal returns, the pointer is invalid
		 * (the widget has been destructed).
		 */
		void panel_removed(QString id);

	private:

		struct Entry
		{
			Entry() = default;
			Entry(Entry &&) noexcept = default;
			Entry &operator=(Entry &&) noexcept = default;

			/*
			 * The widget pointer (heap-allocated because
			 * PanelWidget is not movable and the QHash entry may
			 * be reallocated on insert/remove).
			 */
			Genode::Constructible<PanelWidget> widget { };

			/*
			 * The per-panel click_count Expanding_reporter.
			 * Heap-allocated alongside the widget. The widget
			 * gets a pointer via set_click_reporter() after
			 * construction.
			 */
			Genode::Constructible<Genode::Expanding_reporter> reporter { };
		};

		Genode::Env &_env;
		Theme::Theme const &_theme;
		SettingsController *_settings { nullptr };
		LauncherMenuView *_launcher_view { nullptr };

		/*
		 * The collection of panel widgets, keyed by id. The
		 * "default" id is ALWAYS present (constructed in the
		 * constructor body). All entries are heap-allocated via
		 * std::unique_ptr because QHash cannot store non-movable
		 * values, and Constructible<T> is non-movable (it owns
		 * the T's storage in-place via placement new).
		 */
		std::unordered_map<QString, std::unique_ptr<Entry>> _panels;

		/*
		 * The default panel pointer (always present, lives in
		 * _panels). Cached for fast lookup; nullptr means
		 * "uninitialized" (a programmer error — the constructor
		 * creates it eagerly).
		 */
		PanelWidget *_default_panel { nullptr };

		void _create_panel(QString const &id);
		void _destroy_panel(QString const &id);
		PanelWidget *_find_or_default(QString const &id) const;
		QStringList _parse_csv(QString const &csv) const;
};

}  /* namespace Sponge::Sponge_DE */