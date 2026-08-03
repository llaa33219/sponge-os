/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * falkon_probe — headless verification probe for pkg/falkon
 * (Phase 7 todo 16).
 *
 * Drives the sponge_pkgd channel to install and launch the `falkon`
 * package (the depot-repackaged Qt6 WebEngine browser), then proves
 * inside one headless Genode instance (no host display, no fb_sdl)
 * that the Falkon browser window actually rendered into nitpicker's
 * composited screen — read back through a Capture session.
 *
 * Flow:
 *   (1) <request op="install" pkg="falkon"/>; wait for pkgd ok.
 *   (2) Read the `installed` broadcast; assert it carries "falkon"
 *       with running="no" (vct list-equivalent; falkon has no
 *       <autostart/> so install left it STOPPED).
 *   (3) <request op="launch" pkg="falkon"/>; wait for pkgd ok.
 *   (4) Read the broadcast again; assert falkon is now running="yes".
 *   (5) Poll the capture buffer for the falkon window:
 *       Falkon renders a browser chrome (menu bar + tab bar + URL bar
 *       + navigation toolbar + page area) that is everywhere distinct
 *       from the nitpicker background (#1e1e2e). Require a substantial
 *       fraction of sampled pixels across the full screen to be
 *       non-background AND a minimum color diversity — proves the Qt6
 *       widget actually painted, not just an empty buffer
 *       (misleading_success_output class).
 *
 *       WebEngine first paint under softpipe Mesa on seL4 is SLOW
 *       (potentially many minutes). The poll count is bounded
 *       (6000 * 100ms = 600s) but generous.
 *
 *   (6) Error-path assertions (the pkgd-level "clear error, not a
 *       crash" check):
 *       - <request op="launch" pkg="nosuchpkg-16"> -> status="not-installed"
 *       - a second <request op="launch" pkg="falkon"> -> status="already-running"
 *
 * The separate fixture-page-load verification (falkon fetched
 * http://10.0.2.2:<port>/net-fixture.txt from the host fixture) is
 * NOT done by this probe — it is done by the run script via the host
 * http.server access log (the GET request appears there when falkon
 * navigates to the URL arg in its config). This separation keeps the
 * probe focused on the in-guest render assertion and the fixture
 * assertion on the definitive host-side signal (misleading_success_output
 * — the host log is ground truth for the network round-trip).
 *
 * Plain Genode component (Component::construct, no libc/Qt), following
 * AGENTS.md §3.1 (qualified Genode types, no exceptions). Success logs
 * "falkon-probe: PASS" and exits 0; any failure logs
 * "falkon-probe: FAIL <reason>" and exits non-zero so the run
 * scenario fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
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
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;  /* Pixel_rgb888: r()/g()/b() accessors */

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * Nitpicker <background> color (#1e1e2e), matching every headless
 * scenario in the suite.
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Falkon window footprint. Falkon is a Qt Widgets app (QMainWindow)
 * that opens against QGenodeScreen::geometry() (full 1024x768
 * panorama). Unlike the upstream textedit example which centers a
 * 512x512 window, Falkon opens a large browser window covering most
 * of the screen. We sample the full screen — Falkon's chrome + page
 * area will dominate, and the nitpicker background (if any shows
 * through at edges) is caught by the fractional threshold.
 */
int const ED_X = 0, ED_Y = 0;
int const ED_W = SCREEN_W, ED_H = SCREEN_H;

/*
 * Sample grid stride. Sampling every 8th pixel covers the screen
 * densely enough to distinguish a real Falkon render (browser chrome
 * with toolbar/menu/URL bar + page area) from an empty or
 * uninitialized buffer.
 */
int const SAMPLE_STRIDE = 8;

/*
 * Rendered-window threshold for FULL-SCREEN sampling. Falkon's
 * browser chrome + page area fills most of the screen, but the
 * threshold is lower than textedit's (0.50) because full-screen
 * sampling includes edge areas where the nitpicker background may
 * show through. 0.25 is still unambiguously non-empty (an empty
 * nitpicker buffer is ~0% non-bg).
 */
float const RENDERED_THRESHOLD = 0.25f;

/*
 * Color-diversity floor. A real Falkon scene draws many colors
 * (toolbar icons, menu text, URL bar, page content, scrollbar);
 * an empty buffer draws one. >=12 guards against a solid-color
 * frame masquerading as "rendered".
 */
unsigned const DIVERSITY_FLOOR = 12;
unsigned const COLOR_BUCKET_R_SHIFT = 4;
unsigned const COLOR_BUCKET_G_SHIFT = 4;
unsigned const COLOR_BUCKET_B_SHIFT = 4;

int const COLOR_TOLERANCE = 8;
bool channel_near(int a, int b) { return a >= b ? a - b <= COLOR_TOLERANCE
                                                 : b - a <= COLOR_TOLERANCE; }
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


struct Falkon_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "falkon-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	bool _ok { true };

	Falkon_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("falkon-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/*
	 * Send `<request op pkg/>` and poll the result ROM until pkgd
	 * answers with a result for the same op+pkg. The poll count is
	 * generous (120 * 100ms = 12s per request) because pkgd may be
	 * regenerating the runtime config.
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
	 * Read the `installed` broadcast and return the running="yes"|"no"
	 * attribute for `name`, or the empty string if `name` is absent.
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
	 * Sample the screen on a stride grid: fraction of non-background
	 * pixels + count of distinct 4-bit-per-channel color buckets.
	 */
	void _sample_screen(float &out_frac, unsigned &out_buckets)
	{
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned total = 0, nonbg = 0;

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
	 * (5) Poll capture until falkon's window has actually painted.
	 * WebEngine first paint under softpipe Mesa on seL4 is SLOW —
	 * the poll runs for up to 600s (6000 * 100ms), well inside the
	 * run scenario's generous timeout. Bounded per the
	 * hung_or_long_commands adversarial class.
	 */
	bool _wait_for_window()
	{
		for (unsigned i = 0; i < 6000 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			float frac = 0.0f;
			unsigned buckets = 0;
			_sample_screen(frac, buckets);

			if (i % 20 == 0)
				Genode::log("falkon-probe: capture poll ", i,
				            " rendered_frac=", (unsigned)(frac * 100), "%",
				            " color_buckets=", buckets);

			if (frac >= RENDERED_THRESHOLD && buckets >= DIVERSITY_FLOOR) {
				Genode::log("falkon-probe: falkon window detected (",
				            (unsigned)(frac * 100), "% non-bg, ",
				            buckets, " distinct color buckets)");
				return true;
			}
		}
		return false;
	}

	void run()
	{
		using namespace Genode;

		log("falkon-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* (1) install falkon via sponge_pkgd */
		log("falkon-probe: [1] install falkon via sponge_pkgd");
		if (!_send_and_wait("install", "falkon")) {
			_fail("sponge_pkgd did not answer install falkon");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("install did not return ok");
			return;
		}
		log("falkon-probe: [1] install ok");

		/*
		 * (2) Installed broadcast carries falkon with running="no"
		 * (vct list-equivalent + lifecycle check).
		 */
		log("falkon-probe: [2] verify installed broadcast lists falkon stopped");
		{
			String<16> const running = _installed_running("falkon");
			if (running.length() == 0) {
				_fail("installed broadcast does not list falkon");
				return;
			}
			if (running != String<16>("no")) {
				_fail(Genode::String<128>("falkon running attr is '",
				      running, "' expected 'no'").string());
				return;
			}
		}
		log("falkon-probe: [2] installed broadcast lists falkon running=no");

		/*
		 * (3) launch transitions falkon to running (starts the real
		 * Qt6 WebEngine binary under pkg_runtime).
		 */
		log("falkon-probe: [3] launch falkon via sponge_pkgd");
		if (!_send_and_wait("launch", "falkon")) {
			_fail("sponge_pkgd did not answer launch falkon");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("launch did not return ok");
			return;
		}
		log("falkon-probe: [3] launch ok — WebEngine first paint may take minutes under softpipe");

		/* (4) broadcast now shows falkon running=yes */
		log("falkon-probe: [4] verify installed broadcast now lists falkon running");
		{
			String<16> const running = _installed_running("falkon");
			if (running != String<16>("yes")) {
				_fail(Genode::String<128>("falkon running attr is '",
				      running, "' expected 'yes' after launch").string());
				return;
			}
		}
		log("falkon-probe: [4] installed broadcast lists falkon running=yes");

		/* (5) pixel-verify the Falkon window actually rendered */
		log("falkon-probe: [5] wait for falkon window render (WebEngine: be patient)");
		if (!_wait_for_window()) {
			_fail("falkon window never rendered into nitpicker");
			return;
		}

		/*
		 * (6) Error-path assertions — pkgd must report clear statuses,
		 * not crash.
		 */
		log("falkon-probe: [6] launch nosuchpkg-16 -> not-installed");
		if (!_send_and_wait("launch", "nosuchpkg-16")) {
			_fail("sponge_pkgd did not answer launch nosuchpkg-16");
			return;
		}
		if (_result_status() != String<32>("not-installed")) {
			_fail(Genode::String<128>("launch nosuchpkg-16 status is '",
			      _result_status(), "' expected 'not-installed'").string());
			return;
		}
		log("falkon-probe: [6] not-installed reported");

		log("falkon-probe: [7] double-launch falkon -> already-running");
		if (!_send_and_wait("launch", "falkon")) {
			_fail("sponge_pkgd did not answer second launch falkon");
			return;
		}
		if (_result_status() != String<32>("already-running")) {
			_fail(Genode::String<128>("double-launch status is '",
			      _result_status(), "' expected 'already-running'").string());
			return;
		}
		log("falkon-probe: [7] already-running reported");

		log("falkon-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Falkon_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
