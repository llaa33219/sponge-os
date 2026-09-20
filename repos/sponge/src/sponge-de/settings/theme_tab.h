/* SPDX-License-Identifier: Apache-2.0
 *
 * ThemeTab — the *Theme* page of the Settings dialog (D16.3).
 *
 * Holds the live `theme.active` key (sponge_themed resolves the
 * active theme name). The shipped set is the four W3 themes
 * (default / light / dark / compact); the scenario may stage
 * additional ones via the existing `theme.<name>` ROM route and
 * the combo box surfaces them automatically (the directory
 * listing is hardcoded as `{"default","light","dark","compact"}`
 * for W4 — D16.3 explicitly excludes dynamic-theme-enumeration
 * work, which lives in Phase 17+ if the user adds themes after
 * first boot).
 */

#pragma once

#include <QWidget>

class QComboBox;
class QPushButton;

namespace Sponge::Sponge_DE {

class SettingsController;


class ThemeTab : public QWidget
{
	Q_OBJECT

	public:

		explicit ThemeTab(QWidget *parent = nullptr);

		void set_controller(SettingsController *controller);

	private slots:

		void _on_apply();

	private:

		SettingsController *_controller { nullptr };

		QComboBox   *_combo  { nullptr };
		QPushButton *_apply  { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
