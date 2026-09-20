/* SPDX-License-Identifier: Apache-2.0
 *
 * BackgroundTab — the *Background* page of the Settings dialog.
 *
 * Holds the two W2 flat keys:
 *
 *   background.color   — hex line edit ("#RRGGBB" regex-validated
 *                        by the W2 configd validator; default
 *                        "#1e1e2e").
 *   background.image   — QComboBox populated from the
 *                        background.image allowlist. The shipped
 *                        default allowlist is `[ "/system/background/
 *                        default.png" ]` (one entry, per
 *                        `sponge_configd/README.md` "Background
 *                        key" row); the W6 background widget reads
 *                        the active image and paints it. The
 *                        allowlist is shipped via the DE-side
 *                        component config (`<vfs>` + `<allowlist>`
 *                        block; outside W4 scope but the
 *                        background image key is exposed here so
 *                        the tab is functional when W6 lands).
 *
 * Today (W4) the Background tab exposes the controls but the
 * background widget that PAINTS the image lands in W6. The
 * SettingsController writes the keys regardless — the broadcasts
 * carry the values, and Phase 17 work (or follow-on) reads them.
 * The W4 pass conditions only require that the writes round-trip
 * through configd; the visual rendering is the W6 deliverable.
 */

#pragma once

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QPushButton;

namespace Sponge::Sponge_DE {

class SettingsController;


class BackgroundTab : public QWidget
{
	Q_OBJECT

	public:

		explicit BackgroundTab(QWidget *parent = nullptr);

		void set_controller(SettingsController *controller);

		/*
		 * Inject the background.image allowlist from the
		 * component config. Default allowlist is the single
		 * shipped path `/system/background/default.png`; W6
		 * carries the
		 * `<vfs><allowlist>` config block to override the
		 * list at deployment time.
		 */
		void set_image_allowlist(QStringList const &paths);

	private slots:

		void _on_apply_color();
		void _on_apply_image();

	private:

		SettingsController *_controller { nullptr };

		QLineEdit   *_color_edit { nullptr };
		QComboBox   *_image_combo { nullptr };
		QPushButton *_color_apply { nullptr };
		QPushButton *_image_apply { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
