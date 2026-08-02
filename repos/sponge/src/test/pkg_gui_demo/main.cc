/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_gui_demo — minimal Qt6 colored-window package payload (Phase 7
 * todo 8).
 *
 * The GUI counterpart of pkg_hello. It is the smallest possible Qt6
 * Widgets component that paints a distinctive solid-color window, so
 * the runtime-config generator fixes (binary/config/parent-route/caps)
 * can be pixel-verified end to end through a Capture session.
 *
 * The window fills with a high-contrast green (#00ff00) so the probe
 * (test/pkg_gui_probe) can distinguish it from the nitpicker background
 * (#1e1e2e) and from any theme color, with generous slack for softpipe
 * blending.
 *
 * Qt on Genode requires the libc component model
 * (Libc::Component::construct) plus qpa_init() before QApplication.
 */

#include <base/log.h>
#include <libc/component.h>

#include <qt6_component/qpa_init.h>

#include <QApplication>
#include <QPalette>
#include <QWidget>

namespace {

/*
 * Distinctive fill color: pure green. The probe matches RGB(0,255,0)
 * with tolerance; this color appears nowhere else in the headless
 * scenario (nitpicker bg is #1e1e2e, no theme is loaded).
 */
int const DEMO_R = 0x00, DEMO_G = 0xff, DEMO_B = 0x00;

struct Color_window : QWidget
{
	Color_window(QWidget *parent = nullptr) : QWidget(parent)
	{
		setWindowTitle("Sponge Pkg GUI Demo");

		/*
		 * Placement is owned by nitpicker: the run scenario routes
		 * this window's Gui session into a fixed domain. Qt coords
		 * are domain-relative, so (0,0) lands the window on the
		 * domain origin.
		 */
		setGeometry(0, 0, 320, 240);

		QPalette pal = palette();
		pal.setColor(QPalette::Window, QColor(DEMO_R, DEMO_G, DEMO_B));
		setPalette(pal);
		setAutoFillBackground(true);

		Genode::log("pkg_gui_demo: window shown (color #",
		            Genode::Hex(DEMO_R, Genode::Hex::OMIT_PREFIX,
		                        Genode::Hex::PAD),
		            Genode::Hex(DEMO_G, Genode::Hex::OMIT_PREFIX,
		                        Genode::Hex::PAD),
		            Genode::Hex(DEMO_B, Genode::Hex::OMIT_PREFIX,
		                        Genode::Hex::PAD),
		            ")");
	}
};

} /* namespace */


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&] {
		qpa_init(env);

		int argc = 1;
		char const *argv[] = { "pkg_gui_demo", nullptr };
		QApplication app(argc, const_cast<char **>(argv));

		Color_window w;
		w.show();

		exit(app.exec());
	});
}
