/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of vct's Phase 2 subcommands.
 *
 * Status and component list now read the live init state report via
 * InitStateReader. Commands still warn via Genode::warning when a path
 * is not yet implemented (AGENTS.md §5.3).
 */

#include "commands.h"

#include "init_state.h"
#include "notifier_reporter.h"

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>

#include <sponge/backend_client.h>
#include <sponge/platform.h>
#include <sponge/version.h>

#include <util/string.h>
#include <util/xml_node.h>

using namespace Sponge;
using namespace Sponge::Vct;
using Sponge::Backend::ReportRomClient;

namespace {

bool read_config_key(Genode::Env &env, char const *name,
                     Genode::String<128> &value)
{
	try {
		Genode::Attached_rom_dataspace config { env, "configd" };
		config.update();
		if (!config.valid()) return false;
		bool found { false };
		config.xml().for_each_sub_node("key", [&] (Genode::Xml_node const &key) {
			if (!found && key.attribute_value("name", Genode::String<64>()) ==
			              Genode::String<64>(name)) {
				value = key.attribute_value("value", Genode::String<128>());
				found = true;
			}
		});
		return found;
	}
	catch (Genode::Rom_connection::Rom_connection_failed) { return false; }
	catch (Genode::Service_denied) { return false; }
	catch (Genode::Out_of_ram) { return false; }
	catch (Genode::Out_of_caps) { return false; }
	catch (Genode::Xml_node::Invalid_syntax) { return false; }
}

}  /* namespace */


/* ===================== HelpCommand ===================== */

int HelpCommand::execute(Args const &args)
{
	(void) args;

	Genode::log("vct — Very Convenient Tool");
	Genode::log("version: ", Sponge::VERSION_STRING, " (", Sponge::CODENAME, ")");
	Genode::log("");

	if (args.lang == "ko") {
		Genode::log("Sponge OS 패키지, 컴포넌트, 설정을 관리하는 명령줄 도구입니다.");
		Genode::log("");
		Genode::log("사용 가능한 하위 명령어:");
		Genode::log("  vct status                시스템 상태 요약 보기");
		Genode::log("  vct status --resources    RAM/cap 사용량 + 자식별 통계");
		Genode::log("  vct version               vct / Sponge OS 버전 출력");
		Genode::log("  vct help                  이 도움말 출력");
		Genode::log("  vct component list        실행 중인 컴포넌트 목록 보기");
		Genode::log("  vct install <pkg> --explain  패키지 설치 계획 미리보기");
		Genode::log("  vct install <pkg>         패키지 설치 (--manual: 단계별 출력)");
		Genode::log("  vct remove <pkg>          패키지 제거");
		Genode::log("  vct launch <pkg>          설치된 패키지 시작");
		Genode::log("  vct list                  설치된 패키지 목록");
		Genode::log("  vct search <term>         이미지 내 패키지 저장소 검색");
		Genode::log("  vct update [<pkg>]        설치된 패키지와 저장소 버전 비교");
		Genode::log("  vct shutdown              시스템 종료 (ACPI poweroff)");
		Genode::log("  vct reboot                시스템 재부팅 (ACPI reset)");
		Genode::log("  vct config <key>          설정 값 조회");
		Genode::log("  vct config <key> <value>  설정 값 변경");
		Genode::log("  vct config list           전체 설정 키 목록");
		Genode::log("  vct bake show            베이크 프로필과 현재 설정 비교");
		Genode::log("  vct bake list            이 미디어의 베이크 프로필 보기");
		Genode::log("  vct bake reset           베이크 기본값 다시 적용");
		Genode::log("  vct theme apply <name>    바탕화면 테마 적용");
		Genode::log("  vct leitzentrale          전문가 제어 창 열기 (끄기: vct leitzentrale off)");
		Genode::log("");
		Genode::log("공통 옵션: --explain, --manual, --json, --verbose, --lang ko");
	} else {
		Genode::log("Available subcommands (some are planned, see docs/06-vct.md):");
		Genode::log("  vct status                Show system status");
		Genode::log("  vct status --resources    Show live RAM + cap usage breakdown");
		Genode::log("  vct version               Print vct / Sponge OS version");
		Genode::log("  vct help                  Show this help");
		Genode::log("  vct component list        List the running components");
		Genode::log("  vct install <pkg> --explain  Preview the install plan");
		Genode::log("  vct install <pkg>         Install a package (--manual: step-by-step)");
		Genode::log("  vct remove <pkg>          Remove an installed package");
		Genode::log("  vct launch <pkg>          Start an installed package");
		Genode::log("  vct list                  List installed packages");
		Genode::log("  vct search <term>         Search the on-image package repository");
		Genode::log("  vct update [<pkg>]        Report version deltas vs the on-image repo");
		Genode::log("  vct shutdown              Shut down the system (ACPI poweroff)");
		Genode::log("  vct reboot                Reboot the system (ACPI reset)");
		Genode::log("  vct config <key>          Get a configuration value");
		Genode::log("  vct config <key> <value>  Set a configuration value");
		Genode::log("  vct config list           List all configuration keys");
		Genode::log("  vct bake show            Compare baked defaults with current values");
		Genode::log("  vct bake list            Show the profile carried by this media");
		Genode::log("  vct bake reset           Restore this media's baked defaults");
		Genode::log("  vct theme apply <name>    Apply a desktop theme");
		Genode::log("  vct leitzentrale          Open the Leitzentrale expert window (off: vct leitzentrale off)");
		Genode::log("");
		Genode::log("Common flags: --explain, --manual, --json, --verbose, --lang ko");
	}

	Genode::warning("not implemented: full subcommand set (Phase 4+). status, version, help, component list, install (--explain/--manual/plain), remove, launch, list, search, update, config, theme apply, leitzentrale, shutdown, and reboot work today.");
	return 0;
}


/* ===================== VersionCommand ===================== */

int VersionCommand::execute(Args const &args)
{
	(void) args;

	Genode::log("vct ", Sponge::VERSION_STRING);
	Genode::log("Sponge OS codename: ", Sponge::CODENAME);
	Genode::log("kernel base:        ", Sponge::PLATFORM_BASE,
	            " (", Sponge::PLATFORM_VENDOR, ")");
	return 0;
}


/* ===================== StatusCommand ===================== */

int StatusCommand::execute(Args const &args)
{
	/*
	 * Phase 14 W11 #43: --resources renders the focused RAM + caps
	 * breakdown from the live init state report, closing the
	 * commands.cc:158 placeholder. Plain status keeps its summary
	 * shape. --json composes with both modes.
	 */
	InitStateReader state { _env };
	Genode::String<128> bake_profile { };
	Genode::String<128> bake_version { };
	bool const have_bake = read_config_key(_env, "bake.profile", bake_profile) &&
	                       read_config_key(_env, "bake.version", bake_version) &&
	                       bake_profile != Genode::String<128>("none");

	if (args.resources) {
		if (!state.available()) {
			Genode::warning("vct: init state report unavailable — "
			                "cannot show resources");
			return 1;
		}

		if (args.json) {
			Genode::log("{\"command\":\"status\",\"resources\":true,"
			            "\"ram\":{\"used\":\"",
			            Genode::Number_of_bytes(state.total_ram_used()),
			            "\",\"quota\":\"",
			            Genode::Number_of_bytes(state.total_ram_quota()),
			            "\",\"avail\":\"",
			            Genode::Number_of_bytes(state.total_ram_avail()),
			            "\"},\"caps\":{\"used\":",
			            state.total_caps_used(),
			            ",\"quota\":",
			            state.total_caps_quota(),
			            ",\"avail\":",
			            state.total_caps_avail(),
			            "},\"children\":[");
			bool first = true;
			state.for_each_child([&] (InitStateReader::Child const &c) {
				Genode::log(first ? " {" : " ,{",
				            "\"name\":\"", c.name, "\","
				            "\"binary\":\"", c.binary, "\","
				            "\"state\":\"", c.state, "\","
				            "\"ram\":{\"used\":\"",
				            Genode::Number_of_bytes(c.ram_used),
				            "\",\"quota\":\"",
				            Genode::Number_of_bytes(c.ram_quota),
				            "\"},\"caps\":{\"used\":",
				            c.cap_used, ",\"quota\":",
				            c.cap_quota, "}}");
				first = false;
			});
			Genode::log("]}");
			return 0;
		}

		Genode::log("=== Sponge OS status --resources ===");
		Genode::log("vct version: ", Sponge::VERSION_STRING);
		Genode::log("kernel base: ", Sponge::PLATFORM_BASE);
		if (have_bake)
			Genode::log("bake:        ", bake_profile, " @ v", bake_version);
		else
			Genode::log("bake:        none");
		Genode::log("");
		Genode::log("RESOURCE         USED          QUOTA          AVAIL");
		Genode::log("init RAM       ",
		            Genode::Number_of_bytes(state.total_ram_used()), "   ",
		            Genode::Number_of_bytes(state.total_ram_quota()), "   ",
		            Genode::Number_of_bytes(state.total_ram_avail()));
		if (state.total_caps_quota() > 0 || state.total_caps_used() > 0) {
			Genode::log("init caps      ",
			            state.total_caps_used(), "         ",
			            state.total_caps_quota(), "         ",
			            state.total_caps_avail());
		}
		Genode::log("");
		Genode::log("PER-CHILD:");
		Genode::log("NAME                       BINARY                 RAM(used/quota)        CAPS(used/quota)        STATE");
		Genode::log("----                       ------                 ---------------        ----------------        -----");
		state.for_each_child([&] (InitStateReader::Child const &c) {
			Genode::log(c.name, "  ", c.binary, "  ",
			            Genode::Number_of_bytes(c.ram_used), "/",
			            Genode::Number_of_bytes(c.ram_quota), "        ",
			            c.cap_used, "/", c.cap_quota, "        ",
			            c.state);
		});
		return 0;
	}

	if (args.json) {
		if (state.available()) {
			if (have_bake)
				Genode::log("{\"command\":\"status\",\"ram_quota\":\"",
				            Genode::Number_of_bytes(state.total_ram_quota()),
				            "\",\"ram_used\":\"",
				            Genode::Number_of_bytes(state.total_ram_used()),
				            "\",\"ram_avail\":\"",
				            Genode::Number_of_bytes(state.total_ram_avail()),
				            "\",\"components\":", state.child_count(),
				            ",\"version\":\"", Sponge::VERSION_STRING,
				            "\",\"bake\":{\"profile\":\"", bake_profile,
				            "\",\"version\":", bake_version, "}}");
			else
				Genode::log("{\"command\":\"status\",\"ram_quota\":\"",
				            Genode::Number_of_bytes(state.total_ram_quota()),
				            "\",\"ram_used\":\"",
				            Genode::Number_of_bytes(state.total_ram_used()),
				            "\",\"ram_avail\":\"",
				            Genode::Number_of_bytes(state.total_ram_avail()),
				            "\",\"components\":", state.child_count(),
				            ",\"version\":\"", Sponge::VERSION_STRING,
				            "\",\"bake\":null}");
		} else {
			Genode::log("{\"command\":\"status\",\"ram_quota\":0,\"ram_used\":0,\"ram_avail\":0,\"components\":0,\"version\":\"",
			            Sponge::VERSION_STRING,
			            "\",\"status\":\"scaffold\"}");
		}
		return 0;
	}

	Genode::log("=== Sponge OS status ===");
	Genode::log("vct version: ", Sponge::VERSION_STRING);
	Genode::log("codename:    ", Sponge::CODENAME);
	Genode::log("kernel base: ", Sponge::PLATFORM_BASE);
	if (have_bake)
		Genode::log("bake:        ", bake_profile, " @ v", bake_version);
	else
		Genode::log("bake:        none");

	if (state.available()) {
		if (state.has_error()) {
			Genode::log("state error: ", state.error_string());
		}
		Genode::log("init RAM:    ",
		            Genode::Number_of_bytes(state.total_ram_used()),  " / ",
		            Genode::Number_of_bytes(state.total_ram_quota()), " (avail ",
		            Genode::Number_of_bytes(state.total_ram_avail()), ")");
		if (state.total_caps_quota() > 0 || state.total_caps_used() > 0) {
			Genode::log("init caps:   ",
			            state.total_caps_used(),  " / ",
			            state.total_caps_quota(), " (avail ",
			            state.total_caps_avail(), ")");
		}
		Genode::log("components:  ", state.child_count());
	} else {
		Genode::log("env:         connected (default sessions)");
		Genode::warning("vct: init state report unavailable — showing scaffold data");
		Genode::warning("not implemented: live component / resource statistics "
		                "(Phase 14 W11 #43 closes this for --resources).");
	}

	return 0;
}


/* ===================== ComponentListCommand ===================== */

int ComponentListCommand::execute(Args const &args)
{
	InitStateReader state { _env };

	if (!state.available()) {
		Genode::warning("vct: init state report unavailable — cannot list components");
		Genode::log("No component data available.");
		return 1;
	}

	if (state.has_error()) {
		Genode::log("Component list unavailable: ", state.error_string());
		return 1;
	}

	if (args.json) {
		Genode::log("[");
		bool first = true;
		state.for_each_child([&] (InitStateReader::Child const &c) {
			Genode::log(first ? " {" : " ,{",
			            "\"name\":\"", c.name, "\","
			            "\"binary\":\"", c.binary, "\","
			            "\"state\":\"", c.state, "\","
			            "\"ram\":{\"assigned\":\"", Genode::Number_of_bytes(c.ram_assigned), "\",\"quota\":\"", Genode::Number_of_bytes(c.ram_quota), "\",\"used\":\"", Genode::Number_of_bytes(c.ram_used), "\",\"avail\":\"", Genode::Number_of_bytes(c.ram_avail), "\"},"
			            "\"caps\":{\"assigned\":", c.cap_assigned, ",\"quota\":", c.cap_quota, ",\"used\":", c.cap_used, ",\"avail\":", c.cap_avail, "}"
			            "}");
			first = false;
		});
		Genode::log("]");
		return 0;
	}

	Genode::log("NAME  BINARY  RAM(used/assigned)  CAPS(used/assigned)  STATE");
	Genode::log("----  ------  -----------------  --------------------  -----");

	state.for_each_child([&] (InitStateReader::Child const &c) {
		Genode::log(c.name, "  ", c.binary, "  ram ",
		            Genode::Number_of_bytes(c.ram_used), "/",
		            Genode::Number_of_bytes(c.ram_assigned),
		            "  caps ", c.cap_used, "/", c.cap_assigned,
		            "  ", c.state);
	});

	return 0;
}


/* ===================== InstallCommand ===================== */

namespace {

/* Build the step-1 dependencies line ("a, b, c" or "none") from the
 * <dependencies><dep name="..."/></dependencies> block. */
Genode::String<256> deps_line_from(Genode::Xml_node const &result)
{
	Genode::String<256> out { };
	bool any { false };

	result.with_optional_sub_node("dependencies",
		[&](Genode::Xml_node const &deps) {
			char buf[256] { };
			Genode::size_t pos { 0 };
			deps.for_each_sub_node("dep", [&](Genode::Xml_node const &d) {
				Genode::String<64> const name =
					d.attribute_value("name", Genode::String<64>());
				if (pos == 0) {
					any = true;
				} else if (pos + 2 < sizeof(buf)) {
					buf[pos++] = ',';
					buf[pos++] = ' ';
				}
				char const *s = name.string();
				while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
			});
			buf[pos] = 0;
			out = Genode::String<256>(buf);
		});

	if (!any)
		return Genode::String<256>("none");
	return out;
}


/* Build a JSON array string of the `name` attributes of all <component>
 * children under `node_name` (e.g. "components_added"). Emits "[]" if the
 * node is absent or empty. Used by every install/remove/explain renderer. */
Genode::String<256> json_array_of(Genode::Xml_node const &result, char const *node_name)
{
	char buf[256] { };
	Genode::size_t pos { 0 };

	result.with_optional_sub_node(node_name,
		[&](Genode::Xml_node const &comps) {
			if (pos + 1 < sizeof(buf)) buf[pos++] = '[';
			bool first { true };
			comps.for_each_sub_node("component", [&](Genode::Xml_node const &c) {
				if (!first && pos + 2 < sizeof(buf)) {
					buf[pos++] = ','; buf[pos++] = ' ';
				}
				first = false;
				if (pos + 2 < sizeof(buf)) buf[pos++] = '"';
				Genode::String<64> const name =
					c.attribute_value("name", Genode::String<64>());
				char const *s = name.string();
				while (*s && pos + 2 < sizeof(buf)) buf[pos++] = *s++;
				if (pos + 1 < sizeof(buf)) buf[pos++] = '"';
			});
			if (pos + 1 < sizeof(buf)) buf[pos++] = ']';
		});

	if (pos == 0) { buf[0] = '['; buf[1] = ']'; pos = 2; }
	buf[pos] = 0;
	return Genode::String<256>(buf);
}

}  /* namespace */


int InstallCommand::execute(Args const &args)
{
	char const *const pkg = args.positional.string();

	if (Genode::strcmp(pkg, "") == 0) {
		Genode::warning("vct: install requires a package name");
		Genode::log("Usage: vct install <package> [--explain] [--manual] [--json]");
		return 1;
	}

	ReportRomClient client { _env };

	if (args.manual) {
		/* --manual executes the same install as the automatic path, but
		 * renders each step individually. There is no terminal input
		 * channel in Genode yet, so the per-step [Y/n] prompts from
		 * docs/06-vct.md §5.3 cannot be honored — each step is printed
		 * and the install proceeds. We first explain (to surface the
		 * real plan), then execute the real install. */
		if (!client.request("explain", pkg)) {
			Genode::warning("vct: sponge_pkgd did not answer for '", pkg, "'");
			return 1;
		}
		Genode::Xml_node const plan = client.result_xml();
		if (plan.attribute_value("status", Genode::String<32>()) != Genode::String<32>("ok")) {
			Genode::String<256> const err =
				plan.attribute_value("error", Genode::String<256>());
			Genode::log("install: error: ", err);
			return 1;
		}

		_render_manual_plan(plan);

		if (!client.request("install", pkg)) {
			Genode::warning("vct: sponge_pkgd did not answer for '", pkg, "'");
			return 1;
		}
		Genode::Xml_node const inst = client.result_xml();
		if (inst.attribute_value("status", Genode::String<32>()) != Genode::String<32>("ok")) {
			Genode::String<256> const err =
				inst.attribute_value("error", Genode::String<256>());
			Genode::log("install: error: ", err);
			return 1;
		}

		_render_manual_done(inst);
		return 0;
	}

	if (args.explain) {
		if (!client.request("explain", pkg)) {
			Genode::warning("vct: sponge_pkgd did not answer for '", pkg, "'");
			return 1;
		}
		return args.json ? _render_explain_json(client.result_xml())
		                 : _render_explain_human(client.result_xml());
	}

	if (!client.request("install", pkg)) {
		if (args.json)
			Genode::log("{\"command\":\"install\",\"package\":\"", pkg,
			            "\",\"status\":\"error\",\"error\":\"sponge_pkgd did not answer\"}");
		else
			Genode::warning("vct: sponge_pkgd did not answer for '", pkg, "'");
		return 1;
	}

	return args.json ? _render_install_json(client.result_xml())
	                 : _render_install_human(client.result_xml());
}


int InstallCommand::_render_explain_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("install: error: ", err);
		return 1;
	}

	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());
	Genode::String<32> const ver =
		result.attribute_value("version", Genode::String<32>());

	Genode::log("The following steps are planned:");
	Genode::log("");

	/* Step 1: identity + direct dependencies (docs/06 §5.2). */
	Genode::log("1. Fetch package metadata");
	Genode::log("   - ", pkg, " ", ver);
	Genode::log("   - Dependencies: ", deps_line_from(result));

	/* Step 2: expanded component tree, install order (deps first). */
	Genode::log("2. Add to component tree");
	Genode::log("   Under init:");
	result.with_optional_sub_node("components",
		[&](Genode::Xml_node const &comps) {
			comps.for_each_sub_node("component", [&](Genode::Xml_node const &c) {
				Genode::String<64>  const name =
					c.attribute_value("name", Genode::String<64>());
				Genode::String<128> const req =
					c.attribute_value("requires", Genode::String<128>());
				bool const reused = !c.attribute_value("new", true);

				if (reused)
					Genode::log("     - ", name, " (already present, reused)");
				else if (Genode::strcmp(req.string(), "") != 0)
					Genode::log("     - ", name, " (requires ", req, " sessions)");
				else
					Genode::log("     - ", name);
			});
		});

	/* Step 3: session routes, aligned (docs/06 §5.2 / §8.3). */
	Genode::log("3. Configure session routing");
	result.with_optional_sub_node("routes", [&](Genode::Xml_node const &routes) {
		/* Collect route rows to compute the alignment column. */
		struct Row {
			Genode::String<96> label;
			Genode::String<32> target;
			bool               readonly;
			Genode::String<64> subpath;
		};
		Row rows[32] { };
		unsigned n { 0 };
		Genode::size_t max_len { 0 };

		routes.for_each_sub_node("route", [&](Genode::Xml_node const &r) {
			if (n >= 32) return;
			Row &row = rows[n++];
			row.label = Genode::String<96>(
				r.attribute_value("component", Genode::String<64>()),
				".",
				r.attribute_value("session",   Genode::String<32>()));
			row.target   = r.attribute_value("target",  Genode::String<32>());
			row.readonly = r.attribute_value("readonly", false);
			row.subpath  = r.attribute_value("subpath",  Genode::String<64>());

			Genode::size_t const len = Genode::strlen(row.label.string());
			if (len > max_len) max_len = len;
		});

		for (unsigned i = 0; i < n; ++i) {
			Row const &row = rows[i];
			Genode::size_t const len = Genode::strlen(row.label.string());

			char pad[64] { };
			Genode::size_t const pad_n = (len < max_len) ? (max_len - len) : 0;
			for (Genode::size_t k = 0; k < pad_n && k + 1 < sizeof(pad); ++k)
				pad[k] = ' ';
			pad[pad_n < sizeof(pad) ? pad_n : sizeof(pad) - 1] = 0;

			if (row.readonly && Genode::strcmp(row.subpath.string(), "") != 0)
				Genode::log("   ", row.label, Genode::String<64>(pad), " -> ",
				            row.target, " (read-only: ", row.subpath, ")");
			else
				Genode::log("   ", row.label, Genode::String<64>(pad), " -> ",
				            row.target);
		}
	});

	/* Step 4: launcher entry (omitted entirely if absent, docs/12 §8.4). */
	result.with_optional_sub_node("launcher", [&](Genode::Xml_node const &l) {
		Genode::log("4. Register launcher entry");
		Genode::log("   Category: ", l.attribute_value("category",
		            Genode::String<32>()));
	});

	Genode::log("");
	Genode::log("Re-run without --explain to execute.");
	return 0;
}


int InstallCommand::_render_explain_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"install\",\"package\":\"", pkg,
		            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	/* docs/06-vct.md §6.2 shape; "planned" distinguishes a preview from
	 * an executed install's "success". */
	Genode::String<32> const ver =
		result.attribute_value("version", Genode::String<32>());
	Genode::String<256> const components = json_array_of(result, "components");

	result.with_sub_node("launcher", [&](Genode::Xml_node const &l) {
		Genode::log("{\"command\":\"install\",\"package\":\"", pkg,
		            "\",\"status\":\"planned\",\"version\":\"", ver,
		            "\",\"components\":", components,
		            ",\"launcher\":{\"category\":\"",
		            l.attribute_value("category", Genode::String<32>()), "\"}}");
	},
	[&] {
		Genode::log("{\"command\":\"install\",\"package\":\"", pkg,
		            "\",\"status\":\"planned\",\"version\":\"", ver,
		            "\",\"components\":", components, "}");
	});

	return 0;
}


/* ===================== InstallCommand: install renderers ===================== */

int InstallCommand::_render_install_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("install: error: ", err);
		return 1;
	}

	Genode::String<32> const ver =
		result.attribute_value("version", Genode::String<32>());

	/* Collect the added component names for the summary line. */
	char added_buf[256] { };
	Genode::size_t pos { 0 };
	result.with_optional_sub_node("components_added",
		[&](Genode::Xml_node const &comps) {
			bool first { true };
			comps.for_each_sub_node("component", [&](Genode::Xml_node const &c) {
				if (!first && pos + 2 < sizeof(added_buf)) {
					added_buf[pos++] = ','; added_buf[pos++] = ' ';
				}
				first = false;
				Genode::String<64> const name =
					c.attribute_value("name", Genode::String<64>());
				char const *s = name.string();
				while (*s && pos + 1 < sizeof(added_buf)) added_buf[pos++] = *s++;
			});
		});
	added_buf[pos] = 0;

	Genode::log("Resolving dependencies... done");
	Genode::log("Generating component configuration... done");
	Genode::log("Installing... done");
	Genode::log("");
	Genode::log("Installed package: ", pkg, " ", ver);

	if (pos == 0)
		Genode::log("Components added: (none — already installed)");
	else
		Genode::log("Components added: ", Genode::String<256>(added_buf));

	/* Launcher registration is Phase 5 (sponge_configd); surface the
	 * metadata-declared category as informational, never as "registered". */
	result.with_optional_sub_node("launcher", [&](Genode::Xml_node const &l) {
		Genode::log("Launcher entry: ",
		            l.attribute_value("category", Genode::String<32>()),
		            " (registration deferred to Phase 5)");
	});

	if (_notifier) {
		Genode::String<256> const body =
			(pos == 0) ? Genode::String<256>("already installed")
			           : Genode::String<256>(added_buf);
		_notifier->post("install completed", pkg.string(),
		                "info", 5000);
		(void)body;
	}

	return 0;
}


int InstallCommand::_render_install_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"install\",\"package\":\"", pkg,
		            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	/* docs/06-vct.md §6.2 locked schema. duration_ms is omitted rather
	 * than faked (AGENTS.md §5.3). */
	Genode::String<32> const ver =
		result.attribute_value("version", Genode::String<32>());
	Genode::String<256> const added = json_array_of(result, "components_added");

	Genode::log("{\"command\":\"install\",\"package\":\"", pkg,
	            "\",\"status\":\"success\",\"version\":\"", ver,
	            "\",\"components_added\":", added, "}");
	return 0;
}


/* ===================== InstallCommand: --manual renderers ===================== */

void InstallCommand::_render_manual_plan(Genode::Xml_node const &result)
{
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());
	Genode::String<32> const ver =
		result.attribute_value("version", Genode::String<32>());

	Genode::log("Manual install: ", pkg,
	            "  (step-by-step; per-step [Y/n] confirmation arrives with the interactive shell)");
	Genode::log("");

	Genode::log("1/4 Fetch package metadata");
	Genode::log("   - ", pkg, " ", ver);
	Genode::log("   - Dependencies: ", deps_line_from(result));

	Genode::log("2/4 Resolve dependency tree");
	result.with_optional_sub_node("components",
		[&](Genode::Xml_node const &comps) {
			comps.for_each_sub_node("component", [&](Genode::Xml_node const &c) {
				Genode::String<64>  const name =
					c.attribute_value("name", Genode::String<64>());
				Genode::String<128> const req =
					c.attribute_value("requires", Genode::String<128>());
				bool const reused = !c.attribute_value("new", true);
				if (reused)
					Genode::log("   - ", name, " (already present, reused)");
				else if (Genode::strcmp(req.string(), "") != 0)
					Genode::log("   - ", name, " (requires ", req, " sessions)");
				else
					Genode::log("   - ", name);
			});
		});

	Genode::log("3/4 Generate component configuration");
	Genode::log("   adding under pkg_runtime: ", pkg);
}


void InstallCommand::_render_manual_done(Genode::Xml_node const &result)
{
	char added_buf[256] { };
	Genode::size_t pos { 0 };
	result.with_optional_sub_node("components_added",
		[&](Genode::Xml_node const &comps) {
			bool first { true };
			comps.for_each_sub_node("component", [&](Genode::Xml_node const &c) {
				if (!first && pos + 2 < sizeof(added_buf)) {
					added_buf[pos++] = ','; added_buf[pos++] = ' ';
				}
				first = false;
				Genode::String<64> const name =
					c.attribute_value("name", Genode::String<64>());
				char const *s = name.string();
				while (*s && pos + 1 < sizeof(added_buf)) added_buf[pos++] = *s++;
			});
		});
	added_buf[pos] = 0;

	Genode::log("4/4 Start components");
	if (pos == 0)
		Genode::log("   (no new components — already installed)");
	else
		Genode::log("   starting: ", Genode::String<256>(added_buf));

	Genode::log("");
	Genode::log("Done.");
}


/* ===================== RemoveCommand ===================== */

int RemoveCommand::execute(Args const &args)
{
	char const *const pkg = args.positional.string();

	if (Genode::strcmp(pkg, "") == 0) {
		Genode::warning("vct: remove requires a package name");
		Genode::log("Usage: vct remove <package> [--json]");
		return 1;
	}

	ReportRomClient client { _env };
	if (!client.request("remove", pkg)) {
		if (args.json)
			Genode::log("{\"command\":\"remove\",\"package\":\"", pkg,
			            "\",\"status\":\"error\",\"error\":\"sponge_pkgd did not answer\"}");
		else
			Genode::warning("vct: sponge_pkgd did not answer for '", pkg, "'");
		return 1;
	}

	return args.json ? _render_json(client.result_xml())
	                 : _render_human(client.result_xml());
}


int RemoveCommand::_render_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("remove: error: ", err);
		return 1;
	}

	char removed_buf[256] { };
	Genode::size_t pos { 0 };
	result.with_optional_sub_node("components_removed",
		[&](Genode::Xml_node const &comps) {
			bool first { true };
			comps.for_each_sub_node("component", [&](Genode::Xml_node const &c) {
				if (!first && pos + 2 < sizeof(removed_buf)) {
					removed_buf[pos++] = ','; removed_buf[pos++] = ' ';
				}
				first = false;
				Genode::String<64> const name =
					c.attribute_value("name", Genode::String<64>());
				char const *s = name.string();
				while (*s && pos + 1 < sizeof(removed_buf)) removed_buf[pos++] = *s++;
			});
		});
	removed_buf[pos] = 0;

	Genode::log("Removing package: ", pkg);
	if (pos == 0)
		Genode::log("Components removed: (none)");
	else
		Genode::log("Components removed: ", Genode::String<256>(removed_buf));
	Genode::log("Removed package: ", pkg);

	if (_notifier)
		_notifier->post("remove completed", pkg.string(), "info", 5000);

	return 0;
}


int RemoveCommand::_render_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"remove\",\"package\":\"", pkg,
		            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	Genode::String<256> const removed = json_array_of(result, "components_removed");
	Genode::log("{\"command\":\"remove\",\"package\":\"", pkg,
	            "\",\"status\":\"success\",\"components_removed\":", removed, "}");
	return 0;
}


/* ===================== LaunchCommand ===================== */

int LaunchCommand::execute(Args const &args)
{
	char const *const pkg = args.positional.string();

	if (Genode::strcmp(pkg, "") == 0) {
		Genode::warning("vct: launch requires a package name");
		Genode::log("Usage: vct launch <package> [--json] [--help]");
		return 1;
	}

	/* Audit line before acting (docs/06-vct.md §4.2, control philosophy). */
	Genode::log("vct: launch: requesting start of ", pkg);

	ReportRomClient client { _env };
	if (!client.request("launch", pkg)) {
		if (args.json)
			Genode::log("{\"command\":\"launch\",\"package\":\"", pkg,
			            "\",\"status\":\"error\","
			            "\"error\":\"sponge_pkgd did not answer\"}");
		else
			Genode::warning("vct: sponge_pkgd did not answer launch for '", pkg, "'");
		return 1;
	}

	return args.json ? _render_json(client.result_xml())
	                 : _render_human(client.result_xml());
}


int LaunchCommand::_render_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status == Genode::String<32>("ok")) {
		Genode::log("launched: ", pkg);
		return 0;
	}
	if (status == Genode::String<32>("not-installed")) {
		Genode::log("launch: ", pkg, " is not installed");
		return 1;
	}
	if (status == Genode::String<32>("already-running")) {
		Genode::log("launch: ", pkg, " is already running");
		return 1;
	}

	Genode::String<256> const err =
		result.attribute_value("error", Genode::String<256>());
	Genode::log("launch: error: ", err);
	return 1;
}


int LaunchCommand::_render_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const pkg =
		result.attribute_value("pkg", Genode::String<128>());

	if (status == Genode::String<32>("ok")) {
		Genode::log("{\"command\":\"launch\",\"package\":\"", pkg,
		            "\",\"status\":\"success\"}");
		return 0;
	}
	if (status == Genode::String<32>("not-installed")) {
		Genode::log("{\"command\":\"launch\",\"package\":\"", pkg,
		            "\",\"status\":\"error\",\"error\":\"not-installed\"}");
		return 1;
	}
	if (status == Genode::String<32>("already-running")) {
		Genode::log("{\"command\":\"launch\",\"package\":\"", pkg,
		            "\",\"status\":\"error\",\"error\":\"already-running\"}");
		return 1;
	}

	Genode::String<256> const err =
		result.attribute_value("error", Genode::String<256>());
	Genode::log("{\"command\":\"launch\",\"package\":\"", pkg,
	            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
	return 1;
}


/* ===================== ListCommand ===================== */

int ListCommand::execute(Args const &args)
{
	ReportRomClient client { _env };
	if (!client.request("list")) {
		if (args.json)
			Genode::log("{\"command\":\"list\",\"status\":\"error\","
			            "\"error\":\"sponge_pkgd did not answer\"}");
		else
			Genode::warning("vct: sponge_pkgd did not answer for list");
		return 1;
	}

	return args.json ? _render_json(client.result_xml())
	                 : _render_human(client.result_xml());
}


int ListCommand::_render_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("list: error: ", err);
		return 1;
	}

	unsigned count { 0 };
	result.with_optional_sub_node("packages",
		[&](Genode::Xml_node const &pkgs) {
			pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &) {
				++count;
			});
		});

	if (count == 0) {
		Genode::log("No packages installed.");
		return 0;
	}

	Genode::log("Installed packages:");
	Genode::log("NAME    VERSION");
	Genode::log("------  -------");
	result.with_optional_sub_node("packages",
		[&](Genode::Xml_node const &pkgs) {
			pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
				Genode::String<64> const name =
					p.attribute_value("name", Genode::String<64>());
				Genode::String<32> const ver =
					p.attribute_value("version", Genode::String<32>());
				Genode::log(name, "  ", ver);
			});
		});

	return 0;
}


int ListCommand::_render_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"list\",\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	char buf[512] { };
	Genode::size_t pos { 0 };
	if (pos + 1 < sizeof(buf)) buf[pos++] = '[';
	bool first { true };
	result.with_optional_sub_node("packages",
		[&](Genode::Xml_node const &pkgs) {
			pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
				if (!first && pos + 2 < sizeof(buf)) {
					buf[pos++] = ','; buf[pos++] = ' ';
				}
				first = false;
				if (pos + 2 < sizeof(buf)) buf[pos++] = '{';

				Genode::String<64> const name =
					p.attribute_value("name", Genode::String<64>());
				Genode::String<32> const ver =
					p.attribute_value("version", Genode::String<32>());

				if (pos + 12 < sizeof(buf)) {
					const char *k = "\"name\":\"";
					while (*k && pos + 1 < sizeof(buf)) buf[pos++] = *k++;
				}
				char const *s = name.string();
				while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
				if (pos + 16 < sizeof(buf)) {
					const char *k = "\",\"version\":\"";
					while (*k && pos + 1 < sizeof(buf)) buf[pos++] = *k++;
				}
				s = ver.string();
				while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
				if (pos + 2 < sizeof(buf)) { buf[pos++] = '"'; buf[pos++] = '}';
				}
			});
		});
	if (pos + 1 < sizeof(buf)) buf[pos++] = ']';
	buf[pos] = 0;

	Genode::log("{\"command\":\"list\",\"status\":\"success\",\"packages\":",
	            Genode::String<512>(buf), "}");
	return 0;
}


/* ===================== ConfigCommand ===================== */

int ConfigCommand::execute(Args const &args)
{
	char const *const key = args.positional.string();

	/* `vct config list` — no key/value. */
	if (Genode::strcmp(key, "list") == 0) {
		ReportRomClient client { _env, "config_request", "config_result" };
		if (!client.config_list()) {
			if (args.json)
				Genode::log("{\"command\":\"config\",\"op\":\"list\","
				            "\"status\":\"error\","
				            "\"error\":\"sponge_configd did not answer\"}");
			else
				Genode::warning("vct: sponge_configd did not answer for list");
			return 1;
		}
		return args.json ? _render_list_json(client.result_xml())
		                 : _render_list_human(client.result_xml());
	}

	if (Genode::strcmp(key, "") == 0) {
		Genode::warning("vct: config requires a key or 'list'");
		Genode::log("Usage: vct config <key> [<value>] | vct config list");
		return 1;
	}

	char const *const value = args.positional2.string();
	ReportRomClient client { _env, "config_request", "config_result" };

	/* No value -> get; value present -> set. */
	if (Genode::strcmp(value, "") == 0) {
		if (!client.config_get(key)) {
			if (args.json)
				Genode::log("{\"command\":\"config\",\"op\":\"get\",\"key\":\"", key,
				            "\",\"status\":\"error\","
				            "\"error\":\"sponge_configd did not answer\"}");
			else
				Genode::warning("vct: sponge_configd did not answer for '", key, "'");
			return 1;
		}
		return args.json ? _render_get_json(client.result_xml())
		                 : _render_get_human(client.result_xml());
	}

	if (!client.config_set(key, value)) {
		if (args.json)
			Genode::log("{\"command\":\"config\",\"op\":\"set\",\"key\":\"", key,
			            "\",\"value\":\"", value,
			            "\",\"status\":\"error\","
			            "\"error\":\"sponge_configd did not answer\"}");
		else
			Genode::warning("vct: sponge_configd did not answer for '", key, "'");
		return 1;
	}

	return args.json ? _render_set_json(client.result_xml())
	                 : _render_set_human(client.result_xml());
}


int ConfigCommand::_render_get_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("config: error: ", err);
		return 1;
	}

	Genode::String<128> const key =
		result.attribute_value("key", Genode::String<128>());
	Genode::String<128> const val =
		result.attribute_value("value", Genode::String<128>());
	Genode::log(key, " = ", val);
	return 0;
}


int ConfigCommand::_render_get_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const key =
		result.attribute_value("key", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"config\",\"op\":\"get\",\"key\":\"", key,
		            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	Genode::String<128> const val =
		result.attribute_value("value", Genode::String<128>());
	Genode::log("{\"command\":\"config\",\"op\":\"get\",\"key\":\"", key,
	            "\",\"value\":\"", val, "\",\"status\":\"success\"}");
	return 0;
}


int ConfigCommand::_render_set_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("config: error: ", err);
		return 1;
	}

	Genode::String<128> const key =
		result.attribute_value("key", Genode::String<128>());
	Genode::String<128> const val =
		result.attribute_value("value", Genode::String<128>());
	Genode::log("Set ", key, " = ", val);
	return 0;
}


int ConfigCommand::_render_set_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());
	Genode::String<128> const key =
		result.attribute_value("key", Genode::String<128>());
	Genode::String<128> const val =
		result.attribute_value("value", Genode::String<128>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"config\",\"op\":\"set\",\"key\":\"", key,
		            "\",\"value\":\"", val,
		            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	Genode::log("{\"command\":\"config\",\"op\":\"set\",\"key\":\"", key,
	            "\",\"value\":\"", val, "\",\"status\":\"success\"}");
	return 0;
}


int ConfigCommand::_render_list_human(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("config: error: ", err);
		return 1;
	}

	unsigned count { 0 };
	result.with_optional_sub_node("keys",
		[&](Genode::Xml_node const &keys) {
			keys.for_each_sub_node("key", [&](Genode::Xml_node const &) {
				++count;
			});
		});

	if (count == 0) {
		Genode::log("No configuration keys.");
		return 0;
	}

	Genode::log("Configuration keys:");
	Genode::log("KEY               VALUE");
	Genode::log("---------------   -----");
	result.with_optional_sub_node("keys",
		[&](Genode::Xml_node const &keys) {
			keys.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
				Genode::String<64> const name =
					k.attribute_value("name", Genode::String<64>());
				Genode::String<128> const val =
					k.attribute_value("value", Genode::String<128>());
				Genode::log(name, "   ", val);
			});
		});

	return 0;
}


int ConfigCommand::_render_list_json(Genode::Xml_node const &result)
{
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		Genode::log("{\"command\":\"config\",\"op\":\"list\","
		            "\"status\":\"error\",\"error\":\"", err, "\"}");
		return 1;
	}

	char buf[512] { };
	Genode::size_t pos { 0 };
	if (pos + 1 < sizeof(buf)) buf[pos++] = '[';
	bool first { true };
	result.with_optional_sub_node("keys",
		[&](Genode::Xml_node const &keys) {
			keys.for_each_sub_node("key", [&](Genode::Xml_node const &k) {
				if (!first && pos + 2 < sizeof(buf)) {
					buf[pos++] = ','; buf[pos++] = ' ';
				}
				first = false;
				if (pos + 2 < sizeof(buf)) buf[pos++] = '{';

				Genode::String<64> const name =
					k.attribute_value("name", Genode::String<64>());
				Genode::String<128> const val =
					k.attribute_value("value", Genode::String<128>());

				if (pos + 12 < sizeof(buf)) {
					const char *p = "\"name\":\"";
					while (*p && pos + 1 < sizeof(buf)) buf[pos++] = *p++;
				}
				char const *s = name.string();
				while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
				if (pos + 14 < sizeof(buf)) {
					const char *p = "\",\"value\":\"";
					while (*p && pos + 1 < sizeof(buf)) buf[pos++] = *p++;
				}
				s = val.string();
				while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
				if (pos + 2 < sizeof(buf)) { buf[pos++] = '"'; buf[pos++] = '}';
				}
			});
		});
	if (pos + 1 < sizeof(buf)) buf[pos++] = ']';
	buf[pos] = 0;

	Genode::log("{\"command\":\"config\",\"op\":\"list\","
	            "\"status\":\"success\",\"keys\":",
	            Genode::String<512>(buf), "}");
	return 0;
}


/* ===================== ThemeCommand ===================== */

void ThemeCommand::_print_help(Args const &args)
{
	if (args.lang == "ko") {
		Genode::log("사용법: vct theme apply <이름>");
		Genode::log("바탕화면 테마를 적용합니다.");
		Genode::log("");
		Genode::log("예: vct theme apply light");
		Genode::log("");
		Genode::log("테마는 sponge_themed 가 <이름>.theme ROM 에서 해석합니다.");
		Genode::log("알 수 없는 이름이면 이전 테마가 유지됩니다 (치명적 오류 아님).");
		Genode::log("같은 설정을 직접 제어하려면: vct config theme.active <이름>");
		return;
	}

	Genode::log("Usage: vct theme apply <name>");
	Genode::log("Apply a desktop theme.");
	Genode::log("");
	Genode::log("Example: vct theme apply light");
	Genode::log("");
	Genode::log("The theme is resolved by sponge_themed from a <name>.theme ROM.");
	Genode::log("An unknown name keeps the previous theme (never fatal).");
	Genode::log("For direct control of the same key: vct config theme.active <name>");
}


int ThemeCommand::execute(Args const &args)
{
	char const *const verb = args.positional.string();

	if (Genode::strcmp(verb, "--help") == 0 || Genode::strcmp(verb, "-h") == 0) {
		_print_help(args);
		return 0;
	}

	if (Genode::strcmp(verb, "apply") != 0) {
		Genode::warning("vct: unknown theme subcommand '", verb,
		                "' — expected 'apply'");
		_print_help(args);
		return 1;
	}

	char const *const name = args.positional2.string();
	if (Genode::strcmp(name, "") == 0) {
		Genode::warning("vct: theme apply requires a theme name");
		_print_help(args);
		return 1;
	}

	/*
	 * Reuse the config backend path: write theme.active=<name>. This is
	 * the automation-default path over the exact same Report/ROM channel
	 * as `vct config theme.active <name>`; configd validates the key,
	 * sponge_themed resolves it, sponge-de applies it live.
	 */
	ReportRomClient client { _env, "config_request", "config_result" };
	if (!client.config_set("theme.active", name)) {
		if (args.json)
			Genode::log("{\"command\":\"theme\",\"op\":\"apply\",\"name\":\"", name,
			            "\",\"status\":\"error\","
			            "\"error\":\"sponge_configd did not answer\"}");
		else
			Genode::warning("vct: sponge_configd did not answer for theme '", name, "'");
		return 1;
	}

	Genode::Xml_node const result = client.result_xml();
	Genode::String<32> const status =
		result.attribute_value("status", Genode::String<32>());

	if (status != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		if (args.json)
			Genode::log("{\"command\":\"theme\",\"op\":\"apply\",\"name\":\"", name,
			            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		else
			Genode::log("theme: error: ", err);
		return 1;
	}

	if (args.json)
		Genode::log("{\"command\":\"theme\",\"op\":\"apply\",\"name\":\"", name,
		            "\",\"status\":\"success\"}");
	else
		Genode::log("Applied theme: ", name);
	return 0;
}


/* ===================== LeitzentraleCommand ===================== */

void LeitzentraleCommand::_print_help(Args const &args)
{
	if (args.lang == "ko") {
		Genode::log("사용법: vct leitzentrale [off|status|diff|keep|revert]");
		Genode::log("Leitzentrale 전문가 제어 창의 표시 여부를 토글하고,");
		Genode::log("모델 파일시스템 변경을 감지/동기화/복원합니다.");
		Genode::log("");
		Genode::log("  vct leitzentrale          창 활성화 (나타남)");
		Genode::log("  vct leitzentrale off      창 비활성화 (숨김)");
		Genode::log("  vct leitzentrale status   현재 상태 조회");
		Genode::log("  vct leitzentrale diff     모델 변경(divergence) 조회");
		Genode::log("  vct leitzentrale keep     현재 모델을 새 기준선으로 채택");
		Genode::log("  vct leitzentrale revert   기준선으로 모델 복원");
		Genode::log("");
		Genode::log("sculpt_manager 하위 시스템은 항상 부팅되어 있으며,");
		Genode::log("이 명령은 표시 여부 플래그만 토글합니다.");
		Genode::log("변경 사항은 vct 종료 후에도 유지됩니다 (sponge_configd → 브리지).");
		Genode::log("merge(수동 병합)는 Phase 6에서는 미구현입니다 (문서 참조).");
		return;
	}

	Genode::log("Usage: vct leitzentrale [off|status|diff|keep|revert]");
	Genode::log("Toggle the Leitzentrale window visibility, and detect / sync /");
	Genode::log("restore model-fs changes made inside the Leitzentrale.");
	Genode::log("");
	Genode::log("  vct leitzentrale          Enable (raise the window)");
	Genode::log("  vct leitzentrale off      Disable (hide the window)");
	Genode::log("  vct leitzentrale status   Query the current state");
	Genode::log("  vct leitzentrale diff     Show model divergence (changed files)");
	Genode::log("  vct leitzentrale keep     Adopt the current model as the new baseline");
	Genode::log("  vct leitzentrale revert   Restore the model to the baseline");
	Genode::log("");
	Genode::log("The sculpt_manager subsystem is always booted; enable/off only");
	Genode::log("toggles visibility (persists via sponge_configd -> bridge).");
	Genode::log("diff/keep/revert go directly to lz_watch inside the subsystem.");
	Genode::log("merge (manual conflict resolution) is deferred past Phase 6.");
}


int LeitzentraleCommand::execute(Args const &args)
{
	char const *const verb = args.positional.string();

	if (Genode::strcmp(verb, "--help") == 0 || Genode::strcmp(verb, "-h") == 0) {
		_print_help(args);
		return 0;
	}

	bool const off = (Genode::strcmp(verb, "off") == 0 ||
	                  Genode::strcmp(verb, "disable") == 0 ||
	                  Genode::strcmp(verb, "false") == 0);
	bool const status_only = (Genode::strcmp(verb, "status") == 0);
	bool const diff_op     = (Genode::strcmp(verb, "diff") == 0);
	bool const keep_op     = (Genode::strcmp(verb, "keep") == 0);
	bool const revert_op   = (Genode::strcmp(verb, "revert") == 0);

	bool const sync_op = diff_op || keep_op || revert_op;

	if (!off && !status_only && !sync_op && Genode::strcmp(verb, "") != 0) {
		Genode::warning("vct: unknown leitzentrale argument '", verb,
		                "' — expected off/status/diff/keep/revert or nothing");
		_print_help(args);
		return 1;
	}

	if (sync_op)
		return _run_sync_op(verb, diff_op, keep_op, revert_op, args);

	ReportRomClient client { _env, "config_request", "config_result" };

	char const *const want = off ? "false" : "true";

	if (status_only) {
		if (!client.config_get("leitzentrale.enabled")) {
			if (args.json)
				Genode::log("{\"command\":\"leitzentrale\",\"op\":\"status\",\"status\":\"error\",\"error\":\"sponge_configd did not answer\"}");
			else
				Genode::warning("vct: sponge_configd did not answer");
			return 1;
		}
		Genode::Xml_node const result = client.result_xml();
		Genode::String<32> const value =
			result.attribute_value("value", Genode::String<32>());
		bool const active = (value == Genode::String<32>("true"));
		if (args.json)
			Genode::log("{\"command\":\"leitzentrale\",\"op\":\"status\",\"active\":",
			            active ? "true" : "false", "}");
		else
			Genode::log("Leitzentrale: ", active ? "active" : "inactive");
		return 0;
	}

	if (!client.config_set("leitzentrale.enabled", want)) {
		if (args.json)
			Genode::log("{\"command\":\"leitzentrale\",\"op\":\"set\",\"value\":\"", want,
			            "\",\"status\":\"error\",\"error\":\"sponge_configd did not answer\"}");
		else
			Genode::warning("vct: sponge_configd did not answer for leitzentrale.enabled");
		return 1;
	}

	Genode::Xml_node const result = client.result_xml();
	Genode::String<32> const rstatus =
		result.attribute_value("status", Genode::String<32>());

	if (rstatus != Genode::String<32>("ok")) {
		Genode::String<256> const err =
			result.attribute_value("error", Genode::String<256>());
		if (args.json)
			Genode::log("{\"command\":\"leitzentrale\",\"op\":\"set\",\"value\":\"", want,
			            "\",\"status\":\"error\",\"error\":\"", err, "\"}");
		else
			Genode::log("leitzentrale: error: ", err);
		return 1;
	}

	/*
	 * Audit trail (docs/07 §4.3): log who/what so the enable is
	 * attributable. vct is short-lived so this log line is the record.
	 */
	Genode::log("vct: leitzentrale ", off ? "disabled" : "enabled",
	            " (leitzentrale.enabled=", want, ")");

	if (args.json)
		Genode::log("{\"command\":\"leitzentrale\",\"op\":\"set\",\"value\":\"", want,
		            "\",\"status\":\"success\"}");
	else
		Genode::log(off ? "Leitzentrale hidden." : "Leitzentrale window enabled.");
	return 0;
}

/*
 * Phase 6c sync ops (diff/keep/revert). These talk directly to lz_watch
 * (inside the subsystem) over two channels bridged by the top-level
 * report_rom: lz_model (ROM, read) for diff, and lz_watch_request /
 * lz_watch_result (Report write / ROM read) for keep/revert. configd
 * independently mirrors leitzentrale.diverged from lz_model for other
 * watchers, so vct diff and configd agree.
 */
int LeitzentraleCommand::_run_sync_op(char const *verb, bool diff,
                                       bool keep, bool /*revert*/,
                                       Args const &args)
{
	if (diff) {
		Genode::Attached_rom_dataspace model { _env, "lz_model" };

		/* lz_watch emits periodically; poll a little for the first sample. */
		Timer::Connection timer { _env };
		for (unsigned i = 0; i < 30; ++i) {
			model.update();
			if (model.valid()) break;
			timer.msleep(100);
		}
		if (!model.valid()) {
			if (args.json)
				Genode::log("{\"command\":\"leitzentrale\",\"op\":\"diff\",\"status\":\"error\",\"error\":\"lz_model unavailable\"}");
			else
				Genode::log("leitzentrale diff: lz_watch not reachable");
			return 1;
		}

		bool diverged = (model.xml().attribute_value("diverged",
		                       Genode::String<8>()) == Genode::String<8>("true"));

		if (args.json) {
			Genode::log("{\"command\":\"leitzentrale\",\"op\":\"diff\",\"diverged\":",
			            diverged ? "true" : "false", "}");
		} else {
			Genode::log(diverged ? "Leitzentrale model: DIVERGED"
			                     : "Leitzentrale model: clean (matches baseline)");
			model.xml().for_each_sub_node("file", [&] (Genode::Xml_node const &f) {
				bool const ch = (f.attribute_value("changed", Genode::String<8>()) ==
				                 Genode::String<8>("true"));
				Genode::log("  ", f.attribute_value("name", Genode::String<32>()),
				            ch ? "  CHANGED" : "  clean");
			});
		}
		return 0;
	}

	/* keep / revert: write the request, poll for the result. */
	char const *op = keep ? "snapshot" : "revert";

	Genode::Expanding_reporter req { _env, "lz_watch_request", "lz_watch_request" };
	req.generate_xml([&] (Genode::Xml_generator &g) { g.attribute("op", op); });

	Genode::Attached_rom_dataspace res { _env, "lz_watch_result" };
	Timer::Connection timer { _env };
	bool ok = false;
	for (unsigned i = 0; i < 30; ++i) {
		timer.msleep(100);
		res.update();
		if (!res.valid()) continue;
		Genode::String<16> const rop  = res.xml().attribute_value("op",  Genode::String<16>());
		if (rop == Genode::String<16>(op)) {
			ok = (res.xml().attribute_value("status", Genode::String<16>()) ==
			      Genode::String<16>("ok"));
			break;
		}
	}

	if (args.json) {
		Genode::log("{\"command\":\"leitzentrale\",\"op\":\"", verb,
		            "\",\"status\":\"", ok ? "ok" : "error\"}");
	} else {
		Genode::log("leitzentrale ", verb, ": ",
		            ok ? "done" : "lz_watch did not answer");
	}
	return ok ? 0 : 1;
}


/* ===================== PowerCommand (Phase 7: shutdown / reboot) ===================== */

/*
 * Bounded wait (ms) for the power side effect after publishing the
 * `system` report. On QEMU, acpica's AcpiEnterSleepState(5) /
 * AcpiReset() take effect within milliseconds. The bound is generous
 * enough to cover ACPI evaluation latency on real hardware too, and
 * short enough that the failure-channel scenario fails loudly by
 * timeout (the `hung_or_long_commands` adversarial class) rather than
 * hanging the run.
 */
namespace { constexpr unsigned POWER_SIDE_EFFECT_BOUND_MS = 4000; }


void PowerCommand::_print_help(Args const &args, char const *verb)
{
	bool const ko = (args.lang == "ko");

	if (Genode::strcmp(verb, "reboot") == 0) {
		if (ko) {
			Genode::log("사용법: vct reboot [--json] [--help] [--lang ko]");
			Genode::log("시스템을 재부팅합니다 (ACPI reset).");
			Genode::log("");
		} else {
			Genode::log("Usage: vct reboot [--json] [--help] [--lang ko]");
			Genode::log("Reboot the system (ACPI reset).");
			Genode::log("");
		}
	} else {
		if (ko) {
			Genode::log("사용법: vct shutdown [--json] [--help] [--lang ko]");
			Genode::log("시스템을 종료합니다 (ACPI S5 poweroff).");
			Genode::log("");
		} else {
			Genode::log("Usage: vct shutdown [--json] [--help] [--lang ko]");
			Genode::log("Shut down the system (ACPI S5 poweroff).");
			Genode::log("");
		}
	}

	if (ko) {
		Genode::log("동작:");
		Genode::log("  `system` 리포트를 발행하면 acpica 가 ACPI 전원 제어를 수행합니다.");
		Genode::log("  시스템 소비자(acpica)가 없으면 명확한 오류로 끝나고 게스트는 계속 실행됩니다.");
		Genode::log("");
		Genode::log("수동 탈출구 (QEMU 모니터):");
		Genode::log("  systemctl_powerdown 이 불가능할 때 - Ctrl-A x 또는 -qmp 로 QEMU 모니터에");
		Genode::log("  접근한 뒤 system_powerdown (종료) / system_reset (재부팅) 을 직접 실행.");
	} else {
		Genode::log("Behavior:");
		Genode::log("  Publishes the `system` report; acpica performs the ACPI power action.");
		Genode::log("  If the System consumer (acpica) is unavailable, the command prints a");
		Genode::log("  clear `service unavailable` error, exits non-zero, and the guest keeps running.");
		Genode::log("");
		Genode::log("Manual escape hatch (QEMU monitor):");
		Genode::log("  If ACPI poweroff/reset is unreachable, enter the QEMU monitor with");
		Genode::log("  Ctrl-A x (or connect via -qmp) and run system_powerdown (shutdown) /");
		Genode::log("  system_reset (reboot) directly. Equivalent host-side flags:");
		Genode::log("  qemu-system-x86_64 ... -action panic=shutdown -action reboot=shutdown.");
	}
}


int PowerCommand::execute(Args const &args)
{
	char const *const verb = args.subcommand.string();
	bool const reboot = (Genode::strcmp(verb, "reboot") == 0);
	char const *const state = reboot ? "reset" : "poweroff";

	char const *const pos = args.positional.string();
	if (Genode::strcmp(pos, "--help") == 0 || Genode::strcmp(pos, "-h") == 0) {
		_print_help(args, verb);
		return 0;
	}

	/* Audit line BEFORE acting (docs/06-vct.md §4.7, control philosophy). */
	Genode::log("vct: ", verb, ": requesting ", reboot ? "reset" : "poweroff");

	/*
	 * Publish the `system` report (label "system"). report_rom relays it
	 * as the `system` ROM consumed by acpica. The reporter construction
	 * opens a Report session; the scenario MUST route vct's "system"
	 * Report to report_rom (the failure-channel scenario routes it to a
	 * read-back ROM but starts no acpica — proving the unavailable path).
	 */
	Genode::Expanding_reporter system_report { _env, "system", "system" };
	system_report.generate_xml([&] (Genode::Xml_generator &g) {
		g.attribute("state", state);
	});

	if (_notifier)
		_notifier->post(verb, state, "info", 5000);

	/*
	 * Emit --json BEFORE the bounded wait: on shutdown, acpica's
	 * AcpiEnterSleepState(5) pulls QEMU out from under us and vct may
	 * not get another scheduling slot to log.
	 */
	if (args.json) {
		Genode::log("{\"command\":\"", verb, "\",\"state\":\"", state,
		            "\",\"status\":\"requested\"}");
	}

	/*
	 * Bounded wait for the side effect. If the guest is still alive
	 * after the bound, the System consumer (acpica) is absent — fail
	 * loudly with a clear, actionable error (the control-philosophy
	 * escape hatch points the user at the QEMU monitor).
	 */
	Timer::Connection timer { _env };
	for (unsigned ms = 0; ms < POWER_SIDE_EFFECT_BOUND_MS; ms += 200)
		timer.msleep(200);

	if (args.json) {
		Genode::log("{\"command\":\"", verb, "\",\"state\":\"", state,
		            "\",\"status\":\"error\","
		            "\"error\":\"System service (acpica) unavailable - guest did not ",
		            reboot ? "reset" : "power off",
		            " within ", POWER_SIDE_EFFECT_BOUND_MS, "ms\"}");
	} else {
		Genode::log("vct: ", verb, ": System service (acpica) unavailable - ",
		            "guest did not ", reboot ? "reset" : "power off", " within ",
		            POWER_SIDE_EFFECT_BOUND_MS, "ms");
		Genode::log("vct: ", verb, ": escape hatch - QEMU monitor: ",
		            reboot ? "system_reset" : "system_powerdown",
		            " (Ctrl-A x, or -qmp; see docs/13-installation.md).");
	}
	return 1;
}


/* ===================== SearchCommand (Phase 7: search) ===================== */

void SearchCommand::_print_help(Args const &args)
{
	if (args.lang == "ko") {
		Genode::log("사용법: vct search <검색어> [--json] [--help] [--lang ko]");
		Genode::log("이미지 내 패키지 저장소에서 이름/설명이 일치하는 패키지를 찾습니다.");
		Genode::log("");
		Genode::log("결과가 없으면 'No matches.' 를 출력하고 종료 코드 0 으로 끝납니다 (오류 아님).");
		Genode::log("저장소는 빌드 시점에 고정되므로 네트워크 가져오기는 수행하지 않습니다.");
		return;
	}

	Genode::log("Usage: vct search <term> [--json] [--help] [--lang ko]");
	Genode::log("Search the on-image package repository by name or description.");
	Genode::log("");
	Genode::log("An empty result prints 'No matches.' and exits 0 (never an error).");
	Genode::log("No network fetching - the repository is fixed at image build time.");
}


namespace {

/*
 * Read the `pkg_index.xml` ROM and call `fn(name)` for every staged
 * package name. Returns false if the index ROM is unavailable (the
 * scenario forgot to stage it). Used by both SearchCommand and
 * UpdateCommand so the two commands never disagree about which
 * packages exist in the repo.
 */
template <typename FN>
bool for_each_repo_pkg(Genode::Env &env, FN const &fn)
{
	Genode::Attached_rom_dataspace index { env, "pkg_index.xml" };
	index.update();
	if (!index.valid())
		return false;

	try {
		Genode::Xml_node const root(index.local_addr<char>(), index.size());
		if (!root.has_type("packages"))
			return false;
		root.for_each_sub_node("pkg", [&](Genode::Xml_node const &p) {
			fn(p.attribute_value("name", Genode::String<64>()));
		});
		return true;
	} catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}
}


/*
 * Open the `pkg_<name>.xml` ROM and extract the <name>, <version>,
 * <description> triple. Returns false if the ROM is missing or
 * malformed. Mirrors sponge_pkgd's _load_package parse, minus the
 * dependency/session walk (search/update need only the identity fields).
 */
struct Repo_pkg { Genode::String<64> name; Genode::String<32> version; Genode::String<192> description; };

bool load_repo_pkg(Genode::Env &env, char const *name, Repo_pkg &out)
{
	Genode::String<96> const label("pkg_", name, ".xml");
	try {
		Genode::Attached_rom_dataspace rom { env, label.string() };
		rom.update();
		if (!rom.valid())
			return false;
		Genode::Xml_node const root(rom.local_addr<char>(), rom.size());
		if (!root.has_type("package"))
			return false;
		bool ok { true };
		root.with_sub_node("name", [&](Genode::Xml_node const &n) {
			out.name = n.decoded_content<Genode::String<64>>(); },
			[&] { ok = false; });
		root.with_sub_node("version", [&](Genode::Xml_node const &n) {
			out.version = n.decoded_content<Genode::String<32>>(); },
			[&] { ok = false; });
		root.with_sub_node("description", [&](Genode::Xml_node const &n) {
			out.description = n.decoded_content<Genode::String<192>>(); },
			[&] { ok = false; });
		return ok;
	}
	catch (Genode::Rom_connection::Rom_connection_failed) { return false; }
	catch (Genode::Xml_node::Invalid_syntax)             { return false; }
}


bool ci_contains(Genode::String<192> const &haystack, char const *needle)
{
	char const *h = haystack.string();
	Genode::size_t const hlen = Genode::strlen(h);
	Genode::size_t const nlen = Genode::strlen(needle);
	if (nlen == 0 || nlen > hlen)
		return nlen == 0;
	for (Genode::size_t i = 0; i + nlen <= hlen; ++i) {
		bool match { true };
		for (Genode::size_t k = 0; k < nlen; ++k) {
			char a = h[i + k];
			char b = needle[k];
			if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
			if (a != b) { match = false; break; }
		}
		if (match) return true;
	}
	return false;
}

}  /* namespace */


int SearchCommand::execute(Args const &args)
{
	char const *const term = args.positional.string();

	if (Genode::strcmp(term, "--help") == 0 || Genode::strcmp(term, "-h") == 0) {
		_print_help(args);
		return 0;
	}
	if (Genode::strcmp(term, "") == 0) {
		Genode::warning("vct: search requires a term");
		_print_help(args);
		return 1;
	}

	unsigned count { 0 };
	char json_buf[768] { };
	Genode::size_t jpos { 0 };
	if (args.json) { json_buf[jpos++] = '['; }

	bool index_ok = for_each_repo_pkg(_env, [&](Genode::String<64> const &name) {
		Repo_pkg p { };
		if (!load_repo_pkg(_env, name.string(), p))
			return;
		if (!ci_contains(Genode::String<192>(p.name), term) &&
		    !ci_contains(p.description, term))
			return;

		++count;
		if (args.json) {
			if (count > 1 && jpos + 2 < sizeof(json_buf)) {
				json_buf[jpos++] = ','; json_buf[jpos++] = ' ';
			}
			if (jpos + 2 < sizeof(json_buf)) json_buf[jpos++] = '{';
			const char *k = "\"name\":\"";
			while (*k && jpos + 1 < sizeof(json_buf)) json_buf[jpos++] = *k++;
			for (char const *s = p.name.string(); *s && jpos + 1 < sizeof(json_buf); ++s) json_buf[jpos++] = *s;
			k = "\",\"version\":\"";
			while (*k && jpos + 1 < sizeof(json_buf)) json_buf[jpos++] = *k++;
			for (char const *s = p.version.string(); *s && jpos + 1 < sizeof(json_buf); ++s) json_buf[jpos++] = *s;
			k = "\",\"description\":\"";
			while (*k && jpos + 1 < sizeof(json_buf)) json_buf[jpos++] = *k++;
			for (char const *s = p.description.string(); *s && jpos + 1 < sizeof(json_buf); ++s) json_buf[jpos++] = *s;
			if (jpos + 2 < sizeof(json_buf)) { json_buf[jpos++] = '"'; json_buf[jpos++] = '}'; }
		} else {
			Genode::log(p.name, "  ", p.version, "  ", p.description);
		}
	});

	if (!index_ok) {
		if (args.json)
			Genode::log("{\"command\":\"search\",\"term\":\"", term,
			            "\",\"status\":\"error\","
			            "\"error\":\"pkg_index.xml unavailable\"}");
		else
			Genode::log("search: pkg_index.xml unavailable (no repository staged)");
		return 1;
	}

	if (count == 0) {
		if (args.json) {
			Genode::log("{\"command\":\"search\",\"term\":\"", term,
			            "\",\"status\":\"success\",\"matches\":[]}");
		} else {
			Genode::log("No matches.");
		}
		return 0;
	}

	if (args.json) {
		if (jpos + 1 < sizeof(json_buf)) json_buf[jpos++] = ']';
		json_buf[jpos] = 0;
		Genode::log("{\"command\":\"search\",\"term\":\"", term,
		            "\",\"status\":\"success\",\"matches\":",
		            Genode::String<768>(json_buf), "}");
	}
	return 0;
}


/* ===================== UpdateCommand (Phase 7: update) ===================== */

void UpdateCommand::_print_help(Args const &args)
{
	if (args.lang == "ko") {
		Genode::log("사용법: vct update [<패키지>] [--json] [--help] [--lang ko]");
		Genode::log("설치된 패키지를 이미지 내 저장소 메타데이터와 비교해 버전 차이를 보고합니다.");
		Genode::log("");
		Genode::log("  vct update            모든 설치된 루트 패키지를 검사");
		Genode::log("  vct update <패키지>   해당 패키지만 검사");
		Genode::log("");
		Genode::log("가져오기나 자동 갱신은 수행하지 않습니다 (docs/12 §9.2.2).");
		Genode::log("설치된 패키지 목록은 `vct list` 로, 메타데이터는 pkg/<이름>/metadata.xml 로 직접 확인.");
		return;
	}

	Genode::log("Usage: vct update [<package>] [--json] [--help] [--lang ko]");
	Genode::log("Report version deltas between installed packages and the on-image repo.");
	Genode::log("");
	Genode::log("  vct update            Check every installed root package");
	Genode::log("  vct update <package>  Check just that package");
	Genode::log("");
	Genode::log("No fetching, no auto-upgrade (docs/12 §9.2.2). The repo is fixed at image");
	Genode::log("build time; a newer repo version becomes effective only after a rebuild.");
	Genode::log("Inspect the installed set with `vct list`; read a package's metadata");
	Genode::log("directly at pkg/<name>/metadata.xml (docs/12 §9.3, no hidden state).");
}


int UpdateCommand::execute(Args const &args)
{
	char const *const filter = args.positional.string();

	if (Genode::strcmp(filter, "--help") == 0 || Genode::strcmp(filter, "-h") == 0) {
		_print_help(args);
		return 0;
	}

	/*
	 * The installed set + installed versions come from sponge_pkgd's
	 * `installed` broadcast ROM (the same ROM the launcher reads). vct
	 * opens it directly — minimum privilege, no pkgd request round-trip.
	 */
	Genode::Attached_rom_dataspace installed { _env, "installed" };
	Timer::Connection timer { _env };
	for (unsigned i = 0; i < 20; ++i) {
		installed.update();
		if (installed.valid()) break;
		timer.msleep(100);
	}
	if (!installed.valid()) {
		if (args.json)
			Genode::log("{\"command\":\"update\",\"status\":\"error\","
			            "\"error\":\"installed broadcast unavailable (sponge_pkgd absent)\"}");
		else
			Genode::log("update: installed broadcast unavailable (sponge_pkgd absent)");
		return 1;
	}

	unsigned checked   { 0 };
	unsigned deltas    { 0 };
	bool   filter_miss { false };

	char json_buf[1024] { };
	Genode::size_t jpos { 0 };
	if (args.json) { json_buf[jpos++] = '['; }

	try {
		Genode::Xml_node const root(installed.local_addr<char>(), installed.size());

		root.with_optional_sub_node("packages",
			[&](Genode::Xml_node const &pkgs) {
				pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
					Genode::String<64> const iname =
						p.attribute_value("name", Genode::String<64>());

					if (Genode::strcmp(filter, "") != 0 &&
					    iname != Genode::String<64>(filter))
						return;

					++checked;

					Genode::String<32> const installed_ver =
						p.attribute_value("version", Genode::String<32>());

					Repo_pkg repo { };
					if (!load_repo_pkg(_env, iname.string(), repo)) {
						if (args.json) {
							if (checked > 1 && jpos + 2 < sizeof(json_buf)) {
								json_buf[jpos++] = ','; json_buf[jpos++] = ' ';
							}
							Genode::String<256> row(
								"{\"name\":\"", iname,
								"\",\"status\":\"error\","
								"\"error\":\"repo metadata missing\"}");
							for (char const *s = row.string(); *s && jpos + 1 < sizeof(json_buf); ++s)
								json_buf[jpos++] = *s;
						} else {
							Genode::log("update: error: ", iname,
							            " - repo metadata missing");
						}
						return;
					}

					bool const current =
						(installed_ver == repo.version);

					if (args.json) {
						if (checked > 1 && jpos + 2 < sizeof(json_buf)) {
							json_buf[jpos++] = ','; json_buf[jpos++] = ' ';
						}
						Genode::String<384> row(
							"{\"name\":\"", iname,
							"\",\"installed_version\":\"", installed_ver,
							"\",\"repo_version\":\"", repo.version,
							"\",\"status\":\"", current ? "current" : "delta\"}");
						for (char const *s = row.string(); *s && jpos + 1 < sizeof(json_buf); ++s)
							json_buf[jpos++] = *s;
					} else if (current) {
						Genode::log("already current: ", iname, " ", installed_ver);
					} else {
						Genode::log("repo carries ", repo.version, ", installed ",
						            installed_ver, " — effective after next image build");
					}

					if (!current) ++deltas;
				});
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		if (args.json)
			Genode::log("{\"command\":\"update\",\"status\":\"error\","
			            "\"error\":\"installed broadcast malformed\"}");
		else
			Genode::log("update: installed broadcast malformed");
		return 1;
	}

	if (Genode::strcmp(filter, "") != 0 && checked == 0)
		filter_miss = true;

	if (filter_miss) {
		if (args.json)
			Genode::log("{\"command\":\"update\",\"package\":\"", filter,
			            "\",\"status\":\"error\","
			            "\"error\":\"not installed (use vct list to inspect)\"}");
		else
			Genode::log("update: error: ", filter,
			            " is not installed (use vct list to inspect)");
		return 1;
	}

	if (args.json) {
		if (jpos + 1 < sizeof(json_buf)) json_buf[jpos++] = ']';
		json_buf[jpos] = 0;
		Genode::log("{\"command\":\"update\",\"status\":\"success\","
		            "\"checked\":", checked,
		            ",\"deltas\":", deltas,
		            ",\"results\":", Genode::String<1024>(json_buf), "}");
	}
	return 0;
}
