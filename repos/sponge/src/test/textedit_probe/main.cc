/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * textedit_probe — headless verification probe for pkg/textedit
 * (Phase 7 todo 14).
 *
 * Drives the sponge_pkgd channel to install and launch the `textedit`
 * package (the depot-repackaged qt6_textedit), then proves inside one
 * headless Genode instance (no host display, no fb_sdl) that the Qt6
 * text-editor window actually rendered into nitpicker's composited
 * screen — read back through a Capture session.
 *
 * Flow:
 *   (1) <request op="install" pkg="textedit"/>; wait for pkgd ok.
 *   (2) Read the `installed` broadcast; assert it carries "textedit"
 *       with running="no" (the vct list-equivalent check — the same
 *       ROM the launcher menu reads; textedit has no <autostart/> so
 *       install left it STOPPED, docs/12 §9.2.1).
 *   (3) <request op="launch" pkg="textedit"/>; wait for pkgd ok.
 *   (4) Read the broadcast again; assert textedit is now running="yes".
 *   (5) Poll the capture buffer for the textedit window:
 *       the qt6_textedit widget renders a complex scene (menu bar +
 *       tool bar + rich-text area) that is everywhere distinct from
 *       the nitpicker background (#1e1e2e). Require that a substantial
 *       fraction of the sampled pixels inside the textedit domain are
 *       non-background — proves the Qt widget actually painted, not
 *       just an empty buffer (misleading_success_output class).
 *
 *   (6) Error-path assertions (the pkgd-level "clear error, not a
 *       crash" check):
 *       - <request op="launch" pkg="nosuchpkg-14"> → status="not-installed"
 *       - a second <request op="launch" pkg="textedit"> → status="already-running"
 *       The full missing-binary failure channel (textedit binary not
 *       staged as a boot module) is verified by a scratch scenario
 *       variant — see run/sponge-textedit-fail.run — which fails by
 *       bounded run_genode_until timeout, never a silent hang.
 *
 *   (8)/(9) QMP mode only (<config qmp="yes"/>, Phase 10 criterion 5b,
 *       run/sponge-textedit-qmp.run): emit QMP-TARGET markers so the
 *       host drives a focus click + types "hello" via QMP through the
 *       real ps2 -> event_filter -> nitpicker chain (the pointer is
 *       positioned by closed-loop relative moves — QEMU's absolute
 *       pointer delivery is broken on this host), and verify the
 *       document region changes beyond the cursor-blink baseline. A
 *       native-delivery bisect phase ([8a]: the probe's own Gui view
 *       receives the click + keys directly) cross-checks the chain
 *       without Qt in the path. Default behavior (no qmp attribute) is
 *       unchanged.
 *
 * Plain Genode component (Component::construct, no libc/Qt), following
 * AGENTS.md §3.1 (qualified Genode types, no exceptions). Success logs
 * "textedit-probe: PASS" and exits 0; any failure logs
 * "textedit-probe: FAIL <reason>" and exits non-zero so the run
 * scenario fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <gui_session/connection.h>
#include <input_session/client.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Nitpicker <background> color (#1e1e2e), matching run/sponge-textedit.run
 * and every other headless scenario in the suite.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Textedit window footprint. The upstream qt6_textedit main.cpp does
 *   mw.resize(availableGeometry.width() / 2,
 *             (availableGeometry.height() * 2) / 3);
 *   mw.move((availableGeometry.width()  - mw.width())  / 2,
 *           (availableGeometry.height() - mw.height()) / 2);
 * against QGenodeScreen::geometry(), which reports the full nitpicker
 * panorama (1024x768) — see qgenodescreen.h. So mw.resize(512, 512)
 * and mw.move(256, 128). With the edit domain origin at (0,0)
 * (see run/sponge-textedit.run for why), the window lands at screen
 * coords (256, 128)-(768, 640). The probe samples that footprint for
 * the rendered-fraction check, and uses a BG_PT outside it.
 */
int const ED_X = 256, ED_Y = 128;
int const ED_W = 512, ED_H = 512;

/*
 * Sample grid for the rendered-window check. Sampling every 8th pixel
 * in each direction covers the textedit footprint densely enough to
 * distinguish a real Qt-rendered scene (menu bar / toolbar / rich
 * text) from an empty or uninitialized buffer, without spending
 * capture time on a full per-pixel scan.
 */
int const SAMPLE_STRIDE = 8;

/*
 * Rendered-window threshold: at least this fraction of sampled pixels
 * must be non-background. A fully-rendered qt6_textedit window fills
 * nearly 100% of its footprint (light gray menu bar, white text area,
 * formatted HTML content); 50% leaves generous slack for softpipe
 * blending, anti-aliasing borders, and partial occlusion while still
 * being unambiguously non-empty (an empty/uninitialized nitpicker
 * buffer is typically all-zero black -> 0% non-bg).
 */
float const RENDERED_THRESHOLD = 0.50f;

/*
 * Color-diversity floor: count distinct 4-bit-per-channel color
 * buckets (4096 total). A real qt6_textedit scene draws many colors
 * (menu bar gray, toolbar icons, rich-text fonts, scrollbar, status
 * bar); an empty or single-color buffer draws one. Requiring >=16
 * distinct buckets guards against the misleading_success_output
 * failure mode where a bare exit-0 passes on a stale or solid-color
 * buffer that happens to be "non-bg" everywhere.
 */
unsigned const DIVERSITY_FLOOR = 16;
unsigned const COLOR_BUCKET_R_SHIFT = 4;  /* top 4 bits of each channel */
unsigned const COLOR_BUCKET_G_SHIFT = 4;
unsigned const COLOR_BUCKET_B_SHIFT = 4;

int const COLOR_TOLERANCE = 8;
bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                : b - a <= COLOR_TOLERANCE; }

/*
 * QMP keyboard mode (Phase 10, criterion 5b). With <config qmp="yes"/>
 * the probe, after the unchanged checks (1)-(7), requests a host-driven
 * focus click + the typed string "hello" via QMP-TARGET markers and
 * verifies through Capture that the document region changes beyond the
 * cursor-blink baseline. Without the config attribute the probe behaves
 * exactly as before (sponge-textedit.run regression depends on it).
 *
 * Document (text) region: the rich-text area of the textedit window,
 * i.e. the window footprint minus ~80 px of menu bar / tool bar chrome
 * at the top. Both the blinking cursor and the typed text render here.
 */
int const DOC_X = ED_X;
int const DOC_Y = ED_Y + 80;
int const DOC_W = ED_W;
int const DOC_H = ED_H - 80;

/*
 * Focus/cursor click target: the document-area center in global screen
 * coords (512, 424). The probe drives the pointer there EXACTLY via
 * closed-loop relative moves (QEMU's absolute-pointer path is broken on
 * this host — see _goto_pos), so no landing-offset tolerance is needed;
 * (512,424) sits well inside the text area (y 208..640).
 */
int const DOC_CLICK_X = ED_X + ED_W / 2;   /* 512 */
int const DOC_CLICK_Y = DOC_Y + DOC_H / 2; /* 424 */

/*
 * Number of stride-grid sample points covering the document region
 * ((512/8) * (432/8) = 64*54). The typed-text delta is counted over
 * these points.
 */
enum { DOC_SAMPLES = (DOC_W / SAMPLE_STRIDE) * (DOC_H / SAMPLE_STRIDE) };

/*
 * Absolute floor for the typed-text delta. The pass condition is
 * `typed_delta > 2 * baseline_delta`; on a run where the cursor blink
 * falls between the two baseline samples (baseline_delta == 0) that
 * alone would pass on a single noisy pixel. Requiring a handful of
 * changed sample points keeps a blink-only or noise-only frame from
 * passing (misleading_success_output class). "hello" (~5 glyphs)
 * changes an order of magnitude more.
 */
unsigned const TYPED_FLOOR = 6;
bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R)
	    && channel_near(p.g(), BG_G)
	    && channel_near(p.b(), BG_B);
}

unsigned pixel_bucket(Pixel const &p)
{
	return ((unsigned)p.r() >> COLOR_BUCKET_R_SHIFT) << 8
	     | ((unsigned)p.g() >> COLOR_BUCKET_G_SHIFT) << 4
	     | ((unsigned)p.b() >> COLOR_BUCKET_B_SHIFT);
}

} /* anonymous namespace */


struct Textedit_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "textedit-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	/*
	 * Config ROM for the QMP keyboard mode switch. Read via the
	 * format-agnostic Node API (init's sandbox delivers child configs
	 * in HID format by default since Genode 26.05; `.xml()' would
	 * silently fall back to defaults — the W0/W1 root cause, see
	 * docs/evidence/task-1-phase10-interactive.md). With no config ROM
	 * delivered (run/sponge-textedit.run) `valid()' is false and the
	 * probe takes the byte-identical default path.
	 */
	Genode::Attached_rom_dataspace _config { _env, "config" };

	/* document-region snapshot for the blink-baseline / typed delta */
	unsigned _doc_snap[DOC_SAMPLES] { };

	/*
	 * QMP-mode diagnostics (constructed lazily — only when qmp="yes" —
	 * so the default scenario never requests these ROMs): nitpicker's
	 * focus/clicked reports, relayed via report_rom. They make the
	 * focus-click outcome directly observable (which session, if any,
	 * nitpicker focused after the QMP click).
	 */
	Genode::Constructible<Genode::Attached_rom_dataspace> _focus_rom   {};
	Genode::Constructible<Genode::Attached_rom_dataspace> _clicked_rom {};
	Genode::Constructible<Genode::Attached_rom_dataspace> _hover_rom   {};
	Genode::Constructible<Genode::Attached_rom_dataspace> _pointer_rom {};
	Genode::Constructible<Genode::Attached_rom_dataspace> _keystate_rom {};

	/*
	 * Native-delivery bisect state (qmp mode only): the probe opens its
	 * own tiny Gui view, clicks it into focus, and counts the input
	 * events IT receives — a native Genode client with no Qt in the
	 * path. Distinguishes "nitpicker never delivers to the session"
	 * from "Qt drops the events".
	 */
	Genode::Constructible<Gui::Connection>                 _gui     {};
	Genode::Constructible<Genode::Attached_dataspace>      _gui_fb  {};
	unsigned _bisect_key_presses { 0 };
	unsigned _bisect_btn_presses { 0 };
	unsigned _bisect_other       { 0 };

	void _drain_input()
	{
		_gui->input.for_each_event([&] (Input::Event const &ev) {
			bool counted = false;
			ev.handle_press([&] (Input::Keycode k, Input::Codepoint) {
				counted = true;
				if (k >= Input::BTN_LEFT && k <= Input::BTN_TASK)
					_bisect_btn_presses++;
				else
					_bisect_key_presses++;
			});
			ev.handle_release([&] (Input::Keycode) { counted = true; });
			if (!counted) _bisect_other++;
		});
	}

	bool _ok { true };

	Textedit_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("textedit-probe: FAIL ", reason);
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

	/*
	 * Read the `installed` broadcast (the same ROM the launcher reads)
	 * and return the running="yes"|"no" attribute for `name`, or the
	 * empty string if `name` is absent. The broadcast schema (see
	 * sponge_pkgd's _generate_installed_report) is:
	 *   <installed count="N">
	 *     <packages>
	 *       <package name="..." version="..." running="..." category="..."/>
	 *       ...
	 *     </packages>
	 *   </installed>
	 */
	Genode::String<16> _installed_running(char const *name)
	{
		for (unsigned i = 0; i < 100; ++i) {
			_installed.update();
			if (_installed.valid()) {
				try {
					Genode::String<16> out { };
					_installed.xml().for_each_sub_node("packages",
						[&](Genode::Xml_node const &pkgs) {
							pkgs.for_each_sub_node("package",
								[&](Genode::Xml_node const &p) {
									if (p.attribute_value("name",
									        Genode::String<64>())
									    == Genode::String<64>(name))
										out = p.attribute_value("running",
										        Genode::String<16>());
								});
						});
					if (out.length() > 0)
						return out;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return Genode::String<16>("");
	}

	/*
	 * Sample the textedit footprint on a stride grid and produce two
	 * signals: the fraction (0..1) of pixels that are NOT the
	 * nitpicker background, and the count of distinct 4-bit-per-
	 * channel color buckets. Both together cleanly distinguish a real
	 * Qt render (high non-bg fraction AND high diversity) from an
	 * empty/uninitialized buffer (~0% non-bg, 1 bucket) or a stale
	 * solid-color frame (possibly high non-bg fraction but 1 bucket).
	 */
	void _sample_window(float &out_frac, unsigned &out_buckets)
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned total = 0, nonbg = 0;

		/*
		 * 4096-bit occupancy map for the 4096 possible 12-bit color
		 * buckets. Stack-allocated (512 bytes); zeroed each call.
		 * Using a fixed-size bitset avoids heap allocation in the
		 * probe's hot path.
		 */
		static unsigned const NWORDS = 4096 / 32;
		unsigned occ[NWORDS] { };

		for (int y = ED_Y; y < ED_Y + ED_H; y += SAMPLE_STRIDE) {
			for (int x = ED_X; x < ED_X + ED_W; x += SAMPLE_STRIDE) {
				++total;
				Pixel const &p = px[y * SCREEN_W + x];
				if (!pixel_is_bg(p))
					++nonbg;
				unsigned const b = pixel_bucket(p);
				occ[b >> 5] |= (1u << (b & 31));
			}
		}

		unsigned buckets = 0;
		for (unsigned i = 0; i < NWORDS; ++i)
			buckets += __builtin_popcount(occ[i]);

		out_frac = total ? (float)nonbg / (float)total : 0.0f;
		out_buckets = buckets;
	}

	/*
	 * (5) Poll capture until the textedit window has actually painted:
	 * rendered-fraction AND color-diversity both above threshold.
	 * Together they guard against every misleading_success_output
	 * failure mode: an empty buffer (low frac), a stale buffer (low
	 * diversity), or a partially-rendered frame (one or both below
	 * threshold). The poll count is bounded (1500 * 100ms = 150s,
	 * well inside the run scenario's 420s timeout — the
	 * hung_or_long_commands class).
	 */
	bool _wait_for_window()
	{
		for (unsigned i = 0; i < 1500 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			float frac = 0.0f;
			unsigned buckets = 0;
			_sample_window(frac, buckets);

			if (i % 10 == 0)
				Genode::log("textedit-probe: capture poll ", i,
				            " rendered_frac=", (unsigned)(frac * 100), "%",
				            " color_buckets=", buckets);

			if (frac >= RENDERED_THRESHOLD && buckets >= DIVERSITY_FLOOR) {
				Genode::log("textedit-probe: textedit window detected (",
				            (unsigned)(frac * 100), "% non-bg, ",
				            buckets, " distinct color buckets)");
				return true;
			}
		}
		return false;
	}

	/*
	 * Record the document region's 12-bit color buckets on the stride
	 * grid into `_doc_snap' (baseline / pre-type reference).
	 */
	void _doc_snapshot()
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned i = 0;
		for (int y = DOC_Y; y < DOC_Y + DOC_H; y += SAMPLE_STRIDE)
			for (int x = DOC_X; x < DOC_X + DOC_W; x += SAMPLE_STRIDE)
				_doc_snap[i++] = pixel_bucket(px[y * SCREEN_W + x]);
	}

	/* Count of sampled document-region points changed vs `_doc_snap'. */
	unsigned _doc_delta() const
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned i = 0, changed = 0;
		for (int y = DOC_Y; y < DOC_Y + DOC_H; y += SAMPLE_STRIDE) {
			for (int x = DOC_X; x < DOC_X + DOC_W; x += SAMPLE_STRIDE) {
				if (pixel_bucket(px[y * SCREEN_W + x]) != _doc_snap[i])
					++changed;
				++i;
			}
		}
		return changed;
	}

	/* Log a nitpicker report ROM's raw content (qmp-mode diagnosis). */
	void _log_report_rom(char const *what, Genode::Attached_rom_dataspace &rom)
	{
		using namespace Genode;

		rom.update();
		if (!rom.valid()) {
			log("textedit-probe: ", what, " report ROM unavailable");
			return;
		}
		char const *raw = rom.local_addr<char>();
		Genode::size_t const n = rom.size() < 200 ? rom.size() : 200;
		char buf[201];
		Genode::memcpy(buf, raw, n);
		buf[n] = 0;
		log("textedit-probe: ", what, " report: ", (char const *)buf);
	}

	/*
	 * Number of currently-held keys at nitpicker (HID report, read via
	 * the format-agnostic Node API), or -1 if unavailable. Sampled
	 * while the host types: any value > 0 proves QMP send-key events
	 * traverse ps2 -> event_filter -> nitpicker.
	 */
	long _keystate_count()
	{
		_keystate_rom->update();
		if (!_keystate_rom->valid()) return -1;
		return _keystate_rom->node().attribute_value("count", -1L);
	}

	void _log_all_reports(char const *phase)
	{
		Genode::log("textedit-probe: ", phase, " nitpicker reports:");
		_log_report_rom("focus",    *_focus_rom);
		_log_report_rom("clicked",  *_clicked_rom);
		_log_report_rom("hover",    *_hover_rom);
		_log_report_rom("pointer",  *_pointer_rom);
		_log_report_rom("keystate", *_keystate_rom);
	}

	/*
	 * (8)/(9) QMP keyboard mode (Phase 10 criterion 5b): prove that
	 * host-driven QMP `send-key' input reaches the focused textedit
	 * window through the real chain (ps2 -> event_filter chargen ->
	 * nitpicker focus -> Qt key event -> QTextEdit document).
	 *
	 * Choreography with the run script (run/qmp.inc):
	 *   - emit `QMP-TARGET click <gx> <gy>' at the document-area
	 *     center (nitpicker `focus: click' focuses the edit domain and
	 *     Qt places the text cursor),
	 *   - sample the document region twice ~one blink phase apart to
	 *     measure the cursor-blink baseline delta,
	 *   - emit `QMP-TARGET type hello' (plain lowercase letters — the
	 *     qmp_type mapped subset),
	 *   - poll Capture until the region's delta vs the pre-type
	 *     snapshot exceeds 2x the blink baseline (plus an absolute
	 *     floor) — a blink alone must never PASS.
	 */
	/*
	 * Current pointer position from nitpicker's `pointer' report ROM
	 * (HID format, read via the format-agnostic Node API). Returns
	 * false while the pointer is materialized nowhere (no motion yet).
	 */
	bool _pointer_pos(long &x, long &y)
	{
		_pointer_rom->update();
		if (!_pointer_rom->valid()) return false;
		Genode::Node const n = _pointer_rom->node();
		x = n.attribute_value("xpos", -1L);
		y = n.attribute_value("ypos", -1L);
		return x >= 0 && y >= 0;
	}

	/*
	 * Closed-loop pointer positioning via PS/2 RELATIVE moves. QEMU's
	 * absolute-pointer delivery under -nographic is broken on this host
	 * (QEMU 11.0.2: untargeted abs events are reinterpreted by the PS/2
	 * relative mouse, slamming the pointer to the (1023,767) corner;
	 * device-targeted events crash QEMU outright — see
	 * docs/evidence/task-5-phase10-interactive.md). Relative motion, in
	 * contrast, is delivered deterministically. So the probe reads the
	 * actual pointer position from nitpicker's pointer report and emits
	 * `QMP-TARGET move <dx> <dy>' markers (per-axis step clamped to the
	 * PS/2 protocol range) until the pointer is within tolerance of the
	 * target — self-correcting for any residual acceleration scaling.
	 * Bounded, fails loud.
	 */
	bool _goto_pos(long tx, long ty)
	{
		using namespace Genode;

		for (unsigned i = 0; i < 30; ++i) {
			long px = -1, py = -1;
			if (!_pointer_pos(px, py)) {
				/* not materialized: nudge and retry */
				log("QMP-TARGET move 20 20");
				_timer.msleep(300);
				continue;
			}
			long dx = tx - px, dy = ty - py;
			if (dx >= -3 && dx <= 3 && dy >= -3 && dy <= 3) {
				log("textedit-probe: pointer reached (", px, ",", py,
				    ") ~ target (", tx, ",", ty, ")");
				return true;
			}
			dx = dx > 100 ? 100 : (dx < -100 ? -100 : dx);
			dy = dy > 100 ? 100 : (dy < -100 ? -100 : dy);
			log("QMP-TARGET move ", dx, " ", dy);
			_timer.msleep(300);
		}
		return false;
	}

	void _qmp_keyboard_check()
	{
		using namespace Genode;

		if (!_focus_rom.constructed())    _focus_rom.construct(_env, "focus");
		if (!_clicked_rom.constructed())  _clicked_rom.construct(_env, "clicked");
		if (!_hover_rom.constructed())    _hover_rom.construct(_env, "hover");
		if (!_pointer_rom.constructed())  _pointer_rom.construct(_env, "pointer");
		if (!_keystate_rom.constructed()) _keystate_rom.construct(_env, "keystate");

		/*
		 * Materialize the pointer: one absolute move. Under the broken
		 * -nographic abs translation this deterministically slams the
		 * pointer into the (1023,767) corner — a KNOWN position the
		 * closed loop then starts from.
		 */
		log("QMP-TARGET absmove 512 384");
		_timer.msleep(500);

		/*
		 * Phase [8a] — native-delivery bisect: the probe opens its own
		 * 64x64 Gui view at (64,64) (clear of the textedit window),
		 * asks the host to click it into focus and type 'hello', and
		 * counts the input events it receives. If a native Genode
		 * client receives press+key events, nitpicker's delivery works
		 * and any remaining failure is inside Qt/textedit.
		 */
		{
			_gui.construct(_env, "probe-input");
			_gui->buffer(Framebuffer::Mode { .area  = Gui::Area(64, 64),
			                                 .alpha = false });
			_gui_fb.construct(_env.rm(), _gui->framebuffer.dataspace());
			Pixel *px = _gui_fb->local_addr<Pixel>();
			for (unsigned i = 0; i < 64 * 64; ++i)
				px[i] = Pixel { 0xff, 0x00, 0xff };

			Gui::Session::View_attr const attr {
				.title = "probe",
				.rect  = Gui::Rect(Gui::Point(64, 64), Gui::Area(64, 64)),
				.front = true };
			Gui::View_id const view_id { 1 };
			_gui->view(view_id, attr);
			_gui->execute();

			/*
			 * Self-check: the magenta probe view must be visible at
			 * (96,96) through Capture — proves the Gui session, the
			 * buffer, and the view are all live before the click.
			 */
			_timer.msleep(500);
			_capture.capture_at(Capture::Point(0, 0));
			Pixel const *scr = _cap_ds->local_addr<Pixel>();
			Pixel const  at  = scr[96 * SCREEN_W + 96];
			log("textedit-probe: [8a] bisect view pixel at (96,96): r=",
			    at.r(), " g=", at.g(), " b=", at.b(),
			    (at.r() > 200 && at.b() > 200 && at.g() < 60)
			        ? " (probe view visible)" : " (PROBE VIEW NOT VISIBLE)");

			log("textedit-probe: [8a] bisect -- move pointer onto probe view");
			if (!_goto_pos(96, 96)) {
				_fail("bisect: pointer never reached the probe view");
				return;
			}
			log("QMP-TARGET press");
			_timer.msleep(1500);
			_drain_input();
			_log_all_reports("[8a] post-press");
			log("textedit-probe: [8a] bisect -- request type 'hello' to probe view");
			log("QMP-TARGET type hello");
			for (unsigned i = 0; i < 10; ++i) {
				_timer.msleep(500);
				_drain_input();
			}
			log("textedit-probe: [8a] bisect result: key_presses=",
			    _bisect_key_presses, " btn_presses=", _bisect_btn_presses,
			    " other=", _bisect_other);
		}

		/* pre-click reference snapshot for the click-delta diagnosis */
		_capture.capture_at(Capture::Point(0, 0));
		_doc_snapshot();

		log("textedit-probe: [8] qmp mode -- focus click at document center (",
		    DOC_CLICK_X, ",", DOC_CLICK_Y, ")");
		if (!_goto_pos(DOC_CLICK_X, DOC_CLICK_Y)) {
			_fail("pointer never reached the document center");
			return;
		}
		log("QMP-TARGET press");

		/*
		 * Let the host press, nitpicker focus transition, and Qt
		 * event processing settle before measuring (bounded settle
		 * delay, not a gate).
		 */
		_timer.msleep(2000);

		/*
		 * Click delta: any pixel change attributable to the click
		 * (cursor placement, focus highlight). Zero means the click
		 * produced no visible effect in the document region.
		 */
		_capture.capture_at(Capture::Point(0, 0));
		unsigned const click_delta = _doc_delta();
		log("textedit-probe: [8] click delta=", click_delta,
		    " of ", (unsigned)DOC_SAMPLES, " sampled points");

		_log_all_reports("[8] post-click");

		_capture.capture_at(Capture::Point(0, 0));
		_doc_snapshot();
		_timer.msleep(700);  /* spans one cursor-blink phase */
		_capture.capture_at(Capture::Point(0, 0));
		unsigned const baseline = _doc_delta();
		log("textedit-probe: [8] cursor-blink baseline delta=", baseline,
		    " of ", (unsigned)DOC_SAMPLES, " sampled points");

		log("textedit-probe: [9] request QMP type 'hello'");
		log("QMP-TARGET type hello");
		log("QMP-TARGET end");

		/*
		 * Bounded poll (120 * 500ms = 60s) for the typed text to
		 * render; fail loud on timeout (never a silent hang).
		 */
		for (unsigned i = 0; i < 120; ++i) {
			_timer.msleep(500);
			_capture.capture_at(Capture::Point(0, 0));
			unsigned const delta = _doc_delta();
			long const ks = _keystate_count();
			if (i % 4 == 0 || ks > 0)
				log("textedit-probe: [9] typed-delta poll ", i,
				    " delta=", delta, " keystate=", ks);
			if (i == 40)
				_log_all_reports("[9] mid-poll");
			if (delta >= TYPED_FLOOR && delta > 2 * baseline) {
				log("textedit-probe: [9] typed text rendered via QMP send-key",
				    " (typed delta=", delta, " > 2x baseline ", baseline, ")");
				return;
			}
		}
		_log_all_reports("[9] post-timeout");
		_fail("typed text never rendered "
		      "(QMP send-key -> ps2 -> event_filter -> nitpicker chain broken?)");
	}

	void run()
	{
		using namespace Genode;

		log("textedit-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* (1) install textedit via sponge_pkgd */
		log("textedit-probe: [1] install textedit via sponge_pkgd");
		if (!_send_and_wait("install", "textedit")) {
			_fail("sponge_pkgd did not answer install textedit");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("install did not return ok");
			return;
		}
		log("textedit-probe: [1] install ok");

		/*
		 * (2) Installed broadcast carries textedit with running="no"
		 * (vct list-equivalent + lifecycle check — textedit has no
		 * <autostart/>, so install left it STOPPED).
		 */
		log("textedit-probe: [2] verify installed broadcast lists textedit stopped");
		{
			String<16> const running = _installed_running("textedit");
			if (running.length() == 0) {
				_fail("installed broadcast does not list textedit");
				return;
			}
			if (running != String<16>("no")) {
				_fail(Genode::String<128>("textedit running attr is '",
				      running, "' expected 'no'").string());
				return;
			}
		}
		log("textedit-probe: [2] installed broadcast lists textedit running=no");

		/*
		 * (3) launch transitions textedit to running (starts the
		 * real Qt6 textedit binary under pkg_runtime).
		 */
		log("textedit-probe: [3] launch textedit via sponge_pkgd");
		if (!_send_and_wait("launch", "textedit")) {
			_fail("sponge_pkgd did not answer launch textedit");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("launch did not return ok");
			return;
		}
		log("textedit-probe: [3] launch ok");

		/* (4) broadcast now shows textedit running=yes */
		log("textedit-probe: [4] verify installed broadcast now lists textedit running");
		{
			String<16> const running = _installed_running("textedit");
			if (running != String<16>("yes")) {
				_fail(Genode::String<128>("textedit running attr is '",
				      running, "' expected 'yes' after launch").string());
				return;
			}
		}
		log("textedit-probe: [4] installed broadcast lists textedit running=yes");

		/* (5) pixel-verify the Qt6 textedit window actually rendered */
		log("textedit-probe: [5] wait for textedit window render");
		if (!_wait_for_window()) {
			_fail("textedit window never rendered into nitpicker");
			return;
		}

		/*
		 * (6) Error-path assertions — pkgd must report clear statuses,
		 * not crash. Two cases exercised here:
		 *   - launch <unknown>:  not-installed
		 *   - launch textedit again:  already-running (idempotent)
		 * The full missing-binary failure channel (textedit not staged
		 * as a boot module) is verified separately by the scratch
		 * scenario sponge-textedit-fail.run (bounded-timeout FAIL).
		 */
		log("textedit-probe: [6] launch nosuchpkg-14 -> not-installed");
		if (!_send_and_wait("launch", "nosuchpkg-14")) {
			_fail("sponge_pkgd did not answer launch nosuchpkg-14");
			return;
		}
		if (_result_status() != String<32>("not-installed")) {
			_fail(Genode::String<128>("launch nosuchpkg-14 status is '",
			      _result_status(), "' expected 'not-installed'").string());
			return;
		}
		log("textedit-probe: [6] not-installed reported");

		log("textedit-probe: [7] double-launch textedit -> already-running");
		if (!_send_and_wait("launch", "textedit")) {
			_fail("sponge_pkgd did not answer second launch textedit");
			return;
		}
		if (_result_status() != String<32>("already-running")) {
			_fail(Genode::String<128>("double-launch status is '",
			      _result_status(), "' expected 'already-running'").string());
			return;
		}
		log("textedit-probe: [7] already-running reported");

		/*
		 * (8)/(9) QMP keyboard mode (Phase 10 criterion 5b), only
		 * with <config qmp="yes"/>. With no config ROM delivered
		 * (run/sponge-textedit.run) this is skipped and the probe
		 * behavior is byte-identical to the pre-QMP code.
		 */
		bool const qmp = _config.valid() &&
		                 _config.node().attribute_value("qmp", false);
		if (qmp)
			_qmp_keyboard_check();

		log("textedit-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Textedit_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
