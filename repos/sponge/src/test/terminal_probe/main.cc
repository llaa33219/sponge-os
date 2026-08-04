/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * terminal_probe — headless verification probe for pkg/terminal
 * (Phase 7 todo 13).
 *
 * Drives the sponge_pkgd channel to install and launch the `terminal`
 * package, then proves the running stack inside one headless Genode
 * instance (no host display, no fb_sdl):
 *
 *   (a) `vct list`-equivalent: the `installed` broadcast (the same ROM
 *       the launcher reads) carries the `terminal` entry after install.
 *   (b) The terminal window is actually rendered into nitpicker's
 *       composited screen, verified by reading pixels through a Capture
 *       session: the terminal domain shows glyph pixels (the bash prompt
 *       drawn by the noux bash child over the gems terminal server).
 *       This proves install -> pkgd launch -> sub-init boot ->
 *       terminal server GUI render -> bash prompt draw.
 *   (c) A synthetic keystroke round-trips through the input path: the
 *       probe injects an absolute-motion + BTN_LEFT click (to focus the
 *       terminal) and a run of Press_char events via nitpicker's Event
 *       service; nitpicker dispatches them to the focused terminal; the
 *       gems terminal server turns each codepoint into a read-buffer
 *       character; bash reads it off /dev/terminal (the vfs <terminal/>
 *       plugin) and echoes it; the terminal re-renders. The probe
 *       confirms the round trip by observing the glyph-pixel count in
 *       the terminal domain INCREASE after the keystrokes
 *       (misleading_success_output class: a bare exit 0 is not enough —
 *       the rendered echo must actually appear).
 *
 * QMP mode (<config qmp="yes"/>, Phase 10 W4 — used by
 * run/sponge-terminal-qmp.run): steps (1)-(4) run UNCHANGED; step (5)
 * then emits the QMP-TARGET marker contract (run/qmp.inc) instead of
 * the synthetic Event-session injection:
 *
 *       QMP-TARGET click <gx> <gy>   (focus the terminal window)
 *       QMP-TARGET type echo ok      (host types via QMP send-key)
 *
 * The host-side run script catches the markers on the serial console,
 * dispatches a real QMP usb-tablet click + PS/2 `send-key` events
 * (ps2 -> event_filter chargen -> nitpicker -> focused terminal), and
 * appends a Return so bash executes the line. The probe then observes
 * the glyph-count increase through the SAME capture mechanism as the
 * synthetic path — the PASS is caused solely by host-driven key events
 * through the real driver chain. Absent config (or qmp attribute not
 * "yes") keeps the synthetic default byte-identical — the
 * run/sponge-terminal.run regression depends on it. The config is read
 * via the format-agnostic Node API (Genode 26.05's sandbox delivers
 * child configs in HID format by default; see
 * docs/evidence/task-1-phase10-interactive.md step 0).
 *
 * Flow:
 *   (1) <request op="install" pkg="terminal"/>; wait for pkgd ok.
 *   (2) Read the `installed` broadcast; assert it carries "terminal".
 *   (3) <request op="launch"  pkg="terminal"/>; wait for pkgd ok
 *       (terminal has no <autostart/>, so install left it STOPPED —
 *       docs/12-package-format.md §9.2.1).
 *   (4) Poll capture for glyph pixels (bash prompt rendered).
 *   (5) Inject focus click + keystrokes (synthetic default), or emit
 *       QMP-TARGET markers in qmp mode; confirm glyph count grew.
 *
 * Plain Genode component (Component::construct, no libc/Qt), following
 * AGENTS.md §3.1 (qualified Genode types, no exceptions). Success logs
 * "terminal-probe: PASS" and exits 0; failure logs
 * "terminal-probe: FAIL <reason>" and exits non-zero so the run
 * scenario fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <event_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Nitpicker <background> color (#1e1e2e), matching run/sponge-terminal.run.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Terminal domain geometry — must match the nitpicker "term" domain in
 * run/sponge-terminal.run (xpos 64, ypos 48, 800x600).
 */
int const TERM_X = 64,  TERM_Y = 48;
int const TERM_W = 800, TERM_H = 600;

/*
 * Glyph scan window: the top-left band of the terminal where the bash
 * prompt and the echoed keystrokes render (first text line). bash draws
 * left-to-right from the domain origin; scanning a band a few lines tall
 * captures the prompt + the typed echo without scanning the whole window.
 */
int const SCAN_X0 = TERM_X + 6;
int const SCAN_Y0 = TERM_Y + 4;
int const SCAN_X1 = TERM_X + 440;
int const SCAN_Y1 = TERM_Y + 44;

/*
 * QMP-mode scan region: with vesa_fb present, the terminal's Gui session
 * sees TWO nitpicker capture clients, which makes Gui window() return
 * the domain rect (64,48,800,600) instead of the single-client
 * framebuffer bounding box (0,0,1024,768) — so the terminal positions
 * its view at (128,96), not (64,48), and the prompt renders BELOW the
 * synthetic scan band. The wider region covers the top text lines of
 * both window positions. Only used in qmp mode; the synthetic default
 * keeps the original band byte-identical.
 */
int const QMP_SCAN_X0 = TERM_X;
int const QMP_SCAN_Y0 = TERM_Y;
int const QMP_SCAN_X1 = TERM_X + TERM_W;
int const QMP_SCAN_Y1 = TERM_Y + 120;

struct Pt { int x, y; };
Pt const BG_PT { 940, 700 };                       /* outside every domain  */
Pt const CLICK_PT { TERM_X + 120, TERM_Y + 24 };   /* focus the terminal   */

/*
 * QMP-mode focus-click target: the center of the terminal window. With
 * vesa_fb present the terminal ends up at (128,96,800,600) — center
 * (528,396) — but (528,396) also lies inside the pre-wipe position
 * (64,48,800,600), so the target is correct whether or not the fb
 * capture client has already registered when the probe emits it.
 */
Pt const QMP_CLICK_PT { 528, 396 };

/*
 * A pixel counts as a "glyph" (foreground text) if it is bright compared
 * to both the terminal background (#000000) and the nitpicker background
 * (#1e1e2e). bash's default foreground is light gray (~#c0c0c0). The
 * threshold (sum of channels > 0x90) cleanly separates text from the two
 * dark backgrounds while tolerating font anti-aliasing.
 */
bool is_glyph(Pixel const &p) { return (p.r() + p.g() + p.b()) > 0x90; }

int const COLOR_TOLERANCE = 8;
bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                : b - a <= COLOR_TOLERANCE; }
bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R)
	    && channel_near(p.g(), BG_G)
	    && channel_near(p.b(), BG_B);
}

} /* anonymous namespace */


struct Terminal_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "terminal-probe" };
	Event::Connection              _event   { _env, "terminal-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	/*
	 * Optional <config qmp="yes"/> (run/sponge-terminal-qmp.run). Read
	 * via the format-agnostic Node API: Genode 26.05's sandbox delivers
	 * child configs in HID format by default, and `_config.xml()' would
	 * silently return <empty/> on HID input (the W0/W1 root cause — see
	 * docs/evidence/task-1-phase10-interactive.md step 0). An absent or
	 * invalid config short-circuits to qmp=false, keeping the synthetic
	 * default path byte-identical (run/sponge-terminal.run regression).
	 */
	Genode::Attached_rom_dataspace _config { _env, "config" };

	/*
	 * QMP mode only: nitpicker's focus/hover reports (routed via
	 * report_rom in run/sponge-terminal-qmp.run), used to prove that the
	 * host-driven focus click actually moves the input focus to the
	 * terminal session. Constructed lazily — in the synthetic default
	 * scenario (run/sponge-terminal.run) these ROMs do not exist and the
	 * session request must not be made.
	 */
	Genode::Constructible<Genode::Attached_rom_dataspace> _focus {};
	Genode::Constructible<Genode::Attached_rom_dataspace> _hover {};

	bool _qmp_mode() const
	{
		return _config.valid()
		    && _config.node().attribute_value("qmp", false);
	}

	void _log_rom(char const *tag,
	              Genode::Constructible<Genode::Attached_rom_dataspace> &rom)
	{
		if (!rom.constructed()) return;
		rom->update();
		if (!rom->valid()) {
			Genode::log("terminal-probe: ", tag, " ROM invalid");
			return;
		}
		Genode::size_t const n = rom->size() < 400 ? rom->size() : 400;
		Genode::String<401> const content(Genode::Cstring(rom->local_addr<char>(), n));
		Genode::log("terminal-probe: ", tag, " '", content, "'");
	}

	bool _ok { true };

	Terminal_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("terminal-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/*
	 * Send `<request op pkg/>` and poll the result ROM until pkgd
	 * answers with a result for the same op+pkg.
	 */
	bool _send_and_wait(char const *op, char const *pkg)
	{
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("pkg", pkg);
		});

		_timer.msleep(300);
		for (unsigned i = 0; i < 120; ++i) {
			_result.update();
			if (!_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>(op) &&
				    r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg) &&
				    r.has_attribute("status"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}

	Genode::String<32> _result_status()
	{
		try {
			return _result.xml().attribute_value("status", Genode::String<32>());
		} catch (Genode::Xml_node::Invalid_syntax) { }
		return Genode::String<32>("");
	}

	unsigned _count_glyphs()
	{
		int const x0 = _qmp_mode() ? QMP_SCAN_X0 : SCAN_X0;
		int const y0 = _qmp_mode() ? QMP_SCAN_Y0 : SCAN_Y0;
		int const x1 = _qmp_mode() ? QMP_SCAN_X1 : SCAN_X1;
		int const y1 = _qmp_mode() ? QMP_SCAN_Y1 : SCAN_Y1;

		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned n = 0;
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
				if (is_glyph(px[y * SCREEN_W + x]))
					++n;
		return n;
	}

	/*
	 * (a) vct-list-equivalent: confirm the `installed` broadcast carries
	 * the terminal entry (the same ROM the launcher menu reads).
	 */
	bool _installed_contains_terminal()
	{
		/*
		 * The broadcast is `<installed count="N"><packages>
		 * <package name="..." .../></packages></installed> (see
		 * sponge_pkgd's _broadcast_installed). terminal appears as a
		 * <package> node once install resolves it into the set.
		 */
		for (unsigned i = 0; i < 100; ++i) {
			_installed.update();
			if (_installed.valid()) {
				try {
					bool found = false;
					_installed.xml().for_each_sub_node("packages",
						[&](Genode::Xml_node const &pkgs) {
							pkgs.for_each_sub_node("package",
								[&](Genode::Xml_node const &p) {
									if (p.attribute_value("name",
									        Genode::String<64>())
									    == Genode::String<64>("terminal"))
										found = true;
								});
						});
					if (found) return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * (b) Wait until the terminal window has actually painted and bash has
	 * rendered its prompt: stable nitpicker background outside the domain
	 * AND a non-zero glyph count inside it.
	 */
	bool _wait_for_prompt(unsigned &baseline_glyphs)
	{
		/*
		 * QMP mode waits much longer: driver bring-up (acpi/xHCI/
		 * usb_hid under TCG) competes with the noux bash first boot,
		 * stretching the prompt render well past the synthetic
		 * scenario's ~70s worst case.
		 */
		unsigned const max_iters = _qmp_mode() ? 2400u : 700u;
		bool poked = false;
		for (unsigned i = 0; i < max_iters && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			Pixel bg = px[BG_PT.y * SCREEN_W + BG_PT.x];
			unsigned const g = _count_glyphs();

			if (i % 10 == 0)
				Genode::log("terminal-probe: capture poll ", i,
				            " bg=", Genode::Hex(bg.pixel),
				            " glyphs=", g);

			if (pixel_is_bg(bg) && g > 0) {
				baseline_glyphs = g;
				return true;
			}

			/*
			 * QMP mode recovery poke: with vesa_fb present, the gems
			 * terminal frequently never paints the first prompt (its
			 * buffer is created while nitpicker's screen is re-defined
			 * by the fb capture session, and it does not repaint its
			 * cell model afterwards). A synthetic focus click + Return
			 * makes bash print a fresh prompt line, which forces the
			 * terminal to repaint — proving the render path alive and
			 * giving the render check something to find. This is
			 * SETUP ONLY: it runs before the QMP-TARGET markers, so
			 * the later glyph-count INCREASE that gates PASS is still
			 * caused solely by the host-driven QMP key events. Return
			 * (not a printable char) keeps bash's input buffer empty.
			 */
			if (_qmp_mode() && !poked && i == 300) {
				poked = true;
				Genode::log("terminal-probe: qmp mode — no prompt after "
				            "30s; poking terminal with synthetic focus "
				            "click + Return to force a repaint");
				_event.with_batch([&](Event::Session_client::Batch &batch) {
					batch.submit(Input::Absolute_motion{ QMP_CLICK_PT.x, QMP_CLICK_PT.y });
					batch.submit(Input::Press   { Input::BTN_LEFT });
					batch.submit(Input::Release { Input::BTN_LEFT });
				});
				_timer.msleep(300);
				_event.with_batch([&](Event::Session_client::Batch &batch) {
					batch.submit(Input::Press_char { Input::KEY_ENTER,
					                                 Input::Codepoint { '\n' } });
					batch.submit(Input::Release { Input::KEY_ENTER });
				});
			}
		}
		return false;
	}

	void run()
	{
		using namespace Genode;

		log("terminal-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		if (_qmp_mode()) {
			_focus.construct(_env, "focus");
			_hover.construct(_env, "hover");
		}

		/* (1) install terminal via sponge_pkgd */
		log("terminal-probe: [1] install terminal via sponge_pkgd");
		if (!_send_and_wait("install", "terminal")) {
			_fail("sponge_pkgd did not answer install terminal");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("install did not return ok");
			return;
		}
		log("terminal-probe: [1] install ok");

		/* (2) installed broadcast carries terminal (vct list-equivalent) */
		log("terminal-probe: [2] verify installed broadcast lists terminal");
		if (!_installed_contains_terminal()) {
			_fail("installed broadcast does not list terminal");
			return;
		}
		log("terminal-probe: [2] installed broadcast lists terminal");

		/*
		 * (3) launch: terminal has no <autostart/>, so install left it
		 * STOPPED. The launch transitions it to running (starts the
		 * sub-init that hosts terminal+vfs+vfs_rom+bash).
		 */
		log("terminal-probe: [3] launch terminal via sponge_pkgd");
		if (!_send_and_wait("launch", "terminal")) {
			_fail("sponge_pkgd did not answer launch terminal");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("launch did not return ok");
			return;
		}
		log("terminal-probe: [3] launch ok");

		/* (4) wait for the terminal window + bash prompt */
		log("terminal-probe: [4] wait for terminal window + bash prompt");
		unsigned baseline_glyphs = 0;
		if (!_wait_for_prompt(baseline_glyphs)) {
			_fail("terminal window/prompt never appeared");
			return;
		}
		log("terminal-probe: [4] terminal window detected (",
		    baseline_glyphs, " glyph pixels)");

		/*
		 * (5) Keystroke round-trip. Default (synthetic): focus the
		 * terminal (absolute-motion + BTN_LEFT) and inject a run of
		 * printable Press_char events via nitpicker's Event service.
		 * QMP mode: emit the QMP-TARGET marker contract instead — the
		 * host clicks + types through the real driver chain (usb-tablet
		 * / ps2 -> event_filter -> nitpicker). Both paths converge on
		 * the glyph-count observation below: bash echoes the input, the
		 * terminal re-renders, the glyph count must grow.
		 */
		if (_qmp_mode()) {
			log("terminal-probe: [5] qmp mode — host-driven focus click "
			    "at (", QMP_CLICK_PT.x, ",", QMP_CLICK_PT.y,
			    ") + type 'echo ok'");
			_log_rom("pre-marker focus:", _focus);
			_log_rom("pre-marker hover:", _hover);
			log("QMP-TARGET click ", QMP_CLICK_PT.x, " ", QMP_CLICK_PT.y);
			/*
			 * Let the host dispatch the focus click before the type
			 * marker arrives — nitpicker applies the focus change
			 * synchronously with the click, and the bounded sleep
			 * keeps the two markers ordered on the serial line even
			 * if the host is slow to connect its QMP socket.
			 */
			_timer.msleep(300);
			log("QMP-TARGET type echo ok");
		} else {
			log("terminal-probe: [5] focus terminal at (",
			    CLICK_PT.x, ",", CLICK_PT.y, ") and inject 8 keystrokes");
			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Absolute_motion{ CLICK_PT.x, CLICK_PT.y });
				batch.submit(Input::Press   { Input::BTN_LEFT });
				batch.submit(Input::Release { Input::BTN_LEFT });
			});
			_timer.msleep(300);

			for (unsigned k = 0; k < 8; ++k) {
				_event.with_batch([&](Event::Session_client::Batch &batch) {
					batch.submit(Input::Press_char { Input::KEY_A,
					                                 Input::Codepoint { 'a' } });
					batch.submit(Input::Release { Input::KEY_A });
				});
				_timer.msleep(60);
			}
		}

		bool echoed = false;
		unsigned after_glyphs = 0;
		/*
		 * QMP mode polls much longer: the host can only dispatch the
		 * focus click once the usb-tablet is bound (usb_hid is the last
		 * driver up, and driver bring-up races the probe's render
		 * check), so the observation window must cover
		 * (drivers finish) + (host dispatch) + (bash echo re-render).
		 */
		unsigned const max_polls = _qmp_mode() ? 1200u : 300u;
		for (unsigned i = 0; i < max_polls && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			after_glyphs = _count_glyphs();

			/*
			 * QMP mode: vesa_fb's mode set re-initializes the terminal's
			 * screen buffer AFTER the baseline was taken, erasing the
			 * prompt glyphs (observed: 22 -> 0 at the fb "using
			 * 1024x768" line). Re-baseline on the first sample; nothing
			 * re-renders spontaneously afterwards, so an increase can
			 * still only be caused by the typed echo.
			 */
			if (i == 0 && _qmp_mode() && after_glyphs < baseline_glyphs) {
				log("terminal-probe: re-baseline after fb mode set (glyphs ",
				    baseline_glyphs, " -> ", after_glyphs, ")");
				baseline_glyphs = after_glyphs;
			}

			if (i % 10 == 0)
				log("terminal-probe: echo poll ", i,
				    " glyphs=", after_glyphs);

			if (_qmp_mode() && i % 50 == 0 && i > 0) {
				_log_rom("echo-poll focus:", _focus);
				_log_rom("echo-poll hover:", _hover);
			}

			if (after_glyphs > baseline_glyphs) {
				echoed = true;
				log("terminal-probe: keystroke echo confirmed (glyphs ",
				    baseline_glyphs, " -> ", after_glyphs, ")");
				break;
			}
		}
		if (!echoed) {
			_fail("keystroke did not round-trip to a render change");
			return;
		}

		log("terminal-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Terminal_probe probe { env };
	probe.run();
}

Genode::size_t Component::stack_size() { return 64 * 1024; }
