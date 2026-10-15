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
 *
 * REPRO KNOBS (config-gated, off unless the scenario sets them)
 *
 *   storm_ms / storm_start_ms / storm_hz / storm_lines / storm_grow
 *
 *   The "rapidly dragging the scrollbar" field report (2026-10) is
 *   reproduced headlessly by run/sponge-debug-scrollstorm.run, which
 *   sets these attributes on the package config. They drive the same
 *   relayout+repaint path a scrollbar fling drives, from the Qt thread,
 *   with no host input injection. With no knob set the component is
 *   bit-for-bit the product behavior. Evidence and rationale:
 *   docs/evidence/sponge-debug-scrollstorm.md
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/node.h>
#include <libc/component.h>
#include <util/reconstructible.h>

/* Qt includes */
#include <QApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTimer>
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
	Genode::Attached_rom_dataspace config { env, "config" };

	QWidget   window;
	QVBoxLayout layout;
	QLabel      status;
	QTextEdit   view;
	QWidget     buttons;
	QHBoxLayout buttons_layout;
	QPushButton mark;
	QPushButton clear;
	unsigned    mark_count = 0;

	/*
	 * Repaint-storm instrumentation (config-gated, OFF by default).
	 *
	 * Field report: rapidly dragging the scrollbar in this window until
	 * "text loads fast" freezes the system. run/sponge-debug-scrollstorm.run
	 * reproduces that load headlessly by driving the SAME repaint path
	 * from the Qt thread: a QTimer jumps the viewport between the top and
	 * the bottom of the document at storm_hz, which is precisely what a
	 * scrollbar fling produces (QScrollBar's own drag handler calls
	 * QAbstractSlider::setValue on every motion event) minus the input
	 * decode. Optionally each tick also appends one log line
	 * (storm_grow=1), reproducing the "text loads fast" half of the
	 * report.
	 *
	 * No knob set => the timer is never started and the component behaves
	 * exactly as the product build does. The knobs are a test surface,
	 * not a product feature; see docs/evidence/sponge-debug-scrollstorm.md.
	 */
	unsigned      storm_ms       { 0 };
	unsigned      storm_start_ms { 0 };
	unsigned      storm_hz       { 30 };
	unsigned      storm_lines    { 2000 };
	bool          storm_grow     { false };
	unsigned      storm_ticks    { 0 };
	unsigned      storm_ticks_max { 0 };
	QTimer        storm_timer;
	QTimer        storm_start_timer;
	QElapsedTimer storm_clock;

	Genode::Reconstructible<Watch> installed;
	Genode::Reconstructible<Watch> window_list;
	Genode::Reconstructible<Watch> launcher_result;

	/*
	 * Config read through Genode::Node so an XML and a HID delivery of
	 * the same document both work (the sandbox serves child configs in
	 * HID by default since 26.05 — same reasoning as sponge-de's
	 * main.cc:186-214).
	 */
	void _read_config()
	{
		config.update();
		if (!config.valid() || config.size() == 0)
			return;

		Genode::Node const cfg(Genode::Const_byte_range_ptr(
			config.local_addr<char const>(), config.size()));

		storm_ms       = cfg.attribute_value("storm_ms",       storm_ms);
		storm_start_ms = cfg.attribute_value("storm_start_ms", storm_start_ms);
		storm_hz       = cfg.attribute_value("storm_hz",       storm_hz);
		storm_lines    = cfg.attribute_value("storm_lines",    storm_lines);
		unsigned grow { 0 };
		grow = cfg.attribute_value("storm_grow", grow);
		storm_grow = (grow != 0);

		if (storm_ms == 0) {
			log("sponge_debug: no storm requested (storm_ms=0) — "
			    "product behavior");
			return;
		}
		if (storm_hz == 0)
			storm_hz = 30;
		storm_ticks_max = (storm_ms * storm_hz) / 1000;
		if (storm_ticks_max == 0)
			storm_ticks_max = 1;

		/*
		 * A long document is a precondition for a scroll storm to
		 * mean anything: with a viewport-sized document a scroll
		 * cannot move the scroll value and no relayout happens.
		 * storm_lines seeds the log the field tool accumulates over a
		 * session.
		 */
		if (storm_lines > 0) {
			view.append(QStringLiteral("----- storm seed -----"));
			for (unsigned i = 0; i < storm_lines; i++)
				view.append(QStringLiteral("seed line %1 "
				                           "0000000000000000000000000000")
				                .arg(i));
		}

		log("sponge_debug: STORM armed hz=", storm_hz,
		    " lines=", storm_lines, " grow=", storm_grow,
		    " budget_ms=", storm_ms, " ticks=", storm_ticks_max,
		    " start_in_ms=", storm_start_ms);
	}

	void _storm_tick()
	{
		QScrollBar *bar = view.verticalScrollBar();
		if (!bar) {
			log("sponge_debug: STORM abort — no vertical scroll bar");
			storm_timer.stop();
			return;
		}

		/*
		 * Fling: alternate between the two ends of the document.
		 * Each jump forces a full relayout + a full viewport repaint,
		 * which is the per-frame cost a scrollbar drag produces.
		 */
		bar->setValue(bar->value() > bar->maximum() / 2
		              ? bar->minimum() : bar->maximum());

		if (storm_grow) {
			/*
			 * The "text loads fast" half: one appended report per
			 * tick, the same append() call the report subscription
			 * below performs.
			 */
			view.append(QStringLiteral("----- storm tick %1 -----")
			            .arg(storm_ticks));
		}

		/*
		 * The storm stops on WHICHEVER comes first: the wall-clock
		 * budget or the tick budget. Both are needed — the tick budget
		 * alone never fires when the event loop is starved (which is
		 * the thing being measured), and the wall-clock budget alone
		 * makes the tick count non-reproducible. The closing log line
		 * carries ticks AND elapsed, so the achieved repaint rate is
		 * recoverable: that rate IS the result.
		 */
		++storm_ticks;
		qint64 const elapsed = storm_clock.elapsed();
		if (elapsed >= storm_ms || storm_ticks >= storm_ticks_max) {
			storm_timer.stop();
			log("sponge_debug: STORM done ticks=", storm_ticks,
			    " elapsed_ms=", elapsed,
			    " achieved_hz=", elapsed > 0
			        ? (storm_ticks * 1000) / elapsed : 0);
		}
	}

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

		/*
		 * Storm timer — started LAST, on the Qt thread, only when the
		 * scenario asked for it (see the struct comment).
		 */
		_read_config();
		if (storm_ms > 0) {
			storm_timer.setInterval(1000 / storm_hz);
			QObject::connect(&storm_timer, &QTimer::timeout,
			                 [this]() { _storm_tick(); });

			/*
			 * storm_start_ms exists so a measurement scenario can put
			 * the storm strictly INSIDE its sampling window: the
			 * probe takes its baseline after its own settle period, and
			 * a storm that starts at boot would spend most of its
			 * budget inside that settle period and never be measured.
			 */
			if (storm_start_ms > 0) {
				storm_start_timer.setSingleShot(true);
				QObject::connect(&storm_start_timer, &QTimer::timeout,
				                 [this]() {
						storm_clock.start();
						storm_timer.start();
					});
				storm_start_timer.start(storm_start_ms);
				log("sponge_debug: STORM starts in ", storm_start_ms, " ms");
			} else {
				storm_clock.start();
				storm_timer.start();
			}
		}
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
