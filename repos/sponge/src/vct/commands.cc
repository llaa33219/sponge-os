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
#include "pkg_client.h"

#include <base/log.h>

#include <sponge/platform.h>
#include <sponge/version.h>

#include <util/string.h>
#include <util/xml_node.h>

using namespace Sponge;
using namespace Sponge::Vct;


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
		Genode::log("  vct install <pkg>         패키지 설치");
		Genode::log("  vct remove <pkg>          패키지 제거");
		Genode::log("  vct config <key> [val]    설정 조회/변경  (계획됨)");
		Genode::log("  vct leitzentrale          전문가 제어 창 열기  (계획됨)");
		Genode::log("");
		Genode::log("공통 옵션: --explain, --manual, --json, --verbose, --lang ko");
	} else {
		Genode::log("Available subcommands (some are planned, see docs/06-vct.md):");
		Genode::log("  vct status                Show system status");
		Genode::log("  vct version               Print vct / Sponge OS version");
		Genode::log("  vct help                  Show this help");
		Genode::log("  vct component list        List the running components");
		Genode::log("  vct install <pkg> --explain  Preview the install plan");
		Genode::log("  vct install <pkg>         Install a package");
		Genode::log("  vct remove <pkg>          Remove an installed package");
		Genode::log("  vct config <key> [val]    Get or set a config option (planned)");
		Genode::log("  vct leitzentrale          Open the Leitzentrale expert window (planned)");
		Genode::log("");
		Genode::log("Common flags: --explain, --manual, --json, --verbose, --lang ko");
	}

	Genode::warning("not implemented: full subcommand set (Phase 4+). status, version, help, component list, 'install <pkg> --explain', 'install <pkg>', and 'remove <pkg>' work today.");
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
		Genode::log("Usage: vct install <package> [--explain] [--json]");
		return 1;
	}

	/* Phase 4c: --manual is not implemented yet. */
	if (args.manual) {
		Genode::warning("not implemented: install --manual (Phase 4c)");
		return 1;
	}

	PkgClient client { _env };

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


/* ===================== RemoveCommand ===================== */

int RemoveCommand::execute(Args const &args)
{
	char const *const pkg = args.positional.string();

	if (Genode::strcmp(pkg, "") == 0) {
		Genode::warning("vct: remove requires a package name");
		Genode::log("Usage: vct remove <package> [--json]");
		return 1;
	}

	PkgClient client { _env };
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
