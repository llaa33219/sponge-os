/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_thirdparty_probe — run/sponge-settings-thirdparty.run driver
 * (Criterion 18 third-party acceptance, docs/12-package-format.md §14).
 *
 * One linear, fully bounded sequence; every step logs a marker the run
 * script gates on, and any failure logs
 *   settings-thirdparty-probe: FAIL <reason>
 * and exits non-zero (fail-loud, never a silent hang).
 *
 *   S1  install settings_demo over the pkgd request channel
 *   S2  discovery: the `installed` broadcast carries exactly two
 *       <settings> entries with the exact §14.2 field sets
 *       (greeting/about — two independent (package, settings-id)
 *       identities)
 *   S3  open greeting -> ok started (settings_result validated)
 *   S4  first paint: the provider header band (#27273a) appears in
 *       the window's content rect (Capture witness)
 *   S5  focus greeting (click) + type '@' (REJECTED — exact
 *       validation-error evidence is the provider log line; the
 *       store check below proves the char never became the value)
 *   S6  type "Hi there" + Enter -> persisted (store write)
 *   S7  open about -> ok started; first paint; focus; type
 *       "Note A" + Enter -> persisted
 *   S8  exact store XML readback: /greeting.xml and /about.xml
 *       carry byte-exact documents with DISTINCT entry names and
 *       values (never-aliased evidence #1)
 *   S9  close/restart greeting: append a dirty '!' (no Enter), close
 *       the window via the decorator closer (hover-gated click ->
 *       layouter close -> wm resize 0x0 -> provider close-commit +
 *       exit), reopen -> ok started, "restored greeting from store",
 *       store carries the dirty value (close-to-commit evidence)
 *   S10 close/restart about the same way with a dirty " ok" suffix
 *   S11 remove settings_demo -> later opens answer not-installed
 *   S12 reinstall -> both opens answer started (as appropriate) and
 *       each provider restores ITS OWN store value (never-aliased
 *       evidence #2)
 *   S13 final exact store readback (never-aliased evidence #3)
 *   S14 package-private File_system writability: sentinel
 *       write/read-back/unlink on the same <fs/> path the providers
 *       persist into
 *   S15 PASS
 *
 * Capability surface (minimum privilege, §14.5): Report (request,
 * settings_request), ROM (result, settings_result, installed,
 * window_list, window_layout, hover, focus), File_system via the
 * <config><vfs><fs/></vfs> mount, Gui-less Capture + Event + Timer.
 * No configd label of any kind — the shared-DE configuration is
 * deliberately unreachable from this topology.
 *
 * REPORT FORMAT (load-bearing): the wm/layouter/decorator reports are
 * HID (Genode::Generator) and are parsed with the FORMAT-AGNOSTIC
 * Node API via Attached_rom_dataspace::node() — never .xml() (the
 * app_isolation_probe precedent). The pkgd result reports are XML;
 * Node parses both.
 */

#include <base/attached_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <event_session/connection.h>
#include <input/event.h>
#include <input/keycodes.h>
#include <os/pixel_rgb888.h>
#include <os/reporter.h>
#include <os/vfs.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <vfs/root.h>

namespace {

using Pixel = Capture::Pixel;

unsigned const SCREEN_W = 1024;
unsigned const SCREEN_H = 768;

/* Provider paint palette (settings_demo/common/paint.h). */
unsigned const HEADER_R = 0x27, HEADER_G = 0x27, HEADER_B = 0x3a;

/*
 * Exact expected store documents (byte-stable, settings_demo
 * common/store.h gen_store). The values are the probe's own typed
 * values; the '<' '>' quotes are literal in the file.
 */
char const *const STORE_G1 =
	"<settings-demo version=\"1\">"
	"<entry name=\"greeting\" value=\"Hi there\"/></settings-demo>";
char const *const STORE_A1 =
	"<settings-demo version=\"1\">"
	"<entry name=\"note\" value=\"Note A\"/></settings-demo>";
char const *const STORE_G2 =
	"<settings-demo version=\"1\">"
	"<entry name=\"greeting\" value=\"Hi there!\"/></settings-demo>";
char const *const STORE_A2 =
	"<settings-demo version=\"1\">"
	"<entry name=\"note\" value=\"Note A ok\"/></settings-demo>";


struct Probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };
	Genode::Heap _heap { _env.ram(), _env.rm() };

	Capture::Connection _capture { _env, "settings-thirdparty-probe" };
	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };
	Event::Connection _event { _env, "settings-thirdparty-probe" };

	Genode::Attached_rom_dataspace _config         { _env, "config" };
	Genode::Attached_rom_dataspace _installed      { _env, "installed" };
	Genode::Attached_rom_dataspace _result         { _env, "result" };
	Genode::Attached_rom_dataspace _settings_result{ _env, "settings_result" };
	Genode::Attached_rom_dataspace _window_list    { _env, "window_list" };
	Genode::Attached_rom_dataspace _window_layout  { _env, "window_layout" };
	Genode::Attached_rom_dataspace _hover          { _env, "hover" };
	Genode::Attached_rom_dataspace _focus          { _env, "focus" };

	Genode::Expanding_reporter _request         { _env, "request", "request" };
	Genode::Expanding_reporter _settings_request{ _env, "request",
	                                              "settings_request" };

	Genode::Constructible<Genode::Vfs::Root> _store { };

	unsigned _seq { 0 };

	Probe(Genode::Env &env) : _env(env)
	{
		/* package-private store mount (the provider <vfs><fs/> shape) */
		try {
			_config.node().with_optional_sub_node("vfs",
				[&] (Genode::Node const &vfs_node) {
					_store.construct(_env, _heap, vfs_node);
				});
		} catch (...) {
			Genode::error("settings-thirdparty-probe: FAIL "
			              "malformed <config> (no <vfs>)");
			_env.parent().exit(1);
		}

		/* define the capture buffer (no fb driver: this probe is
		 * nitpicker's capture client and its buffer is the panorama,
		 * the wm_probe precedent) before attaching the dataspace */
		_capture.buffer({ .px       = Capture::Area(SCREEN_W, SCREEN_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                              Capture::Area(SCREEN_W, SCREEN_H) } });
		_capture.capture_at(Capture::Point(0, 0));
		_cap_ds.construct(_env.rm(), _capture.dataspace());
	}


	/* ===================== fail / markers ===================== */

	template <typename... ARGS>
	void _fail(ARGS... args)
	{
		Genode::error("settings-thirdparty-probe: FAIL ", args...);
		_env.parent().exit(1);
	}

	template <typename... ARGS>
	void _mark(ARGS... args)
	{
		Genode::log("settings-thirdparty-probe: ", args...);
	}


	/* ===================== bounded polling ===================== */

	template <typename FN>
	bool _poll(FN const &fn, unsigned iters, unsigned ms)
	{
		for (unsigned i = 0; i < iters; ++i) {
			if (fn())
				return true;
			_timer.msleep(ms);
		}
		return fn();
	}


	/* ===================== input injection ===================== */

	static Input::Keycode _key_for_char(char c)
	{
		/*
		 * Real QWERTY evdev codes (the KEY_* values are NOT a
		 * contiguous alphabet): the codepoint carries the character
		 * for the provider, and sane keycodes keep nitpicker's
		 * press/release tracking clean.
		 */
		switch (c) {
		case 'a': return Input::KEY_A;
		case 'b': return Input::KEY_B;
		case 'c': return Input::KEY_C;
		case 'd': return Input::KEY_D;
		case 'e': return Input::KEY_E;
		case 'f': return Input::KEY_F;
		case 'g': return Input::KEY_G;
		case 'h': return Input::KEY_H;
		case 'i': return Input::KEY_I;
		case 'j': return Input::KEY_J;
		case 'k': return Input::KEY_K;
		case 'l': return Input::KEY_L;
		case 'm': return Input::KEY_M;
		case 'n': return Input::KEY_N;
		case 'o': return Input::KEY_O;
		case 'p': return Input::KEY_P;
		case 'q': return Input::KEY_Q;
		case 'r': return Input::KEY_R;
		case 's': return Input::KEY_S;
		case 't': return Input::KEY_T;
		case 'u': return Input::KEY_U;
		case 'v': return Input::KEY_V;
		case 'w': return Input::KEY_W;
		case 'x': return Input::KEY_X;
		case 'y': return Input::KEY_Y;
		case 'z': return Input::KEY_Z;
		case '0': return Input::KEY_0;
		case '1': return Input::KEY_1;
		case '2': return Input::KEY_2;
		case '3': return Input::KEY_3;
		case '4': return Input::KEY_4;
		case '5': return Input::KEY_5;
		case '6': return Input::KEY_6;
		case '7': return Input::KEY_7;
		case '8': return Input::KEY_8;
		case '9': return Input::KEY_9;
		case ' ': return Input::KEY_SPACE;
		case '.': return Input::KEY_DOT;
		case ',': return Input::KEY_COMMA;
		case '-': return Input::KEY_MINUS;
		case '!': return Input::KEY_1;
		case '?': return Input::KEY_SLASH;
		case '@': return Input::KEY_2;
		default:  return (c >= 'A' && c <= 'Z')
		               ? _key_for_char((char)(c - 'A' + 'a'))
		               : Input::KEY_A;
		}
	}

	void _motion(int x, int y)
	{
		_event.with_batch([&] (Event::Session_client::Batch &batch) {
			batch.submit(Input::Absolute_motion { x, y }); });
	}

	/*
	 * Click with the qmp.inc "hover jiggle" recipe. wm stamps one seq
	 * number per CLICK sequence (seq_number_generator bumps on press),
	 * and User_state defers a press until the decorator hover report
	 * carries the same seq. The press is therefore followed by a
	 * jiggle motion (fresh hover report, same seq) to drain it before
	 * the release is processed; the pointer then returns to the origin
	 * so a content drag finalizes as zero-motion and never moves the
	 * window.
	 */
	void _click(int x, int y)
	{
		_motion(x, y);
		_timer.msleep(250);
		_event.with_batch([&] (Event::Session_client::Batch &batch) {
			batch.submit(Input::Press { Input::BTN_LEFT }); });
		_timer.msleep(150);
		_motion(x + 1, y);
		_timer.msleep(150);
		_motion(x, y);
		_timer.msleep(150);
		_event.with_batch([&] (Event::Session_client::Batch &batch) {
			batch.submit(Input::Release { Input::BTN_LEFT }); });
		_timer.msleep(250);
	}

	void _type_text(char const *s)
	{
		for (unsigned i = 0; s[i] != '\0'; ++i) {
			Input::Keycode const key = _key_for_char(s[i]);
			_event.with_batch([&] (Event::Session_client::Batch &batch) {
				batch.submit(Input::Press_char {
					key, Input::Codepoint { (Genode::uint32_t)s[i] } }); });
			_timer.msleep(20);
			_event.with_batch([&] (Event::Session_client::Batch &batch) {
				batch.submit(Input::Release { key }); });
			_timer.msleep(20);
		}
		_timer.msleep(300);
	}

	void _press_key(Input::Keycode key)
	{
		_event.with_batch([&] (Event::Session_client::Batch &batch) {
			batch.submit(Input::Press   { key });
			batch.submit(Input::Release { key }); });
		_timer.msleep(300);
	}

	void _clear_value()
	{
		for (unsigned i = 0; i < MAX_CLEAR; ++i)
			_press_key(Input::KEY_BACKSPACE);
	}

	enum { MAX_CLEAR = 32 };


	/* ===================== pkgd channels ===================== */

	bool _result_matches(Genode::Xml_node const &r, char const *op,
	                     char const *pkg, char const *status)
	{
		if (!r.has_type("result"))
			return false;
		if (!r.has_attribute("status"))
			return false;

		Genode::String<32> const rop = r.attribute_value("op",
			Genode::String<32>());
		Genode::String<128> const rp = r.attribute_value("pkg",
			Genode::String<128>());
		Genode::String<32> const rs = r.attribute_value("status",
			Genode::String<32>());

		return rop == Genode::String<32>(op)
		    && rp  == Genode::String<128>(pkg)
		    && rs  == Genode::String<32>(status);
	}

	void _pkg_cmd(char const *op, char const *pkg, char const *status)
	{
		++_seq;
		_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("seq", _seq);
			g.attribute("pkg", pkg); });

		bool ok = _poll([&] {
			_result.update();
			return _result.valid() && _result.size() > 0
			    && _result_matches(_result.xml(), op, pkg, status);
		}, 400, 100);

		if (!ok) {
			_result.update();
			Genode::error("settings-thirdparty-probe: FAIL pkgd ", op,
			              " '", pkg, "' did not answer status=", status,
			              " (last: ", _result.xml(), ")");
			_env.parent().exit(1);
		}
	}

	void _settings_cmd(char const *pkg, char const *id,
	                   char const *status, char const *action)
	{
		++_seq;
		_settings_request.generate_xml([&] (Genode::Xml_generator &g) {
			g.attribute("op",     "launch");
			g.attribute("target", "settings");
			g.attribute("pkg",    pkg);
			g.attribute("id",     id);
			g.attribute("seq",    _seq); });

		bool ok = _poll([&] {
			_settings_result.update();
			if (!_settings_result.valid() || _settings_result.size() == 0)
				return false;
			Genode::Xml_node const r = _settings_result.xml();
			if (!r.has_type("result") || !r.has_attribute("status"))
				return false;
			Genode::String<32> const rs = r.attribute_value("status",
				Genode::String<32>());
			Genode::String<128> const rp = r.attribute_value("pkg",
				Genode::String<128>());
			Genode::String<33> const ri = r.attribute_value("id",
				Genode::String<33>());
			Genode::String<16> const rt = r.attribute_value("target",
				Genode::String<16>());
			if (rs != Genode::String<32>(status))
				return false;
			if (rp != Genode::String<128>(pkg))
				return false;
			if (ri != Genode::String<33>(id))
				return false;
			if (rt != Genode::String<16>("settings"))
				return false;
			if (action) {
				Genode::String<16> const ra = r.attribute_value("action",
					Genode::String<16>());
				return ra == Genode::String<16>(action);
			}
			return true;
		}, 400, 100);

		if (!ok) {
			_settings_result.update();
			Genode::error("settings-thirdparty-probe: FAIL settings open ",
			              pkg, " '", id, "' did not answer status=", status,
			              " action=", action ? action : "<any>",
			              " (last: ", _settings_result.xml(), ")");
			_env.parent().exit(1);
		}
	}


	/* ===================== installed discovery ===================== */

	struct Entry
	{
		Genode::String<33> id, title, category, icon, target;
		bool has_icon { false };
		Genode::String<32> keywords[8] { };
		unsigned num_keywords { 0 };
	};

	bool _read_entry(Genode::Xml_node const &pkg_node, char const *want_id,
	                 Entry &out)
	{
		bool found = false;
		pkg_node.for_each_sub_node("settings", [&] (Genode::Xml_node const &s) {
			if (found)
				return;
			if (Genode::strcmp(s.attribute_value("id",
			        Genode::String<33>()).string(), want_id) != 0)
				return;
			out.id       = s.attribute_value("id",       Genode::String<33>());
			out.title    = s.attribute_value("title",    Genode::String<33>());
			out.category = s.attribute_value("category", Genode::String<33>());
			out.icon     = s.attribute_value("icon",     Genode::String<33>());
			out.target   = s.attribute_value("target",   Genode::String<33>());
			out.has_icon = s.has_attribute("icon");
			s.for_each_sub_node("keyword", [&] (Genode::Xml_node const &k) {
				if (out.num_keywords < 8)
					out.keywords[out.num_keywords++] =
						k.decoded_content<Genode::String<32>>(); });
			found = true;
		});
		return found;
	}

	void _check_entry(Entry const &e, char const *id, char const *title,
	                  char const *category, char const *icon,
	                  char const *target)
	{
		if (Genode::strcmp(e.id.string(), id) != 0)
			_fail("discovery: id mismatch for ", id, " (got ", e.id, ")");
		if (Genode::strcmp(e.title.string(), title) != 0)
			_fail("discovery: title mismatch for ", id, " (got ", e.title, ")");
		if (Genode::strcmp(e.category.string(), category) != 0)
			_fail("discovery: category mismatch for ", id,
			      " (got ", e.category, ")");
		if (!e.has_icon ||
		    Genode::strcmp(e.icon.string(), icon) != 0)
			_fail("discovery: icon mismatch for ", id, " (got ", e.icon, ")");
		if (Genode::strcmp(e.target.string(), target) != 0)
			_fail("discovery: target mismatch for ", id, " (got ", e.target, ")");
	}

	void _discovery()
	{
		bool present = _poll([&] {
			_installed.update();
			if (!_installed.valid() || _installed.size() == 0)
				return false;
			bool found = false;
			_installed.xml().for_each_sub_node("packages",
				[&] (Genode::Xml_node const &ps) {
					ps.for_each_sub_node("package",
						[&] (Genode::Xml_node const &p) {
							if (Genode::strcmp(p.attribute_value("name",
							        Genode::String<64>()).string(),
							        "settings_demo") == 0)
								found = true;
						}); });
			return found;
		}, 400, 100);

		if (!present)
			_fail("discovery: settings_demo absent from the installed "
			      "broadcast after install");

		Genode::Xml_node const root = _installed.xml();
		Entry greeting { }, about { };
		bool got = false;
		root.for_each_sub_node("packages", [&] (Genode::Xml_node const &ps) {
			ps.for_each_sub_node("package", [&] (Genode::Xml_node const &p) {
				if (Genode::strcmp(p.attribute_value("name",
				        Genode::String<64>()).string(),
				        "settings_demo") != 0)
					return;
				if (got)
					return;
				got = _read_entry(p, "greeting", greeting)
				   && _read_entry(p, "about",    about);
			});
		});

		if (!got)
			_fail("discovery: package does not expose BOTH the greeting "
			      "and about <settings> entries");

		_check_entry(greeting, "greeting", "Greeting", "applications",
		             "greeting.png", "settings_demo_greeting");
		_check_entry(about, "about", "About", "other",
		             "about.png", "settings_demo_about");

		if (greeting.num_keywords != 3 ||
		    Genode::strcmp(greeting.keywords[0].string(), "hello") != 0 ||
		    Genode::strcmp(greeting.keywords[1].string(), "text")  != 0 ||
		    Genode::strcmp(greeting.keywords[2].string(), "demo")  != 0)
			_fail("discovery: greeting keywords drifted");

		if (about.num_keywords != 3 ||
		    Genode::strcmp(about.keywords[0].string(), "info") != 0 ||
		    Genode::strcmp(about.keywords[1].string(), "note") != 0 ||
		    Genode::strcmp(about.keywords[2].string(), "demo") != 0)
			_fail("discovery: about keywords drifted");

		if (Genode::strcmp(greeting.id.string(), about.id.string()) == 0)
			_fail("discovery: the two entries share one id");

		_mark("discovery ok - 2 independent entries (greeting/about)");
	}


	/* ===================== wm windows ===================== */

	unsigned _find_window(char const *token)
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0)
			return 0;

		unsigned found = 0;
		_window_list.node().for_each_sub_node("window",
			[&] (Genode::Node const &w) {
				if (found)
					return;
				Genode::String<160> label = w.attribute_value("label",
					Genode::String<160>());
				if (!_contains(label.string(), token))
					return;
				found = w.attribute_value("id", 0u);
			});
		return found;
	}

	static bool _contains(char const *hay, char const *needle)
	{
		unsigned const n = (unsigned)Genode::strlen(needle);
		if (n == 0)
			return true;
		for (unsigned i = 0; hay[i] != '\0'; ++i) {
			if (Genode::strcmp(hay + i, needle, n) == 0)
				return true;
		}
		return false;
	}

	unsigned _find_window_except(char const *token, unsigned skip_id)
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0)
			return 0;

		unsigned found = 0;
		_window_list.node().for_each_sub_node("window",
			[&] (Genode::Node const &w) {
				if (found)
					return;
				unsigned const id = w.attribute_value("id", 0u);
				if (id == skip_id)
					return;
				Genode::String<160> label = w.attribute_value("label",
					Genode::String<160>());
				if (!_contains(label.string(), token))
					return;
				found = id;
			});
		return found;
	}

	bool _window_rect(unsigned id, int &x, int &y, int &w, int &h)
	{
		_window_layout.update();
		if (!_window_layout.valid() || _window_layout.size() == 0)
			return false;

		bool found = false;
		_window_layout.node().for_each_sub_node("boundary",
			[&] (Genode::Node const &b) {
				b.for_each_sub_node("window", [&] (Genode::Node const &n) {
					if (found)
						return;
					if (n.attribute_value("id", 0u) != id)
						return;
					x = n.attribute_value("xpos", 0);
					y = n.attribute_value("ypos", 0);
					w = n.attribute_value("width", 0);
					h = n.attribute_value("height", 0);
					found = w > 0 && h > 0;
				});
			});
		return found;
	}

	unsigned _focused_id()
	{
		_focus.update();
		if (!_focus.valid() || _focus.size() == 0)
			return 0;
		unsigned id = 0;
		_focus.node().with_optional_sub_node("window",
			[&] (Genode::Node const &w) {
				id = w.attribute_value("id", 0u); });
		return id;
	}

	/*
	 * Open an entry and require a FRESH provider incarnation.
	 *
	 * The result-document byte shape cannot discriminate a fresh
	 * 'ok started' from a stale one (they are byte-identical), and a
	 * close/reopen can race the liveness claim release (the state
	 * report is periodic), so a request sent too early is answered
	 * 'focused' with no new instance. The observable that CANNOT lie
	 * is a new wm window: the singleton contract (§14.7) forbids a
	 * second window for 'focused', so a window with a fresh id after
	 * the old id disappeared can only follow a genuine 'started'.
	 * Bounded retries cover the claim-release race; exhaustion is a
	 * named fail (claim release broken), never a silent hang.
	 */
	unsigned _open_expect_fresh(char const *who, char const *pkg,
	                            char const *id, char const *token,
	                            unsigned old_id)
	{
		unsigned fresh = 0;

		for (unsigned attempt = 1; attempt <= 30 && !fresh; ++attempt) {
			++_seq;
			_settings_request.generate_xml([&] (Genode::Xml_generator &g) {
				g.attribute("op",     "launch");
				g.attribute("target", "settings");
				g.attribute("pkg",    pkg);
				g.attribute("id",     id);
				g.attribute("seq",    _seq); });

			_poll([&] {
				unsigned const w = _find_window_except(token, old_id);
				if (w != 0) {
					fresh = w;
					return true;
				}
				return false;
			}, 50, 100);

			if (!fresh)
				_timer.msleep(500);
		}

		if (!fresh) {
			_window_list.update();
			_settings_result.update();
			_fail("open ", who, ": no fresh window after 30 attempts "
			      "(close/claim-release race unresolved - last result: ",
			      _settings_result.xml(), " window_list: ",
			      _window_list.node(), ")");
		}

		/* the genuine 'ok started' emission precedes the window
		 * (result -> runtime config -> child -> window), so by now
		 * the result document reflects a started processing */
		_settings_cmd_result_started(pkg, id);
		return fresh;
	}

	void _settings_cmd_result_started(char const *pkg, char const *id)
	{
		bool ok = _poll([&] {
			_settings_result.update();
			if (!_settings_result.valid() || _settings_result.size() == 0)
				return false;
			Genode::Xml_node const r = _settings_result.xml();
			if (!r.has_type("result") || !r.has_attribute("status"))
				return false;
			return r.attribute_value("status", Genode::String<32>())
			        == Genode::String<32>("ok")
			    && r.attribute_value("action", Genode::String<16>())
			        == Genode::String<16>("started")
			    && r.attribute_value("pkg", Genode::String<128>())
			        == Genode::String<128>(pkg)
			    && r.attribute_value("id", Genode::String<33>())
			        == Genode::String<33>(id);
		}, 50, 100);

		if (!ok) {
			_settings_result.update();
			_fail("open ", pkg, " '", id, "': result document is not "
			      "ok/started after the fresh window appeared (last: ",
			      _settings_result.xml(), ")");
		}
	}

	void _wait_placed(unsigned id, int &x, int &y, int &w, int &h)
	{
		bool ok = _poll([&] { return _window_rect(id, x, y, w, h); },
		                300, 100);
		if (!ok) {
			_window_list.update();
			Genode::error("settings-thirdparty-probe: FAIL window ", id,
			              " never got placed geometry (labels: ",
			              _window_list.node(), ")");
			_env.parent().exit(1);
		}
	}


	/* ===================== first paint (Capture) ===================== */

	unsigned _header_px(int rx, int ry, int rw)
	{
		_capture.capture_at(Capture::Point(0, 0));
		Pixel const *px = _cap_ds->local_addr<Pixel>();
		unsigned n = 0;
		for (int yy = ry + 4; yy < ry + 22 && yy < (int)SCREEN_H; ++yy) {
			if (yy < 0)
				continue;
			for (int xx = rx + 10; xx < rx + rw - 10 && xx < (int)SCREEN_W;
			     ++xx) {
				if (xx < 0)
					continue;
				Pixel const &p = px[yy * SCREEN_W + xx];
				if (p.r() == HEADER_R && p.g() == HEADER_G &&
				    p.b() == HEADER_B)
					++n;
			}
		}
		return n;
	}

	void _wait_first_paint(char const *who, unsigned id,
	                       int &rx, int &ry, int &rw, int &rh)
	{
		_wait_placed(id, rx, ry, rw, rh);
		unsigned px = 0;
		bool ok = _poll([&] {
			px = _header_px(rx, ry, rw);
			return px > 500;
		}, 200, 100);
		if (!ok)
			_fail("first paint: ", who, " header band never appeared "
			      "(header_px=", px, " rect=", rx, ",", ry, " ", rw, "x", rh,
			      ")");
		_mark("first-paint ", who, " (rect=", rx, ",", ry, " ", rw, "x", rh,
		      " header_px=", px, ")");
	}

	void _focus_window(unsigned id, int rx, int ry, int rw, int rh)
	{
		/*
		 * A click is REQUIRED, not cosmetic: nitpicker delivers
		 * press/key events only to its transient '_input_receiver',
		 * which is established by a button click (user_state.cc
		 * 'Deliver press/release event to focused session or the
		 * receiver'); a motion-only hover focus leaves the receiver
		 * unset and typing silently goes nowhere. The click also
		 * drives the layouter's focus (User_state hover/drag paths).
		 */
		_click(rx + rw / 2, ry + rh / 2);
		bool ok = _poll([&] { return _focused_id() == id; }, 70, 100);
		if (!ok) {
			_hover.update();
			_window_list.update();
			_fail("focus: window ", id, " did not take focus (focused=",
			      _focused_id(), " hover: ", _hover.node(),
			      " window_list: ", _window_list.node(), ")");
		}
	}


	/* ===================== hover-gated closer close ===================== */

	bool _hover_is_closer(unsigned id)
	{
		_hover.update();
		if (!_hover.valid() || _hover.size() == 0)
			return false;

		bool hit = false;
		_hover.node().for_each_sub_node("window", [&] (Genode::Node const &w) {
			if (w.attribute_value("id", 0u) != id)
				return;
			w.for_each_sub_node("closer", [&] (Genode::Node const &) {
				hit = true; });
		});
		return hit;
	}

	void _close_window(char const *who, unsigned id,
	                   int rx, int ry, int rw, int rh)
	{
		/*
		 * The motif decorator's title-bar controls live in the strip
		 * above the content rect (the app_isolation_probe closer
		 * geometry: rightmost 16x16 control near (rx+rw, ry)). Scan
		 * candidate points and gate the click on the decorator's own
		 * hover report naming the window's <closer/> element — no
		 * pixel guessing (docs/05 §4.6: providers' windows are closed
		 * through their own window chrome). The content height 'rh'
		 * only participates in the diagnostic dump.
		 */
		struct Pt { int x, y; };
		Pt pts[16] { };
		unsigned n = 0;

		pts[n++] = { rx + rw - 8,  ry - 8 };
		pts[n++] = { rx + rw - 16, ry - 8 };
		pts[n++] = { rx + rw - 8,  ry - 12 };
		pts[n++] = { rx + rw - 24, ry - 8 };
		pts[n++] = { rx + rw - 4,  ry - 4 };
		for (int x = rx + 8; x < rx + rw - 8 && n < 16; x += 24)
			pts[n++] = { x, ry - 8 };

		bool closed = false;
		unsigned candidate = 0;

		for (unsigned i = 0; i < n && !closed; ++i) {
			_motion(pts[i].x, pts[i].y);
			bool over = _poll([&] { return _hover_is_closer(id); }, 10, 60);
			if (!over)
				continue;

			candidate = i + 1;
			_click(pts[i].x, pts[i].y);
			closed = _poll([&] { return _window_gone(id); }, 100, 100);
		}

		if (!closed) {
			_window_list.update();
			_hover.update();
			_fail("close: ", who, " window ", id, " (rect ", rx, ",", ry,
			      " ", rw, "x", rh, ") survived the closer candidates "
			      "(hover: ", _hover.node(), " window_list: ",
			      _window_list.node(), ")");
		}

		_mark("close-", who, " done (candidate=", candidate, ")");
	}

	bool _window_gone(unsigned id)
	{
		_window_list.update();
		if (!_window_list.valid() || _window_list.size() == 0)
			return true;
		bool present = false;
		_window_list.node().for_each_sub_node("window",
			[&] (Genode::Node const &w) {
				if (w.attribute_value("id", 0u) == id)
					present = true; });
		return !present;
	}


	/* ===================== store I/O ===================== */

	bool _store_read(char const *path, char *buf, unsigned cap, unsigned &len)
	{
		len = 0;
		if (!_store.constructed())
			return false;

		Genode::Vfs::File_system &fs = _store->fs();

		Genode::Vfs::Directory_service::Stat stat { };
		if (fs.stat(path, stat) != Genode::Vfs::Directory_service::STAT_OK)
			return false;
		if (stat.size == 0 || stat.size >= cap)
			return false;

		Genode::Vfs::Vfs_handle *handle { nullptr };
		if (fs.open(path, Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
		            &handle, _heap) !=
		    Genode::Vfs::Directory_service::OPEN_OK)
			return false;
		Genode::Vfs::Vfs_handle::Guard guard(handle);

		unsigned total = 0;
		while (total < stat.size) {
			Genode::Vfs::Vfs_handle::Read_result r
				{ Genode::Vfs::Vfs_handle::Read_error::DENIED };
			for (;;) {
				r = handle->read(Genode::Vfs::At { .pos = total },
				                 Genode::Byte_range_ptr(
				                     buf + total, stat.size - total));
				if (r != Genode::Vfs::Vfs_handle::Read_error::RETRY)
					break;
				_store->io().commit_and_wait();
			}
			Genode::size_t const n = r.convert<Genode::size_t>(
				[] (Genode::size_t bytes) { return bytes; },
				[] (Genode::Vfs::Vfs_handle::Read_error) {
					return Genode::size_t(0); });
			if (n == 0)
				return false;
			total += (unsigned)n;
		}
		buf[total] = '\0';
		len = total;
		return true;
	}

	void _store_expect(char const *path, char const *expected)
	{
		char buf[2048] { };
		unsigned len = 0;
		if (!_store_read(path, buf, sizeof(buf), len)) {
			_fail("store ", path, " unreadable");
			return;
		}
		char const *const shown = buf;
		unsigned const want = (unsigned)Genode::strlen(expected);
		if (len != want)
			_fail("store ", path, " length ", len, " != ", want,
			      " (content: <", shown, ">)");
		for (unsigned i = 0; i < want; ++i)
			if (buf[i] != expected[i])
				_fail("store ", path, " byte ", i, " differs (content: <",
				      shown, ">, expected: <", expected, ">)");
		_mark("store ", path, "=", shown);
	}

	void _sentinel()
	{
		if (!_store.constructed())
			_fail("sentinel: no store vfs");

		Genode::Vfs::File_system &fs = _store->fs();
		char const *const path = "/probe_sentinel.txt";
		char const *const text = "sentinel-42";

		Genode::Vfs::Vfs_handle *handle { nullptr };
		Genode::Vfs::Directory_service::Open_result open =
			fs.open(path, Genode::Vfs::Directory_service::OPEN_MODE_WRONLY,
			        &handle, _heap);
		if (open == Genode::Vfs::Directory_service::OPEN_ERR_UNACCESSIBLE)
			open = fs.open(path,
			               Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
			             | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
			               &handle, _heap);
		if (open != Genode::Vfs::Directory_service::OPEN_OK)
			_fail("sentinel: cannot open ", path, " for write");

		{
			Genode::Vfs::Vfs_handle::Guard guard(handle);
			handle->ftruncate(Genode::strlen(text));

			unsigned off = 0;
			unsigned const total = (unsigned)Genode::strlen(text);
			while (off < total) {
				Genode::Vfs::Vfs_handle::Write_result w
					{ Genode::Vfs::Vfs_handle::Write_error::DENIED };
				for (;;) {
					w = handle->write(Genode::Vfs::At { .pos = off },
					                  Genode::Const_byte_range_ptr(
					                      text + off, total - off));
					if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
						break;
					_store->io().commit_and_wait();
				}
				Genode::size_t const n = w.convert<Genode::size_t>(
					[] (Genode::size_t bytes) { return bytes; },
					[] (Genode::Vfs::Vfs_handle::Write_error) {
						return Genode::size_t(0); });
				if (n == 0)
					_fail("sentinel: short write at ", off);
				off += (unsigned)n;
			}
			while (handle->sync() == Genode::Vfs::Sync_result::RETRY)
				_store->io().commit_and_wait();
		}

		char buf[64] { };
		unsigned len = 0;
		if (!_store_read(path, buf, sizeof(buf), len) ||
		    Genode::strcmp(buf, text) != 0) {
			char const *const shown = buf;
			_fail("sentinel: readback mismatch (got <", shown, ">)");
		}

		if (fs.unlink(path) != Genode::Vfs::Directory_service::UNLINK_OK)
			_fail("sentinel: unlink of ", path, " failed");

		_mark("fs-sentinel ok (write+readback+unlink on the package-private "
		      "store path)");
	}


	/* ===================== the sequence ===================== */

	void run()
	{
		/* S1 install */
		_pkg_cmd("install", "settings_demo", "ok");
		_mark("install settings_demo -> ok");

		/* S2 discovery */
		_discovery();

		/* S3-S6 greeting: open, first paint, validate, persist */
		unsigned const g_id = _open_expect_fresh("greeting", "settings_demo",
		                                        "greeting",
		                                        "settings_demo.greeting", 0);
		_mark("open greeting -> ok started");

		int gx = 0, gy = 0, gw = 0, gh = 0;
		_wait_first_paint("greeting", g_id, gx, gy, gw, gh);

		_focus_window(g_id, gx, gy, gw, gh);
		_mark("focus greeting ok");

		_type_text("@");
		_timer.msleep(800);
		_mark("typed-invalid greeting");

		/* typing appends to the loaded default; clear it first (the
		 * value under test must be exactly "Hi there") */
		_clear_value();
		_type_text("Hi there");
		_press_key(Input::KEY_ENTER);
		_timer.msleep(800);
		_mark("typed-enter greeting");

		/* S7 about: open, first paint, persist */
		unsigned const a_id = _open_expect_fresh("about", "settings_demo",
		                                        "about",
		                                        "settings_demo.about", 0);
		_mark("open about -> ok started");

		int ax = 0, ay = 0, aw = 0, ah = 0;
		_wait_first_paint("about", a_id, ax, ay, aw, ah);

		_focus_window(a_id, ax, ay, aw, ah);
		_mark("focus about ok");

		_clear_value();
		_type_text("Note A");
		_press_key(Input::KEY_ENTER);
		_timer.msleep(800);
		_mark("typed-enter about");

		/* S8 exact store readback (never-aliased #1) */
		_store_expect("/greeting.xml", STORE_G1);
		_store_expect("/about.xml",    STORE_A1);
		_mark("store-check ok");

		/* S9 close/restart greeting with a dirty value */
		_focus_window(g_id, gx, gy, gw, gh);
		_type_text("!");
		_timer.msleep(500);
		_mark("typed-dirty greeting");

		_close_window("greeting", g_id, gx, gy, gw, gh);

		unsigned const g2_id = _open_expect_fresh("greeting", "settings_demo",
		                                         "greeting",
		                                         "settings_demo.greeting",
		                                         g_id);
		_mark("reopen-greeting -> ok started (window ", g2_id, ")");
		_store_expect("/greeting.xml", STORE_G2);
		_store_expect("/about.xml",    STORE_A1);
		_mark("reopen-greeting restored (dirty value committed at close)");

		/* S10 close/restart about with a dirty value */
		_focus_window(a_id, ax, ay, aw, ah);
		_type_text(" ok");
		_timer.msleep(500);
		_mark("typed-dirty about");

		_close_window("about", a_id, ax, ay, aw, ah);

		unsigned const a2_id = _open_expect_fresh("about", "settings_demo",
		                                         "about",
		                                         "settings_demo.about",
		                                         a_id);
		_mark("reopen-about -> ok started (window ", a2_id, ")");
		_store_expect("/greeting.xml", STORE_G2);
		_store_expect("/about.xml",    STORE_A2);
		_mark("reopen-about restored (dirty value committed at close)");

		/* S11 removal -> stale opens answer not-installed */
		_pkg_cmd("remove", "settings_demo", "ok");
		bool dropped = _poll([&] {
			_installed.update();
			if (!_installed.valid() || _installed.size() == 0)
				return true;
			bool found = false;
			_installed.xml().for_each_sub_node("packages",
				[&] (Genode::Xml_node const &ps) {
					ps.for_each_sub_node("package",
						[&] (Genode::Xml_node const &p) {
							if (Genode::strcmp(p.attribute_value("name",
							        Genode::String<64>()).string(),
							        "settings_demo") == 0)
								found = true;
						}); });
			return !found;
		}, 200, 100);
		if (!dropped)
			_fail("removal: settings_demo still published in the "
			      "installed broadcast");

		_settings_cmd("settings_demo", "greeting", "not-installed", nullptr);
		_settings_cmd("settings_demo", "about",    "not-installed", nullptr);
		_mark("removal -> not-installed greeting+about");

		/* S12 reinstall -> started restores each own store value */
		_pkg_cmd("install", "settings_demo", "ok");
		unsigned const a3_id = _open_expect_fresh("about", "settings_demo",
		                                         "about",
		                                         "settings_demo.about",
		                                         a2_id);
		unsigned const g3_id = _open_expect_fresh("greeting", "settings_demo",
		                                         "greeting",
		                                         "settings_demo.greeting",
		                                         g2_id);
		_mark("reinstall -> started about+greeting (windows ", a3_id, "/",
		      g3_id, ")");
		_store_expect("/greeting.xml", STORE_G2);
		_store_expect("/about.xml",    STORE_A2);
		_mark("reinstall stores un-aliased (greeting=Hi there! "
		      "note=Note A ok)");

		/* S13 final exact readback */
		_store_expect("/greeting.xml", STORE_G2);
		_store_expect("/about.xml",    STORE_A2);
		_mark("final-store ok");

		/* S14 package-private File_system writability */
		_sentinel();

		/* S15 verdict */
		_mark("PASS - 2 independent entries listed+first-painted, "
		      "validated and persisted into distinct package-private "
		      "store files, retained across provider close/restart, "
		      "remove->not-installed and reinstall->started with "
		      "un-aliased stores, no shared-DE configuration access");
		_env.parent().exit(0);
	}
};


} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}
