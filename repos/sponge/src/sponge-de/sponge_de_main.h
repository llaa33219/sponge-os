/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Demo window widget for Sponge DE.
 *
 * Phase 3: proves that Qt rendering and input work on top of Genode's
 * nitpicker. The window is styled by the loaded theme and reports the
 * theme values it received, so a visual check also verifies the theme
 * pipeline. The Genode::Env reference is kept for the "input" Report
 * session (input-path introspection) and future window-management glue.
 */

#pragma once

#include <base/component.h>
#include <os/reporter.h>

#include <QPoint>
#include <QWidget>

namespace Sponge::Sponge_DE {

namespace Theme { struct Theme; }

class Main : public QWidget
{
	Q_OBJECT

	public:

		Main(Genode::Env &env, Theme::Theme const &theme, QWidget *parent = nullptr);

	protected:

		/*
		 * Window-level mouse handling (catches presses on the window
		 * background; presses on child widgets are observed via the
		 * application event filter below).
		 */
		void mousePressEvent(QMouseEvent *e) override;

		/*
		 * Observes every input event delivered anywhere in the
		 * application, including presses reaching child widgets such as
		 * the demo button. This is the genuine, spec-compliant way to
		 * expose the full input path as a diagnosable Report.
		 */
		bool eventFilter(QObject *watched, QEvent *event) override;

	private:

		Genode::Env &_env;

		/*
		 * Report session (label "input"). Each received pointer press
		 * updates the report with the event type and position, e.g.
		 * "<input press="x,y"/>". Used by the headless verification
		 * probe and, more generally, as an input-path introspection
		 * point for window management and debugging.
		 */
		Genode::Reporter _input_report;

		void _report_press(QPoint pos);
};

}  /* namespace Sponge::Sponge_DE */
