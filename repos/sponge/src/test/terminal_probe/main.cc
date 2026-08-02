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
 * Flow:
 *   (1) <request op="install" pkg="terminal"/>; wait for pkgd ok.
 *   (2) Read the `installed` broadcast; assert it carries "terminal".
 *   (3) <request op="launch"  pkg="terminal"/>; wait for pkgd ok
 *       (terminal has no <autostart/>, so install left it STOPPED —
 *       docs/12-package-format.md §9.2.1).
 *   (4) Poll capture for glyph pixels (bash prompt rendered).
 *   (5) Inject focus click + keystrokes; confirm glyph count grew.
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

struct Pt { int x, y; };
Pt const BG_PT { 940, 700 };                       /* outside every domain  */
Pt const CLICK_PT { TERM_X + 120, TERM_Y + 24 };   /* focus the terminal   */

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
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned n = 0;
		for (int y = SCAN_Y0; y < SCAN_Y1; ++y)
			for (int x = SCAN_X0; x < SCAN_X1; ++x)
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
		for (unsigned i = 0; i < 700 && _ok; ++i) {
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
		 * (5) Keystroke round-trip. Focus the terminal (absolute-motion +
		 * BTN_LEFT), then inject a run of printable Press_char events.
		 * bash echoes each; the glyph count must grow.
		 */
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

		bool echoed = false;
		unsigned after_glyphs = 0;
		for (unsigned i = 0; i < 300 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));
			after_glyphs = _count_glyphs();

			if (i % 10 == 0)
				log("terminal-probe: echo poll ", i,
				    " glyphs=", after_glyphs);

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
