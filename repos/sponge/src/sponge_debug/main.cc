/* SPDX-License-Identifier: Apache-2.0
 *
 * SPONGE DEBUG — the on-desktop diagnostic package.
 *
 * A read-only system monitor: it subscribes to the report_rom relays
 * that carry the launch chain's observable state and renders every
 * update in a scrolling log view. The intended field workflow is:
 *
 *   1. launch SPONGE DEBUG from the launcher (its own window appearing
 *      already proves the pkg_runtime launch path),
 *   2. press "Mark",
 *   3. click a launcher entry,
 *   4. read which reports followed the mark:
 *        launcher_result  — pkgd answered the launch request
 *        installed        — the target entered the running set
 *        window_list      — the window reached the wm
 *
 * A step that never lights up localizes the broken link without any
 * serial console.
 *
 * Subscribed ROMs (relayed by the outer report_rom, wired by the
 * product scenarios):
 *   installed        <- sponge_pkgd -> installed
 *   window_list      <- wm -> window_list
 *   launcher_result  <- sponge_pkgd -> launcher_result
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <libc/component.h>
#include <util/reconstructible.h>

/* Qt includes */
#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <qt6_component/qpa_init.h>

using namespace Genode;

namespace {

/*
 * One watched report: a ROM session plus a signal handler that defers
 * the Qt-side rendering to the Qt event loop (QMetaObject::invokeMethod
 * with Qt::QueuedConnection, the same marshalling sponge-de uses — ROM
 * signals arrive on the Genode entrypoint, widgets may only be touched
 * from the Qt thread).
 */
struct Watch
{
	Attached_rom_dataspace         rom;
	Signal_handler<Watch>          sigh;
	QTextEdit                     &view;
	char const                    *name;

	void _handle()
	{
		rom.update();

		/*
		 * Serial-side trace: makes the subscription flow verifiable in
		 * QEMU run logs without reading pixels.
		 */
		log("sponge_debug: report '", name, "' updated (",
		    rom.valid() ? rom.size() : 0, " bytes)");

		QString const text = rom.valid()
			? QString::fromUtf8(rom.local_addr<char const>(), (int)rom.size())
			: QStringLiteral("<invalid>");

		QMetaObject::invokeMethod(&view, [this, text]() {
			view.append(QStringLiteral("----- ") + name +
			            QStringLiteral(" -----\n") + text +
			            QStringLiteral("\n"));
		}, Qt::QueuedConnection);
	}

	Watch(Genode::Env &env, char const *label, QTextEdit &view)
	:
		rom(env, label), sigh(env.ep(), *this, &Watch::_handle),
		view(view), name(label)
	{
		rom.sigh(sigh);
		_handle();
	}
};

struct Main
{
	Env                    &env;

	QWidget   window;
	QVBoxLayout layout;
	QLabel      status;
	QTextEdit   view;
	QWidget     buttons;
	QHBoxLayout buttons_layout;
	QPushButton mark;
	QPushButton clear;
	unsigned    mark_count = 0;

	Genode::Reconstructible<Watch> installed;
	Genode::Reconstructible<Watch> window_list;
	Genode::Reconstructible<Watch> launcher_result;

	void _mark()
	{
		++mark_count;
		view.append(QStringLiteral("========== MARK %1 ==========").arg(mark_count));
		Genode::log("sponge_debug: MARK ", mark_count);
	}

	Main(Env &env) : env(env),
		installed(env, "installed", view),
		window_list(env, "window_list", view),
		launcher_result(env, "launcher_result", view)
	{
		window.setWindowTitle(QStringLiteral("SPONGE DEBUG"));
		window.resize(700, 480);

		status.setText(QStringLiteral("watching: installed / window_list / launcher_result"));
		status.setWordWrap(true);

		view.setReadOnly(true);
		view.setFont(QFont(QStringLiteral("monospace")));
		view.append(QStringLiteral("SPONGE DEBUG ready — press Mark, then click a launcher entry."));

		mark.setText(QStringLiteral("Mark"));
		clear.setText(QStringLiteral("Clear"));
		buttons_layout.addWidget(&mark);
		buttons_layout.addWidget(&clear);
		buttons_layout.addStretch(1);
		buttons.setLayout(&buttons_layout);

		layout.addWidget(&status);
		layout.addWidget(&view, 1);
		layout.addWidget(&buttons);
		window.setLayout(&layout);

		QObject::connect(&mark, &QPushButton::clicked, [this]() { _mark(); });
		QObject::connect(&clear, &QPushButton::clicked, [this]() {
			view.clear();
			view.append(QStringLiteral("cleared"));
		});

		window.show();
	}
};

} /* anonymous namespace */


void Libc::Component::construct(Libc::Env &env)
{
	Libc::with_libc([&]() {
		qpa_init(env);

		static int argc = 1;
		static char argv0[] = { 's','p','o','n','g','e','_','d','e','b','u','g',0 };
		static char *argv[2] = { argv0, nullptr };

		QApplication app(argc, argv);

		static Main main(env);

		app.exec();
	});
}
