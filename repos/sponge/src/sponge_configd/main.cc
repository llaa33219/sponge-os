/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * sponge_configd — configuration backend daemon (Phase 5a, Phase 14 W6).
 *
 * A long-lived, signal-driven Genode component. It watches a
 * "config_request" ROM (relayed by report_rom from vct's request
 * report), validates the requested key/value against a known-key
 * registry, applies it to an in-memory flat dotted key-value store,
 * and answers vct through a "config_result" report that report_rom
 * relays back as a ROM. This Report/ROM channel is the settled
 * vct<->backend design (docs/04-components.md §5); there is no RPC
 * stub or IDL.
 *
 * A second Expanding_reporter ("config") broadcasts the whole store as
 * a ROM, regenerated on every successful set (and once at startup with
 * the defaults). Future watchers (sponge_themed, sponge-de) read this
 * ROM to react to config changes without issuing requests.
 *
 * Operations:
 *   - get:  return one key's value (error if the key is unknown).
 *   - set:  validate + store one key/value, regenerate the broadcast
 *           (error if the key is unknown or the value is invalid).
 *           When the component's <config> carries a <vfs> node, the
 *           new store is also persisted to that File_system (Phase 14
 *           W6 — closes the Phase 4 / Phase 13 "settings revert on
 *           reboot" carryover).
 *   - list: enumerate every known key/value, name-sorted.
 *
 * The store is flat and dotted (theme.active, panel.position). Keys are
 * a closed registry: an unknown key is a structured error, never a
 * silent write. The registry carries each key's type so set can reject
 * an out-of-range enum before touching the store.
 *
 * Determinism: list, the broadcast, and the on-disk store all emit
 * keys in name-sorted order with a fixed attribute order and no
 * volatile fields, so a watcher's config-diff is stable across
 * unrelated sets.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <os/vfs.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <vfs/simple_env.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Configd {

class Main;

namespace { }

}  /* namespace Sponge::Configd */


/* ===================== key registry ===================== */

/*
 * The known-key registry. A configuration key is valid only if it
 * appears here; everything else is rejected with a structured error.
 * This keeps the store closed and inspectable (AGENTS.md §1.2 — no
 * hidden global state, parent config is explicit).
 *
 * Phase 11 keys:
 *   - clock.format          : structural printable-ASCII format string.
 *   - leitzentrale.enabled  : enum {true, false}.
 *   - launcher.sort_by      : enum {manual, alpha}.
 *   - panel.height          : uint range [16..128].
 *   - panel.position        : enum {top, bottom, left, right}.
 *   - panel.visible_widgets : enum-list {clock, launcher}.
 *   - theme.active          : free-form string (a theme name).
 *
 * The registry is declared in name-sorted order; the output generators
 * also sort defensively (selection sort, like sponge_pkgd) so adding a
 * key out of order cannot perturb a watcher's config-diff.
 */
class Sponge::Configd::Main
{
	public:

		explicit Main(Genode::Env &env);

	private:

		static constexpr unsigned MAX_KEYS  = 16;
		static constexpr unsigned MAX_ENUMS = 8;

		struct Key_def
		{
			char const *name;
			bool        is_enum;
			char const *enum_values[MAX_ENUMS];
			unsigned    num_enums;
			char const *default_value;
			enum class Kind { String, Enum, UintRange, EnumList, FormatString } kind;
			unsigned    min_value;
			unsigned    max_value;
		};

		/* Known keys, name-sorted. */
		static Key_def const _registry[MAX_KEYS];
		static unsigned const _num_keys;

		Genode::Env &_env;

		/* Request ROM: report_rom relays vct's "config_request" report. */
		Genode::Attached_rom_dataspace _request_rom { _env, "config_request" };

		/* Result report: report_rom relays this to vct's "config_result" ROM. */
		Genode::Expanding_reporter _result_reporter { _env, "result", "config_result" };

		/*
		 * Broadcast report: report_rom relays this as a "config" ROM so
		 * watchers (sponge_themed, sponge-de) read the full store. It is
		 * regenerated on every successful set; an initial report with the
		 * defaults is emitted in the constructor before any watcher can
		 * request it.
		 */
		Genode::Expanding_reporter _broadcast_reporter { _env, "config", "config" };

		Genode::Signal_handler<Main> _request_handler {
			_env.ep(), *this, &Main::_handle_request };

		/*
		 * Phase 6c: lz_watch (inside the Leitzentrale subsystem) emits an
		 * lz_model report describing model-fs divergence. configd watches it
		 * and mirrors a read-only leitzentrale.diverged key in the broadcast
		 * (the "synchronization with sponge_configd" criterion). This key is
		 * computed, never settable via config_set. Only enabled when the
		 * configd config ROM contains <lz_model/>, so scenarios without the
		 * Leitzentrale subsystem don't try to open a non-existent ROM.
		 *
		 * Phase 14 W6: the same <config> gate also activates the optional
		 * persistent store when the config carries a <vfs> node (same
		 * opt-in contract as sponge_pkgd, docs/12 §13.4).
		 */
		Genode::Attached_rom_dataspace _config_rom { _env, "config" };
		Genode::Constructible<Genode::Attached_rom_dataspace> _lz_model_rom { };
		Genode::Signal_handler<Main> _lz_model_handler {
			_env.ep(), *this, &Main::_handle_lz_model };
		bool _lz_diverged { false };

		/*
		 * Optional read-only bake inputs. The parent opts in with a <bake/>
		 * node and routes the files served from /system/bake under these
		 * explicit labels. With no <bake/> node no sessions are requested,
		 * preserving the pre-Phase-15 deployment contract.
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _bake_defaults_rom { };
		Genode::Constructible<Genode::Attached_rom_dataspace> _bake_manifest_rom { };
		bool _bake_available { false };

		/*
		 * Optional persistent store (Phase 14 W6 — closes the Phase 4 /
		 * Phase 13 "settings revert on reboot" carryover). Activated
		 * only when this component's <config> carries a <vfs> node;
		 * otherwise _vfs_env stays deconstructed and the store
		 * load/save paths are no-ops (Phase 5a byte-identical in-memory
		 * behaviour). Single XML file at STORE_PATH on whatever
		 * File_system session the <vfs> mounts. The store is
		 * single-writer (Phase 4 §13.2 contract); crash-consistent
		 * writes (write-tmp + rename) are added by a follow-up commit
		 * on top of this initial activation.
		 */
		static char        const STORE_PATH[];
		static char        const STORE_TMP_PATH[];
		static unsigned    const STORE_VERSION;
		static Genode::size_t const STORE_BUF;

		Genode::Heap                                _heap     { _env.ram(), _env.rm() };
		Genode::Constructible<Genode::Vfs::Simple_env> _vfs_env { };

		void _handle_lz_model();

		/*
		 * De-duplication of the request ROM. ROM signals can fire more
		 * than once for the same content, and a repeated set with an
		 * identical signature is a no-op (the value is already stored).
		 * Skipping it avoids regenerating an identical broadcast and
		 * lets vct observe the already-correct result. The signature is
		 * op|key|value.
		 */
		Genode::String<320> _last_request_sig { };

		/* Current value per registry index. Defaults are applied in the
		 * constructor so the store is never empty. */
		Genode::String<128> _values[MAX_KEYS] { };

		/* ---- request handling ---- */
		void _handle_request();
		void _do_get(char const *key);
		void _do_set(char const *key, char const *value);
		void _do_list();

		/* ---- registry helpers ---- */
		bool _find_key(char const *key, unsigned &idx) const;
		bool _value_valid(unsigned idx, char const *value,
		                  Genode::String<256> &why) const;

		/* ---- deterministic name-sorted index order ---- */
		void _sorted_order(unsigned *order) const;

		/* ---- output ---- */
		void _generate_broadcast();
		void _report_get_ok(char const *key, char const *value);
		void _report_set_ok(char const *key, char const *value);
		void _report_list_ok();
		void _report_error(char const *op, char const *key, char const *message);

		/* ---- bake defaults (optional) ---- */
		void _init_bake();
		bool _apply_bake_defaults();
		bool _apply_validated_value(char const *key, char const *value,
		                            char const *source);

		/* ---- persistent store (optional) ---- */
		bool _store_enabled() const { return _vfs_env.constructed(); }
		void _init_store();
		void _load_store();
		void _save_store();
};


Sponge::Configd::Main::Key_def const Sponge::Configd::Main::_registry[MAX_KEYS] = {
	{ "bake.applied",          true,
	  { "yes", "no" }, 2, "no", Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0 },
	{ "bake.profile",          false,
	  { }, 0, "none", Sponge::Configd::Main::Key_def::Kind::String, 0, 0 },
	{ "bake.version",          false,
	  { }, 0, "0", Sponge::Configd::Main::Key_def::Kind::UintRange, 0, ~0U },
	{ "clock.format",          false,
	  { }, 0, "HH:mm", Sponge::Configd::Main::Key_def::Kind::FormatString, 0, 0 },
	{ "leitzentrale.enabled",  true,
	  { "true", "false" }, 2, "false", Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0 },
	{ "launcher.sort_by",      true,
	  { "manual", "alpha" }, 2, "alpha", Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0 },
	{ "panel.height",          false,
	  { }, 0, "28", Sponge::Configd::Main::Key_def::Kind::UintRange, 16, 128 },
	{ "panel.position",        true,
	  { "top", "bottom", "left", "right" }, 4, "bottom",
	  Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0 },
	{ "panel.visible_widgets", false,
	  { "clock", "launcher" }, 2, "clock,launcher",
	  Sponge::Configd::Main::Key_def::Kind::EnumList, 0, 0 },
	{ "theme.active",          false,
	  { }, 0, "light", Sponge::Configd::Main::Key_def::Kind::String, 0, 0 },
};

unsigned const Sponge::Configd::Main::_num_keys = 10;


/* ===================== registry helpers ===================== */

bool Sponge::Configd::Main::_find_key(char const *key, unsigned &idx) const
{
	for (unsigned i = 0; i < _num_keys; ++i)
		if (Genode::strcmp(_registry[i].name, key) == 0) {
			idx = i;
			return true;
		}
	return false;
}


bool Sponge::Configd::Main::_value_valid(unsigned idx, char const *value,
                                         Genode::String<256> &why) const
{
	Key_def const &d = _registry[idx];

	if (Genode::strcmp(value, "") == 0) {
		char const *const expected = d.kind == Key_def::Kind::EnumList
		                           ? "non-empty comma-separated list"
		                           : d.kind == Key_def::Kind::FormatString
		                           ? "non-empty printable ASCII string"
		                           : "non-empty value";
		why = Genode::String<256>("invalid value '' for key '", d.name,
		                          "' (expected: ", expected, ")");
		return false;
	}

	if (d.kind == Key_def::Kind::String)
		return true;

	if (d.kind == Key_def::Kind::UintRange) {
		unsigned parsed { 0 };
		for (Genode::size_t i = 0; value[i] != 0; ++i) {
			char const c = value[i];
			if (c < '0' || c > '9') {
				why = Genode::String<256>("invalid value '", Genode::String<128>(value),
				                          "' for key '", d.name,
				                          "' (expected: base-10 unsigned integer in range [",
				                          d.min_value, "..", d.max_value, "])");
				return false;
			}

			unsigned const digit = (unsigned)(c - '0');
			if (parsed > ((~0U) - digit) / 10U) {
				why = Genode::String<256>("invalid value '", Genode::String<128>(value),
				                          "' for key '", d.name,
				                          "' (expected: base-10 unsigned integer in range [",
				                          d.min_value, "..", d.max_value, "])");
				return false;
			}
			parsed = parsed * 10U + digit;
		}

		if (parsed < d.min_value || parsed > d.max_value) {
			why = Genode::String<256>("value '", Genode::String<128>(value),
			                          "' for key '", d.name,
			                          "' out of range [", d.min_value,
			                          "..", d.max_value, "]");
			return false;
		}
		return true;
	}

	if (d.kind == Key_def::Kind::FormatString) {
		Genode::size_t const length = Genode::strlen(value);
		if (length > 64) {
			why = Genode::String<256>("invalid value '", Genode::String<128>(value),
			                          "' for key '", d.name,
			                          "' (expected: at most 64 printable ASCII characters)");
			return false;
		}
		for (Genode::size_t i = 0; i < length; ++i) {
			unsigned char const c = (unsigned char)value[i];
			if (c < 0x20U || c > 0x7eU) {
				why = Genode::String<256>("invalid value '", Genode::String<128>(value),
				                          "' for key '", d.name,
				                          "' (expected: printable ASCII characters 0x20..0x7e)");
				return false;
			}
		}
		return true;
	}

	char expected[128] { "expected: " };
	Genode::size_t pos = Genode::strlen(expected);
	for (unsigned i = 0; i < d.num_enums; ++i) {
		if (i > 0 && pos + 2 < sizeof(expected)) {
			expected[pos++] = ',';
			expected[pos++] = ' ';
		}
		char const *s = d.enum_values[i];
		while (*s && pos + 1 < sizeof(expected))
			expected[pos++] = *s++;
	}
	expected[pos] = 0;

	if (d.kind == Key_def::Kind::Enum) {
		for (unsigned i = 0; i < d.num_enums; ++i)
			if (Genode::strcmp(value, d.enum_values[i]) == 0)
				return true;

		why = Genode::String<256>("invalid value '", Genode::String<128>(value),
		                          "' for key '", Genode::String<64>(d.name),
		                          "' (", Genode::String<128>(expected), ")");
		return false;
	}

	if (d.kind == Key_def::Kind::EnumList) {
		auto const whitespace = [] (char c) {
			return c == ' ' || c == '\t' || c == '\n' || c == '\r';
		};

		Genode::size_t const length = Genode::strlen(value);
		Genode::size_t start { 0 };
		while (start <= length) {
			Genode::size_t end = start;
			while (end < length && value[end] != ',') ++end;

			Genode::size_t first = start;
			Genode::size_t last  = end;
			while (first < last && whitespace(value[first])) ++first;
			while (last > first && whitespace(value[last - 1])) --last;

			char token[128] { };
			Genode::size_t const token_length = last - first;
			for (Genode::size_t i = 0; i < token_length && i + 1 < sizeof(token); ++i)
				token[i] = value[first + i];

			bool known { false };
			if (token_length > 0 && token_length < sizeof(token))
				for (unsigned i = 0; i < d.num_enums; ++i)
					if (Genode::strcmp(token, d.enum_values[i]) == 0) {
						known = true;
						break;
					}

			if (!known) {
				why = Genode::String<256>("invalid token '", Genode::String<128>(token),
				                          "' in list for key '", d.name,
				                          "' (", Genode::String<128>(expected), ")");
				return false;
			}

			if (end == length) break;
			start = end + 1;
		}
		return true;
	}

	why = Genode::String<256>("invalid value '", Genode::String<128>(value),
	                          "' for key '", d.name,
	                          "' (expected: registered validation kind)");
	return false;
}


void Sponge::Configd::Main::_sorted_order(unsigned *order) const
{
	for (unsigned i = 0; i < _num_keys; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_keys; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_keys; ++j)
			if (Genode::strcmp(_registry[order[j]].name,
			                   _registry[order[best]].name) < 0)
				best = j;
		if (best != i) {
			unsigned tmp = order[i];
			order[i] = order[best];
			order[best] = tmp;
		}
	}
}


/* ===================== request handling ===================== */

bool Sponge::Configd::Main::_apply_validated_value(char const *key,
                                                    char const *value,
                                                    char const *source)
{
	unsigned idx { 0 };
	if (!_find_key(key, idx)) {
		Genode::warning("sponge_configd: ", source, " skipped unknown key '", key, "'");
		return false;
	}

	Genode::String<256> why { };
	if (!_value_valid(idx, value, why)) {
		Genode::warning("sponge_configd: ", source, " skipped ", why);
		return false;
	}

	_values[idx] = Genode::String<128>(value);
	return true;
}


void Sponge::Configd::Main::_handle_request()
{
	_request_rom.update();

	if (!_request_rom.valid())
		return;

	try {
		Genode::Xml_node const req = _request_rom.xml();

		if (!req.has_type("request")) {
			_report_error("get", "", "request root is not <request>");
			return;
		}

		Genode::String<32>  const op    = req.attribute_value("op",
		                                              Genode::String<32>());
		Genode::String<128> const key   = req.attribute_value("key",
		                                              Genode::String<128>());
		Genode::String<128> const value = req.attribute_value("value",
		                                              Genode::String<128>());

		Genode::String<320> const sig(op, "|", key, "|", value);
		if (sig == _last_request_sig)
			return;
		_last_request_sig = sig;

		if (Genode::strcmp(op.string(), "list") == 0) {
			_do_list();
			return;
		}

		if (Genode::strcmp(key.string(), "") == 0) {
			_report_error(op.string(), "", "no key specified in request");
			return;
		}

		if (Genode::strcmp(op.string(), "get") == 0) {
			_do_get(key.string());
			return;
		}
		if (Genode::strcmp(op.string(), "set") == 0) {
			_do_set(key.string(), value.string());
			return;
		}

		_report_error(op.string(), key.string(), "unknown operation");
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("get", "", "malformed request ROM");
	}
}


void Sponge::Configd::Main::_do_get(char const *key)
{
	unsigned idx { 0 };
	if (!_find_key(key, idx)) {
		_report_error("get", key,
		              Genode::String<128>("unknown key: ", key).string());
		return;
	}

	_report_get_ok(key, _values[idx].string());
}


void Sponge::Configd::Main::_do_set(char const *key, char const *value)
{
	if (Genode::strcmp(key, "bake.applied") == 0 &&
	    Genode::strcmp(value, "no") == 0) {
		if (!_apply_bake_defaults()) {
			_report_error("set", key, "baked defaults are unavailable");
			return;
		}
		_save_store();
		_generate_broadcast();
		_report_set_ok(key, value);
		return;
	}

	if (Genode::strcmp(key, "bake.applied") == 0 ||
	    Genode::strcmp(key, "bake.profile") == 0 ||
	    Genode::strcmp(key, "bake.version") == 0) {
		_report_error("set", key,
		              "bake metadata is read-only (use bake.applied=no to reset)");
		return;
	}

	unsigned idx { 0 };
	if (!_find_key(key, idx)) {
		_report_error("set", key,
		              Genode::String<128>("unknown key: ", key).string());
		return;
	}

	Genode::String<256> why { };
	if (!_value_valid(idx, value, why)) {
		_report_error("set", key, why.string());
		return;
	}

	_values[idx] = Genode::String<128>(value);

	/*
	 * Persist the new store BEFORE regenerating the broadcast. If the
	 * save fails, the in-memory + broadcast change still proceeds
	 * (the cross-reboot durability is lost for that one mutation,
	 * see _save_store comment). The ordering matches docs/12 §13.3
	 * so a crash between the set response and a watcher reading the
	 * new broadcast still has the durable copy on disk.
	 */
	_save_store();

	/* Regenerate the broadcast so every watcher sees the new store. */
	_generate_broadcast();

	_report_set_ok(key, value);
}


void Sponge::Configd::Main::_do_list()
{
	_report_list_ok();
}


/* ===================== output generation ===================== */

/*
 * Emit the full store as <config><key name="..." value="..."/></config>,
 * name-sorted. The root is "config" (the reporter's node type) so a
 * watcher reads it as a standard config ROM. Deterministic: fixed
 * attribute order, no volatile fields.
 */
void Sponge::Configd::Main::_generate_broadcast()
{
	unsigned order[MAX_KEYS] { };
	_sorted_order(order);

	_broadcast_reporter.generate_xml([&](Genode::Xml_generator &g) {
		for (unsigned n = 0; n < _num_keys; ++n) {
			unsigned const i = order[n];
			g.node("key", [&] {
				g.attribute("name",  Genode::String<64>(_registry[i].name));
				g.attribute("value", _values[i]);
			});
		}
		/* Read-only computed key mirrored from lz_watch (Phase 6c). */
		g.node("key", [&] {
			g.attribute("name",  "leitzentrale.diverged");
			g.attribute("value", _lz_diverged ? "true" : "false");
		});
	});
}


void Sponge::Configd::Main::_handle_lz_model()
{
	if (!_lz_model_rom.constructed()) return;
	_lz_model_rom->update();
	if (!_lz_model_rom->valid()) return;

	bool diverged = false;
	_lz_model_rom->xml().for_each_sub_node("file", [&] (Genode::Xml_node const &f) {
		if (f.attribute_value("changed", Genode::String<8>()) ==
		    Genode::String<8>("true"))
			diverged = true;
	});

	if (diverged != _lz_diverged) {
		_lz_diverged = diverged;
		_generate_broadcast();
	}
}


/* ===================== bake defaults (optional) ===================== */

void Sponge::Configd::Main::_init_bake()
{
	_config_rom.update();
	if (!_config_rom.valid()) return;

	bool enabled { false };
	try {
		_config_rom.node().with_optional_sub_node("bake",
			[&](Genode::Node const &) { enabled = true; });
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_configd: malformed <config> — bake defaults disabled");
		return;
	}
	if (!enabled) return;

	try {
		_bake_defaults_rom.construct(_env, "bake_config_defaults");
		_bake_manifest_rom.construct(_env, "bake_manifest");
		_bake_defaults_rom->update();
		_bake_manifest_rom->update();
		_bake_available = _bake_defaults_rom->valid() && _bake_manifest_rom->valid();
		if (_bake_available)
			Genode::log("sponge_configd: baked defaults available");
		else
			Genode::warning("sponge_configd: bake ROMs routed but invalid");
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::warning("sponge_configd: bake ROM connection failed");
	}
	catch (Genode::Service_denied) {
		Genode::warning("sponge_configd: bake ROM service denied");
	}
	catch (Genode::Out_of_ram) {
		Genode::warning("sponge_configd: no RAM for bake ROM sessions");
	}
	catch (Genode::Out_of_caps) {
		Genode::warning("sponge_configd: no caps for bake ROM sessions");
	}
}


bool Sponge::Configd::Main::_apply_bake_defaults()
{
	if (!_bake_available) return false;

	_bake_defaults_rom->update();
	_bake_manifest_rom->update();
	if (!_bake_defaults_rom->valid() || !_bake_manifest_rom->valid())
		return false;

	char const *manifest = _bake_manifest_rom->local_addr<char const>();
	Genode::size_t const manifest_size = _bake_manifest_rom->size();

	auto find_json_value = [&] (char const *name, char *out,
	                           Genode::size_t out_size, bool quoted) {
		Genode::String<96> const token("\"", name, "\"");
		Genode::size_t const token_len = Genode::strlen(token.string());
		for (Genode::size_t i = 0; i + token_len < manifest_size; ++i) {
			if (Genode::strcmp(manifest + i, token.string(), token_len) != 0)
				continue;
			Genode::size_t p = i + token_len;
			while (p < manifest_size && (manifest[p] == ' ' || manifest[p] == '\t' ||
			       manifest[p] == '\r' || manifest[p] == '\n')) ++p;
			if (p >= manifest_size || manifest[p++] != ':') return false;
			while (p < manifest_size && (manifest[p] == ' ' || manifest[p] == '\t' ||
			       manifest[p] == '\r' || manifest[p] == '\n')) ++p;
			if (quoted && (p >= manifest_size || manifest[p++] != '"')) return false;
			Genode::size_t n { 0 };
			while (p < manifest_size && n + 1 < out_size) {
				char const c = manifest[p++];
				if ((quoted && c == '"') || (!quoted && (c < '0' || c > '9')))
					break;
				out[n++] = c;
			}
			out[n] = 0;
			return n > 0;
		}
		return false;
	};

	char schema[16] { };
	char version[16] { };
	char profile[128] { };
	char theme[128] { };
	if (!find_json_value("schema_version", schema, sizeof(schema), false) ||
	    !find_json_value("profile_config_version", version, sizeof(version), false) ||
	    !find_json_value("profile", profile, sizeof(profile), true) ||
	    Genode::strcmp(schema, "1") != 0 || Genode::strcmp(version, "1") != 0) {
		Genode::warning("sponge_configd: unsupported or malformed bake manifest");
		return false;
	}
	bool const have_theme = find_json_value("theme", theme, sizeof(theme), true);

	unsigned applied { 0 };
	char const *defaults = _bake_defaults_rom->local_addr<char const>();
	Genode::size_t const defaults_size = _bake_defaults_rom->size();
	Genode::size_t line_start { 0 };
	while (line_start < defaults_size && defaults[line_start] != 0) {
		Genode::size_t line_end = line_start;
		while (line_end < defaults_size && defaults[line_end] != '\n') ++line_end;

		Genode::size_t first = line_start;
		Genode::size_t last = line_end;
		while (first < last && (defaults[first] == ' ' || defaults[first] == '\t' ||
		       defaults[first] == '\r')) ++first;
		while (last > first && (defaults[last - 1] == ' ' || defaults[last - 1] == '\t' ||
		       defaults[last - 1] == '\r')) --last;

		if (first < last && defaults[first] != '#') {
			Genode::size_t eq = first;
			while (eq < last && defaults[eq] != '=') ++eq;
			if (eq == last) {
				Genode::warning("sponge_configd: bake defaults skipped malformed line");
			} else {
				Genode::size_t key_last = eq;
				while (key_last > first && (defaults[key_last - 1] == ' ' ||
				       defaults[key_last - 1] == '\t')) --key_last;
				Genode::size_t value_first = eq + 1;
				while (value_first < last && (defaults[value_first] == ' ' ||
				       defaults[value_first] == '\t')) ++value_first;

				char key[128] { };
				char value[128] { };
				Genode::size_t const key_len = key_last - first;
				Genode::size_t const value_len = last - value_first;
				if (key_len == 0 || key_len >= sizeof(key) || value_len >= sizeof(value)) {
					Genode::warning("sponge_configd: bake defaults skipped oversized line");
				} else {
					for (Genode::size_t i = 0; i < key_len; ++i) key[i] = defaults[first + i];
					for (Genode::size_t i = 0; i < value_len; ++i) value[i] = defaults[value_first + i];
					if (_apply_validated_value(key, value, "bake defaults")) ++applied;
				}
			}
		}
		line_start = line_end + 1;
	}

	if (have_theme && _apply_validated_value("theme.active", theme, "bake manifest"))
		++applied;
	_apply_validated_value("bake.profile", profile, "bake manifest");
	_apply_validated_value("bake.version", version, "bake manifest");
	_apply_validated_value("bake.applied", "yes", "bake sentinel");

	Genode::log("sponge_configd: applied ", applied, " baked default(s) from profile '",
	            Genode::String<128>(profile), "' @ v", Genode::String<16>(version));
	return true;
}


/* ===================== persistent store (optional) ===================== */

/*
 * Build the Vfs environment only when the component <config> carries a
 * <vfs> node. With no <vfs> node the store stays disabled and the
 * daemon behaves byte-identically to the in-memory Phase 5a build —
 * the opt-in contract that keeps every non-persistent scenario
 * working unchanged (sponge_pkgd's pattern, docs/12 §13.4).
 */
void Sponge::Configd::Main::_init_store()
{
	_config_rom.update();
	if (!_config_rom.valid())
		return;

	try {
		_config_rom.node().with_optional_sub_node("vfs",
			[&](Genode::Node const &vfs_node) {
				_vfs_env.construct(_env, _heap, vfs_node);
				Genode::log("sponge_configd: persistent store enabled at ", STORE_PATH);
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_configd: malformed <config> — persistence disabled");
	}
}


/*
 * Restore _values[] from the store. Every failure mode — missing
 * file, empty/oversized, unreadable, wrong root element, unsupported
 * version, malformed XML — resolves to the same safe state: an empty
 * store plus a warning, never a crash (docs/12 §13.2 contract). The
 * caller re-applies registry defaults for any keys still empty after
 * the load, so a partial / torn store recovers gracefully: present
 * keys are honored, missing keys fall back to their defaults.
 */
void Sponge::Configd::Main::_load_store()
{
	if (!_store_enabled()) return;

	Genode::Vfs::File_system &vfs = _vfs_env->root_dir();

	Genode::Vfs::Directory_service::Stat stat { };
	if (vfs.stat(STORE_PATH, stat) != Genode::Vfs::Directory_service::STAT_OK) {
		Genode::log("sponge_configd: no store — starting with defaults");
		return;
	}
	if (stat.size == 0 || stat.size > STORE_BUF) {
		Genode::warning("sponge_configd: store size ", stat.size,
		                " out of range — starting with defaults");
		return;
	}

	Genode::Vfs::Vfs_handle *handle { nullptr };
	if (vfs.open(STORE_PATH, Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
	             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_configd: store open failed — starting with defaults");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard guard(handle);

	char buf[STORE_BUF] { };
	Genode::size_t total { 0 };
	bool ok { true };
	while (total < stat.size) {
		handle->seek(total);
		handle->fs().queue_read(handle, stat.size - total);
		Genode::size_t n { 0 };
		Genode::Vfs::File_io_service::Read_result r;
		while ((r = handle->fs().complete_read(handle,
		            Genode::Byte_range_ptr(buf + total, sizeof(buf) - total),
		            n)) == Genode::Vfs::File_io_service::READ_QUEUED)
			_vfs_env->io().commit_and_wait();
		if (r != Genode::Vfs::File_io_service::READ_OK || n == 0) {
			ok = false; break;
		}
		total += n;
	}

	if (!ok || total == 0) {
		Genode::warning("sponge_configd: store unreadable — starting with defaults");
		return;
	}

	unsigned restored { 0 };
	try {
		Genode::Xml_node const root(buf, total);
		if (!root.has_type("sponge-config")) {
			Genode::warning("sponge_configd: store root is not <sponge-config> "
			                "— starting with defaults");
			return;
		}
		unsigned const version = root.attribute_value("version", 0U);
		if (version != STORE_VERSION) {
			Genode::warning("sponge_configd: store version ", version,
			                " unsupported (expected ", STORE_VERSION,
			                ") — starting with defaults");
			return;
		}
		root.for_each_sub_node("entry", [&](Genode::Xml_node const &n) {
			Genode::String<128> const key =
				n.attribute_value("name", Genode::String<128>());
			Genode::String<128> const val =
				n.attribute_value("value", Genode::String<128>());
			unsigned idx { 0 };
			if (Genode::strcmp(key.string(), "") != 0 &&
			    Genode::strcmp(val.string(), "") != 0 &&
			    _find_key(key.string(), idx)) {
				_values[idx] = val;
				++restored;
			}
		});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_configd: store is not valid XML — starting with defaults");
		for (unsigned i = 0; i < _num_keys; ++i) _values[i] = Genode::String<128>();
		return;
	}

	Genode::log("sponge_configd: restored ", restored, " key(s) from store");
}


/*
 * Persist _values[] to the store. Output is name-sorted with a fixed
 * attribute order so the file is byte-stable for a given store
 * (matching the determinism contract of the broadcast generator). A
 * failed write is logged but never blocks the set: the in-memory
 * state and the broadcast still reflect the requested change, only
 * the across-reboot durability is lost for that one mutation.
 *
 * The <lz_diverged> mirrored key is NOT persisted — it is computed
 * live from the lz_model ROM on every broadcast and would just be
 * stale on the next boot.
 */
void Sponge::Configd::Main::_save_store()
{
	if (!_store_enabled()) return;

	unsigned order[MAX_KEYS] { };
	_sorted_order(order);

	char buf[STORE_BUF] { };
	Genode::size_t pos { 0 };
	auto append = [&buf, &pos](char const *s) {
		while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
	};
	append("<sponge-config version=\"1\">");
	for (unsigned i = 0; i < _num_keys; ++i) {
		unsigned const idx = order[i];
		append("<entry name=\"");
		append(_registry[idx].name);
		append("\" value=\"");
		append(_values[idx].string());
		append("\"/>");
	}
	append("</sponge-config>");
	Genode::size_t const len = pos;

	Genode::Vfs::File_system &vfs = _vfs_env->root_dir();

	/*
	 * Crash-consistent write (Phase 4 §13.2): write STORE_TMP_PATH
	 * first, then rename over STORE_PATH. A torn mid-write leaves the
	 * previous store intact and the tmp as garbage for the next boot's
	 * _load_store to warn-and-discard. The rename is atomic on the
	 * single-writer vfs.
	 */
	Genode::Vfs::Vfs_handle *tmp_handle { nullptr };
	Genode::Vfs::Directory_service::Open_result tmp_open =
		vfs.open(STORE_TMP_PATH,
		         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY,
		         &tmp_handle, _heap);
	if (tmp_open == Genode::Vfs::Directory_service::OPEN_ERR_UNACCESSIBLE) {
		tmp_open = vfs.open(STORE_TMP_PATH,
		         Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
		         | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
		         &tmp_handle, _heap);
	}
	if (tmp_open != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_configd: cannot open store tmp for write");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard tmp_guard(tmp_handle);

	tmp_handle->fs().ftruncate(tmp_handle, len);

	{
		Genode::size_t off { 0 };
		bool ok { true };
		while (off < len) {
			tmp_handle->seek(off);
			Genode::size_t n { 0 };
			Genode::Vfs::File_io_service::Write_result const w =
				tmp_handle->fs().write(tmp_handle,
				    Genode::Const_byte_range_ptr(buf + off, len - off), n);
			if (w == Genode::Vfs::File_io_service::WRITE_OK) {
				if (n == 0) { ok = false; break; }
				off += n;
			} else if (w == Genode::Vfs::File_io_service::WRITE_ERR_WOULD_BLOCK) {
				_vfs_env->io().commit_and_wait();
			} else {
				ok = false; break;
			}
		}

		tmp_handle->fs().queue_sync(tmp_handle);
		while (tmp_handle->fs().complete_sync(tmp_handle) ==
		       Genode::Vfs::File_io_service::SYNC_QUEUED)
			_vfs_env->io().commit_and_wait();

		if (!ok) {
			Genode::warning("sponge_configd: store tmp write incomplete");
			return;
		}
	}
	/* (tmp_handle + tmp_guard released here by RAII scope exit) */

	if (vfs.rename(STORE_TMP_PATH, STORE_PATH) !=
	    Genode::Vfs::Directory_service::RENAME_OK) {
		Genode::warning("sponge_configd: store rename failed");
	}
}


void Sponge::Configd::Main::_report_get_ok(char const *key, char const *value)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "get");
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("value",  Genode::String<128>(value));
	});
}


void Sponge::Configd::Main::_report_set_ok(char const *key, char const *value)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "set");
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("value",  Genode::String<128>(value));
	});
}


void Sponge::Configd::Main::_report_list_ok()
{
	unsigned order[MAX_KEYS] { };
	_sorted_order(order);

	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "list");
		g.attribute("count",  _num_keys);

		g.node("keys", [&] {
			for (unsigned n = 0; n < _num_keys; ++n) {
				unsigned const i = order[n];
				g.node("key", [&] {
					g.attribute("name",  Genode::String<64>(_registry[i].name));
					g.attribute("value", _values[i]);
				});
			}
		});
	});
}


void Sponge::Configd::Main::_report_error(char const *op, char const *key,
                                          char const *message)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "error");
		g.attribute("op",     op);
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("error",  message);
	});
}


/* ===================== component wiring ===================== */

/* Store layout (Phase 14 W6). The path is at the <vfs> mount root;
 * the .tmp sibling is reserved for the atomic-rename path added in
 * the fix(configd) follow-up commit. */
char const Sponge::Configd::Main::STORE_PATH[]     = "/store.xml";
char const Sponge::Configd::Main::STORE_TMP_PATH[] = "/store.xml.tmp";
unsigned const     Sponge::Configd::Main::STORE_VERSION  = 1;
Genode::size_t const Sponge::Configd::Main::STORE_BUF    = 4096;


Sponge::Configd::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_configd: ready");

	/*
	 * Optional persistent store: activate it if <config> declares a
	 * <vfs>, then reload the previously-persisted store (if any). In
	 * the non-persistent scenarios both calls are no-ops. Load runs
	 * BEFORE defaults are applied so a restored boot sees the
	 * persisted values, not the registry defaults.
	 */
	_init_store();
	_load_store();

	/* Apply registry defaults so the store is never empty and the
	 * initial broadcast reflects a usable configuration. Defaults
	 * only fill in keys that were absent from the on-disk store (a
	 * restored key wins over its default). */
	for (unsigned i = 0; i < _num_keys; ++i)
		if (Genode::strcmp(_values[i].string(), "") == 0)
			_values[i] = Genode::String<128>(_registry[i].default_value);

	_init_bake();
	unsigned bake_applied_idx { 0 };
	if (_bake_available &&
	    _find_key("bake.applied", bake_applied_idx) &&
	    Genode::strcmp(_values[bake_applied_idx].string(), "yes") != 0 &&
	    _apply_bake_defaults())
		_save_store();

	/* Publish the store before any watcher can request it (init
	 * starts children in config order). On a restored boot this
	 * already carries the persisted values, so sponge_themed /
	 * sponge-de see them right away. */
	_generate_broadcast();

	_request_rom.sigh(_request_handler);
	_request_rom.update();

	/*
	 * Enable lz_model watching only when the configd config ROM explicitly
	 * requests it (<lz_model/>). Other scenarios don't provide the ROM and
	 * must not try to open it.
	 */
	_config_rom.update();
	bool const config_valid = _config_rom.valid();
	bool       watch_lz_model = false;
	if (config_valid) {
		char const *p = _config_rom.local_addr<char const>();
		for (Genode::size_t i = 0; p[i]; ++i) {
			if (p[i] == 'l' && p[i+1] == 'z' && p[i+2] == '_' &&
			    p[i+3] == 'm' && p[i+4] == 'o' && p[i+5] == 'd' &&
			    p[i+6] == 'e' && p[i+7] == 'l') {
				watch_lz_model = true;
				break;
			}
		}
	}
	if (watch_lz_model) {
		Genode::log("sponge_configd: lz_model watching enabled");
		_lz_model_rom.construct(_env, "lz_model");
		_lz_model_rom->sigh(_lz_model_handler);
		_lz_model_rom->update();
		_handle_lz_model();
	}

	/* Process a request that arrived before the signal handler was wired. */
	_handle_request();
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Configd::Main main { env };
}


/* Carries request-handling state on the stack; keep it comfortable. */
Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
