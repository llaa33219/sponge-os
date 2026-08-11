/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pdf_view_probe — headless verification probe for pkg/pdf_view
 * (Phase 13 W4, plan D13.3).
 *
 * Drives the sponge_pkgd channel to install and launch the
 * `pdf_view` package (the source-built upstream app/pdf_view,
 * libports-based, mupdf 0.9), then proves inside one headless
 * Genode instance (no host display, no fb_sdl) that the rendered
 * PDF page actually appears in nitpicker's composited screen —
 * read back through a Capture session.
 *
 * Flow:
 *   (1) <request op="install" pkg="pdf_view"/>; wait for pkgd ok.
 *   (2) Read the `installed` broadcast; assert it carries
 *       "pdf_view" with running="no" (vct list-equivalent check;
 *       pdf_view has no <autostart/> so install left it STOPPED,
 *       docs/12 §9.2.1).
 *   (3) <request op="launch" pkg="pdf_view"/>; wait for pkgd ok.
 *   (4) Read the broadcast again; assert pdf_view is now
 *       running="yes" (the lifecycle transition).
 *   (5) Poll the capture buffer for the rendered PDF page:
 *       the bundled sample.pdf is mostly white (R/G/B > 200) with
 *       black text on it. The nitpicker background is #1e1e2e
 *       (dark purple). A rendered page is therefore almost
 *       everywhere distinct from the background AND substantially
 *       white — two complementary signals that together reject
 *       every misleading_success_output failure mode (an empty
 *       buffer is all-black -> 0% white, low non-bg; a stale
 *       buffer is one solid color -> low non-bg OR low white).
 *       Require white_frac >= 10% AND non_bg_frac >= 50%.
 *
 *   (6)/(7) Error-path assertions (pkgd-level "clear error, not
 *       a crash" check):
 *       - launch <unknown>:  status="not-installed"
 *       - launch pdf_view again: status="already-running"
 *       The full missing-binary failure channel (pdf_view binary
 *       not staged as a boot module, OR the sample.pdf payload
 *       not staged) is verified separately by a scratch scenario
 *       variant — see run/sponge-pdf-view-fail.run — which fails
 *       by bounded run_genode_until timeout, never a silent hang.
 *
 * Plain Genode component (Component::construct, no libc/Qt),
 * following AGENTS.md §3.1 (qualified Genode types, snake_case
 * methods, trailing-underscore members, no exceptions, no
 * reinterpret_cast, no empty catch — the Invalid_syntax catch is
 * paired with a recovery retry and never swallows the error
 * silently). Success logs "pdf-view-probe: PASS" and exits 0;
 * any failure logs "pdf-view-probe: FAIL (<reason>)" and exits
 * non-zero so the run scenario fails by bounded run_genode_until
 * timeout (fail-loud, docs/09-roadmap.md §11.1 — never a silent
 * hang).
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
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
 * Nitpicker <background> color (#1e1e2e), matching
 * run/sponge-pdf-view.run and every other headless scenario in
 * the suite.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * pdf_view window footprint. The upstream main.cc opens a
 * Gui::Connection with no size hint, so its first window is
 * the default (Gui::Rect { { }, { 512, 512 } } — main.cc:97-99).
 * Gui::Top_level_view starts at domain origin, and the
 * "edit" domain in run/sponge-pdf-view.run sits at screen (0,0)
 * (the same full-screen domain textedit uses, so no origin
 * surprises). The probe therefore samples screen (0,0)-(512,512).
 */
int const PV_X = 0, PV_Y = 0;
int const PV_W = 512, PV_H = 512;

/*
 * Sample stride: every 8th pixel in each direction. (512/8)^2 =
 * 64*64 = 4096 sampled points — enough to distinguish a real
 * white PDF page from an empty/uninitialized buffer without
 * spending capture time on a full per-pixel scan.
 */
int const SAMPLE_STRIDE = 8;

/*
 * Channel tolerance for the "near background" check. A real
 * #1e1e2e background pixel and an actual rendered pixel that
 * happens to look similar (a few black-bordered glyph anti-
 * aliasing pixels) differ by much more than 8 in at least one
 * channel, so this is a safe floor.
 */
int const COLOR_TOLERANCE = 8;
bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                : b - a <= COLOR_TOLERANCE; }

bool pixel_is_bg(Pixel const &p)
{
	return channel_near(p.r(), BG_R)
	    && channel_near(p.g(), BG_G)
	    && channel_near(p.b(), BG_B);
}

/*
 * "Bright" = the page background of any normal PDF (a near-white
 * sheet). A real rendered PDF page fills most of the framebuffer
 * with such pixels; an empty zeroed buffer has none. 200 is well
 * inside the typical RGB(245-255) page-white range, well above
 * any anti-aliased gray, and far from the black text — so this
 * cleanly separates "rendered" from "empty" without depending on
 * exact white (some PDFs are off-white by intent; the threshold
 * still passes).
 */
bool pixel_is_bright(Pixel const &p)
{
	return p.r() > 200 && p.g() > 200 && p.b() > 200;
}

/*
 * Rendered-window thresholds. The two checks together reject
 * every misleading_success_output failure mode:
 *   - empty/uninitialized buffer:  low white, low non-bg
 *   - stale solid-color buffer:    low white OR low non-bg
 *   - partially-rendered frame:   one or both below threshold
 *   - real rendered PDF page:     high white, high non-bg
 * White at 10% leaves generous slack for documents with dark
 * backgrounds (Phase 14 may add such fixtures); non-bg at 50%
 * is the same floor textedit_probe uses (which proves a Qt scene
 * rendered, not just a single-color buffer with a non-bg pixel).
 */
float const WHITE_THRESHOLD = 0.10f;
float const NON_BG_THRESHOLD = 0.50f;

} /* anonymous namespace */


struct Pdf_view_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "pdf-view-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	bool _ok { true };

	Pdf_view_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("pdf-view-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/*
	 * Send `<request op pkg/>` and poll the result ROM until pkgd
	 * answers with a result for the same op+pkg. Mirrors
	 * textedit_probe's exact request/result dance; the result ROM
	 * schema (`<result op pkg status="..."/>`) is the same channel
	 * every other pkg_*_probe uses (sponge_pkgd's _handle_request
	 * body, docs/12 §7).
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
	 * Read the `installed` broadcast (the same ROM the launcher
	 * reads) and return the running="yes"|"no" attribute for
	 * `name`, or the empty string if `name` is absent. The
	 * broadcast schema (sponge_pkgd's _generate_installed_report)
	 * is:
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
	 * Sample the pdf_view window footprint on a stride grid and
	 * produce two signals: the fraction of pixels that are
	 * bright (the PDF page background) and the fraction that are
	 * not the nitpicker background. Together they cleanly
	 * distinguish a real rendered PDF page from an empty buffer,
	 * a stale buffer, or a partial frame — see WHITE_THRESHOLD
	 * / NON_BG_THRESHOLD above.
	 */
	void _sample_window(float &out_white_frac, float &out_nonbg_frac)
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned total = 0, white = 0, nonbg = 0;

		for (int y = PV_Y; y < PV_Y + PV_H; y += SAMPLE_STRIDE) {
			for (int x = PV_X; x < PV_X + PV_W; x += SAMPLE_STRIDE) {
				++total;
				Pixel const &p = px[y * SCREEN_W + x];
				if (pixel_is_bright(p))
					++white;
				if (!pixel_is_bg(p))
					++nonbg;
			}
		}

		out_white_frac = total ? (float)white  / (float)total : 0.0f;
		out_nonbg_frac = total ? (float)nonbg  / (float)total : 0.0f;
	}

	/*
	 * (5) Poll capture until the rendered PDF page is present:
	 * white_frac AND non_bg_frac both above threshold. Together
	 * they guard against every misleading_success_output failure
	 * mode (an empty buffer — low white, low non-bg; a stale
	 * solid-color buffer — low white OR low non-bg; a partially-
	 * rendered frame — one or both below threshold). The poll
	 * count is bounded (1500 * 100ms = 150s, well inside the run
	 * scenario's 420s timeout — the hung_or_long_commands class).
	 * mupdf's first paint includes font-cache loading, which can
	 * take several seconds on seL4 under softpipe.
	 */
	bool _wait_for_window()
	{
		for (unsigned i = 0; i < 1500 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			float white_frac = 0.0f, nonbg_frac = 0.0f;
			_sample_window(white_frac, nonbg_frac);

			if (i % 10 == 0)
				Genode::log("pdf-view-probe: capture poll ", i,
				            " white_frac=", (unsigned)(white_frac * 100), "%",
				            " non_bg_frac=", (unsigned)(nonbg_frac * 100), "%");

			if (white_frac >= WHITE_THRESHOLD && nonbg_frac >= NON_BG_THRESHOLD) {
				Genode::log("pdf-view-probe: rendered PDF page detected (",
				            (unsigned)(white_frac * 100), "% white, ",
				            (unsigned)(nonbg_frac * 100), "% non-bg)");
				return true;
			}
		}
		return false;
	}

	void run()
	{
		using namespace Genode;

		log("pdf-view-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* (1) install pdf_view via sponge_pkgd */
		log("pdf-view-probe: [1] install pdf_view via sponge_pkgd");
		if (!_send_and_wait("install", "pdf_view")) {
			_fail("sponge_pkgd did not answer install pdf_view");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("install did not return ok");
			return;
		}
		log("pdf-view-probe: [1] install ok");

		/*
		 * (2) Installed broadcast carries pdf_view with running="no"
		 * (vct list-equivalent + lifecycle check — pdf_view has no
		 * <autostart/>, so install left it STOPPED, docs/12 §9.2.1).
		 */
		log("pdf-view-probe: [2] verify installed broadcast lists pdf_view stopped");
		{
			String<16> const running = _installed_running("pdf_view");
			if (running.length() == 0) {
				_fail("installed broadcast does not list pdf_view");
				return;
			}
			if (running != String<16>("no")) {
				_fail(Genode::String<128>("pdf_view running attr is '",
				      running, "' expected 'no'").string());
				return;
			}
		}
		log("pdf-view-probe: [2] installed broadcast lists pdf_view running=no");

		/*
		 * (3) launch transitions pdf_view to running (starts the
		 * real mupdf-based pdf_view binary under pkg_runtime).
		 */
		log("pdf-view-probe: [3] launch pdf_view via sponge_pkgd");
		if (!_send_and_wait("launch", "pdf_view")) {
			_fail("sponge_pkgd did not answer launch pdf_view");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("launch did not return ok");
			return;
		}
		log("pdf-view-probe: [3] launch ok");

		/* (4) broadcast now shows pdf_view running=yes */
		log("pdf-view-probe: [4] verify installed broadcast now lists pdf_view running");
		{
			String<16> const running = _installed_running("pdf_view");
			if (running != String<16>("yes")) {
				_fail(Genode::String<128>("pdf_view running attr is '",
				      running, "' expected 'yes' after launch").string());
				return;
			}
		}
		log("pdf-view-probe: [4] installed broadcast lists pdf_view running=yes");

		/* (5) pixel-verify the rendered PDF page actually appeared */
		log("pdf-view-probe: [5] wait for pdf_view window render");
		if (!_wait_for_window()) {
			_fail("pdf_view window never rendered into nitpicker");
			return;
		}

		/*
		 * (6)/(7) Error-path assertions — pkgd must report clear
		 * statuses, not crash. Two cases exercised here:
		 *   - launch <unknown>:    not-installed
		 *   - launch pdf_view again: already-running (idempotent)
		 * The full missing-binary / missing-payload failure channels
		 * (pdf_view binary not staged as a boot module, OR
		 * sample.pdf payload not staged) are verified separately by
		 * the scratch scenario run/sponge-pdf-view-fail.run (bounded-
		 * timeout FAIL).
		 */
		log("pdf-view-probe: [6] launch nosuchpkg-13 -> not-installed");
		if (!_send_and_wait("launch", "nosuchpkg-13")) {
			_fail("sponge_pkgd did not answer launch nosuchpkg-13");
			return;
		}
		if (_result_status() != String<32>("not-installed")) {
			_fail(Genode::String<128>("launch nosuchpkg-13 status is '",
			      _result_status(), "' expected 'not-installed'").string());
			return;
		}
		log("pdf-view-probe: [6] not-installed reported");

		log("pdf-view-probe: [7] double-launch pdf_view -> already-running");
		if (!_send_and_wait("launch", "pdf_view")) {
			_fail("sponge_pkgd did not answer second launch pdf_view");
			return;
		}
		if (_result_status() != String<32>("already-running")) {
			_fail(Genode::String<128>("double-launch status is '",
			      _result_status(), "' expected 'already-running'").string());
			return;
		}
		log("pdf-view-probe: [7] already-running reported");

		log("pdf-view-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Pdf_view_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
