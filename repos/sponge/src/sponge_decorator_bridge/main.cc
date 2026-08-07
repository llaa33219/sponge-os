/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * sponge_decorator_bridge — themed_decorator live-policy color bridge
 *                            (Phase 11 W4).
 *
 * A small, signal-driven Genode component that bridges the
 * `sponge_themed` theme ROM (the same channel sponge-de's
 * ThemeController reads) to themed_decorator's `<config>` ROM. The
 * bridge regenerates a `<policy ... color=".."/>``` block derived from
 * the active theme's `[colors] panel_bg` key, so palette colors flow
 * LIVE without a tar reload (the decorator's assets are cached in
 * statics; see genode/repos/gems/src/app/themed_decorator/theme.cc:49-70).
 *
 * Channel ownership: SINGLE WRITER for the `decorator_config` report.
 * The bridge is the only writer; themed_decorator reads the report
 * via a report_rom policy in the run script. Cross-writer collisions
 * would corrupt the policy state.
 *
 *   vct --[config_request]--> sponge_configd --[config]--> sponge_themed
 *                                                            |
 *                                                            v
 *                                                  [theme] report_rom
 *                                                            |
 *                                                            v
 *                                           sponge_decorator_bridge (this)
 *                                                            |
 *                                                            v
 *                                          [decorator_config] report_rom
 *                                                            |
 *                                                            v
 *                                                    themed_decorator
 *
 * Minimum privilege (AGENTS.md §1.2, §5.5 risk-register row 11):
 *   - ROM session "theme"        (one) — reads sponge_themed's report
 *   - Report session "decorator_config" (one) — writes the policy XML
 * No Timer, no libc, no File_system, no NIC.
 *
 * Why the policy color is `panel_bg` and not `accent`:
 *   themed_decorator's `_config.base_color()` is TINTED over the
 *   texture palette (see window.h:285-288 `Tint_painter::paint`). The
 *   texture itself is the default.png stretchable 9-slice frame. The
 *   bridge's choice is the visible color the user sees as the chrome
 *   base — `panel_bg` matches the desktop's panel surface, which is
 *   what the user expects for the title bar. `accent` is reserved for
 *   the Sponge DE accent role (the toggle's hover ring, etc.) and
 *   would re-color the chrome on every theme switch with an unintended
 *   hue.
 *
 * Config-update routing (the `config` ROM label reservation):
 *   themed_decorator reads its live config via the `"config"` ROM
 *   label, which Genode init reserves for the child's inline `<config>`
 *   block (genode/repos/os/src/lib/sandbox/child.cc:510-524). The run
 *   script therefore declares NO inline `<config>` for the decorator
 *   child and routes the `"config"` ROM request through report_rom to
 *   the bridge's report:
 *
 *     <start name="themed_decorator">
 *       <route>
 *         <service name="ROM" label="config">
 *           <child name="report_rom"/>
 *         </service>
 *         ...
 *       </route>
 *     </start>
 *
 *     <policy label="themed_decorator -> config" report="sponge_decorator_bridge -> decorator_config"/>
 *
 *   This is the exact label syntax used in run/sponge-wm-qmp.run's
 *   existing policies (e.g. `label: decorator -> pointer`).
 *
 * De-duplication (AGENTS.md §3.5):
 *   The bridge tracks the last color it published and skips re-emits
 *   when the parsed color is unchanged. The XML reporter would dedup
 *   byte-identical payloads anyway (report_rom's `Module::size()`
 *   stays the same), but the explicit check keeps the bookkeeping
 *   trivial and the log noise low.
 *
 * Fault tolerance (Phase 11 W4 README requirement):
 *   The bridge is NOT on the critical boot path. If the `theme` ROM
 *   is unreadable (e.g. a scenario wires the bridge but forgets the
 *   `sponge_themed` report_rom policy), the bridge logs a warning
 *   and publishes a `<default-policy color=".."/>` fallback so the
 *   decorator at least boots with a sane color. The decorator
 *   itself is the source of truth for the texture frame; the color
 *   is purely a tint over the frame.
 *
 *   If the bridge component fails to start (e.g. Out_of_ram at boot),
 *   themed_decorator's `config` ROM is empty. The decorator's
 *   `Config` constructor still accepts an empty `<config>` node — it
 *   just applies `default-policy` (no color override). The scenario
 *   MUST boot; the title bar will be untinted (the texture only).
 *
 * Motion attribute (Phase 11 risk-register row 18 enforcement):
 *   The hard-coded `<policy motion="1"/>` is emitted because the
 *   decorator's parsed `motion` attribute is `unsigned` (config.h:63-66
 *   `_policy_attribute(..., "motion", 0U)`). The string "yes" is a
 *   parse error, "1" is the unsigned literal. We want animated drag
 *   motion (the layouter's deferred-DRAG protocol relies on the
 *   window animating toward the target for the Input::Seq_number to
 *   settle).
 *
 * Threading: ROM signals fire on the Genode entrypoint dispatcher
 * thread; the handler is pure ROM read + Xml_generator-only. No
 * shared state across threads.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <os/reporter.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Decorator_Bridge {

class Main;

}  /* namespace Sponge::Decorator_Bridge */


class Sponge::Decorator_Bridge::Main
{
	public:

		explicit Main(Genode::Env &env);

	private:

		Genode::Env &_env;

		/*
		 * The theme ROM (the same channel sponge-de's ThemeController
		 * reads, sourced from sponge_themed via report_rom).
		 *
		 * The ROM carries the raw theme INI as the decoded text content
		 * of a <theme name="..."> node, e.g.
		 *   <theme name="light">[colors]\npanel_bg = #...​...</theme>
		 * (see sponge_themed/main.cc:_publish).
		 */
		Genode::Attached_rom_dataspace _theme_rom { _env, "theme" };

		Genode::Signal_handler<Main> _theme_sigh {
			_env.ep(), *this, &Main::_handle_theme };

		/*
		 * The fully-rendered `decorator_config` report. The bridge owns
		 * this label exclusively; report_rom is single-writer per label.
		 */
		Genode::Expanding_reporter _config_reporter {
			_env, "decorator_config", "decorator_config" };

		/*
		 * Last emitted color. Empty string until the first emit. Used
		 * for de-duplication (no re-emit when the palette-derived color
		 * is unchanged).
		 */
		Genode::String<16> _last_color { };

		/* ---- handlers ---- */
		void _handle_theme();

		/* ---- resolution ---- */
		bool _read_panel_bg(Genode::String<16> &out) const;
		void _publish_config(char const *color);
		void _publish_fallback();
};


bool Sponge::Decorator_Bridge::Main::_read_panel_bg(
        Genode::String<16> &out) const
{
	if (!_theme_rom.valid())
		return false;

	/*
	 * The theme ROM is the sponge_themed report: <theme name="...">[raw INI]</theme>.
	 * We read the INI text content and apply a tiny INI parser to extract
	 * the [colors] panel_bg entry. The parser tolerates CR/CRLF/LF line
	 * endings and trims whitespace.
	 *
	 * A full INI library is overkill for one key — but the parser is
	 * section-aware so a future "panel_bg OR accent fallback" can be
	 * added without parsing twice.
	 */
	Genode::Xml_node const root = _theme_rom.xml();

	if (!root.has_type("theme")) {
		Genode::warning("sponge_decorator_bridge: theme ROM root is not "
		                "<theme> (got <", root.type(), ">)");
		return false;
	}

	/*
	 * Extract the decoded text content of the <theme> node. The
	 * sponge_themed report embeds the raw INI via append_sanitized,
	 * which is available as decoded_content.
	 */
	Genode::String<8192> const ini = root.decoded_content<Genode::String<8192>>();

	char const *p = ini.string();
	Genode::size_t const len = ini.length();

	enum class Section { Other, Colors };
	Section section = Section::Other;

	/* Scan every line; remember the [colors] section; for that section,
	 * pick the first `panel_bg = X` we see. */
	char const *end = p + len;
	char const *line = p;

	while (line < end) {
		/* find end-of-line */
		char const *eol = line;
		while (eol < end && *eol != '\n' && *eol != '\r') ++eol;

		/* trim leading whitespace */
		char const *s = line;
		while (s < eol && (*s == ' ' || *s == '\t')) ++s;

		/* skip blank lines and comments */
		if (s == eol || *s == '#' || *s == ';') {
			line = eol;
			/* advance past \r\n */
			if (line < end && *line == '\r') ++line;
			if (line < end && *line == '\n') ++line;
			continue;
		}

		/* [section] header */
		if (*s == '[') {
			++s;
			char const *tag = s;
			while (s < eol && *s != ']') ++s;
			if (s < eol) {
				Genode::String<32> tag_str;
				tag_str = Genode::String<32>(tag, s - tag);
				if (tag_str == Genode::String<32>("colors")) {
					section = Section::Colors;
				} else {
					section = Section::Other;
				}
			}
		}
		/* key = value */
		else if (section == Section::Colors) {
			/* parse key */
			char const *k = s;
			while (s < eol && *s != '=' && *s != ' ' && *s != '\t') ++s;
			Genode::String<32> key(k, s - k);

			/* skip to '=' */
			while (s < eol && (*s == ' ' || *s == '\t')) ++s;
			if (s < eol && *s == '=') ++s;
			while (s < eol && (*s == ' ' || *s == '\t')) ++s;

			/* value is the rest of the line (trim trailing CR/space) */
			char const *v = s;
			char const *vend = eol;
			while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t'
			                    || vend[-1] == '\r')) --vend;

			Genode::String<32> const key_panel_bg("panel_bg");
			if (key == key_panel_bg) {
				/*
				 * The decor color must be a #RRGGBB hex literal. We
				 * accept any non-empty token here; a malformed color
				 * survives the decorator's Color parser as black
				 * (the documented `Color::black()` default in
				 * config.h:73-79), which is benign — the user sees a
				 * black tint, not a crash.
				 */
				Genode::String<16> val;
				val = Genode::String<16>(v, vend - v);
				if (val.length() > 0) {
					out = val;
					return true;
				}
			}
		}

		line = eol;
		if (line < end && *line == '\r') ++line;
		if (line < end && *line == '\n') ++line;
	}

	return false;
}


void Sponge::Decorator_Bridge::Main::_publish_config(char const *color)
{
	/*
	 * Emit the COMPLETE decorator config shape:
	 *
	 *   <config>
	 *     <libc stdout="/dev/log" stderr="/dev/log"/>
	 *     <vfs>
	 *       <dir name="dev"><log/></dir>
	 *       <tar name="decor.tar"/>
	 *     </vfs>
	 *     <policy label_prefix="" decoration="yes" motion="1" color="..."/>
	 *     <default-policy decoration="yes" motion="1" color="..."/>
	 *   </config>
	 *
	 * The run script MUST NOT declare an inline <config> on the
	 * themed_decorator child (sandbox/child.cc:510-524 reserves the
	 * "config" ROM label for the inline block — an inline block
	 * shadows the report_rom route and the bridge's report is dead).
	 * The bridge is therefore the SOLE source of the decorator's
	 * entire config — libc + vfs + policy nodes — and this function
	 * emits all of them.
	 *
	 * The empty label_prefix makes the policy match any window and
	 * keeps the same color across all decorated windows. The
	 * default-policy is a defense-in-depth: if a future window title
	 * doesn't match any explicit policy, the decorator still gets the
	 * themed color.
	 *
	 * motion="1" is REQUIRED (risk-register row 18: the decorator's
	 * `_policy_attribute` parses `motion` as `unsigned`, and "yes" is
	 * a parse error). motion=1 enables the window-move animation the
	 * layouter's deferred-DRAG protocol relies on.
	 *
	 * decoration="yes" is the documented default behavior; we make it
	 * explicit so the policy is self-describing when read from the
	 * report_rom log.
	 */
	_config_reporter.generate_xml([&](Genode::Xml_generator &g) {
		/* libc: stdout/stderr -> /dev/log so the component's
		 * warning() / error() / log() calls reach the run tool. */
		g.node("libc", [&] {
			g.attribute("stdout", "/dev/log");
			g.attribute("stderr", "/dev/log");
		});

		/* vfs: <dev/log> for libc + <decor.tar> mount.
		 * The tar's internal layout is theme/{default.png,
		 * closer.png, maximizer.png, font.tff, metadata};
		 * the themed_decorator reads the metadata via File("theme/
		 * metadata", alloc) in theme.cc:99-104. */
		g.node("vfs", [&] {
			g.node("dir", [&] {
				g.attribute("name", "dev");
				g.node("log");
			});
			g.node("tar", [&] {
				g.attribute("name", "decor.tar");
			});
		});

		g.node("policy", [&] {
			g.attribute("label_prefix", "");
			g.attribute("decoration", "yes");
			g.attribute("motion",       "1");
			g.attribute("color",        color);
		});
		g.node("default-policy", [&] {
			g.attribute("decoration", "yes");
			g.attribute("motion",       "1");
			g.attribute("color",        color);
		});
	});

	Genode::log("sponge_decorator_bridge: published decorator_config color=\"",
	            color, "\"");
}


void Sponge::Decorator_Bridge::Main::_publish_fallback()
{
	/*
	 * Fallback: the theme ROM was unreadable (not present, not yet
	 * valid, or malformed). The C++ standard watercolor we emit matches
	 * default.theme's panel_bg (#1e1e2e), so the chrome looks the same
	 * as a default-theme boot. The decorator always renders a sane
	 * untinted title bar in this case.
	 *
	 * The fallback is emitted inline instead of leaving the report
	 * empty so the decorator's first read after construction sees a
	 * complete config (the very first config sigh handler runs even
	 * before the theme ROM is ready — see Constructor order below).
	 */
	_publish_config("#1e1e2e");
	Genode::warning("sponge_decorator_bridge: theme ROM unavailable; "
	                "falling back to default color");
}


void Sponge::Decorator_Bridge::Main::_handle_theme()
{
	_theme_rom.update();

	Genode::String<16> color;
	if (!_read_panel_bg(color)) {
		/*
		 * No panel_bg in the theme. Don't overwrite the last good
		 * color; the user-visible chrome keeps the last applied theme's
		 * tint (which is the desired behavior — a theme switch that
		 * drops the panel_bg entry should not blank the chrome).
		 *
		 * If we never published anything, publish the fallback.
		 */
		if (_last_color == Genode::String<16>()) {
			_publish_fallback();
		} else {
			Genode::warning("sponge_decorator_bridge: theme ROM has no "
			                "[colors] panel_bg; keeping previous color '",
			                _last_color.string(), "'");
		}
		return;
	}

	/* De-duplicate: identical color → no re-emit. */
	if (color == _last_color)
		return;

	_last_color = color;
	_publish_config(color.string());
}


/* ===================== component wiring ===================== */

Sponge::Decorator_Bridge::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_decorator_bridge: ready");

	/*
	 * Resolve the initial theme immediately so the decorator's first
	 * `_handle_config` (which runs in its constructor before the sigh
	 * handler is wired — see main.cc:144-145) sees the config ROM.
	 * sponge_themed's initial publish is synchronous from its own
	 * constructor (line 260-264), and report_rom relays the
	 * module-on-demand with no race.
	 *
	 * If the theme ROM is not yet valid (e.g. the report_rom policy
	 * forgot to route `theme` to the bridge), the fallback publish
	 * ensures the decorator's config ROM is never empty.
	 */
	_theme_rom.update();
	_handle_theme();

	_theme_rom.sigh(_theme_sigh);
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Decorator_Bridge::Main main { env };
}


/*
 * Resolution touches one ROM and emits an XML report; the default
 * stack (16 KiB) is sufficient. Stack is query-pointed upward if
 * a future parser grows.
 */
Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
