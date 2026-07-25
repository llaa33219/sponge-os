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

#include <base/log.h>

#include <sponge/backend_client.h>
#include <sponge/platform.h>
#include <sponge/version.h>

#include <util/string.h>
#include <util/xml_node.h>

using namespace Sponge;
using namespace Sponge::Vct;
using Sponge::Backend::ReportRomClient;


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
		Genode::log("  vct version               vct / Sponge OS 버전 출력");
		Genode::log("  vct help                  이 도움말 출력");
		Genode::log("  vct component list        실행 중인 컴포넌트 목록 보기");
		Genode::log("  vct install <pkg> --explain  패키지 설치 계획 미리보기");
		Genode::log("  vct install <pkg>         패키지 설치 (--manual: 단계별 출력)");
		Genode::log("  vct remove <pkg>          패키지 제거");
		Genode::log("  vct list                  설치된 패키지 목록");
		Genode::log("  vct config <key>          설정 값 조회");
		Genode::log("  vct config <key> <value>  설정 값 변경");
		Genode::log("  vct config list           전체 설정 키 목록");
		Genode::log("  vct theme apply <name>    바탕화면 테마 적용");
		Genode::log("  vct leitzentrale          전문가 제어 창 열기 (끄기: vct leitzentrale off)");
		Genode::log("");
		Genode::log("공통 옵션: --explain, --manual, --json, --verbose, --lang ko");
	} else {
		Genode::log("Available subcommands (some are planned, see docs/06-vct.md):");
		Genode::log("  vct status                Show system status");
		Genode::log("  vct version               Print vct / Sponge OS version");
		Genode::log("  vct help                  Show this help");
		Genode::log("  vct component list        List the running components");
		Genode::log("  vct install <pkg> --explain  Preview the install plan");
		Genode::log("  vct install <pkg>         Install a package (--manual: step-by-step)");
		Genode::log("  vct remove <pkg>          Remove an installed package");
		Genode::log("  vct list                  List installed packages");
		Genode::log("  vct config <key>          Get a configuration value");
		Genode::log("  vct config <key> <value>  Set a configuration value");
		Genode::log("  vct config list           List all configuration keys");
		Genode::log("  vct theme apply <name>    Apply a desktop theme");
		Genode::log("  vct leitzentrale          Open the Leitzentrale expert window (off: vct leitzentrale off)");
		Genode::log("");
		Genode::log("Common flags: --explain, --manual, --json, --verbose, --lang ko");
	}

	Genode::warning("not implemented: full subcommand set (Phase 4+). status, version, help, component list, install (--explain/--manual/plain), remove, list, config, and theme apply work today.");
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
	InitStateReader state { _env };

	if (args.json) {
		if (state.available()) {
			Genode::log("{\"command\":\"status\",\"ram_quota\":\"",
			            Genode::Number_of_bytes(state.total_ram_quota()),
			            "\",\"ram_used\":\"",
			            Genode::Number_of_bytes(state.total_ram_used()),
			            "\",\"ram_avail\":\"",
			            Genode::Number_of_bytes(state.total_ram_avail()),
			            "\",\"components\":",
			            state.child_count(),
			            ",\"version\":\"",
			            Sponge::VERSION_STRING,
			            "\"}");
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
		Genode::warning("not implemented: live component / resource statistics (Phase 4).");
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
		Genode::log("사용법: vct leitzentrale [off|status]");
		Genode::log("Leitzentrale 전문가 제어 창의 표시 여부를 토글합니다.");
		Genode::log("");
		Genode::log("  vct leitzentrale          창 활성화 (나타남)");
		Genode::log("  vct leitzentrale off      창 비활성화 (숨김)");
		Genode::log("  vct leitzentrale status   현재 상태 조회");
		Genode::log("");
		Genode::log("sculpt_manager 하위 시스템은 항상 부팅되어 있으며,");
		Genode::log("이 명령은 표시 여부 플래그만 토글합니다.");
		Genode::log("변경 사항은 vct 종료 후에도 유지됩니다 (sponge_configd → 브리지).");
		Genode::log("같은 설정을 직접 제어하려면: vct config leitzentrale.enabled <true|false>");
		return;
	}

	Genode::log("Usage: vct leitzentrale [off|status]");
	Genode::log("Toggle the visibility of the Leitzentrale expert window.");
	Genode::log("");
	Genode::log("  vct leitzentrale          Enable (raise the window)");
	Genode::log("  vct leitzentrale off      Disable (hide the window)");
	Genode::log("  vct leitzentrale status   Query the current state");
	Genode::log("");
	Genode::log("The sculpt_manager subsystem is always booted; this command only");
	Genode::log("toggles the visibility flag. The change persists after vct exits");
	Genode::log("(sponge_configd -> bridge -> gui_fader).");
	Genode::log("For direct control of the same key: vct config leitzentrale.enabled <true|false>");
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

	if (!off && !status_only && Genode::strcmp(verb, "") != 0) {
		Genode::warning("vct: unknown leitzentrale argument '", verb,
		                "' — expected 'off', 'status', or nothing");
		_print_help(args);
		return 1;
	}

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
