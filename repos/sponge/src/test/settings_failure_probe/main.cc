/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_failure_probe — Criterion 18 settings failure-contract
 * probe (companion of run/sponge-settings-failure.run).
 *
 * A FOCUSED harness with exactly four jobs (target.mk carries the
 * rationale); marker grammar "settings-failure-probe: ...", every wait
 * bounded, every contract deviation a loud FAIL.
 *
 * 1. Gui service for the settings providers that sponge_pkgd launches
 *    into pkg_runtime. Each Gui::Session is self-contained (its own
 *    framebuffer dataspace, Input::Session_component, and info ROM
 *    backed by Dynamic_rom_session) — the providers' windows are real
 *    sessions with real buffers, and the v1.1 app-visible close
 *    channel (docs/05 §4.6) is delivered for real: the info ROM flips
 *    from <capture width/height> to <capture closed="yes"/>, which is
 *    what sponge_wm's wm_session.cc emits on a window close, and the
 *    provider's production _handle_info commits and exits(0). A window
 *    is never hidden to fake a crash: the provider's own exit path is
 *    the only termination used.
 *
 * 2. State-report evidence. The probe watches pkg_runtime's `state`
 *    report — the same HID report sponge_pkgd's <settings_liveness/>
 *    gate scans — and logs every settings_demo.greeting transition:
 *      - the explicit `exited` attribute that drives the claim
 *        release (with the sibling check: about's child is present
 *        without `exited` in the same report),
 *      - the post-release ABSENCE of the child (the stale <start> node
 *        was dropped; absence alone is never an exit — while a claim
 *        is held, the only release observed across the whole boot is
 *        the one backed by the explicit attribute),
 *      - the fresh instance (no `exited` attribute) of the relaunch.
 *
 * 3. probe_request caller: sends `remove settings_demo`, then the
 *    stale open of the removed identity. The stale open must answer
 *    not-installed (docs/12 §14.7 "Removal") on the probe's own
 *    probe_result channel.
 *
 * 4. system config relay (run/sponge-de-pkgd-failure.run pattern):
 *    publishes the nested `system` init config as report
 *    `system_config` (v1 with sponge_pkgd), and on choreography
 *    demand publishes v2 without sponge_pkgd and with a late `vct`
 *    caller (`settings open settings_demo greeting`) so the
 *    package-manager-unavailable fail-fast leg runs against a
 *    genuinely dead package manager.
 *
 * Capability surface: Timer, Report (system_config, request), ROM
 * (state, focus_hint, result), and the announced Gui service for
 * pkg_runtime. No File_system, no configd, no package payloads.
 */

#include <base/attached_ram_dataspace.h>
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/registry.h>
#include <base/session_label.h>
#include <base/session_object.h>
#include <framebuffer_session/framebuffer_session.h>
#include <gui_session/gui_session.h>
#include <input/component.h>
#include <os/dynamic_rom_session.h>
#include <os/reporter.h>
#include <root/component.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Settings_failure_probe {

using namespace Genode;

enum : unsigned {
	/* bound for one probe_request round trip (remove / stale open) */
	RESULT_TIMEOUT_US = 10 * 1000 * 1000
};

/* the choreography target (docs/12 §14.7 legs) */
static char const *const TARGET_PKG         = "settings_demo";
static char const *const TARGET_ID          = "greeting";
static char const *const TARGET_START_NAME  = "settings_demo.greeting";
static char const *const SIBLING_START_NAME = "settings_demo.about";

/* substring search over a session label (no libc) */
static bool contains(char const *haystack, char const *needle)
{
	if (!haystack || !needle)
		return false;

	for (char const *h = haystack; *h != '\0'; ++h) {
		char const *a = h, *b = needle;
		while (*a != '\0' && *b != '\0' && *a == *b) { ++a; ++b; }
		if (*b == '\0')
			return true;
	}
	return needle[0] == '\0';
}

struct Main;
struct Gui_session;


/*****************************
 ** Per-session framebuffer **
 *****************************/

/*
 * Self-contained Framebuffer::Session: a RAM dataspace sized by the
 * client's buffer() request. Painting works (the provider's surface
 * arithmetic sees a real buffer); compositing is not this harness's
 * concern — the scenario's claims are lifecycle claims, and the
 * center scenario's own nitpicker is headless for the same reason.
 */
struct Fb_session : Rpc_object<Framebuffer::Session>
{
	Env &_env;

	Constructible<Attached_ram_dataspace> _ds { };

	Framebuffer::Mode _mode { .area = { 1, 1 }, .alpha = false };

	Signal_context_capability _mode_sigh { };
	Signal_context_capability _sync_sigh { };

	explicit Fb_session(Env &env) : _env(env) { }

	void _alloc()
	{
		_ds.construct(_env.ram(), _env.rm(), Gui::Session::ram_quota(_mode));
	}

	void set_mode(Framebuffer::Mode mode)
	{
		if (mode.area.valid())
			_mode = mode;

		_ds.destruct();

		if (_mode_sigh.valid())
			Signal_transmitter(_mode_sigh).submit();
	}

	Dataspace_capability dataspace() override
	{
		if (!_ds.constructed())
			_alloc();
		return _ds->cap();
	}

	Framebuffer::Mode mode() const override { return _mode; }

	void mode_sigh(Signal_context_capability sigh) override
	{
		_mode_sigh = sigh;
	}

	void refresh(Framebuffer::Rect) override { }

	Blit_result blit(Framebuffer::Blit_batch const &) override
	{
		return Blit_result::OK;
	}

	void panning(Framebuffer::Point) override { }

	void sync_sigh(Signal_context_capability sigh) override
	{
		_sync_sigh = sigh;
	}

	void sync_source(Session_label const &) override { }
};


/**********************
 ** Gui session      **
 **********************/

struct Gui_session : Session_object<Gui::Session>,
                     Dynamic_rom_session::Xml_producer,
                     Input::Session_component::Action
{
	Env  &_env;
	Main &_main;

	Registry<Gui_session>::Element _reg_elem;

	/* command buffer shared with the client (consumed, never forged) */
	Attached_ram_dataspace _command_ds {
		_env.ram(), _env.rm(), sizeof(Gui::Session::Command_buffer) };

	Gui::Session::Command_buffer &_commands =
		*_command_ds.local_addr<Gui::Session::Command_buffer>();

	Fb_session _fb { _env };

	Framebuffer::Session_capability _fb_cap { _env.ep().manage(_fb) };

	Input::Session_component _input {
		_env.ep(), _env.ram(), _env.rm(), *this };

	Constructible<Dynamic_rom_session> _info { };

	/* app-visible close latch (docs/05 §4.6) */
	bool _close_pending { false };

	Gui_session(Env &env, Main &main, Registry<Gui_session> &registry,
	            Resources const &res, Label const &label)
	:
		Session_object(env.ep(), res, label),
		Xml_producer("info"),
		_env(env), _main(main),
		_reg_elem(registry, *this)
	{
		log("settings-failure-probe: gui session open: ", label);
	}

	~Gui_session()
	{
		log("settings-failure-probe: gui session closed: ", label());
		_env.ep().dissolve(_fb);
	}

	/* deliver the v1.1 app-visible close (the wm's info-ROM shape) */
	void request_close()
	{
		if (_close_pending)
			return;

		_close_pending = true;

		if (_info.constructed())
			_info->trigger_update();
	}

	bool close_pending() const { return _close_pending; }


	/***********************************
	 ** Dynamic_rom_session producer  **
	 ***********************************/

	void produce_xml(Xml_generator &g) override
	{
		g.attribute("width",  1024L);
		g.attribute("height", 768L);

		if (_close_pending)
			g.node("capture", [&] { g.attribute("closed", "yes"); });
		else
			g.node("capture", [&] {
				g.attribute("width",  440L);
				g.attribute("height", 240L); });
	}


	/*********************************
	 ** Input::Session::Action      **
	 *********************************/

	void exclusive_input_requested(bool) override { }


	/*************************
	 ** Gui::Session        **
	 *************************/

	Framebuffer::Session_capability framebuffer() override
	{
		return _fb_cap;
	}

	Input::Session_capability input() override
	{
		return _input.cap();
	}

	Info_result info() override
	{
		if (!_info.constructed())
			_info.construct(_env.ep(), _env.ram(), _env.rm(), *this);
		return _info->cap();
	}

	View_result view(Gui::View_id, View_attr const &) override
	{
		return View_result::OK;
	}

	Child_view_result child_view(Gui::View_id, Gui::View_id, View_attr const &) override
	{
		return Child_view_result::OK;
	}

	void destroy_view(Gui::View_id) override { }

	Associate_result associate(Gui::View_id, Gui::View_capability) override
	{
		return Associate_result::OK;
	}

	View_capability_result view_capability(Gui::View_id) override
	{
		/* the harness shares no views */
		return Gui::View_capability();
	}

	void release_view_id(Gui::View_id) override { }

	Dataspace_capability command_dataspace() override
	{
		return _command_ds.cap();
	}

	void execute() override
	{
		/* consume the batch; the client resets the buffer */
		for (unsigned i = 0; i < _commands.num(); i++)
			(void)_commands.get(i);
	}

	Buffer_result buffer(Framebuffer::Mode mode) override
	{
		if (!mode.area.valid())
			mode.area = { 1, 1 };

		_fb.set_mode(mode);
		return Buffer_result::OK;
	}

	void focus(Capability<Gui::Session>) override { }
};


/*****************
 ** Gui root    **
 *****************/

struct Gui_root : Root_component<Gui_session>
{
	Env  &_env;
	Main &_main;

	Registry<Gui_session> _sessions { };

	Gui_root(Env &env, Main &main, Allocator &md_alloc)
	:
		Root_component<Gui_session>(env.ep(), md_alloc),
		_env(env), _main(main)
	{ }

	Create_result _create_session(char const *args, Affinity const &) override
	{
		Gui::Session::Label const    label = label_from_args(args);
		Gui::Session::Resources const res  = session_resources_from_args(args);

		/*
		 * Floor budget: command buffer + input event buffer + info
		 * ROM + one framebuffer row of slack. A smaller donation
		 * cannot serve the session (same refusal shape as wm_root).
		 */
		enum : size_t {
			INPUT_DS = Input::Event_queue::QUEUE_SIZE*sizeof(Input::Event),
			FLOOR    = INPUT_DS + sizeof(Gui::Session::Command_buffer)
			         + 8*1024
		};

		if (res.ram_quota.value < FLOOR)
			return Session_error::INSUFFICIENT_RAM;

		if (res.cap_quota.value < Gui::Session::CAP_QUOTA)
			return Session_error::INSUFFICIENT_CAPS;

		return _alloc_obj(_env, _main, _sessions, res, label);
	}

	void _upgrade_session(Gui_session &s, char const *args) override
	{
		s.upgrade(ram_quota_from_args(args));
		s.upgrade(cap_quota_from_args(args));
	}

	template <typename FN> void with_each_session(FN const &fn)
	{
		_sessions.for_each([&] (Gui_session &s) { fn(s); });
	}
};


/***************
 ** Main      **
 ***************/

struct Main
{
	Env &_env;

	Timer::Connection _timer { _env };

	Timer::Connection _watch_timer { _env };

	/* pkg_runtime's sandbox state report (the liveness input) */
	Attached_rom_dataspace _state { _env, "state" };

	/* pkgd's opt-in focus-hint emission (the close trigger) */
	Attached_rom_dataspace _hint { _env, "focus_hint" };

	/* probe_result channel (remove / stale-open answers) */
	Attached_rom_dataspace _result { _env, "result" };

	/* probe_request channel (the probe's own pkgd writer label) */
	Expanding_reporter _request { _env, "request", "request" };

	/* the nested `system` init config relay */
	Reporter _system_config { _env, "config", "system_config", 16 * 1024 };

	Heap _heap { _env.ram(), _env.rm() };

	Gui_root _gui_root { _env, *this, _heap };

	Signal_handler<Main> _state_handler {
		_env.ep(), *this, &Main::_handle_state };

	Signal_handler<Main> _hint_handler {
		_env.ep(), *this, &Main::_handle_hint };

	Signal_handler<Main> _result_handler {
		_env.ep(), *this, &Main::_handle_result };

	Signal_handler<Main> _timeout_handler {
		_env.ep(), *this, &Main::_handle_timeout };

	Signal_handler<Main> _watch_handler {
		_env.ep(), *this, &Main::_handle_exit_watch };

	/* ---- choreography state ---- */

	enum class Step { IDLE, REMOVE_SENT, STALE_SENT, DONE };

	Step _step { Step::IDLE };

	bool     _exit_watch          { false };
	uint64_t _exit_watch_deadline { 0 };

	unsigned _target_hints { 0 };

	/* settings_demo.greeting state-report phase */
	enum class G_phase { UNSEEN, RUNNING, EXITED, ABSENT, REBORN };

	G_phase _g_phase { G_phase::UNSEEN };

	bool _sibling_logged { false };
	bool _reborn_logged { false };
	bool _absent_logged { false };
	bool _exited_logged { false };


	Main(Env &env) : _env(env)
	{
		_state.sigh(_state_handler);
		_hint.sigh(_hint_handler);
		_result.sigh(_result_handler);
		_timer.sigh(_timeout_handler);
		_watch_timer.sigh(_watch_handler);

		_publish_system_config(false);

		_env.parent().announce(_env.ep().manage(_gui_root));

		log("settings-failure-probe: ready (Gui service + choreography "
		    "armed; system config v1 published)");
	}


	void _fail(char const *msg)
	{
		error("settings-failure-probe: FAIL: ", msg);
		_env.parent().exit(1);
	}


	/* ---------------- system config relay ---------------- */

	void _publish_system_config(bool stop_pkgd)
	{
		_system_config.enabled(true);

		static char const *const parent_services[] = {
			"ROM", "PD", "RM", "CPU", "LOG", "Timer", "Report"
		};

		Reporter::Result const res =
			_system_config.generate_xml([&] (Xml_generator &g) {

			g.node("parent-provides", [&] {
				for (char const *s : parent_services)
					g.node("service", [&] { g.attribute("name", s); });
			});

			g.node("default", [&] {
				g.attribute("caps", "200");
				g.attribute("ram", "4M");
			});

			g.node("default-route", [&] {
				g.node("any-service", [&] { g.node("parent", [&] { }); });
			});

			if (!stop_pkgd) {

				g.node("start", [&] {
					g.attribute("name", "sponge_pkgd");
					g.attribute("version", "1");
					g.attribute("caps", "800");
					g.attribute("ram", "16M");

					/*
					 * The Criterion 18 opt-in gates plus the
					 * probe's own request channel (the same
					 * config-gate pattern as run/sponge-de-
					 * pkgd-failure.run).
					 */
					g.node("config", [&] {
						g.node("settings_request", [&] { });
						g.node("settings_liveness", [&] { });
						g.node("settings_focus_hint", [&] { });
						g.node("probe_request", [&] { });
					});

					g.node("route", [&] {
						g.node("service", [&] {
							g.attribute("name", "ROM");
							g.attribute("label_last", "ld.lib.so");
							g.node("parent", [&] { });
						});
						g.node("any-service", [&] {
							g.node("parent", [&] { });
						});
					});
				});
				return;
			}

			/*
			 * v2: sponge_pkgd is GONE (its start node is dropped,
			 * init destroys the incarnation) and the late vct
			 * caller takes its place. vct's `settings open` owns
			 * the production package-manager-unavailable fail-fast
			 * surface (docs/06 §4.8): one bounded result wait,
			 * derived locally, never a poll loop.
			 */
			g.node("start", [&] {
				g.attribute("name", "vct_late");
				g.attribute("version", "1");
				g.attribute("caps", "200");
				g.attribute("ram", "8M");

				g.node("binary", [&] { g.attribute("name", "vct"); });

				static char const *const args[] = {
					"settings", "open", TARGET_PKG, TARGET_ID
				};

				/*
				 * The `name` attribute form (not element content):
				 * init re-serializes a child's <config> into the HID
				 * text format, where content-only args lose their
				 * text and vct's HID parser reads only `name`.
				 */
				g.node("config", [&] {
					g.node("args", [&] {
						for (char const *a : args)
							g.node("arg", [&] {
								g.attribute("name", a);
							});
					});
				});

				g.node("route", [&] {
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label_last", "ld.lib.so");
						g.node("parent", [&] { });
					});
					g.node("any-service", [&] {
						g.node("parent", [&] { });
					});
				});
			});
		});

		if (!res.ok())
			_fail("system config publication overflowed the report buffer");
	}


	/* ---------------- probe_request steps ---------------- */

	void _send_remove()
	{
		log("settings-failure-probe: remove ", TARGET_PKG,
		    " (probe_request)");

		_request.generate_xml([&] (Xml_generator &g) {
			g.attribute("op",  "remove");
			g.attribute("pkg", TARGET_PKG);
		});

		_step = Step::REMOVE_SENT;
		_timer.trigger_once(RESULT_TIMEOUT_US);
	}

	void _send_stale_open()
	{
		log("settings-failure-probe: stale open ", TARGET_PKG, "/",
		    TARGET_ID, " (probe_request)");

		_request.generate_xml([&] (Xml_generator &g) {
			g.attribute("op",     "launch");
			g.attribute("target", "settings");
			g.attribute("pkg",    TARGET_PKG);
			g.attribute("id",     TARGET_ID);
		});

		_step = Step::STALE_SENT;
		_timer.trigger_once(RESULT_TIMEOUT_US);
	}

	void _finish()
	{
		_step = Step::DONE;

		_publish_system_config(true);

		log("settings-failure-probe: system config v2 published — ",
		    "sponge_pkgd stopped, vct_late armed");
		log("settings-failure-probe: choreography complete");
	}


	/* ---------------- state-report evidence ---------------- */

	/*
	 * One scan of the current HID state-report content. Shared by the
	 * signal handler and the bounded direct poll below.
	 */
	struct Scan
	{
		bool target_seen   { false }, target_exited { false };
		long target_code   { 0 };
		bool sibling_seen  { false }, sibling_exited { false };
	};

	Scan _scan_state()
	{
		Scan s { };

		if (!_state.valid() || _state.size() == 0)
			return s;

		char const *const buf = _state.local_addr<char const>();
		size_t      const len = _state.size();

		/* Hid_node marks malformed input invalid instead of throwing */
		Hid_node const root(Const_byte_range_ptr(buf, len));

		root.for_each_sub_node([&] (Hid_node const &n) {
			if (!n.has_type("child"))
				return;

			String<64> const name =
				n.attribute_value("name", String<64>());

			if (name == String<64>(TARGET_START_NAME)) {
				s.target_seen = true;
				if (n.has_attribute("exited")) {
					s.target_exited = true;
					s.target_code   = n.attribute_value("exited", 0L);
				}
			}
			if (name == String<64>(SIBLING_START_NAME)) {
				s.sibling_seen = true;
				if (n.has_attribute("exited"))
					s.sibling_exited = true;
			}
		});

		return s;
	}

	void _log_exit_evidence(Scan const &s)
	{
		_exited_logged = true;
		_g_phase = G_phase::EXITED;

		log("settings-failure-probe: state evidence: child ",
		    TARGET_START_NAME, " explicit exited: ", s.target_code,
		    " (only an explicit attribute is an exit)");

		if (s.sibling_seen && !s.sibling_exited) {
			_sibling_logged = true;
			log("settings-failure-probe: state evidence: sibling ",
			    SIBLING_START_NAME,
			    " present without exited (another child's exit "
			    "never releases this name)");
		}
	}

	/*
	 * Bounded re-check of the state report after the app-visible
	 * close. The exit produces ONE immediate state report carrying
	 * the explicit `exited` attribute; the attribute stays until
	 * pkgd's claim release regenerates the runtime config and the
	 * stale <start> node is dropped. The check is TIMER-DRIVEN, never
	 * a blocking poll: the close makes the provider read its info ROM
	 * back through THIS entrypoint, so blocking it would starve the
	 * very RPC that delivers the close. Each tick samples the ROM
	 * content directly and re-arms (5 ms x 1000 = 5 s bound), failing
	 * loudly on expiry.
	 */
	void _start_exit_watch()
	{
		_exit_watch = true;
		_exit_watch_deadline = _watch_timer.elapsed_ms() + 5000;
		_watch_timer.trigger_once(5);
	}

	void _handle_exit_watch()
	{
		if (!_exit_watch)
			return;

		if (_exited_logged) {
			_exit_watch = false;
			return;
		}

		_state.update();
		Scan const s = _scan_state();

		if (s.target_seen && s.target_exited) {
			_exit_watch = false;
			_log_exit_evidence(s);
			return;
		}

		if (_watch_timer.elapsed_ms() >= _exit_watch_deadline) {
			_exit_watch = false;
			_fail("no state report with the explicit exited attribute "
			      "within bound after the app-visible close");
			return;
		}

		_watch_timer.trigger_once(5);
	}


	/* ---------------- ROM handlers ---------------- */

	void _handle_result()
	{
		if (_step != Step::REMOVE_SENT && _step != Step::STALE_SENT)
			return;

		_result.update();
		if (!_result.valid())
			return;

		try {
			Xml_node const r = _result.xml();
			if (!r.has_type("result"))
				return;

			String<32>  const op     = r.attribute_value("op",
			                                  String<32>());
			String<32>  const status = r.attribute_value("status",
			                                  String<32>());

			if (_step == Step::REMOVE_SENT) {

				if (op != String<32>("remove"))
					return;

				if (status != String<32>("ok")) {
					_fail("remove settings_demo did not answer ok");
					return;
				}

				log("settings-failure-probe: remove ", TARGET_PKG,
				    " -> ok (probe_result)");
				_send_stale_open();
				return;
			}

			/* STALE_SENT: the stale open's answer */

			if (op != String<32>("launch"))
				return;

			if (status != String<32>("not-installed")) {
				_fail("stale open answered something other than "
				      "not-installed after removal");
				return;
			}

			log("settings-failure-probe: stale open ", TARGET_PKG, "/",
			    TARGET_ID, " -> not-installed (probe_result)");
			_finish();
		}
		catch (Xml_node::Invalid_syntax) {
			_fail("probe_result carried malformed XML");
		}
	}


	void _handle_timeout()
	{
		switch (_step) {
		case Step::REMOVE_SENT:
			_fail("remove settings_demo unanswered within bound");
			break;
		case Step::STALE_SENT:
			_fail("stale open unanswered within bound");
			break;
		default:
			break;  /* stale timer tick after a completed step */
		}
	}


	void _handle_hint()
	{
		_hint.update();
		if (!_hint.valid())
			return;

		try {
			Xml_node const h = _hint.xml();
			if (!h.has_type("settings_focus_hint"))
				return;

			String<128> const pkg = h.attribute_value("pkg",
			                                   String<128>());
			String<33>  const id  = h.attribute_value("id",
			                                   String<33>());

			if (pkg != String<128>(TARGET_PKG) ||
			    id  != String<33>(TARGET_ID))
				return;

			++_target_hints;

			if (_target_hints == 1) {

				bool found { false };
				_gui_root.with_each_session([&] (Gui_session &s) {
					if (found || s.close_pending())
						return;
					if (!contains(s.label().string(),
					              TARGET_START_NAME))
						return;
					found = true;
					log("settings-failure-probe: app-visible close "
					    "-> ", TARGET_START_NAME,
					    " (info ROM <capture closed=\"yes\"/>)");
					s.request_close();
				});

				if (!found) {
					_fail("focus hint for greeting but no live "
					      "Gui session to close");
					return;
				}

				_start_exit_watch();
				return;
			}

			if (_target_hints == 2 && _step == Step::IDLE) {
				/*
				 * The second focused open of the target proves
				 * its claim survived the relaunch window — the
				 * choreography may now remove the package.
				 */
				_send_remove();
			}
		}
		catch (Xml_node::Invalid_syntax) {
			_fail("focus_hint carried malformed XML");
		}
	}


	void _handle_state()
	{
		_state.update();
		if (!_state.valid() || _state.size() == 0)
			return;

		Scan const s = _scan_state();

		if (s.target_seen && s.target_exited && !_exited_logged) {
			_log_exit_evidence(s);
			return;
		}

		if (!s.target_seen && _g_phase == G_phase::EXITED && !_absent_logged) {
			_absent_logged = true;
			_g_phase = G_phase::ABSENT;

			log("settings-failure-probe: state evidence: child ",
			    TARGET_START_NAME,
			    " absent (stale <start> node dropped after release; "
			    "absence alone is never an exit)");
			return;
		}

		if (s.target_seen && !s.target_exited &&
		    _g_phase == G_phase::ABSENT && !_reborn_logged) {
			_reborn_logged = true;
			_g_phase = G_phase::REBORN;

			log("settings-failure-probe: state evidence: child ",
			    TARGET_START_NAME,
			    " fresh instance (no exited attribute)");
			return;
		}

		if (s.target_seen && !s.target_exited && _g_phase == G_phase::UNSEEN)
			_g_phase = G_phase::RUNNING;
	}
};


} /* namespace Sponge::Settings_failure_probe */


void Component::construct(Genode::Env &env)
{
	static Sponge::Settings_failure_probe::Main main { env };
}
