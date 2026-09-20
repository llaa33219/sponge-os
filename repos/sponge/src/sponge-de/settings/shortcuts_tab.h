/* SPDX-License-Identifier: Apache-2.0
 *
 * ShortcutsTab — the *Shortcuts* page of the Settings dialog.
 *
 * Holds the structured `shortcuts.bindings` key (U16.4 / D16.5).
 * Each row of the table is `<action_token>\t<key_sequence>`
 * (the literal TAB separates the two fields; lines separate
 * bindings). The action token is a member of the closed enum
 * {launcher, focus_next, dismiss}. The key sequence is a
 * dash-separated list of synonyms (e.g. "Super", "Alt-Tab",
 * "Escape").
 *
 * The structured value is shown as a table (one row per binding);
 * Add appends a row, Remove deletes the selected row, Apply
 * serializes the table back into the structured value and routes
 * the write through SettingsController.
 *
 * The validator parity for the structured value (D16.5) lives
 * entirely in sponge_configd; the tab is a thin editor that
 * only builds / parses the multi-line value. The
 * `request_set("shortcuts.bindings", ...)` call goes through the
 * SettingsController → sponge_configd → `_shortcuts_valid`
 * validator; any single rejected line rejects the WHOLE write
 * and the controller emits request_failed with the structured
 * error text.
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QWidget>

class QTableWidget;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace Sponge::Sponge_DE {

class SettingsController;


class ShortcutsTab : public QWidget
{
	Q_OBJECT

	public:

		explicit ShortcutsTab(QWidget *parent = nullptr);

		void set_controller(SettingsController *controller);

	private slots:

		void _on_add_row();
		void _on_remove_row();
		void _on_apply();

	private:

		SettingsController *_controller { nullptr };

		QTableWidget *_table          { nullptr };
		QComboBox    *_action_combo   { nullptr };
		QLineEdit    *_sequence_edit  { nullptr };
		QPushButton  *_add_button     { nullptr };
		QPushButton  *_remove_button  { nullptr };
		QPushButton  *_apply_button   { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
