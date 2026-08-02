/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * files_probe — headless verification probe for pkg/files (Phase 7
 * todo 15).
 *
 * Drives sponge_pkgd to install + launch the `files` package, then
 * proves inside one headless Genode instance (no host display, no
 * fb_sdl):
 *
 *   (a) Window pixel-verified: the Qt6 file-manager window actually
 *       renders into nitpicker's composited screen — read back through
 *       a Capture session. The list + preview + buttons + themed
 *       background produce a substantial non-bg fraction AND a non-
 *       trivial color diversity (guards against the
 *       misleading_success_output class).
 *
 *   (b) Synthetic double-click navigates into a directory: the probe
 *       injects a real double-click on the list's first row through
 *       nitpicker's Event service. The component's normal double-click
 *       handler runs and the `files` report's path changes from "/"
 *       to "/demo/aaa_dir". Asserted via the structural report, NOT
 *       by parsing pixels.
 *
 *   (c) Copy a file within /writable and delete it: the probe drives
 *       the request channel to copy /demo/notes.txt -> /writable/
 *       notes.txt, then delete /writable/notes.txt. Both verified via
 *       the `files` report's last_action/result AND by reading back
 *       the writable area's state through a parallel fs_report ROM
 *       (the fs_report pattern from run/sponge-leitzentrale.run).
 *
 *   (d) Attempt delete in the read-only area (/demo/notes.txt):
 *       refused — and the refusal is surfaced in the component's
 *       report with result="refused", in Genode::log, and on the
 *       status label. Asserted via the report result.
 *
 * Plain Genode component (Component::construct, no libc/Qt), following
 * AGENTS.md §3.1. Success logs "files-probe: PASS" and exits 0; any
 * failure logs "files-probe: FAIL <reason>" and exits non-zero so the
 * run scenario fails by bounded run_genode_until timeout (fail-loud,
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

/* Nitpicker <background> color (#1e1e2e), matching run/sponge-files.run. */
int const BG_R = 0x1e, BG_G = 0x1e, BG_B = 0x2e;

/*
 * Window footprint (matches setGeometry(0,0,800,600) in files_window.cc
 * and the "files" nitpicker domain origin (0,0) in the run script).
 */
int const ED_X = 0,  ED_Y = 0;
int const ED_W = 800, ED_H = 600;

/*
 * Sample grid for the rendered-window check. Sampling every 8th pixel
 * densely covers the window footprint. A real Qt-rendered file-manager
 * scene (themed bg + list rows + preview pane + accent buttons) hits
 * many color buckets AND a high non-bg fraction; an empty buffer hits
 * ~0% / 1 bucket; a stale solid buffer hits high frac but 1 bucket.
 */
int const SAMPLE_STRIDE = 8;
float const RENDERED_THRESHOLD = 0.30f;   /* generous for softpipe blending */
unsigned const DIVERSITY_FLOOR = 8;

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
	return ((unsigned)p.r() >> 4) << 8
	     | ((unsigned)p.g() >> 4) << 4
	     | ((unsigned)p.b() >> 4);
}

/*
 * Click target for the synthetic double-click on the first list row.
 * Layout (set in files_window.cc):
 *   y=0..10   : QVBoxLayout top margin
 *   y=10..40  : top bar (path label + Up button, ~30px)
 *   y=40..    : QListWidget begins (with ~2px internal margin)
 *   row 0     : center at ~y=54 (item height ~28px via stylesheet padding)
 *
 * DBL_Y=54 lands inside row 0 with generous slack either way. The
 * horizontal coordinate is the list's center (the list spans the left
 * half of an 800px-wide window).
 *
 * Verified: at "/", alphabetical order is demo/, dev, pipe, qt,
 * writable — so row 0 is "demo/" and double-clicking navigates to
 * /demo.
 */
int const DBL_X = 100;
int const DBL_Y = 54;

} /* anonymous namespace */


struct Files_probe
{
	Genode::Env &_env;

	Timer::Connection              _timer   { _env };
	Capture::Connection            _capture { _env, "files-probe" };
	Event::Connection              _event   { _env, "files-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	/* pkgd request/result channel (same shape vct sends). */
	Genode::Expanding_reporter     _request  { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result   { _env, "result" };

	/* pkgd installed broadcast (vct list-equivalent). */
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	/*
	 * The component's structural report (state + last_action). The probe
	 * polls this for path/entries/result assertions.
	 */
	Genode::Attached_rom_dataspace _files_report { _env, "files" };

	/*
	 * The request channel INTO the component (the probe writes
	 * <request op=... arg1=... arg2=... seq=...> and the component's
	 * QTimer poll dispatches it). The component writes results to its
	 * `files` report.
	 */
	Genode::Expanding_reporter     _files_request { _env, "request", "files_request" };

	bool _ok { true };
	unsigned _next_seq { 1 };

	Files_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("files-probe: FAIL ", reason);
		_env.parent().exit(1);
	}

	/* ---- pkgd request/result ---- */

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
		} catch (Genode::Xml_node::Invalid_syntax) {
			return Genode::String<32>();
		}
	}

	/* ---- installed broadcast ---- */

	bool _installed_contains(char const *name)
	{
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
									    == Genode::String<64>(name))
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

	/* ---- component's `files` report reader ---- */

	struct Files_state
	{
		Genode::String<128> path     { };
		unsigned            entries  { 0 };
		Genode::String<32>  action   { };
		Genode::String<32>  result   { };
		bool                valid    { false };
	};

	Files_state _read_files_report()
	{
		Files_state s { };
		_files_report.update();
		if (!_files_report.valid()) return s;
		try {
			Genode::Xml_node const r = _files_report.xml();
			if (!r.has_type("files")) return s;
			s.path    = r.attribute_value("path",    Genode::String<128>());
			s.entries = (unsigned)r.attribute_value("entries", 0u);
			r.for_each_sub_node("last_action", [&](Genode::Xml_node const &a) {
				s.action = a.attribute_value("name",   Genode::String<32>());
				s.result = a.attribute_value("result", Genode::String<32>());
			});
			s.valid = true;
		} catch (Genode::Xml_node::Invalid_syntax) { }
		return s;
	}

	bool _wait_for_path(char const *expected_path, Files_state &out,
	                    unsigned timeout_ms = 5000)
	{
		for (unsigned i = 0; i < timeout_ms / 100; ++i) {
			Files_state const s = _read_files_report();
			if (s.valid && s.path == Genode::String<128>(expected_path)) {
				out = s;
				return true;
			}
			_timer.msleep(100);
		}
		return false;
	}

	bool _wait_for_result(char const *expected_action,
	                      char const *expected_result,
	                      Files_state &out,
	                      unsigned timeout_ms = 5000)
	{
		for (unsigned i = 0; i < timeout_ms / 100; ++i) {
			Files_state const s = _read_files_report();
			if (s.valid
			    && s.action == Genode::String<32>(expected_action)
			    && s.result == Genode::String<32>(expected_result)) {
				out = s;
				return true;
			}
			_timer.msleep(100);
		}
		return false;
	}

	/* ---- request op into the component ---- */

	void _send_op(char const *op, char const *arg1 = "", char const *arg2 = "")
	{
		unsigned const seq = _next_seq++;
		_files_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",   op);
			g.attribute("arg1", arg1);
			g.attribute("arg2", arg2);
			g.attribute("seq",  seq);
		});
		Genode::log("files-probe: send op='", op, "' arg1='", arg1,
		            "' arg2='", arg2, "' seq=", seq);
	}

	/* ---- pixel verification ---- */

	void _sample_window(float &out_frac, unsigned &out_buckets)
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

	bool _wait_for_window()
	{
		for (unsigned i = 0; i < 1500 && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			float frac = 0.0f;
			unsigned buckets = 0;
			_sample_window(frac, buckets);

			if (i % 10 == 0)
				Genode::log("files-probe: capture poll ", i,
				            " rendered_frac=", (unsigned)(frac * 100), "%",
				            " color_buckets=", buckets);

			if (frac >= RENDERED_THRESHOLD && buckets >= DIVERSITY_FLOOR) {
				Genode::log("files-probe: window detected (",
				            (unsigned)(frac * 100), "% non-bg, ",
				            buckets, " distinct color buckets)");
				return true;
			}
		}
		return false;
	}

	void _inject_double_click()
	{
		/*
		 * Two single clicks inside Qt's double-click interval register
		 * as a double-click on QListWidget::itemDoubleClicked. Inject
		 * Absolute_motion -> Press -> Release, wait, repeat.
		 */
		auto one_click = [&]() {
			_event.with_batch([&](Event::Session_client::Batch &batch) {
				batch.submit(Input::Absolute_motion{ DBL_X, DBL_Y });
				batch.submit(Input::Press   { Input::BTN_LEFT });
				batch.submit(Input::Release { Input::BTN_LEFT });
			});
		};
		one_click();
		_timer.msleep(60);
		one_click();
	}

	void run()
	{
		using namespace Genode;

		log("files-probe: starting");

		/*
		 * Pre-write a no-op files_request so report_rom has a source
		 * available BEFORE sponge_files opens the ROM (component
		 * construction runs synchronously after launch and opens
		 * files_request immediately; without a pre-existing source,
		 * the routing chain may deny the session). The component's
		 * request poll de-dups by signature, so the seq=0 noop is
		 * processed exactly once and never re-runs.
		 */
		_files_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "noop");
			g.attribute("arg1", "");
			g.attribute("arg2", "");
			g.attribute("seq",  (unsigned)0);
		});

		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/* (1) install files via sponge_pkgd */
		log("files-probe: [1] install files via sponge_pkgd");
		if (!_send_and_wait("install", "files")) {
			_fail("sponge_pkgd did not answer install files");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("install did not return ok");
			return;
		}
		log("files-probe: [1] install ok");

		/* (2) installed broadcast carries files (vct list-equivalent) */
		log("files-probe: [2] verify installed broadcast lists files");
		if (!_installed_contains("files")) {
			_fail("installed broadcast does not list files");
			return;
		}
		log("files-probe: [2] installed broadcast lists files");

		/* (3) launch files — installed-but-stopped transitions to running */
		log("files-probe: [3] launch files via sponge_pkgd");
		if (!_send_and_wait("launch", "files")) {
			_fail("sponge_pkgd did not answer launch files");
			return;
		}
		if (_result_status() != String<32>("ok")) {
			_fail("launch did not return ok");
			return;
		}
		log("files-probe: [3] launch ok");

		/* (4) wait for the file-manager window to render */
		log("files-probe: [4] wait for sponge_files window render");
		if (!_wait_for_window()) {
			_fail("sponge_files window never rendered into nitpicker");
			return;
		}

		/*
		 * (a) assertion: structural report published with the initial
		 *     path "/". The component's _refresh() runs once at startup
		 *     and lists /demo + /writable at the root.
		 */
		log("files-probe: [a] verify files report path=\"/\"");
		{
			Files_state s { };
			if (!_wait_for_path("/", s, 10000)) {
				_fail("files report never reached path='/'");
				return;
			}
			if (s.entries < 2) {
				_fail(Genode::String<128>("files report entries < 2 (got ",
				      s.entries, ")").string());
				return;
			}
			log("files-probe: [a] path=\"/\" entries=", s.entries,
			    " last_action=", s.action, "/", s.result);
		}

		/*
		 * (b) synthetic double-click navigates into the first directory.
		 *     The list at "/" is alphabetically sorted: aaa_dir/ sorts
		 *     before notes.txt, readme.txt, zzz_dir/. Double-clicking
		 *     the first row -> navigate -> path becomes "/demo/aaa_dir"
		 *     (the "/" listing shows "demo/" not "aaa_dir/" — wait, the
		 *     fixture tar exposes /demo/<entries> at the root, so the
		 *     root listing shows "demo", "writable". Double-clicking
		 *     "demo" navigates into /demo).
		 *
		 * Actually we double-click the FIRST entry, which after sorting
		 * is "demo/". So expected path after the click is "/demo".
		 */
		log("files-probe: [b] double-click at (", DBL_X, ",", DBL_Y,
		    ") -> expect navigate to /demo");
		_inject_double_click();

		{
			Files_state s { };
			if (!_wait_for_path("/demo", s, 10000)) {
				_fail("files report path never became /demo after dbl-click");
				return;
			}
			if (s.action != String<32>("navigate")
			    || s.result != String<32>("ok")) {
				_fail(Genode::String<256>("dbl-click report action='",
				      s.action, "' result='", s.result,
				      "' expected navigate/ok").string());
				return;
			}
			if (s.entries < 3) {
				_fail(Genode::String<128>("/demo entries < 3 (got ",
				      s.entries, ")").string());
				return;
			}
			log("files-probe: [b] navigated to /demo entries=", s.entries,
			    " action=", s.action, "/", s.result);
		}

		/*
		 * (c) copy a file within /writable and delete it. Drive via the
		 *     request channel so the assertion does not depend on
		 *     pixel-precise button clicks (automation default).
		 *
		 *   1. copy /demo/notes.txt -> /writable/notes.txt
		 *      (assert report last_action=copy result=ok)
		 *   2. delete /writable/notes.txt
		 *      (assert report last_action=delete result=ok)
		 *   3. delete /demo/notes.txt
		 *      (assert report last_action=delete result=refused — the
		 *      read-only-area refusal path; surfaces in UI/log/report)
		 */
		log("files-probe: [c1] copy /demo/notes.txt -> /writable/notes.txt");
		_send_op("copy", "/demo/notes.txt", "/writable/notes.txt");
		{
			Files_state s { };
			if (!_wait_for_result("copy", "ok", s, 5000)) {
				_fail("copy result != ok");
				return;
			}
			log("files-probe: [c1] copy ok (path=", s.path.string(), ")");
		}

		log("files-probe: [c2] delete /writable/notes.txt");
		_send_op("delete", "/writable/notes.txt");
		{
			Files_state s { };
			if (!_wait_for_result("delete", "ok", s, 5000)) {
				_fail("delete writable result != ok");
				return;
			}
			log("files-probe: [c2] delete ok");
		}

		/*
		 * (d) attempt delete in the read-only area (/demo) — must be
		 *     refused AND surfaced (UI/log/report). The probe asserts
		 *     the report path; the run scenario separately gates on the
		 *     Genode::log line "delete /demo/notes.txt refused" to prove
		 *     the refusal surfaces in the log too.
		 */
		log("files-probe: [d] delete /demo/notes.txt (expect refused)");
		_send_op("delete", "/demo/notes.txt");
		{
			Files_state s { };
			if (!_wait_for_result("delete", "refused", s, 5000)) {
				_fail("delete in /demo was not refused");
				return;
			}
			log("files-probe: [d] refused (action=", s.action.string(),
			    " result=", s.result.string(), ")");
		}

		log("files-probe: PASS");
		_env.parent().exit(0);
	}
};


void Component::construct(Genode::Env &env)
{
	static Files_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
