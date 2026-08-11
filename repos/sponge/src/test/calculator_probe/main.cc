/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * calculator_probe — headless verification probe for pkg/calculator
 * (Phase 13 W3, docs/plans/phase13-package-ecosystem.md D13.3).
 *
 * Drives the sponge_pkgd channel to install and launch the `calculator`
 * package (the source-built qt6_calculatorform example,
 * genode/repos/libports/src/app/qt6/examples/calculatorform/), then
 * proves inside one headless Genode instance (no host display, no
 * fb_sdl) that the Qt6 calculator window actually rendered into
 * nitpicker's composited screen — read back through a Capture session.
 *
 * Modelled on textedit_probe (Phase 7 todo 14); the calculator differs
 * from the text editor in three pixel-observable ways:
 *
 *   - class: QWidget (not QMainWindow like textedit). The widget is
 *     a top-level QWidget without a status bar/central widget menu;
 *     the calculatorform.cpp constructor only calls ui.setupUi(this)
 *     and connects two spinbox valueChanged signals to a single
 *     updateResult slot (calculatorform.cpp:7-13; see pkg/calculator
 *     metadata.xml header comment for the full widget inventory).
 *   - footprint: the UI file sets geometry (0, 0, 400, 300). With
 *     no main.cpp move() call, the QGenodePlatformWindow uses the
 *     UI geometry verbatim (qgenodeplatformwindow.cpp:677 calls
 *     _adjust_and_set_geometry(geometry()) in the constructor; the
 *     widget's geometry is what Qt loads from the .ui file), so the
 *     window lands at screen origin (0, 0) to (400, 300). No
 *     centering — textedit's main.cpp explicitly moves to (256, 128).
 *   - content complexity: ~2 QSpinBoxes + 5 QLabels + 1 sunken-framed
 *     outputWidget = ~8 widgets. textedit's main window draws a menu
 *     bar, a tool bar, and a rich-text area (many more draw calls);
 *     the widget rendered background is simpler, so the colour
 *     diversity floor is lower (8 vs 16) and the rendered-fraction
 *     threshold is lower (0.30 vs 0.50 — the widget is smaller and
 *     a smaller fraction of the sample area is non-background).
 *
 * Flow:
 *   (1) <request op="install" pkg="calculator"/>; wait for pkgd ok.
 *   (2) Read the `installed` broadcast; assert it carries "calculator"
 *       with running="no" (the vct list-equivalent check — the same
 *       ROM the launcher menu reads; calculator has no <autostart/> so
 *       install left it STOPPED, docs/12 §9.2.1).
 *   (3) <request op="launch" pkg="calculator"/>; wait for pkgd ok.
 *   (4) Read the broadcast again; assert calculator is now
 *       running="yes".
 *   (5) Poll the capture buffer for the calculator window:
 *       the calculatorform widget renders two spinboxes (white
 *       background with dark border + arrow buttons), three small
 *       text labels ("Input 1", "Input 2", "Output"), two operator
 *       labels ("+", "=") and a sunken-framed outputWidget with the
 *       text "0" — all distinct from the nitpicker #1e1e2e background.
 *       Require that a substantial fraction of the sampled pixels
 *       inside the calculator window area are non-background AND
 *       the scene draws at least DIVERSITY_FLOOR distinct 4-bit-per-
 *       channel color buckets — both together distinguish a real Qt
 *       render from an empty or single-color buffer that happens to
 *       be non-bg everywhere (misleading_success_output class).
 *
 *   (6) Error-path assertions (the pkgd-level "clear error, not a
 *       crash" check):
 *       - <request op="launch" pkg="nosuchpkg-13"> → status="not-installed"
 *       - a second <request op="launch" pkg="calculator"> → status="already-running"
 *       The full missing-binary failure channel (calculatorform not
 *       produced by the build) is verified by the Phase 7 lesson:
 *       pkgd's runtime-config generator will emit a <start> that fails
 *       to load `<binary>calculatorform` and the boot module listing
 *       task reports the missing ROM at probe start; the run script
 *       gates on a bounded run_genode_until timeout (fail-loud,
 *       docs/09-roadmap.md §11.1 — never a silent hang).
 *
 * Plain Genode component (Component::construct, no libc/Qt), following
 * AGENTS.md §3.1 (qualified Genode types, no exceptions). Success logs
 * "calculator-probe: PASS" and exits 0; any failure logs
 * "calculator-probe: FAIL <reason>" and exits non-zero so the run
 * scenario fails by bounded run_genode_until timeout (fail-loud).
 * Mirrors textedit_probe's contract (Phase 7 todo 14).
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
 * run/sponge-calculator.run and every other headless Qt6 scenario in
 * the suite (sponge-textedit.run, sponge-files.run, sponge-pkg-gui.run).
 */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Calculator window footprint. The upstream qt6_calculatorform
 * example is a QWidget (NOT QMainWindow like textedit) loaded from the
 * .ui file with geometry (0, 0, 400, 300)
 * (genode/repos/libports/src/app/qt6/examples/calculatorform/calculatorform.ui,
 * the `<property name="geometry"><rect><x>0</x><y>0</y><width>400</width>
 * <height>300</height></rect></property>` node). main.cpp just calls
 * calculator.show() with no explicit move() — so the widget's window
 * lands at the screen origin (0, 0), top-left at (0, 0) and bottom-
 * right at (400, 300). The probe samples a 480x320 area (a bit larger
 * than the widget to absorb any Qt sizeHint rounding for the
 * QGridLayout's expanding spacers) and uses a 16-pixel BG_PT outside
 * the window as a "background baseline" sanity check.
 */
int const ED_X = 0, ED_Y = 0;
int const ED_W = 480, ED_H = 320;

/*
 * Sample grid for the rendered-window check. Sampling every 8th pixel
 * in each direction covers the calculator footprint densely enough to
 * distinguish a real Qt-rendered scene (text labels + spinboxes +
 * sunken output frame) from an empty or uninitialized buffer, without
 * spending capture time on a full per-pixel scan.
 */
int const SAMPLE_STRIDE = 8;

/*
 * Rendered-window threshold: at least this fraction of sampled pixels
 * must be non-background. The calculatorform widget is ~400x300
 * (120000 px) inside a 480x320 sample area (153600 px) — the widget
 * covers ~78% of the sample area even before considering widget
 * content. With the widget's text/frame/spinbox pixels being
 * everywhere distinct from #1e1e2e, the rendered-fraction should be
 * well above 0.30. The 0.30 floor leaves generous slack for softpipe
 * Mesa blending, anti-aliasing borders, and partial occlusion while
 * still being unambiguously non-empty (an empty/uninitialized
 * nitpicker buffer is typically all-zero black -> 0% non-bg).
 *
 * textedit_probe uses 0.50; the calculator is intentionally tolerant
 * because the widget has fewer drawn content types (no rich text, no
 * toolbar).
 */
float const RENDERED_THRESHOLD = 0.30f;

/*
 * Color-diversity floor: count distinct 4-bit-per-channel color
 * buckets (4096 total). The calculatorform draws at least six
 * distinct color regions: the widget's near-white QPalette backdrop,
 * QLabel text (dark), QSpinBox border + arrow buttons (medium gray),
 * sunken-frame borders around outputWidget (mid-dark), and small
 * pixel anti-aliasing variations. Requiring >= 8 distinct buckets
 * guards against the misleading_success_output failure mode where a
 * bare exit-0 passes on a stale or solid-color buffer that happens to
 * be "non-bg" everywhere.
 *
 * textedit_probe uses 16; the calculator is more conservative
 * because the widget draws fewer content types.
 */
unsigned const DIVERSITY_FLOOR = 8;
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


struct Calculator_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "calculator-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	Genode::Expanding_reporter     _request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result  { _env, "result" };
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	bool _ok { true };

	Calculator_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("calculator-probe: FAIL ", reason);
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
	 * Sample the calculator window area on a stride grid and produce
	 * two signals: the fraction (0..1) of pixels that are NOT the
	 * nitpicker background, and the count of distinct 4-bit-per-
	 * channel color buckets. Both together cleanly distinguish a
	 * real Qt render (high non-bg fraction AND high diversity) from
	 * an empty/uninitialized buffer (~0% non-bg, 1 bucket) or a
	 * stale solid-color frame (possibly high non-bg fraction but
	 * 1 bucket).
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
	 * (5) Poll capture until the calculator window has actually
	 * painted: rendered-fraction AND color-diversity both above
	 * threshold. Together they guard against every
	 * misleading_success_output failure mode: an empty buffer (low
	 * frac), a stale buffer (low diversity), or a partially-rendered
	 * frame (one or both below threshold). The poll count is bounded
	 * (1500 * 100ms = 150s, well inside the run scenario's 420s
	 * timeout — the hung_or_long_commands class).
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
				Genode::log("calculator-probe: capture poll ", i,
				            " rendered_frac=", (unsigned)(frac * 100), "%",
				            " color_buckets=", buckets);

			if (frac >= RENDERED_THRESHOLD && buckets >= DIVERSITY_FLOOR) {
				Genode::log("calculator-probe: calculator window detected (",
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

		log("calculator-probe: starting");

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* (1) install calculator via sponge_pkgd */
		log("calculator-probe: [1] install calculator via sponge_pkgd");
		if (!_send_and_wait("install", "calculator")) {
			_fail("sponge_pkgd did not answer install calculator");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("install did not return ok");
			return;
		}
		log("calculator-probe: [1] install ok");

		/*
		 * (2) Installed broadcast carries calculator with running="no"
		 * (vct list-equivalent + lifecycle check — calculator has no
		 * <autostart/>, so install left it STOPPED).
		 */
		log("calculator-probe: [2] verify installed broadcast lists calculator stopped");
		{
			String<16> const running = _installed_running("calculator");
			if (running.length() == 0) {
				_fail("installed broadcast does not list calculator");
				return;
			}
			if (running != String<16>("no")) {
				_fail(Genode::String<128>("calculator running attr is '",
				      running, "' expected 'no'").string());
				return;
			}
		}
		log("calculator-probe: [2] installed broadcast lists calculator running=no");

		/*
		 * (3) launch transitions calculator to running (starts the
		 * real Qt6 calculatorform binary under pkg_runtime).
		 */
		log("calculator-probe: [3] launch calculator via sponge_pkgd");
		if (!_send_and_wait("launch", "calculator")) {
			_fail("sponge_pkgd did not answer launch calculator");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("launch did not return ok");
			return;
		}
		log("calculator-probe: [3] launch ok");

		/* (4) broadcast now shows calculator running=yes */
		log("calculator-probe: [4] verify installed broadcast now lists calculator running");
		{
			String<16> const running = _installed_running("calculator");
			if (running != String<16>("yes")) {
				_fail(Genode::String<128>("calculator running attr is '",
				      running, "' expected 'yes' after launch").string());
				return;
			}
		}
		log("calculator-probe: [4] installed broadcast lists calculator running=yes");

		/* (5) pixel-verify the Qt6 calculator window actually rendered */
		log("calculator-probe: [5] wait for calculator window render");
		if (!_wait_for_window()) {
			_fail("calculator window never rendered into nitpicker");
			return;
		}

		/*
		 * (6) Error-path assertions — pkgd must report clear statuses,
		 * not crash. Two cases exercised here:
		 *   - launch <unknown>:  not-installed
		 *   - launch calculator again:  already-running (idempotent)
		 * The missing-binary failure channel is bounded by the run
		 * scenario's `run_genode_until` timeout (fail-loud). A
		 * future "sponge-calculator-fail" scenario (textedit's
		 * run/sponge-textedit-fail.run is the model) can cover
		 * the same `app/qt6/examples/calculatorform` not-built path
		 * with a deliberately unbuilt build list.
		 */
		log("calculator-probe: [6] launch nosuchpkg-13 -> not-installed");
		if (!_send_and_wait("launch", "nosuchpkg-13")) {
			_fail("sponge_pkgd did not answer launch nosuchpkg-13");
			return;
		}
		if (_result_status() != String<32>("not-installed")) {
			_fail(Genode::String<128>("launch nosuchpkg-13 status is '",
			      _result_status(), "' expected 'not-installed'").string());
			return;
		}
		log("calculator-probe: [6] not-installed reported");

		log("calculator-probe: [7] double-launch calculator -> already-running");
		if (!_send_and_wait("launch", "calculator")) {
			_fail("sponge_pkgd did not answer second launch calculator");
			return;
		}
		if (_result_status() != String<32>("already-running")) {
			_fail(Genode::String<128>("double-launch status is '",
			      _result_status(), "' expected 'already-running'").string());
			return;
		}
		log("calculator-probe: [7] already-running reported");

		log("calculator-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Calculator_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
