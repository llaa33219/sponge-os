/* SPDX-License-Identifier: Apache-2.0
 *
 * SettingsDialog implementation. See settings_dialog.h.
 */

#include "settings_dialog.h"

#include "panel_tab.h"
#include "theme_tab.h"
#include "background_tab.h"
#include "shortcuts_tab.h"
#include "defaults_tab.h"

#include "../config/settings_controller.h"

#include <QDialogButtonBox>
#include <QMessageBox>
#include <QTabWidget>
#include <QVBoxLayout>

using namespace Sponge::Sponge_DE;


SettingsDialog::SettingsDialog(QWidget *parent)
:
	QDialog(parent)
{
	setWindowTitle(QStringLiteral("Sponge DE Settings"));
	resize(640, 480);

	auto *root = new QVBoxLayout(this);

	_tabs = new QTabWidget(this);

	_panel_tab      = new PanelTab(this);
	_theme_tab      = new ThemeTab(this);
	_background_tab = new BackgroundTab(this);
	/*
	 * Default shipped background image allowlist is the single
	 * entry `/system/background/default.png`. W6 will read the
	 * `<vfs><allowlist>` config block to widen it; today's tab
	 * uses the compiled-in fallback.
	 */
	_background_tab->set_image_allowlist({});
	_shortcuts_tab  = new ShortcutsTab(this);
	_defaults_tab   = new DefaultsTab(this);

	_tabs->addTab(_panel_tab,      QStringLiteral("Panel"));
	_tabs->addTab(_theme_tab,      QStringLiteral("Theme"));
	_tabs->addTab(_background_tab, QStringLiteral("Background"));
	_tabs->addTab(_shortcuts_tab,  QStringLiteral("Shortcuts"));
	_tabs->addTab(_defaults_tab,   QStringLiteral("Defaults"));

	root->addWidget(_tabs);

	_button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
	                                    this);
	root->addWidget(_button_box);

	/*
	 * Standard OK / Cancel semantics. The per-tab Apply buttons
	 * commit individual writes; OK closes the dialog; Cancel
	 * discards any open uncommitted tab edits but does NOT
	 * revert already-Applied writes (each Apply is its own
	 * configd commit; the user-set value is durable from the
	 * moment configd accepts it).
	 */
	connect(_button_box, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}


void SettingsDialog::set_controller(SettingsController *controller)
{
	_controller = controller;
	if (_panel_tab)      _panel_tab->set_controller(_controller);
	if (_theme_tab)      _theme_tab->set_controller(_controller);
	if (_background_tab) _background_tab->set_controller(_controller);
	if (_shortcuts_tab)  _shortcuts_tab->set_controller(_controller);
	if (_defaults_tab)   _defaults_tab->set_controller(_controller);
}


void SettingsDialog::on_request_failed(QString key, QString error)
{
	QMessageBox::warning(this,
	                     QStringLiteral("Settings — configd rejected write"),
	                     QStringLiteral("'%1':\n\n%2").arg(key, error));
}


void SettingsDialog::on_request_succeeded(QString key, QString value)
{
	/*
	 * Append (key, value) to the session_writes JSON knob
	 * (D16.3). Naive escape: replace `\` with `\\` and `"` with
	 * `\"` in both fields. The values are user-typed so they
	 * could contain either character; minimal escaping keeps
	 * the output JSON-parseable.
	 */
	auto escape = [](QString const &s) -> QString {
		QString out;
		for (QChar ch : s) {
			if      (ch == QLatin1Char('\\')) out += QStringLiteral("\\\\");
			else if (ch == QLatin1Char('"'))  out += QStringLiteral("\\\"");
			else                              out += ch;
		}
		return out;
	};

	/*
	 * Strip the trailing `]`, splice in the new entry, re-tail
	 * the array. The JSON shape is invariant (`{"writes": [...]}`).
	 */
	QString const prefix = QStringLiteral("{\"writes\": [");
	QString const suffix = QStringLiteral("]}");
	QString mid = _session_writes_json;
	if (mid.startsWith(prefix) && mid.endsWith(suffix)) {
		mid = mid.mid(prefix.length(), mid.length() - prefix.length() - suffix.length());
	}
	QString const entry = QStringLiteral("%1{\"key\":\"%2\",\"value\":\"%3\"}")
	    .arg(mid.isEmpty() ? QString() : QStringLiteral(","))
	    .arg(escape(key))
	    .arg(escape(value));
	QString const new_mid = mid + entry;
	_session_writes_json = prefix + new_mid + suffix;
}
