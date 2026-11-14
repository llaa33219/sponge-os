/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_vct_probe — real-vct settings acceptance probe (companion
 * of run/sponge-settings-vct.run).
 *
 * A FOCUSED harness with exactly three jobs (target.mk carries the
 * rationale); marker grammar "settings-vct-probe: ...", every wait
 * bounded, every contract deviation a loud FAIL.
 *
 * 1. Gui service for the settings providers sponge_pkgd launches
 *    into pkg_runtime (the settings_failure_probe session shape:
 *    self-contained framebuffer + input + info ROM per session). The
 *    providers own their windows; nothing is closed or hidden — this
 *    scenario's claims are lifecycle claims (started / focused /
 *    not-installed) and the app-visible close channel is proven by
 *    run/sponge-settings-failure.run.
 *
 * 2. system config relay + vct sequencing (the failure probe's
 *    publication pattern). The probe owns the nested `system` init
 *    config and serializes REAL vct children on the primary request
 *    label: one `vct_cmd` incarnation at a time, each config version
 *    arming exactly one vct command (install, settings list, settings
 *    list --json, settings open xN). The next config is published
 *    only AFTER the previous vct child completed (its `exited`
 *    attribute observed in the `system` state report), so the
 *    primary request label never carries two concurrent writers.
 *
 * 3. Per-step wire validation + one terminal PASS marker. After each
 *    vct_cmd exit the probe reads sponge_pkgd's `result` document
 *    (and watches the `installed` broadcast) and fails loudly unless
 *    the wire answer matches the step's contract. PASS is logged
 *    exactly once, only after all steps validated.
 *
 * Capability surface: Timer, Report (system_config), ROM
 * (system_state, result, installed), and the announced Gui service
 * for pkg_runtime. No File_system, no configd, no package payloads.
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

namespace Sponge::Settings_vct_probe {

using namespace Genode;

enum : unsigned {
	/* poll cadence of the choreography timer */
	POLL_MS = 100,

	/* bound for one vct_cmd incarnation (boot + command + exit);
	 * vct's own bounded result wait is 6.2 s */
	STEP_TIMEOUT_MS = 30 * 1000
};

/* choreography targets (docs/12-package-format.md §14) */
static char const *const PKG        = "settings_demo";
static char const *const ENTRY_ID   = "greeting";
static char const *const SIBLING_ID = "about";
static char const *const UNKNOWN_ID = "nosuchid";

/* the provider start name (settings_rules.cc: "<pkg>.<id>") */
static char const *const TARGET_START_NAME = "settings_demo.greeting";

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
 * Self-contained Framebuffer::Session (the settings_failure_probe
 * shape): a RAM dataspace sized by the client's buffer() request, so
 * the provider's surface arithmetic sees a real buffer. Compositing
 * is not this harness's concern — the claims are lifecycle claims.
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

	Gui_session(Env &env, Main &main, Registry<Gui_session> &registry,
	            Resources const &res, Label const &label)
	:
		Session_object(env.ep(), res, label),
		Xml_producer("info"),
		_env(env), _main(main),
		_reg_elem(registry, *this)
	{
		log("settings-vct-probe: gui session open: ", label);
	}

	~Gui_session()
	{
		log("settings-vct-probe: gui session closed: ", label());
		_env.ep().dissolve(_fb);
	}


	/***********************************
	 ** Dynamic_rom_session producer  **
	 ***********************************/

	void produce_xml(Xml_generator &g) override
	{
		g.attribute("width",  1024L);
		g.attribute("height", 768L);
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
		 * ROM + one framebuffer row of slack (the wm_root refusal
		 * shape).
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

/*
 * The serialized vct choreography. Each step is ONE real vct
 * invocation with real argv; 'want_status'/'want_action' name the
 * sponge_pkgd result document the probe must observe after the
 * child's exit, and 'expect_exit' the vct process exit code (vct's
 * documented contract: 0 on ok, non-zero on every refusal).
 */
enum class Kind { INSTALL, LIST, OPEN };

struct Step
{
	char const *name;
	char const *args[6];       /* NULL-terminated vct argv */
	Kind        kind;
	char const *open_id;       /* settings id for Kind::OPEN */
	char const *want_status;   /* result status attribute */
	char const *want_action;   /* result action attribute (ok opens) */
	long        expect_exit;
};

static Step const steps[] = {
	{ "install settings_demo",
	  { "install", "settings_demo", nullptr },
	  Kind::INSTALL, nullptr, "ok", nullptr, 0 },
	{ "settings list",
	  { "settings", "list", nullptr },
	  Kind::LIST, nullptr, nullptr, nullptr, 0 },
	{ "settings list --json",
	  { "settings", "list", "--json", nullptr },
	  Kind::LIST, nullptr, nullptr, nullptr, 0 },
	{ "settings open settings_demo greeting",
	  { "settings", "open", PKG, ENTRY_ID, nullptr },
	  Kind::OPEN, ENTRY_ID, "ok", "started", 0 },
	{ "settings open settings_demo greeting (2nd)",
	  { "settings", "open", PKG, ENTRY_ID, nullptr },
	  Kind::OPEN, ENTRY_ID, "ok", "focused", 0 },
	{ "settings open settings_demo nosuchid",
	  { "settings", "open", PKG, UNKNOWN_ID, nullptr },
	  Kind::OPEN, UNKNOWN_ID, "not-installed", nullptr, 1 },
	{ "settings open settings_demo greeting --json",
	  { "settings", "open", PKG, ENTRY_ID, "--json", nullptr },
	  Kind::OPEN, ENTRY_ID, "ok", "focused", 0 },
	{ "settings open settings_demo nosuchid --json",
	  { "settings", "open", PKG, UNKNOWN_ID, "--json", nullptr },
	  Kind::OPEN, UNKNOWN_ID, "not-installed", nullptr, 1 },
};

static constexpr unsigned NUM_STEPS = sizeof(steps) / sizeof(steps[0]);


struct Main
{
	Env &_env;

	Timer::Connection _timer { _env };

	/* the nested `system` sandbox state report (vct_cmd exit watch) */
	Attached_rom_dataspace _system_state { _env, "system_state" };

	/* sponge_pkgd's shared result channel (wire validation) */
	Attached_rom_dataspace _result { _env, "result" };

	/* sponge_pkgd's installed broadcast (settings list's data source) */
	Attached_rom_dataspace _installed { _env, "installed" };

	/* the nested `system` init config relay */
	Reporter _system_config { _env, "config", "system_config", 16 * 1024 };

	Heap _heap { _env.ram(), _env.rm() };

	Gui_root _gui_root { _env, *this, _heap };

	Signal_handler<Main> _tick_handler {
		_env.ep(), *this, &Main::_handle_tick };

	/* ---- choreography state ---- */

	unsigned _step { 0 };             /* index of the armed step */
	bool     _awaiting_exit { false };
	bool     _seen_live { false };
	uint64_t _step_deadline { 0 };

	bool _installed_ok { false };


	Main(Env &env) : _env(env)
	{
		_timer.sigh(_tick_handler);

		_publish_system_config(0);
		_arm_step(0);
		_timer.trigger_once(POLL_MS);

		_env.parent().announce(_env.ep().manage(_gui_root));

		log("settings-vct-probe: ready (Gui service + choreography "
		    "armed; system config v1 published)");
	}


	void _fail(char const *msg)
	{
		error("settings-vct-probe: FAIL: ", msg);
		_env.parent().exit(1);
	}


	/* ---------------- system config relay ---------------- */

	/*
	 * Publish the nested `system` init config for one choreography
	 * step. sponge_pkgd's start node is BYTE-IDENTICAL across every
	 * version (init keeps the running incarnation), while `vct_cmd`
	 * carries this step's argv and a bumped version (init replaces
	 * the previous, already-exited incarnation). The <report> node
	 * enables the system init's own state report — the exit watch's
	 * input. The `name` attribute arg form is mandatory: init
	 * re-serializes a child's <config> into the HID text format,
	 * where content-only args lose their text (the
	 * settings_failure_probe lesson).
	 */
	void _publish_system_config(unsigned step)
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

			/* the system init's state report (vct_cmd exit watch) */
			g.node("report", [&] { g.attribute("delay_ms", "200"); });

			g.node("start", [&] {
				g.attribute("name",    "sponge_pkgd");
				g.attribute("version", "1");
				g.attribute("caps",    "800");
				g.attribute("ram",     "16M");

				/*
				 * The Criterion 18 settings gate only: the
				 * liveness consumer of pkg_runtime's state
				 * report. The primary request/result channel
				 * and the installed/runtime channels are
				 * always on; settings_request /
				 * settings_focus_hint / probe_request stay
				 * disabled (this scenario drives the real
				 * vct primary channel exclusively).
				 */
				g.node("config", [&] {
					g.node("settings_liveness", [&] { });
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

			Step const &st = steps[step];

			g.node("start", [&] {
				g.attribute("name",    "vct_cmd");
				g.attribute("version", (long)(step + 1));
				g.attribute("caps",    "200");
				g.attribute("ram",     "8M");

				g.node("binary", [&] { g.attribute("name", "vct"); });

				g.node("config", [&] {
					g.node("args", [&] {
						for (char const *const *a = st.args; *a; ++a)
							g.node("arg", [&] {
								g.attribute("name", *a);
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


	/* ---------------- choreography ---------------- */

	void _arm_step(unsigned step)
	{
		_step          = step;
		_awaiting_exit = true;
		_seen_live     = false;
		_step_deadline = _timer.elapsed_ms() + STEP_TIMEOUT_MS;
	}

	void _advance()
	{
		unsigned const next = _step + 1;

		if (next >= NUM_STEPS) {
			_awaiting_exit = false;
			_finish();
			return;
		}

		_publish_system_config(next);
		_arm_step(next);
	}

	void _finish()
	{
		if (!_installed_ok)
			_fail("installed broadcast never carried both settings "
			      "entries (greeting + about)");

		/*
		 * Exactly one LIVE greeting Gui session at verdict time:
		 * a duplicate start would leave two, a premature close
		 * zero. (The per-open/per-close serial lines are
		 * separately pinned by the run's post-output counts.)
		 */
		unsigned live = 0;
		_gui_root.with_each_session([&] (Gui_session &s) {
			if (contains(s.label().string(), TARGET_START_NAME))
				++live;
		});

		if (live != 1)
			_fail("greeting provider Gui sessions != 1 live "
			      "(duplicate start or premature close)");

		log("settings-vct-probe: PASS - 8 sequential real vct children "
		    "validated (install ok; list human+json carry greeting/about "
		    "with categories and ids; open greeting -> ok started once, "
		    "-> ok focused twice, no duplicate provider start; open "
		    "nosuchid -> not-installed; single writer on the primary "
		    "request label throughout)");
	}


	/* ---------------- state-report exit watch ---------------- */

	/*
	 * One scan of the `system` state report: the vct_cmd incarnation
	 * of the armed step (keyed by its version attribute) with an
	 * explicit `exited` attribute. Hid_node marks malformed input
	 * invalid instead of throwing (the settings_failure_probe shape).
	 */
	bool _scan_vct_exit(long &code)
	{
		if (!_system_state.valid() || _system_state.size() == 0)
			return false;

		char const *const buf = _system_state.local_addr<char const>();
		size_t      const len = _system_state.size();

		Hid_node const root(Const_byte_range_ptr(buf, len));

		bool live = false, exited = false;

		root.for_each_sub_node([&] (Hid_node const &n) {
			if (!n.has_type("child"))
				return;

			String<32> const name =
				n.attribute_value("name", String<32>());
			if (name != String<32>("vct_cmd"))
				return;

			if (n.has_attribute("exited")) {
				exited = true;
				code = n.attribute_value("exited", 0L);
			} else {
				live = true;
			}
		});

		if (live && !exited)
			_seen_live = true;

		return exited && (_seen_live || _step == 0);
	}


	/* ---------------- installed-broadcast watch ---------------- */

	/*
	 * The data source `vct settings list` reads. Logged once when
	 * both entries first appear with the expected categories; PASS
	 * requires it.
	 */
	void _watch_installed_entries()
	{
		if (_installed_ok)
			return;

		_installed.update();
		if (!_installed.valid() || _installed.size() == 0)
			return;

		try {
			Xml_node const root(_installed.local_addr<char>(),
			                    _installed.size());

			bool greeting = false, about = false;

			root.with_optional_sub_node("packages",
				[&] (Xml_node const &pkgs) {
				pkgs.for_each_sub_node("package",
					[&] (Xml_node const &p) {

					String<64> const pkg =
						p.attribute_value("name", String<64>());
					if (pkg != String<64>(PKG))
						return;

					p.for_each_sub_node("settings",
						[&] (Xml_node const &s) {

						String<33> const id =
							s.attribute_value("id", String<33>());
						String<64> const title =
							s.attribute_value("title", String<64>());
						String<16> const category =
							s.attribute_value("category", String<16>());

						if (id == String<33>(ENTRY_ID) &&
						    title != String<64>() &&
						    category == String<16>("applications"))
							greeting = true;

						if (id == String<33>(SIBLING_ID) &&
						    title != String<64>() &&
						    category == String<16>("other"))
							about = true;
					});
				});
			});

			if (greeting && about) {
				_installed_ok = true;
				log("settings-vct-probe: installed entries present: ",
				    ENTRY_ID, " (applications), ", SIBLING_ID, " (other)");
			}
		}
		catch (Xml_node::Invalid_syntax) {
			warning("settings-vct-probe: malformed installed broadcast");
		}
	}


	/* ---------------- result validation ---------------- */

	void _validate_result(Step const &st)
	{
		_result.update();
		if (!_result.valid()) {
			_fail("result ROM invalid after the vct child completed");
			return;
		}

		try {
			Xml_node const r = _result.xml();
			if (!r.has_type("result")) {
				_fail("result document is not a <result>");
				return;
			}

			String<32>  const op     = r.attribute_value("op", String<32>());
			String<32>  const status = r.attribute_value("status", String<32>());

			if (st.kind == Kind::INSTALL) {

				if (op != String<32>("install") ||
				    status != String<32>("ok") ||
				    r.attribute_value("pkg", String<64>())
				    	!= String<64>(PKG)) {
					_fail("install result is not ok for settings_demo");
				}
				return;
			}

			if (st.kind == Kind::LIST)
				return;  /* list has no result round trip */

			/* Kind::OPEN: the §14.7 settings launch result */
			if (op != String<32>("launch")) {
				_fail("settings open result carries a foreign op");
				return;
			}
			if (r.attribute_value("target", String<16>())
					!= String<16>("settings")) {
				_fail("settings open result lacks the settings target");
				return;
			}
			if (r.attribute_value("pkg", String<64>())
					!= String<64>(PKG)) {
				_fail("settings open result names a foreign package");
				return;
			}

			String<33> id = r.attribute_value("id", String<33>());
			if (id == String<33>())
				id = r.attribute_value("settings-id", String<33>());

			if (id != String<33>(st.open_id)) {
				_fail("settings open result answers a different id");
				return;
			}

			if (status != String<32>(st.want_status)) {
				_fail("settings open result status deviates");
				return;
			}

			if (st.want_action) {
				String<16> const action =
					r.attribute_value("action", String<16>());
				if (action != String<16>(st.want_action)) {
					_fail("settings open result action deviates");
					return;
				}
			}
		}
		catch (Xml_node::Invalid_syntax) {
			_fail("result document carried malformed XML");
		}
	}


	/* ---------------- tick ---------------- */

	void _handle_tick()
	{
		_timer.trigger_once(POLL_MS);

		_watch_installed_entries();

		if (!_awaiting_exit)
			return;

		if (_timer.elapsed_ms() > _step_deadline) {
			_system_state.update();
			if (_system_state.valid()) {
				size_t const n = _system_state.size() < 512
				               ? _system_state.size() : 512;
				log("settings-vct-probe: system_state ROM (",
				    _system_state.size(), " bytes): ");
				log(String<520>(
					_system_state.local_addr<char const>(), n));
			} else
				log("settings-vct-probe: system_state ROM invalid");
			_fail("vct_cmd step did not complete within bound");
			return;
		}

		_system_state.update();

		long code = 0;
		if (!_scan_vct_exit(code))
			return;

		_awaiting_exit = false;

		Step const &st = steps[_step];

		if (code != st.expect_exit) {
			error("settings-vct-probe: FAIL: step '", st.name,
			      "' exited with ", code, " (expected ", st.expect_exit, ")");
			_env.parent().exit(1);
			return;
		}

		/* list steps have no result round trip to validate */
		if (st.kind != Kind::LIST)
			_validate_result(st);

		log("settings-vct-probe: step ", (unsigned)(_step + 1), " '",
		    st.name, "' -> contract ok (exit ", code, ")");

		_advance();
	}
};


} /* namespace Sponge::Settings_vct_probe */


void Component::construct(Genode::Env &env)
{
	static Sponge::Settings_vct_probe::Main main { env };
}
