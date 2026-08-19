/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary */

#include "commands.h"

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <sponge/backend_client.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_node.h>

using namespace Sponge;
using namespace Sponge::Vct;
using Sponge::Backend::ReportRomClient;

namespace {

struct Optional_rom
{
	Genode::Constructible<Genode::Attached_rom_dataspace> rom { };

	Optional_rom(Genode::Env &env, char const *label)
	{
		try {
			rom.construct(env, label);
			rom->update();
		}
		catch (Genode::Rom_connection::Rom_connection_failed) { rom.destruct(); }
		catch (Genode::Service_denied) { rom.destruct(); }
		catch (Genode::Out_of_ram) { rom.destruct(); }
		catch (Genode::Out_of_caps) { rom.destruct(); }
	}

	bool valid() const { return rom.constructed() && rom->valid(); }
};


bool json_value(Optional_rom const &manifest, char const *name,
                char *out, Genode::size_t out_size, bool quoted)
{
	if (!manifest.valid()) return false;
	char const *data = manifest.rom->local_addr<char const>();
	Genode::size_t const size = manifest.rom->size();
	Genode::String<96> const token("\"", name, "\"");
	Genode::size_t const token_len = Genode::strlen(token.string());

	for (Genode::size_t i = 0; i + token_len < size; ++i) {
		if (Genode::strcmp(data + i, token.string(), token_len) != 0) continue;
		Genode::size_t p = i + token_len;
		while (p < size && (data[p] == ' ' || data[p] == '\t' ||
		       data[p] == '\r' || data[p] == '\n')) ++p;
		if (p >= size || data[p++] != ':') return false;
		while (p < size && (data[p] == ' ' || data[p] == '\t' ||
		       data[p] == '\r' || data[p] == '\n')) ++p;
		if (quoted && (p >= size || data[p++] != '"')) return false;
		Genode::size_t n { 0 };
		while (p < size && n + 1 < out_size) {
			char const c = data[p++];
			if ((quoted && c == '"') || (!quoted && (c < '0' || c > '9'))) break;
			out[n++] = c;
		}
		out[n] = 0;
		return n > 0;
	}
	return false;
}


bool config_value(Optional_rom const &config, char const *name,
                  Genode::String<128> &value)
{
	if (!config.valid()) return false;
	try {
		bool found { false };
		config.rom->xml().for_each_sub_node("key", [&] (Genode::Xml_node const &key) {
			if (!found && key.attribute_value("name", Genode::String<64>()) ==
			              Genode::String<64>(name)) {
				value = key.attribute_value("value", Genode::String<128>());
				found = true;
			}
		});
		return found;
	}
	catch (Genode::Xml_node::Invalid_syntax) { return false; }
}


template <typename FN>
void for_each_default(Optional_rom const &defaults, FN const &fn)
{
	if (!defaults.valid()) return;
	char const *data = defaults.rom->local_addr<char const>();
	Genode::size_t const size = defaults.rom->size();
	Genode::size_t line_start { 0 };

	while (line_start < size && data[line_start] != 0) {
		Genode::size_t line_end = line_start;
		while (line_end < size && data[line_end] != '\n') ++line_end;
		Genode::size_t first = line_start;
		Genode::size_t last = line_end;
		while (first < last && (data[first] == ' ' || data[first] == '\t' ||
		       data[first] == '\r')) ++first;
		while (last > first && (data[last - 1] == ' ' || data[last - 1] == '\t' ||
		       data[last - 1] == '\r')) --last;

		if (first < last && data[first] != '#') {
			Genode::size_t eq = first;
			while (eq < last && data[eq] != '=') ++eq;
			if (eq < last) {
				Genode::size_t key_last = eq;
				while (key_last > first && (data[key_last - 1] == ' ' ||
				       data[key_last - 1] == '\t')) --key_last;
				Genode::size_t value_first = eq + 1;
				while (value_first < last && (data[value_first] == ' ' ||
				       data[value_first] == '\t')) ++value_first;
				char key[128] { };
				char value[128] { };
				Genode::size_t const key_len = key_last - first;
				Genode::size_t const value_len = last - value_first;
				if (key_len > 0 && key_len < sizeof(key) && value_len < sizeof(value)) {
					for (Genode::size_t i = 0; i < key_len; ++i) key[i] = data[first + i];
					for (Genode::size_t i = 0; i < value_len; ++i) value[i] = data[value_first + i];
					fn(key, value);
				}
			}
		}
		line_start = line_end + 1;
	}
}


bool profile_selected(Args const &args, char const *profile)
{
	return args.profile == Genode::String<32>() ||
	       args.profile == Genode::String<32>(profile);
}

}  /* namespace */


void BakeCommand::_print_help(Args const &args)
{
	if (args.lang == "ko") {
		Genode::log("사용법: vct bake <list|show|reset> [--profile <이름>] [--json] [--manual]");
		Genode::log("이 미디어에 구운 기본 설정을 확인하거나 다시 적용합니다.");
		Genode::log("");
		Genode::log("  list   이 미디어에서 발견한 프로필 표시");
		Genode::log("  show   구운 기본값과 현재 설정 비교");
		Genode::log("  reset  구운 키만 기본값으로 복원");
		Genode::log("reset은 구운 목록에 없는 사용자 키를 그대로 둡니다.");
		return;
	}

	Genode::log("Usage: vct bake <list|show|reset> [--profile <name>] [--json] [--manual]");
	Genode::log("Inspect or restore the defaults baked into this media.");
	Genode::log("");
	Genode::log("  list   Show the profile discoverable on this media");
	Genode::log("  show   Compare every baked default with its current configd value");
	Genode::log("  reset  Re-apply baked defaults and persist the result");
	Genode::log("");
	Genode::log("Reset changes only keys present in the baked defaults (plus theme.active);");
	Genode::log("user keys outside that set are left untouched. --manual prints each step.");
}


int BakeCommand::execute(Args const &args)
{
	if (args.help || args.positional == Genode::String<128>()) {
		_print_help(args);
		return 0;
	}
	if (args.positional == Genode::String<128>("list")) return _list(args);
	if (args.positional == Genode::String<128>("show")) return _show(args);
	if (args.positional == Genode::String<128>("reset")) return _reset(args);

	Genode::warning("vct: unknown bake subcommand '", args.positional,
	                "' — expected list, show, or reset");
	_print_help(args);
	return 1;
}


int BakeCommand::_list(Args const &args)
{
	Optional_rom manifest { _env, "bake_manifest" };
	char profile[128] { };
	char version[16] { };
	if (!json_value(manifest, "profile", profile, sizeof(profile), true) ||
	    !json_value(manifest, "profile_config_version", version, sizeof(version), false)) {
		if (args.json)
			Genode::log("{\"command\":\"bake\",\"op\":\"list\",\"status\":\"success\",\"profiles\":[]}");
		else if (args.lang == "ko")
			Genode::log("이 미디어에는 베이크 매니페스트가 없습니다.");
		else
			Genode::log("No bake manifest on this media.");
		return 0;
	}
	if (!profile_selected(args, profile)) {
		if (args.json)
			Genode::log("{\"command\":\"bake\",\"op\":\"list\",\"status\":\"success\",\"profiles\":[]}");
		else
			Genode::log("Profile '", args.profile, "' is not present on this media.");
		return 0;
	}

	if (args.json)
		Genode::log("{\"command\":\"bake\",\"op\":\"list\",\"status\":\"success\",\"profiles\":[{\"name\":\"",
		            Genode::String<128>(profile), "\",\"version\":", Genode::String<16>(version), "}]}");
	else if (args.lang == "ko")
		Genode::log("이 미디어의 베이크 프로필: ", Genode::String<128>(profile),
		            " @ v", Genode::String<16>(version));
	else
		Genode::log("Bake profile on this media: ", Genode::String<128>(profile),
		            " @ v", Genode::String<16>(version));
	return 0;
}


int BakeCommand::_show(Args const &args)
{
	Optional_rom manifest { _env, "bake_manifest" };
	Optional_rom defaults { _env, "bake_config_defaults" };
	Optional_rom config { _env, "configd" };
	char media_profile[128] { };
	char media_version[16] { };
	if (!json_value(manifest, "profile", media_profile, sizeof(media_profile), true) ||
	    !json_value(manifest, "profile_config_version", media_version,
	                sizeof(media_version), false) || !defaults.valid()) {
		if (args.json)
			Genode::log("{\"command\":\"bake\",\"op\":\"show\",\"status\":\"error\",\"error\":\"no bake manifest on this media\"}");
		else
			Genode::log("No bake manifest on this media.");
		return 1;
	}
	if (!profile_selected(args, media_profile)) {
		Genode::log("Profile '", args.profile, "' is not present on this media.");
		return 1;
	}

	Genode::String<128> profile { "none" };
	Genode::String<128> version { "0" };
	Genode::String<128> applied { "no" };
	config_value(config, "bake.profile", profile);
	config_value(config, "bake.version", version);
	config_value(config, "bake.applied", applied);

	if (args.json) {
		char rows[2048] { };
		Genode::size_t pos { 0 };
		auto append = [&] (char const *s) {
			while (*s && pos + 1 < sizeof(rows)) rows[pos++] = *s++;
		};
		bool first { true };
		for_each_default(defaults, [&] (char const *key, char const *baked) {
			Genode::String<128> current { };
			config_value(config, key, current);
			if (!first) append(",");
			first = false;
			append("{\"key\":\""); append(key); append("\",\"baked\":\"");
			append(baked); append("\",\"current\":\""); append(current.string()); append("\"}");
		});
		char theme[128] { };
		if (json_value(manifest, "theme", theme, sizeof(theme), true)) {
			Genode::String<128> current { };
			config_value(config, "theme.active", current);
			if (!first) append(",");
			append("{\"key\":\"theme.active\",\"baked\":\""); append(theme);
			append("\",\"current\":\""); append(current.string()); append("\"}");
		}
		rows[pos] = 0;
		Genode::log("{\"command\":\"bake\",\"op\":\"show\",\"status\":\"success\",\"profile\":\"",
		            profile, "\",\"version\":", version, ",\"applied\":\"", applied,
		            "\",\"defaults\":[", Genode::String<2048>(rows), "]}");
		return 0;
	}

	if (args.lang == "ko")
		Genode::log("현재 베이크 상태: ", profile, " @ v", version, " (적용: ", applied, ")");
	else
		Genode::log("Bake state: ", profile, " @ v", version, " (applied: ", applied, ")");
	Genode::log("KEY  BAKED  CURRENT");
	for_each_default(defaults, [&] (char const *key, char const *baked) {
		Genode::String<128> current { "(unavailable)" };
		config_value(config, key, current);
		Genode::log(key, "  ", baked, "  ", current);
	});
	char theme[128] { };
	if (json_value(manifest, "theme", theme, sizeof(theme), true)) {
		Genode::String<128> current { "(unavailable)" };
		config_value(config, "theme.active", current);
		Genode::log("theme.active  ", Genode::String<128>(theme), "  ", current);
	}
	return 0;
}


int BakeCommand::_reset(Args const &args)
{
	Optional_rom manifest { _env, "bake_manifest" };
	char profile[128] { };
	char version[16] { };
	if (!json_value(manifest, "profile", profile, sizeof(profile), true) ||
	    !json_value(manifest, "profile_config_version", version, sizeof(version), false)) {
		if (args.json)
			Genode::log("{\"command\":\"bake\",\"op\":\"reset\",\"status\":\"error\",\"error\":\"no bake manifest on this media\"}");
		else
			Genode::log("No bake manifest on this media.");
		return 1;
	}
	if (!profile_selected(args, profile)) {
		Genode::log("Profile '", args.profile, "' is not present on this media.");
		return 1;
	}

	if (args.manual) {
		if (args.lang == "ko") {
			Genode::log("1/3 현재 베이크 상태 확인");
			Genode::log("2/3 configd에 다시 적용 요청");
			Genode::log("3/3 구운 키 저장 및 브로드캐스트");
		} else {
			Genode::log("1/3 Read current bake state");
			Genode::log("2/3 Ask configd to re-apply the media defaults");
			Genode::log("3/3 Persist and broadcast the baked keys");
		}
	}

	ReportRomClient client { _env, "config_request", "config_result" };
	if (!client.config_set("bake.applied", "no")) {
		if (args.json)
			Genode::log("{\"command\":\"bake\",\"op\":\"reset\",\"status\":\"error\",\"error\":\"sponge_configd did not answer\"}");
		else
			Genode::warning("vct: sponge_configd did not answer bake reset");
		return 1;
	}
	Genode::Xml_node const result = client.result_xml();
	if (result.attribute_value("status", Genode::String<32>()) != Genode::String<32>("ok")) {
		Genode::String<256> const error =
			result.attribute_value("error", Genode::String<256>("reset rejected"));
		if (args.json)
			Genode::log("{\"command\":\"bake\",\"op\":\"reset\",\"status\":\"error\",\"error\":\"", error, "\"}");
		else
			Genode::log("bake reset: error: ", error);
		return 1;
	}

	if (args.json)
		Genode::log("{\"command\":\"bake\",\"op\":\"reset\",\"status\":\"success\",\"profile\":\"",
		            Genode::String<128>(profile), "\",\"version\":", Genode::String<16>(version), "}");
	else if (args.lang == "ko")
		Genode::log("베이크 기본값을 다시 적용했습니다: ", Genode::String<128>(profile),
		            " @ v", Genode::String<16>(version));
	else
		Genode::log("Re-applied baked defaults: ", Genode::String<128>(profile),
		            " @ v", Genode::String<16>(version));
	return 0;
}
