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

#include <sponge/platform.h>
#include <sponge/version.h>

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
		Genode::log("  vct install <pkg>         패키지 설치  (계획됨)");
		Genode::log("  vct remove <pkg>          패키지 제거  (계획됨)");
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
		Genode::log("  vct install <pkg>         Install a package  (planned)");
		Genode::log("  vct remove <pkg>          Remove a package   (planned)");
		Genode::log("  vct config <key> [val]    Get or set a config option (planned)");
		Genode::log("  vct leitzentrale          Open the Leitzentrale expert window (planned)");
		Genode::log("");
		Genode::log("Common flags: --explain, --manual, --json, --verbose, --lang ko");
	}

	Genode::warning("not implemented: full subcommand set (Phase 4+). Only status, version, help, and component list work today.");
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
