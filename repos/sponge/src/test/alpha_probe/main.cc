/* SPDX-License-Identifier: Apache-2.0
 *
 * alpha_probe — Phase 7 todo 4 composite Alpha-desktop verifier.
 *
 * Asserts all four Alpha criteria in bounded iterations inside ONE
 * headless Genode instance, then logs exactly "alpha-probe: PASS". Any
 * failure logs "alpha-probe: FAIL <reason>" and exits non-zero so the
 * run scenario fails by bounded run_genode_until timeout (fail-loud,
 * docs/09-roadmap.md §11.1 — never a silent hang).
 *
 * Criteria:
 *   (a) themed sponge-de panel composited (Capture pixel check on the
 *       panel band — the default theme's panel_bg #1e1e2e is non-zero,
 *       proving the Qt6/Mesa-on-seL4 desktop painted),
 *   (b) sponge-de's "launcher" ROM carries <app name="hello"
 *       category="Utilities"/> — proves the pkgd install + broadcast +
 *       sponge-de launcher feed all work,
 *   (c) configd's broadcast "config" ROM is readable, non-empty, and
 *       parses as <config> with at least one <key> child — proves the
 *       live config pipeline is up,
 *   (d) lz_viewer's Leitzentrale window appears on the outer nitpicker
 *       (the marker patch #bf5fbf at (122,94)) — only happens after the
 *       probe flips leitzentrale.enabled=true via configd and the lz
 *       subsystem fader fades in.
 *
 * The probe owns both the pkgd request channel (installs hello) and the
 * configd config_request channel (enables leitzentrale) — report_rom is
 * single-writer per label and there is no vct in this scenario.
 *
 * Plain Genode component following AGENTS.md §3.1 (qualified Genode
 * types, no exceptions, Component::construct/stack_size exactly as the
 * framework expects).
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
#include <util/construct_at.h>

namespace {

using Pixel = Capture::Pixel;

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/*
 * lz_viewer stamps a distinct marker patch (#bf5fbf) into the top-left
 * of each streamed frame; lz_viewer places the window at (112,84) and
 * the marker at offset (10,10) within it, so the marker lands at
 * (122,94) on the outer nitpicker. Same constants as lz_viz_probe
 * (proven in run/sponge-leitzentrale.run).
 */
int const MARK_R = 0xbf, MARK_G = 0x5f, MARK_B = 0xbf;
int const MARK_X = 112 + 10;
int const MARK_Y = 84  + 10;
int const MARK_W = 40, MARK_H = 40;
unsigned const MARK_PIXEL_THRESHOLD = 500;

int const COLOR_SLACK = 24;


bool pixel_is_marker(Pixel const &p)
{
	auto near = [](int a, int b) { return a >= b ? a - b <= COLOR_SLACK
	                                             : b - a <= COLOR_SLACK; };
	return near(p.r(), MARK_R) && near(p.g(), MARK_G) && near(p.b(), MARK_B);
}


/*
 * Bounded iteration budgets. Each poll is 100-200ms; the totals are
 * generous because Qt6's first paint under softpipe Mesa is markedly
 * slower on seL4 than on base-linux, and the lz subsystem has a long
 * cold-start tail.
 */
unsigned const RENDER_POLL_ITERS   = 1200; /* ~120s for Qt first paint  */
unsigned const INSTALL_WAIT_ITERS  = 120;  /* ~12s for pkgd install ack */
unsigned const LAUNCHER_POLL_ITERS = 400;  /* ~80s for launcher report  */
unsigned const CONFIGD_POLL_ITERS  = 200;  /* ~20s for configd broadcast*/
unsigned const LZ_ENABLE_WAIT_ITERS= 120;  /* ~12s for configd set ack  */
unsigned const LZ_VIEWER_POLL_ITERS= 900;  /* ~90s for fader + first frame */


/*
 * Phase 16 W3 (D16.4 + plan W3 #3) — closed tables of launcher pairs
 * and baked defaults the probe asserts on first boot. The canonical
 * iteration order matches the breadcrumb in the plan's named sentry
 * placeholder (the first non-hello pair is `terminal`, which the
 * FAIL marker calls out when the timeout fires there). The category
 * values are the exact `category=` strings in each pkg/<name>/
 * metadata.xml (docs/12 §4.6); the probe asserts on the wire form.
 */
struct Launcher_pair { char const *name; char const *category; };

static Launcher_pair const LAUNCHER_PAIRS[] = {
	{ "hello",      "Utilities" },
	{ "terminal",   "System"    },
	{ "textedit",   "Editors"   },
	{ "files",      "Utilities" },
	{ "calculator", "Utilities" },
	{ "pdf_view",   "Utilities" },
	{ "falkon",     "Internet"  },
};
static unsigned const LAUNCHER_PAIR_COUNT =
	sizeof(LAUNCHER_PAIRS) / sizeof(LAUNCHER_PAIRS[0]);
static unsigned const LAUNCHER_PAIR_WAIT_ITERS = 300; /* ~30s per pair */

struct Baked_key { char const *name; char const *value; };

static Baked_key const REQUIRED_BAKED_KEYS[] = {
	{ "bake.profile",            "desktop"        },
	{ "bake.version",            "1"              },
	{ "bake.applied",            "yes"            },
	{ "theme.active",            "default"        },
	{ "panel.height",            "28"             },
	{ "panel.visible_widgets",   "clock,launcher,tasklist" },
	{ "clock.format",            "HH:mm"          },
	{ "launcher.sort_by",        "alpha"          },
};
static unsigned const REQUIRED_BAKED_KEY_COUNT =
	sizeof(REQUIRED_BAKED_KEYS) / sizeof(REQUIRED_BAKED_KEYS[0]);

/*
 * True if the candidate node is the empty sentinel ("<empty/>") or
 * not the expected "<config>" root. The Xml_node API's copy ctor is
 * public but the assignment is private; we return by value and let
 * the caller branch on the type tag instead.
 */
static bool _is_broadcast_root_empty(Genode::Xml_node const &n)
{
	return n.has_type("empty") || !n.has_type("config");
}


struct Alpha_probe
{
	Genode::Env &_env;

	Timer::Connection   _timer   { _env };
	Capture::Connection _capture { _env, "alpha-probe" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds {};

	/* pkgd channel: install hello. */
	Genode::Expanding_reporter     _pkg_request { _env, "request", "request" };
	Genode::Attached_rom_dataspace _pkg_result  { _env, "result" };

	/* configd channel: enable leitzentrale + read broadcast. */
	Genode::Expanding_reporter     _cfg_request { _env, "request", "config_request" };
	Genode::Attached_rom_dataspace _cfg_result  { _env, "config_result" };
	/*
	 * configd broadcast: in the alpha scenario (no inline config), read
	 * from "config" (served by report_rom). In the disk-desktop scenario,
	 * the inline <config skip_lz="yes"/> causes the sandbox to intercept
	 * "config" — so the broadcast is also available at "configd_config"
	 * via a separate report_rom policy. _cfgd_config is Constructible
	 * because the alpha scenario does not route it (denial is graceful).
	 */
	Genode::Attached_rom_dataspace               _cfg_broadcast { _env, "config" };
	Genode::Constructible<Genode::Attached_rom_dataspace> _cfgd_config { };

	/*
	 * Phase 16 W3: Xml_node is not copy-constructible (see
	 * genode/repos/base/include/util/xml_node.h), so each ROM
	 * read yields a fresh value that must NOT be copied or
	 * returned by value. We construct the snapshot on-demand
	 * from the underlying dataspace content (`local_addr` +
	 * `size()`) and hand the caller a pointer to a member-held
	 * storage. The pointer stays valid until the next call to
	 * _fetch_broadcast_root() — the probe is single-threaded.
	 */
	Genode::Xml_node _cfgd_root   { "<empty/>" };
	Genode::Xml_node _cfgsys_root { "<empty/>" };

	/* sponge-de's launcher report. */
	Genode::Attached_rom_dataspace _launcher { _env, "sponge_de_launcher" };

	bool _ok { true };

	/*
	 * Phase 8 P2: the Leitzentrale subsystem is not yet bootable from
	 * disk (P5 work). When <config skip_lz="yes"/> is set, the probe
	 * skips criterion (d) and the configd leitzentrale-enable step,
	 * logging PASS after (a)/(b)/(c) only. This keeps the same probe
	 * binary usable in both the full Alpha (lz on) and the P2 disk
	 * desktop (lz off) scenarios.
	 */
	bool _skip_lz { false };

	Alpha_probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("alpha-probe: FAIL ", reason);
		_env.parent().exit(1);
	}


	/*
	 * Report-style helpers: submit a request and poll the matching
	 * result ROM for an ok status. Shared shape with launcher_probe +
	 * theme_probe (proven).
	 */
	bool _pkg_install_and_wait(char const *pkg)
	{
		_pkg_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  "install");
			g.attribute("pkg", pkg);
		});

		_timer.msleep(300);
		for (unsigned i = 0; i < INSTALL_WAIT_ITERS; ++i) {
			_pkg_result.update();
			if (!_pkg_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _pkg_result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("install") &&
				    r.attribute_value("pkg", Genode::String<128>()) == Genode::String<128>(pkg) &&
				    r.has_attribute("status"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}

	bool _cfg_set_and_wait(char const *key, char const *value)
	{
		_cfg_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < LZ_ENABLE_WAIT_ITERS; ++i) {
			_cfg_result.update();
			if (!_cfg_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _cfg_result.xml();
				if (r.has_type("result") &&
				    r.attribute_value("op",  Genode::String<32>()) == Genode::String<32>("set") &&
				    r.attribute_value("key", Genode::String<128>()) == Genode::String<128>(key) &&
				    r.attribute_value("value", Genode::String<128>()) == Genode::String<128>(value) &&
				    r.attribute_value("status", Genode::String<32>()) == Genode::String<32>("ok"))
					return true;
			} catch (Genode::Xml_node::Invalid_syntax) { }
			_timer.msleep(100);
		}
		return false;
	}


	/*
	 * Criterion (a): themed panel composited. Poll the panel band
	 * (x=512, y=4..24) for non-background pixels. The nitpicker
	 * background #1e1e2e is non-zero but must NOT count as "panel
	 * rendered" — without this check, the background alone triggers a
	 * false positive on the disk-desktop scenario where sponge-de is
	 * slow to start.
	 */
	static Genode::uint32_t const BG_PIXEL = 0x1e1e2e;

	bool _panel_rendered()
	{
		for (unsigned i = 0; i < RENDER_POLL_ITERS && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			for (int y = 4; y <= 24; y += 4) {
				Pixel p = px[y * SCREEN_W + 512];
				if (p.pixel != 0 && p.pixel != BG_PIXEL) {
					Genode::log("alpha-probe: (a) panel band rendered at (512,", y,
					            ") = ", Genode::Hex(p.pixel));
					return true;
				}
			}

			if (i % 20 == 0) {
				Pixel p = px[14 * SCREEN_W + 512];
				Genode::log("alpha-probe: (a) panel poll ", i,
				            " pixel=", Genode::Hex(p.pixel));
			}
		}
		return false;
	}


	/*
	 * Criterion (c) — Phase 16 W3. The 8 baked keys the desktop
	 * profile seeds on first boot (D15.9 + D16.4). On any missing
	 * key the probe fails with the named sentry marker
	 * `defaults-firstboot-stub: FAIL (missing baked key ...)`
	 * the run scenario's stub gate matches against.
	 *
	 * Xml_node's copy ctor and assignment are private (see
	 * genode/repos/base/include/util/xml_node.h:209-222), so each
	 * ROM read yields a fresh value that must NOT be copied. We
	 * stash the latest snapshot in two member fields (one per
	 * ROM source) and hand the caller a pointer to whichever is
	 * current. The pointers stay valid until the next call into
	 * this function (no concurrent threads — the probe is single-
	 * threaded by design).
	 */
	bool _fetch_broadcast_root(Genode::Xml_node const *&out)
	{
		if (_cfgd_config.constructed()) {
			_cfgd_config->update();
			if (_cfgd_config->valid()) {
				/*
				 * Construct a fresh Xml_node from the underlying
				 * dataspace content. The copy ctor and operator=
				 * are private (see util/xml_node.h), so we MUST
				 * build the node in place from local_addr+size
				 * rather than reusing _cfgd_root.
				 */
				_cfgd_root.~Xml_node();
				Genode::construct_at<Genode::Xml_node>(&_cfgd_root,
					_cfgd_config->local_addr<char const>(),
					_cfgd_config->size());
				if (!_is_broadcast_root_empty(_cfgd_root)) {
					out = &_cfgd_root;
					return true;
				}
			}
		}
		_cfg_broadcast.update();
		if (!_cfg_broadcast.valid()) return false;
		_cfgsys_root.~Xml_node();
		Genode::construct_at<Genode::Xml_node>(&_cfgsys_root,
			_cfg_broadcast.local_addr<char const>(),
			_cfg_broadcast.size());
		if (_is_broadcast_root_empty(_cfgsys_root)) return false;
		out = &_cfgsys_root;
		return true;
	}

	bool _broadcast_has_baked_keys()
	{
		for (unsigned attempt = 0; attempt < CONFIGD_POLL_ITERS && _ok; ++attempt) {
			Genode::Xml_node const *root = nullptr;
			if (!_fetch_broadcast_root(root)) { _timer.msleep(100); continue; }

			for (unsigned i = 0; i < REQUIRED_BAKED_KEY_COUNT; ++i) {
				char const *need_name  = REQUIRED_BAKED_KEYS[i].name;
				char const *need_value = REQUIRED_BAKED_KEYS[i].value;
				bool found { false };
				root->for_each_sub_node("key", [&](Genode::Xml_node const &key) {
					if (found) return;
					if (key.attribute_value("name", Genode::String<64>()) !=
					    Genode::String<64>(need_name)) return;
					if (key.attribute_value("value", Genode::String<128>()) ==
					    Genode::String<128>(need_value))
						found = true;
				});
				if (!found) {
					/*
					 * Named sentry marker for the first missing
					 * baked key (plan W3: the gate is the FAIL
					 * line, not the PASS line). Emitted BEFORE
					 * _fail() so the run tool captures it as the
					 * gate's match pattern.
					 */
					Genode::error("alpha-probe: defaults-firstboot-stub: FAIL "
					              "(missing baked key ", need_name, "=",
					              need_value, ")");
					_fail(Genode::String<256>("missing baked key ",
					                          need_name, "=", need_value).string());
					return false;
				}
			}
			Genode::log("alpha-probe: (c) configd broadcast carries all ",
			            REQUIRED_BAKED_KEY_COUNT, " baked keys");
			return true;
		}
		return false;
	}


	/*
	 * Criterion (b): launcher report contains hello/Utilities.
	 *
	 * Phase 16 W3 extends the assertion to all 7 desktop packages the
	 * baked `desktop` profile pre-stages (D16.4 + plan W3 #3). The
	 * iteration order is canonical and stable; the per-pair timeout is
	 * bounded by LAUNCHER_PAIR_WAIT_ITERS (~30s) so the worst case is
	 * 7 * 30 = 210s. The probe fails on the first missing pair and
	 * emits the named sentry marker (defaults-firstboot-stub: FAIL ...)
	 * the run scenario's stub gate matches against.
	 */
	bool _launcher_has_pair(char const *name, char const *category)
	{
		_launcher.update();
		if (!_launcher.valid()) return false;
		try {
			Genode::Xml_node const root = _launcher.xml();
			if (!root.has_type("launcher")) return false;

			bool found { false };
			root.for_each_sub_node("app", [&](Genode::Xml_node const &a) {
				if (!found &&
				    a.attribute_value("name", Genode::String<64>())
				       == Genode::String<64>(name) &&
				    a.attribute_value("category", Genode::String<64>())
				       == Genode::String<64>(category))
					found = true;
			});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	bool _wait_launcher_has_pair(char const *name, char const *category)
	{
		for (unsigned i = 0; i < LAUNCHER_PAIR_WAIT_ITERS && _ok; ++i) {
			if (_launcher_has_pair(name, category)) {
				Genode::log("alpha-probe: (b) launcher report contains ",
				            name, "/", category);
				return true;
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Backwards-compatible name used by older scenarios (single-pair
	 * hello check). Kept so the (b) log line is still recognizable.
	 */
	bool _wait_launcher_has_hello()
	{
		return _wait_launcher_has_pair("hello", "Utilities");
	}

	/*
	 * Phase 16 W3 (D16.4 / plan W3 #3): iterate the full launcher set
	 * with a bounded per-pair timeout. Returns true when every expected
	 * pair is present; on first miss emits the named sentry marker the
	 * run script's stub gate matches and returns false.
	 *
	 * The first non-hello pair in the canonical iteration order is
	 * `terminal`, matching the plan's named sentry placeholder.
	 */
	bool _wait_launcher_has_all_pairs()
	{
		for (unsigned i = 0; i < LAUNCHER_PAIR_COUNT; ++i) {
			char const *name     = LAUNCHER_PAIRS[i].name;
			char const *category = LAUNCHER_PAIRS[i].category;
			if (_wait_launcher_has_pair(name, category)) continue;

			/*
			 * The plan's named sentry marker names the FIRST missing
			 * pair (canonical order: terminal). Emit the diagnostic
			 * BEFORE _fail() so the run script's stub gate matches
			 * the FAIL line first.
			 */
			Genode::error("alpha-probe: defaults-firstboot-stub: FAIL "
			              "(no bake-applied, alpha_probe extended set "
			              "timed out at ", name, ")");
			_fail(Genode::String<256>("launcher set timed out at ",
			                          name).string());
			return false;
		}
		Genode::log("alpha-probe: (b) launcher report contains all ",
		            LAUNCHER_PAIR_COUNT, " desktop packages");
		return true;
	}


	/*
	 * Criterion (d): lz_viewer marker pixel appears on the outer
	 * nitpicker. Same math as lz_viz_probe (proven).
	 */
	bool _lz_viewer_visible()
	{
		for (unsigned i = 0; i < LZ_VIEWER_POLL_ITERS && _ok; ++i) {
			_timer.msleep(100);
			_capture.capture_at(Capture::Point(0, 0));

			Pixel const *px = _cap_ds->local_addr<Pixel>();
			unsigned mark_pixels = 0;
			for (int y = MARK_Y; y < MARK_Y + MARK_H; ++y) {
				for (int x = MARK_X; x < MARK_X + MARK_W; ++x) {
					if (pixel_is_marker(px[y * SCREEN_W + x]))
						++mark_pixels;
				}
			}

			if (i % 10 == 0)
				Genode::log("alpha-probe: (d) lz_viewer poll ", i,
				            " marker pixels=", mark_pixels);

			if (mark_pixels >= MARK_PIXEL_THRESHOLD) {
				Genode::log("alpha-probe: (d) Leitzentrale window live (",
				            mark_pixels, " marker pixels at (", MARK_X, ",",
				            MARK_Y, "))");
				return true;
			}
		}
		return false;
	}


	void run()
	{
		Genode::log("alpha-probe: starting");

		/* Read optional <config skip_lz="yes"/> (Phase 8 P2 disk-desktop
		 * mode — the lz subsystem arrives in P5). Use .node() (not .xml())
		 * because init delivers inline configs in HID format, which Xml_node
		 * cannot parse but Node handles natively. */
		Genode::Attached_rom_dataspace cfg { _env, "config" };
		cfg.update();
		if (cfg.valid()) {
			_skip_lz = cfg.node().attribute_value("skip_lz", false);
			if (_skip_lz)
				Genode::log("alpha-probe: skip_lz=yes (criterion d deferred to P5)");
		}

		/*
		 * In the disk-desktop scenario (_skip_lz), open the
		 * "configd_config" ROM for the configd broadcast. This is
		 * routed by the disk-desktop run script; the inline "config"
		 * is intercepted by the sandbox (§configd_config workaround).
		 * Genode 26.05 session denial calls sleep_forever(), so this
		 * MUST only be opened when the routing is guaranteed.
		 */
		if (_skip_lz) {
			_cfgd_config.construct(_env, "configd_config");
			_cfgd_config->update();
		}

		/* Allocate the capture buffer (defines the outer panorama). */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Step 1: install hello via pkgd (for criterion b). In the
		 * full Alpha also enable leitzentrale via configd (for
		 * criterion d) — skipped when _skip_lz is set.
		 */
		Genode::log("alpha-probe: [1] install the 7 desktop packages via sponge_pkgd");
		for (unsigned i = 0; i < LAUNCHER_PAIR_COUNT; ++i) {
			char const *pkg = LAUNCHER_PAIRS[i].name;
			if (!_pkg_install_and_wait(pkg)) {
				_fail(Genode::String<256>("sponge_pkgd did not answer install ",
				                          pkg).string());
				return;
			}
			Genode::String<32> status { };
			try {
				status = _pkg_result.xml().attribute_value("status",
				                                           Genode::String<32>());
			} catch (Genode::Xml_node::Invalid_syntax) { }
			if (status != Genode::String<32>("ok")) {
				_fail(Genode::String<256>("install ", pkg, " returned: ",
				                          status).string());
				return;
			}
			Genode::log("alpha-probe: [1] install ", pkg, " ok");
		}

		if (!_skip_lz) {
			Genode::log("alpha-probe: [2] set leitzentrale.enabled=true");
			if (!_cfg_set_and_wait("leitzentrale.enabled", "true")) {
				_fail("configd did not accept set leitzentrale.enabled=true");
				return;
			}
			Genode::log("alpha-probe: [2] leitzentrale.enabled=true ack");
		}

		/*
		 * Step 2: assert criteria, each in bounded iterations.
		 * Order: (c) configd broadcast (now asserts the 8 baked
		 * keys — D16.4 / plan W3 #3), then (a) panel pixel, then
		 * (b) launcher report (now asserts the full 7-pair set).
		 * Criterion (d) lz_viewer pixel is last and only checked
		 * when _skip_lz is false.
		 */
		Genode::log("alpha-probe: [3] assert (c) configd broadcast carries "
		            "the baked key set");
		if (!_broadcast_has_baked_keys()) {
			_fail("configd broadcast did not carry the full baked key set");
			return;
		}

		Genode::log("alpha-probe: [4] assert (a) themed panel rendered");
		if (!_panel_rendered()) {
			_fail("themed panel never composited on nitpicker");
			return;
		}

		Genode::log("alpha-probe: [5] assert (b) launcher has all 7 desktop packages");
		if (!_wait_launcher_has_all_pairs()) {
			_fail("launcher report missing one or more desktop packages");
			return;
		}

		if (!_skip_lz) {
			Genode::log("alpha-probe: [6] assert (d) lz_viewer window visible");
			if (!_lz_viewer_visible()) {
				_fail("lz_viewer Leitzentrale window never appeared on screen");
				return;
			}
		}

		Genode::log("alpha-probe: PASS");
		_env.parent().exit(0);
	}
};


} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Alpha_probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
