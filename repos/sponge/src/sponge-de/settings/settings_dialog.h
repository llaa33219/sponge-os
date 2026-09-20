/* SPDX-License-Identifier: Apache-2.0
 *
 * SettingsDialog — first-party Sponge DE Settings app (Phase 16 W4,
 * D16.3). Lazy-loaded by SettingsController::open_settings_dialog()
 * on the first click of the panel context menu's *Settings* entry
 * (W5 wires the panel-side trigger; today the entry point is the
 * SettingsController slot itself).
 *
 * The dialog is a top-level modal QDialog with five tabs:
 *
 *   Panel     — height spinbox, position radio (top/bottom enabled,
 *               left/right disabled per D16.2 honest-disclosure
 *               rule), visible_widgets checkboxes (clock, launcher,
 *               tasklist).
 *   Theme     — active theme combo box (the four shipped themes +
 *               any future staged ones).
 *   Background — color hex line edit + image combobox fed by the
 *               background.image allowlist.
 *   Shortcuts — table of action<tab>sequence pairs (the structured
 *               shortcuts.bindings editor), with Add / Remove
 *               row buttons.
 *   Defaults  — "Reset to baked defaults" button (writes
 *               bake.applied=no via the W3 sentinel mechanism; the
 *               configd reset path re-seeds only the baked keys
 *               + theme.active).
 *
 * Every write routes through the SettingsController's
 * `request_set(key, value)` slot, which carries the D16.9 validator
 * parity with vct. The dialog itself never opens a write channel
 * directly — every key reaches configd through the controller's
 * shared backend client.
 *
 * === Failure surfacing ===
 *
 * The dialog connects SettingsController's `request_failed(key,
 * error)` signal to `on_request_failed(key, error)` (a QMessageBox).
 * Every per-tab failure is shown; the dialog never silently drops
 * a write. Phase 16 F2.
 *
 * === JSON knob (D16.3) ===
 *
 * The dialog's `accept()` emits the per-session `session_writes`
 * JSON listing every key written in the session. The regression
 * scenario's accept-button invocation byte-matches the JSON shape
 * — proving every per-tab write reached configd. The probe-side
 * JSON parser is minimal: a list of `{key, value, status}` triples
 * keyed by the order of writes.
 *
 * The dialog is exec()'d synchronously (modal). The per-tab
 * writes happen during exec() (Apply click → request_set →
 * request_failed/request_succeeded → QMessageBox on error). The
 * session JSON is built up by `record_write(key, value)` calls on
 * every successful write and emitted on `accept()`.
 *
 * === Wiring contract ===
 *
 * The dialog holds:
 *   - SettingsController *_controller: back-pointer to the
 *     controller that owns the write side. The controller is
 *     never owned by the dialog (lifetime == the controller's
 *     lifetime == sponge-de's lifetime).
 *   - Per-tab widgets, each responsible for ONE key's UI.
 *
 * The SettingsController connects the dialog's controller set +
 * the request_failed / request_succeeded signals — see
 * settings_controller.cc.
 */

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QTabWidget;
class QDialogButtonBox;

namespace Sponge::Sponge_DE {

class SettingsController;

/*
 * Forward-declared tab classes. They each manage their own UI and
 * own their own per-key apply signal-to-slot wiring. SettingsDialog
 * constructs them in the constructor and shows them inside a
 * QTabWidget. The tabs are not owned externally (the dialog takes
 * ownership via Qt's parent-child mechanism).
 */
class PanelTab;
class ThemeTab;
class BackgroundTab;
class ShortcutsTab;
class DefaultsTab;


class SettingsDialog : public QDialog
{
	Q_OBJECT

	public:

		SettingsDialog(QWidget *parent = nullptr);

		/*
		 * Wire the controller back-pointer. Called by
		 * SettingsController::open_settings_dialog() right after
		 * construction. Per-tab signals route writes through the
		 * controller's `request_set(key, value)` slot.
		 */
		void set_controller(SettingsController *controller);

		/*
		 * Per-session JSON knob (D16.3). Returns a JSON document
		 * listing every key written in this dialog session, in
		 * write order. The regression scenario's probe compares
		 * this string byte-for-byte against an expected golden.
		 *
		 * Shape:
		 *
		 *   {"writes": [{"key": "...", "value": "..."}, ...]}
		 *
		 * The "writes" list grows on every successful controller
		 * write (request_succeeded handler appends) and is reset
		 * to empty on every reject — a reject does NOT append a
		 * phantom write.
		 */
		QString session_writes_json() const { return _session_writes_json; }

	public slots:

		/*
		 * Connected to SettingsController::request_failed.
		 * Surfaces the structured error in a QMessageBox.
		 */
		void on_request_failed(QString key, QString error);

		/*
		 * Connected to SettingsController::request_succeeded.
		 * Appends (key, value) to the per-session JSON knob.
		 */
		void on_request_succeeded(QString key, QString value);

	private:

		SettingsController *_controller { nullptr };

		QTabWidget        *_tabs       { nullptr };
		QDialogButtonBox  *_button_box { nullptr };

		PanelTab      *_panel_tab      { nullptr };
		ThemeTab      *_theme_tab      { nullptr };
		BackgroundTab *_background_tab { nullptr };
		ShortcutsTab  *_shortcuts_tab  { nullptr };
		DefaultsTab   *_defaults_tab   { nullptr };

		/*
		 * The session_writes JSON knob. Built up by
		 * on_request_succeeded; emitted via session_writes_json()
		 * on the dialog's accept(). The empty state is the literal
		 * `{"writes": []}`; an accept with zero writes returns
		 * that string (the regression scenario gates on a non-empty
		 * list — the W4 channel round-trip write must produce at
		 * least one entry).
		 */
		QString _session_writes_json { QStringLiteral("{\"writes\": []}") };
};

}  /* namespace Sponge::Sponge_DE */
