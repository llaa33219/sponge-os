/* SPDX-License-Identifier: Apache-2.0
 *
 * DefaultsTab — the *Defaults* page of the Settings dialog.
 *
 * Holds the single "Reset to baked defaults" action. The reset
 * path is implemented in `sponge_configd/main.cc` (the W3 sentinel
 * work): a write of `bake.applied=no` triggers a re-seed of the
 * baked keys (panel.height, panel.visible_widgets, clock.format,
 * launcher.sort_by, theme.active) — leaving the user's panel.position,
 * panel.<id>.*, background.color/image, leitzentrale.enabled, and
 * shortcuts.bindings untouched (only baked keys + theme.active are
 * re-seeded per `sponge_configd/README.md` "Baked defaults" contract).
 *
 * The tab's button writes `bake.applied=no` via the SettingsController
 * (the same path vct's `vct bake reset` uses, per the W3 sentinel
 * contract). On confirm, the controller emits request_succeeded;
 * the broadcast regenerates and the live keys re-apply without a
 * reboot.
 */

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace Sponge::Sponge_DE {

class SettingsController;


class DefaultsTab : public QWidget
{
	Q_OBJECT

	public:

		explicit DefaultsTab(QWidget *parent = nullptr);

		void set_controller(SettingsController *controller);

	private slots:

		void _on_reset();

	private:

		SettingsController *_controller { nullptr };

		QLabel      *_description { nullptr };
		QPushButton *_reset       { nullptr };
};

}  /* namespace Sponge::Sponge_DE */
