/* SPDX-License-Identifier: Apache-2.0
 *
 * PanelTab — the *Panel* page of the Settings dialog (D16.3).
 *
 * Holds the three W2 live panel keys:
 *
 *   panel.height            — QSpinBox (uint [16..128], default 28).
 *   panel.position          — QButtonGroup of 4 radio buttons:
 *                             top / bottom (enabled), left / right
 *                             (DISABLED with the Phase-17+ tooltip
 *                             per D16.2 honest-disclosure rule).
 *   panel.visible_widgets   — three QCheckBox (clock, launcher,
 *                             tasklist) joined into a comma-list
 *                             on Apply.
 *
 * Every Apply (per-control Enter or per-tab "Apply" button)
 * routes the matching write through SettingsController's
 * `request_set(key, value)`. The dialog itself never opens a
 * write channel directly — D16.9 validator parity enforced via
 * controller.
 */

#pragma once

#include <QWidget>

class QSpinBox;
class QLineEdit;
class QCheckBox;
class QButtonGroup;
class QPushButton;

namespace Sponge::Sponge_DE {

class SettingsController;


class PanelTab : public QWidget
{
	Q_OBJECT

	public:

		explicit PanelTab(QWidget *parent = nullptr);

		void set_controller(SettingsController *controller);

	private slots:

		void _on_apply_height(int v);
		void _on_apply_position(int id);
		void _on_apply_visible_widgets();

	private:

		SettingsController *_controller { nullptr };

		QSpinBox     *_height_spin   { nullptr };
		QButtonGroup *_position_grp  { nullptr };
		QCheckBox    *_clock_chk     { nullptr };
		QCheckBox    *_launcher_chk  { nullptr };
		QCheckBox    *_tasklist_chk  { nullptr };
		QPushButton  *_apply_widgets { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
