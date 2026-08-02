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
