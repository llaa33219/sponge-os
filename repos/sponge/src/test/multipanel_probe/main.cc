/* SPDX-License-Identifier: Apache-2.0
 *
 * multipanel_probe — Phase 16 W8 multi-panel acceptance probe
 * (U16.5 / D16.5).
 *
 * Phase 16 W8 binds (U16.5 / D16.5):
 *
 *   - Arbitrary panel count on arbitrary edges, per-panel config
 *     namespace `panel.<id>.{height,position,visible_widgets}`.
 *   - Each panel instance has its OWN nitpicker domain and a
 *     DISTINCT Gui session label suffix (e.g. "Sponge Panel Alpha",
 *     "Sponge Panel Beta"). This is the F5 nitpicker label_prefix
 *     trap defense: two identically-prefixed Gui sessions would
 *     land on the same domain and clicks would not be steered to
 *     the correct panel widget.
 *
 * The probe drives the test in three coordinated modes:
 *
 *   1. de_config_request writer: writes `panel.ids=alpha,beta` and
 *      the per-id keys (`panel.alpha.position=top`, `panel.alpha.
 *      height=40`, `panel.beta.position=bottom`, `panel.beta.height
 *      =28`) through the dedicated `sponge-de -> de_config_request`
 *      channel (D16.1). configd validates each write against the
 *      per-id pattern validator; the broadcast emits every key.
 *
 *   2. ROM consumer of `panel_alpha` + `panel_beta`: each PanelWidget
 *      instance owns an Expanding_reporter labeled `panel_<id>`
 *      (the F5 defense makes the labels distinct). The reporter
 *      body is `<panel id="<id>" click_count="N"/>` — a
 *      monotonically-increasing counter bumped by mousePressEvent.
 *
 *   3. QMP click dispatcher: emits QMP-TARGET click markers at the
 *      exact per-panel coordinates the Momus-reviewed W8 task 4
 *      pins:
 *
 *        qmp_click 100 14   -> panel-alpha (top edge, y=14,
 *                              height=40, 0<=y<40)
 *        qmp_click 800 740  -> panel-beta  (bottom edge, y=740,
 *                              height=28, 740<=y<768)
 *
 *      After the first click, ONLY panel_alpha.click_count=1
 *      (panel_beta.click_count=0). After the second click, ONLY
 *      panel_beta.click_count=1 (panel_alpha.click_count=1, stays).
 *
 * The cross-panel assertion catches the F5 label_prefix trap: if
 * both panels landed on the same nitpicker domain, the click at
 * (100, 14) would land on whichever panel widget was drawn there,
 * and panel_beta.click_count could increment instead of
 * panel_alpha.click_count — the assertion would FAIL.
 *
 * Final gate: "multipanel-probe: PASS".
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

struct Probe
{
	Genode::Env &_env;

	Timer::Connection              _timer           { _env };
	Genode::Attached_rom_dataspace _config          { _env, "config" };

	/*
	 * de_config_request / de_config_result channel (D16.1). Lazy-
	 * constructed so scenarios that only observe do not request
	 * unwired ROM sessions.
	 */
	Genode::Constructible<Genode::Expanding_reporter>     _de_request   { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _de_result    { };

	/*
	 * The configd broadcast (panel.ids + per-id keys). Required
	 * for the write side to verify per-id round-trip.
	 */
	Genode::Attached_rom_dataspace _broadcast        { _env, "broadcast" };

	/*
	 * Per-panel click_count reporters (F5 defense: distinct labels
	 * per instance). Constructed lazily so the probe can boot in
	 * a topology where the reporters are not wired (the FAIL
	 * sentry catches the absence).
	 */
	Genode::Constructible<Genode::Attached_rom_dataspace> _panel_alpha { };
	Genode::Constructible<Genode::Attached_rom_dataspace> _panel_beta  { };

	Probe(Genode::Env &env) : _env(env) { }

	bool _wait_for_broadcast()
	{
		for (unsigned i = 0; i < 50; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _broadcast.xml();
					if (root.has_type("config"))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	bool _send_set(char const *key, char const *value)
	{
		if (!_de_request.constructed())
			_de_request.construct(_env, "request", "de_config_request");
		if (!_de_result.constructed())
			_de_result.construct(_env, "de_config_result");

		_de_request->generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",    "set");
			g.attribute("key",   key);
			g.attribute("value", value);
		});

		for (unsigned i = 0; i < 80; ++i) {
			_de_result->update();
			if (_de_result->valid()) {
				try {
					Genode::Xml_node const r = _de_result->xml();
					if (r.attribute_value("status", Genode::String<32>()) ==
					    Genode::String<32>("ok") &&
					    r.attribute_value("op",    Genode::String<32>()) ==
					    Genode::String<32>("set") &&
					    r.attribute_value("key",   Genode::String<128>()) ==
					    Genode::String<128>(key) &&
					    r.attribute_value("value", Genode::String<128>()) ==
					    Genode::String<128>(value))
						return true;
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	bool _broadcast_has(char const *key, char const *value)
	{
		for (unsigned i = 0; i < 50; ++i) {
			_broadcast.update();
			if (_broadcast.valid()) {
				try {
					Genode::Xml_node const root = _broadcast.xml();
					if (root.has_type("config")) {
						bool found { false };
						root.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
							if (!found &&
							    k.attribute_value("name",  Genode::String<64>()) ==
							        Genode::String<64>(key) &&
							    k.attribute_value("value", Genode::String<128>()) ==
							        Genode::String<128>(value))
								found = true;
						});
						if (found) return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

	/*
	 * Wait for the per-panel click_count report ROM to carry
	 * <panel id="X" click_count="N"/> with the requested count.
	 * Used to gate each QMP click on the click reaching the right
	 * panel widget — the F5 label_prefix defense.
	 */
	bool _panel_click_count_is(char const *label, char const *panel_id,
	                           unsigned expected_count)
	{
		Genode::Attached_rom_dataspace *rom = nullptr;
		if (Genode::strcmp(label, "panel_alpha") == 0) {
			if (!_panel_alpha.constructed())
				_panel_alpha.construct(_env, label);
			rom = &*_panel_alpha;
		} else if (Genode::strcmp(label, "panel_beta") == 0) {
			if (!_panel_beta.constructed())
				_panel_beta.construct(_env, label);
			rom = &*_panel_beta;
		} else {
			return false;
		}

		/*
		 * Render the unsigned integer into a Genode::String. The
		 * fixed 32-byte backing storage is large enough for any
		 * unsigned value a click_count can reach in a regression
		 * scenario (realistically N < 1000).
		 */
		Genode::String<32> expected_str { };
		{
			char tmp[32];
			unsigned v = expected_count;
			int idx = sizeof(tmp) - 1;
			tmp[idx] = '\0';
			if (v == 0) { tmp[--idx] = '0'; }
			else while (v > 0 && idx > 0) {
				tmp[--idx] = (char)('0' + v % 10);
				v /= 10;
			}
			expected_str = Genode::String<32>(const_cast<char const *>(tmp + idx));
		}

		for (unsigned i = 0; i < 50; ++i) {
			rom->update();
			if (rom->valid()) {
				try {
					Genode::Xml_node const root = rom->xml();
					if (root.has_type("panel")) {
						Genode::String<32> const id_attr =
							root.attribute_value("id", Genode::String<32>());
						Genode::String<32> const cnt_attr =
							root.attribute_value("click_count", Genode::String<32>());
						if (id_attr == Genode::String<32>(panel_id) &&
						    cnt_attr == expected_str)
							return true;
					}
				} catch (Genode::Xml_node::Invalid_syntax) { }
			}
			_timer.msleep(100);
		}
		return false;
	}

void _fail(char const *probe_name, char const *reason)
{
	Genode::error(probe_name, ": FAIL ", reason);
	_env.parent().exit(1);
	Genode::sleep_forever();
}

	void run()
	{
		char const *const PROBE = "multipanel-probe";

		if (!_wait_for_broadcast()) {
			_fail(PROBE, "configd broadcast never arrived");
			return;
		}
		Genode::log(PROBE, ": configd broadcast arrived");

		/*
		 * Wait ~5s for sponge-de to construct (the PanelCollection +
		 * initial default panel need to exist before writes fan out).
		 */
		_timer.msleep(5000);

		/*
		 * STEP 1: declare the active panel ids (U16.5 / D16.5).
		 * configd pattern-key mechanism: writes panel.ids and
		 * instantiates panel.<id>.{height,position,visible_widgets}
		 * slots on first per-id write.
		 */
		if (!_send_set("panel.ids", "alpha,beta")) {
			_fail(PROBE, "set panel.ids=alpha,beta not answered");
			return;
		}
		if (!_broadcast_has("panel.ids", "alpha,beta")) {
			_fail(PROBE, "broadcast missing panel.ids=alpha,beta");
			return;
		}
		Genode::log(PROBE, ": panel.ids=alpha,beta round-tripped");

		/*
		 * STEP 2: configure each per-id pattern key.
		 *
		 *   panel.alpha.position=top     (panel-alpha at top edge)
		 *   panel.alpha.height=40        (panel-alpha thickness)
		 *   panel.beta.position=bottom   (panel-beta at bottom edge)
		 *   panel.beta.height=28         (panel-beta thickness)
		 *
		 * The 1024x768 reference screen puts panel-alpha at y=0..40
		 * (the y=14 click target) and panel-beta at y=740..768 (the
		 * y=740 click target).
		 */
		auto write_id_key = [&](char const *k, char const *v,
		                        char const *desc) {
			if (!_send_set(k, v)) {
				_fail(PROBE, desc);
				return false;
			}
			if (!_broadcast_has(k, v)) {
				_fail(PROBE, "broadcast missing per-id key");
				return false;
			}
			Genode::log(PROBE, ": ", k, "=", v, " round-tripped");
			return true;
		};

		if (!write_id_key("panel.alpha.position", "top",
		                  "set panel.alpha.position=top not answered"))
			return;
		if (!write_id_key("panel.alpha.height", "40",
		                  "set panel.alpha.height=40 not answered"))
			return;
		if (!write_id_key("panel.beta.position", "bottom",
		                  "set panel.beta.position=bottom not answered"))
			return;
		if (!write_id_key("panel.beta.height", "28",
		                  "set panel.beta.height=28 not answered"))
			return;

		/*
		 * STEP 3: ~3s for sponge-de's PanelCollection to instantiate
		 * the two PanelWidget instances (their Gui sessions are
		 * routed to their own nitpicker domains via the F5-distinct
		 * label suffixes "Sponge Panel Alpha" / "Sponge Panel Beta").
		 */
		_timer.msleep(3000);

		/*
		 * STEP 4: per-panel click_count reporters must exist (the
		 * initial click_count=0 report is emitted by mousePressEvent's
		 * first paint; we poll for click_count=0 explicitly to gate
		 * the click phase).
		 *
		 * If the reporters are absent (no panel_alpha ROM), the
		 * Phase 16 W8 implementation has not landed yet — the probe
		 * gates the FAIL sentry here.
		 */
		if (!_panel_click_count_is("panel_alpha", "alpha", 0)) {
			_fail(PROBE, "panel_alpha reporter missing or click_count != 0 "
			              "(W8 PanelCollection / per-panel reporters not landed)");
			return;
		}
		Genode::log(PROBE, ": panel_alpha reporter present (click_count=0)");

		if (!_panel_click_count_is("panel_beta", "beta", 0)) {
			_fail(PROBE, "panel_beta reporter missing or click_count != 0 "
			              "(W8 PanelCollection / per-panel reporters not landed)");
			return;
		}
		Genode::log(PROBE, ": panel_beta reporter present (click_count=0)");

/*
		 * STEP 5: QMP click at panel-alpha (100, 14). The plan W8
		 * task 4 spec:
		 *
		 *   "qmp_click 100 14 (panel-alpha's clicked-coordinate at
		 *    its top-edge y=14); the probe asserts ONLY
		 *    panel_alpha.click_count=1 = increments"
		 *
		 * The host's qmp_click (qmp.inc, Phase 10 calibrated PS/2
		 * recipe) dispatches a real BTN_LEFT press at that pixel.
		 * nitpicker's policy routes the Gui session "Sponge Panel
		 * alpha" to <domain name="panel_alpha"> at y=0..40, so the
		 * click registers on the panel_alpha widget only.
		 */
		Genode::log("QMP-TARGET click 100 14");

		if (!_panel_click_count_is("panel_alpha", "alpha", 1)) {
			_fail(PROBE, "panel_alpha.click_count did not become 1 after "
			              "qmp_click 100 14 (F5 trap or panel not constructed)");
			return;
		}
		/*
		 * The "ONLY" assertion: panel_beta MUST NOT have
		 * incremented from the panel-alpha click. The two panels
		 * are on disjoint nitpicker domains, so the click at
		 * (100, 14) is geometrically impossible to reach panel-beta
		 * (which covers y=740..768). If panel_beta's click_count
		 * is non-zero, both panels likely collapsed onto the same
		 * domain — F5 trap signature.
		 */
		if (!_panel_click_count_is("panel_beta", "beta", 0)) {
			_fail(PROBE, "panel_beta.click_count != 0 after panel-alpha click "
			              "(F5 label_prefix trap: both panels collapsed onto the "
			              "same nitpicker domain)");
			return;
		}
		Genode::log(PROBE, ": qmp_click 100 14 -> panel_alpha.click_count=1, "
		                  "panel_beta.click_count=0 (F5 trap avoided)");

/*
		 * STEP 6: QMP click at panel-beta (800, 740). The plan W8
		 * task 4 spec:
		 *
		 *   "qmp_click 800 740 (panel-beta's bottom y=740 at default
		 *    screen 1024x768 and panel.height=28); the probe asserts
		 *    ONLY panel_beta.click_count=1 = increments
		 *    (panel_alpha.click_count stays at 1)"
		 */
		Genode::log("QMP-TARGET click 800 740");

		if (!_panel_click_count_is("panel_beta", "beta", 1)) {
			_fail(PROBE, "panel_beta.click_count did not become 1 after "
			              "qmp_click 800 740");
			return;
		}
		/*
		 * panel_alpha MUST stay at 1 — the click at (800, 740) is
		 * geometrically impossible to reach panel-alpha
		 * (y=0..40). A panel_alpha click_count of 2 means the
		 * click crossed to the wrong domain (F5 trap).
		 */
		if (!_panel_click_count_is("panel_alpha", "alpha", 1)) {
			_fail(PROBE, "panel_alpha.click_count != 1 after panel-beta click "
			              "(F5 label_prefix trap: click crossed to wrong domain)");
			return;
		}
		Genode::log(PROBE, ": qmp_click 800 740 -> panel_beta.click_count=1, "
		                  "panel_alpha.click_count=1 (F5 trap avoided)");

		Genode::log(PROBE, ": PASS");
		_env.parent().exit(0);
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }