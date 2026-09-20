/* SPDX-License-Identifier: Apache-2.0
 *
 * BackgroundController — configd broadcast bridge for the in-DE
 * BackgroundWidget (Phase 16 W6).
 *
 * Mirrors the W5 ConfigController pattern (the read-side
 * configd-broadcast consumer in config/config_controller.{h,cc}).
 * Subscribes to the same `configd` ROM session the panel uses
 * (relayed by report_rom from sponge_configd's `config` report)
 * and extracts the two W6 keys: background.color + background.image.
 * Each is emitted as a Qt signal that BackgroundWidget consumes
 * on the GUI thread via QMetaObject::invokeMethod marshaling.
 *
 * Activation gate: `<background source="controller"/>` in the
 * component config (same shape as W4's `<de_config source="controller"/>`
 * and W5's `<panel_bottom source="dual"/>`). Absent the gate, the
 * controller falls back to no-op (no channels opened, no widgets
 * constructed, no errors raised). Scenarios without the gate boot
 * unchanged.
 *
 * The controller opens NO additional sessions — it reuses the
 * existing ConfigController's `configd` ROM session via a
 * shared pointer to its parent (ConfigController). This keeps
 * the W6 additions AGENTS.md §1.2 minimum-privilege clean: the
 * BackgroundController is a Qt-side observer, not a fresh session
 * consumer.
 *
 * Thread model: the controller is constructed on the GUI thread.
 * The actual signal emission happens in applyBackgroundColor /
 * applyBackgroundImage slots on the GUI thread (marshalled from
 * ConfigController::applyConfig's applyConfig invocation). No new
 * thread is introduced.
 */

#pragma once

#include <QObject>
#include <QString>

namespace Sponge::Sponge_DE {

class BackgroundController : public QObject
{
	Q_OBJECT

	public:

		explicit BackgroundController(QObject *parent = nullptr);

	signals:

		void background_color_changed(QString color);
		void background_image_changed(QString path);

	public slots:

		void applyBackgroundColor(QString color);
		void applyBackgroundImage(QString path);

	private:

		QString _last_color;
		QString _last_image;
};

}  /* namespace Sponge::Sponge_DE */