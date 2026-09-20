/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_configd — configuration backend daemon (Phase 5a, Phase 14 W6,
 * Phase 16 W2).
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
 * Phase 16 W2 extends the registry with three coordinated pieces
 * (U16.4/U16.5/D16.5/D16.10):
 *
 *   - Four new flat keys (background.color, background.image,
 *     shortcuts.bindings, panel.ids) bring the total to 14 flat
 *     slots; MAX_KEYS is bumped 16 -> 32 as the interim ceiling.
 *
 *   - A separate MAX_PATTERN_KEYS = 32 array carries pattern templates
 *     (panel.<id>.{height,position,visible_widgets}). On first write
 *     of a per-id name the registry parser scans the templates,
 *     validates <id> against [a-z0-9_-]{1,16}, and clones the
 *     template's Key_def into the dynamic _instantiated[] array; the
 *     cloned validator then runs on the value. Reads of an
 *     uninstantiated template name find nothing in the broadcast
 *     (the key is only visible after a successful write — the
 *     plan's instantiation-timing rule, W2 #3).
 *
 *   - shortcuts.bindings is a single structured-value key carrying a
 *     multi-line "action<TAB>key_sequence" list with a strict
 *     per-line validator (closed action enum + Genode Input-event
 *     keycode names). This is the deliberate exception to the
 *     one-key-per-setting rule (U16.4) and is documented as such in
 *     repos/sponge/src/sponge_configd/README.md.
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
#include <input/keycodes.h>
#include <os/reporter.h>
#include <os/vfs.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <vfs/root.h>
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
 * Phase 11 flat keys (10):
 *   - bake.applied, bake.profile, bake.version
 *   - clock.format
 *   - leitzentrale.enabled
 *   - launcher.sort_by
 *   - panel.height, panel.position, panel.visible_widgets
 *   - theme.active
 *
 * Phase 16 W2 flat keys (+4):
 *   - background.color     : hex #RRGGBB
 *   - background.image     : allowlist (default ["/system/background/default.png"])
 *   - shortcuts.bindings   : structured multi-line (the deliberate
 *                            exception to one-key-per-setting,
 *                            documented in README)
 *   - panel.ids            : comma-list of active panel ids (each
 *                            matches [a-z0-9_-]{1,16})
 *
 * Phase 16 W2 pattern templates (+3, instantiated on first write):
 *   - panel.<id>.height           : clones panel.height validator
 *   - panel.<id>.position         : clones panel.position validator
 *   - panel.<id>.visible_widgets  : clones panel.visible_widgets
 *                                   validator (extended to {clock,
 *                                   launcher, tasklist} per W2 #7)
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

		/*
		 * MAX_KEYS is the flat-key ceiling (Phase 5a: 10 used;
		 * Phase 16 W2 adds 4 → 14 used, ceiling bumped 16 → 32 as
		 * the interim headroom). MAX_PATTERN_KEYS is a separate
		 * fixed array of pattern-key templates AND of instantiated
		 * per-id clones — the two share one slot pool because the
		 * set of <id>s is open-ended while the template count is
		 * bounded by the design (panel.{height, position,
		 * visible_widgets} = 3 templates).
		 */
		static constexpr unsigned MAX_KEYS         = 32;
		static constexpr unsigned MAX_PATTERN_KEYS = 32;
		static constexpr unsigned MAX_ENUMS        = 8;

		struct Key_def
		{
			char const *name;
			bool        is_enum;
			char const *enum_values[MAX_ENUMS];
			unsigned    num_enums;
			char const *default_value;
			enum class Kind { String, Enum, UintRange, EnumList,
			                  FormatString, HexColor, Allowlist,
			                  CommaList, Shortcuts } kind;
			unsigned    min_value;
			unsigned    max_value;

			/* For Allowlist kind: closed set of allowed paths. */
			char const *allowed_paths[MAX_ENUMS];
			unsigned    num_allowed;
		};

		/* Known flat keys, name-sorted. */
		static Key_def const _registry[MAX_KEYS];
		static unsigned const _num_keys;

		/*
		 * Pattern templates. Each template's `name` carries the
		 * literal `<id>` placeholder (e.g. "panel.<id>.height"). A
		 * template is instantiated into _instantiated[] on first
		 * write of a matching per-id key; the cloned entry stores
		 * the actual name (e.g. "panel.alpha.height"). Both arrays
		 * share MAX_PATTERN_KEYS so the runtime instance count
		 * (open-ended) plus the static template count (3) are
		 * bounded together.
		 */
		static Key_def const _pattern_registry[MAX_PATTERN_KEYS];
		static unsigned const _num_pattern_templates;

		Genode::Env &_env;

		/*
		 * Request ROMs. Phase 16 W4 (D16.1): the long-lived sponge-de
		 * needs its own dedicated `de_config_request` label distinct
		 * from vct's `config_request` (report_rom is single-writer
		 * per label slot, mirrors the Phase 14 launcher precedent).
		 * Both labels feed the SAME `_handle_request` body — the
		 * validator + registry answers are identical for both
		 * writers (D16.9 validator parity). The `_request_rom`
		 * (vct) is constructed unconditionally; the
		 * `_de_request_rom` (DE-side) is constructed lazily so
		 * scenarios without a sponge-de-side label route never
		 * see a ROM-session-denied fatal.
		 */
		Genode::Attached_rom_dataspace _request_rom { _env, "config_request" };
		Genode::Constructible<Genode::Attached_rom_dataspace> _de_request_rom { };

		/*
		 * Result reports. Same dual-label shape: vct's
		 * `config_result` is emitted unconditionally; the
		 * DE-side `de_config_result` reporter is constructed
		 * lazily (only on first DE-side write) so scenarios
		 * without an active sponge-de never reserve the label
		 * slot for no reason.
		 */
		Genode::Expanding_reporter _result_reporter { _env, "result", "config_result" };
		Genode::Constructible<Genode::Expanding_reporter> _de_result_reporter { };

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
		 * Phase 16 W4 (D16.1): second signal handler for the
		 * DE-side `de_config_request` ROM. The handler is
		 * identical to `_handle_request` but reuses the SAME
		 * `_last_request_sig` + `_apply_validated_value` chain
		 * (D16.9 validator parity). The DE-side result is
		 * emitted on the DE-side `_de_result_reporter` so the
		 * SettingsController's `de_config_result` ROM receives
		 * it.
		 */
		Genode::Signal_handler<Main> _de_request_handler {
			_env.ep(), *this, &Main::_handle_de_request };

		/*
		 * Lazy-construct the DE-side ROM + reporter + wire the
		 * signal handler on first use. Called from
		 * `_handle_de_request`'s first invocation. The check
		 * is cheap (a constructed() flag) and idempotent.
		 */
		void _ensure_de_channel();

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
		/*
		 * STORE_BUF is the on-disk XML store size cap. Phase 16 W2
		 * widens the registry to 14 flat + up to 32 instantiated
		 * pattern keys; the shortcuts.bindings default alone is ~46
		 * bytes. 16 KiB leaves plenty of headroom for the binding
		 * list and future keys; a torn / oversized file is detected
		 * by the loader (logs a warning, starts with defaults).
		 */
		static Genode::size_t const STORE_BUF;

		Genode::Heap                                _heap     { _env.ram(), _env.rm() };
		Genode::Constructible<Genode::Vfs::Root>     _vfs_env { };

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

		/* Current value per flat-key index. Defaults are applied in the
		 * constructor so the store is never empty. */
		Genode::String<128> _values[MAX_KEYS] { };

		/*
		 * Instantiated pattern-key entries (Phase 16 W2). On first
		 * write of a per-id name that matches a pattern template, the
		 * template is cloned into _instantiated[] with the actual
		 * name (e.g. "panel.alpha.height") and the cloned validator
		 * is run on the value. _num_instantiated counts the live
		 * entries; _instantiated_names[] owns the per-id strings
		 * (the clone's `name` field points into the parallel
		 * _instantiated_names[] buffer so the broadcast, the store,
		 * and subsequent lookups all see a stable pointer — copying
		 * the caller's stack pointer would yield a dangling read).
		 */
		Key_def             _instantiated[MAX_PATTERN_KEYS] { };
		unsigned            _num_instantiated               { 0 };
		Genode::String<128> _instantiated_names [MAX_PATTERN_KEYS] { };
		Genode::String<128> _instantiated_values[MAX_PATTERN_KEYS] { };

		/* ---- request handling ---- */
		void _handle_request();
		void _handle_de_request();
		void _handle_request_impl(Genode::Xml_node const &req);
		void _do_get(char const *key);
		void _do_set(char const *key, char const *value);
		void _do_list();

		/* ---- registry helpers ---- */
		bool _find_key(char const *key, unsigned &idx) const;
		bool _find_instantiated(char const *key, unsigned &idx) const;

		/*
		 * Match a key against the pattern templates; on match,
		 * allocate an instantiated slot, clone the template's Key_def
		 * fields into it (rewriting `name` to the actual per-id key),
		 * and return the slot index via `idx`. Returns false on no
		 * template match, on a charset violation, or when no slot is
		 * free (the registry is full). Charset validation runs BEFORE
		 * any value work, so a charset-fail leaves the store untouched.
		 */
		bool _instantiate_pattern(char const *key, unsigned &idx,
		                         Genode::String<256> &why);

		bool _value_valid(Key_def const &d, char const *value,
		                  Genode::String<256> &why) const;

		/* ---- structured validator (shortcuts.bindings) ---- */
		bool _shortcuts_valid(char const *value,
		                      Genode::String<256> &why) const;

		/*
		 * Closest-known-key suggestion for typo errors (F15 validator
		 * parity). Walks the flat registry AND the instantiated
		 * pattern set (read-only — never the templates, which have
		 * no concrete name to suggest). Returns the candidate name
		 * with the smallest Levenshtein distance; an empty string if
		 * every candidate is too far away to suggest.
		 */
		Genode::String<128> _suggest_similar(char const *key) const;

		/* ---- deterministic name-sorted index order ---- */
		void _sorted_order_flat(unsigned *order) const;
		void _sorted_order_instantiated(unsigned *order) const;

		/* ---- output ---- */
		void _generate_broadcast();
		void _report_get_ok(char const *key, char const *value);
		void _report_set_ok(char const *key, char const *value);
		void _report_list_ok();
		void _report_error(char const *op, char const *key, char const *message);

		/*
		 * Phase 16 W4 (D16.1) — dual-label result reporter
		 * selector. The active channel (vct or DE-side) chooses
		 * which `Expanding_reporter` carries the next result
		 * (mirrors pkgd's `_result()` selector at
		 * `sponge_pkgd/main.cc:441`). The member function is
		 * referenced from `_report_*` and the entry-point
		 * `_handle_request` / `_handle_de_request` set the
		 * flag before dispatching.
		 */
		Genode::Expanding_reporter &_active_result_reporter();

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
	/*
	 * Phase 16 W2 additions: background.color (hex #RRGGBB),
	 * background.image (allowlist of staged paths — D16.10's F10 path
	 * traversal defense), shortcuts.bindings (the structured
	 * multi-line value, validated by _shortcuts_valid), panel.ids
	 * (the comma-list of instantiated panel <id>s).
	 */
	{ "background.color",      false,
	  { }, 0, "#1e1e2e", Sponge::Configd::Main::Key_def::Kind::HexColor, 0, 0,
	  { }, 0 },
	{ "background.image",      false,
	  { }, 0, "/system/background/default.png",
	  Sponge::Configd::Main::Key_def::Kind::Allowlist, 0, 0,
	  { "/system/background/default.png" }, 1 },
	{ "bake.applied",          true,
	  { "yes", "no" }, 2, "no", Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0,
	  { }, 0 },
	{ "bake.profile",          false,
	  { }, 0, "none", Sponge::Configd::Main::Key_def::Kind::String, 0, 0,
	  { }, 0 },
	{ "bake.version",          false,
	  { }, 0, "0", Sponge::Configd::Main::Key_def::Kind::UintRange, 0, ~0U,
	  { }, 0 },
	{ "clock.format",          false,
	  { }, 0, "HH:mm", Sponge::Configd::Main::Key_def::Kind::FormatString, 0, 0,
	  { }, 0 },
	{ "leitzentrale.enabled",  true,
	  { "true", "false" }, 2, "false", Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0,
	  { }, 0 },
	{ "launcher.sort_by",      true,
	  { "manual", "alpha" }, 2, "alpha", Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0,
	  { }, 0 },
	{ "panel.height",          false,
	  { }, 0, "28", Sponge::Configd::Main::Key_def::Kind::UintRange, 16, 128,
	  { }, 0 },
	{ "panel.ids",             false,
	  { }, 0, "", Sponge::Configd::Main::Key_def::Kind::CommaList, 0, 0,
	  { }, 0 },
	{ "panel.position",        true,
	  { "top", "bottom", "left", "right" }, 4, "bottom",
	  Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0, { }, 0 },
	/*
	 * panel.visible_widgets enum-list extended to {clock, launcher,
	 * tasklist} per W2 #7. The "tasklist" token is the Phase 14 W7
	 * panel-side parse addition; the configd-side validator did NOT
	 * accept it before this commit (F15-style latent bug — the panel
	 * widget parsed the token locally but a configd round-trip of
	 * "tasklist" would have been rejected). The panel_widget.cc:311-313
	 * stale comment is corrected in the same work package.
	 */
	{ "panel.visible_widgets", false,
	  { "clock", "launcher", "tasklist" }, 3, "clock,launcher",
	  Sponge::Configd::Main::Key_def::Kind::EnumList, 0, 0, { }, 0 },
	{ "shortcuts.bindings",    false,
	  { }, 0, "launcher\tSuper\nfocus_next\tAlt-Tab\ndismiss\tEscape",
	  Sponge::Configd::Main::Key_def::Kind::Shortcuts, 0, 0, { }, 0 },
	{ "theme.active",          false,
	  { }, 0, "light", Sponge::Configd::Main::Key_def::Kind::String, 0, 0,
	  { }, 0 },
};

unsigned const Sponge::Configd::Main::_num_keys = 14;


/*
 * Pattern templates (Phase 16 W2, U16.5/D16.5/D16.10). Each template
 * declares a per-id key shape; the placeholder "<id>" in `name` is
 * replaced at instantiation time with the actual id. The Kind and
 * default / range / enum-list fields are the same as the flat
 * counterparts — a successful instantiation clones them verbatim, so
 * the cloned validator runs the same logic as the flat key.
 */
Sponge::Configd::Main::Key_def const Sponge::Configd::Main::_pattern_registry[MAX_PATTERN_KEYS] = {
	{ "panel.<id>.height",           false,
	  { }, 0, "28", Sponge::Configd::Main::Key_def::Kind::UintRange, 16, 128,
	  { }, 0 },
	{ "panel.<id>.position",         true,
	  { "top", "bottom", "left", "right" }, 4, "bottom",
	  Sponge::Configd::Main::Key_def::Kind::Enum, 0, 0, { }, 0 },
	{ "panel.<id>.visible_widgets",  false,
	  { "clock", "launcher", "tasklist" }, 3, "clock,launcher",
	  Sponge::Configd::Main::Key_def::Kind::EnumList, 0, 0, { }, 0 },
};

unsigned const Sponge::Configd::Main::_num_pattern_templates = 3;


/* ===================== registry helpers ===================== */

/*
 * Validate a single character against the panel <id> charset
 * [a-z0-9_-]. The id may be 1..16 chars long; the placeholder "<id>"
 * in pattern templates is replaced wholesale, no sub-segmentation.
 */
static bool _id_charset_ok(char c)
{
	return (c >= 'a' && c <= 'z')
	    || (c >= '0' && c <= '9')
	    || c == '_' || c == '-';
}


/*
 * Match a key against a pattern template's SHAPE (prefix + suffix)
 * and, on a shape match, extract the <id> segment. Returns true on
 * full match with a valid <id>; fills `id_out` with the extracted
 * segment. Returns false on either a shape miss (key doesn't fit
 * this template) OR a charset violation (the key looks like this
 * template but the <id> segment isn't [a-z0-9_-]{1,16}).
 *
 * The two failure modes need to be distinguished by the caller
 * (Phase 16 W2 #3 step b): a shape miss means "key is unknown",
 * a charset fail means "key looks like a pattern key but the id is
 * invalid". The charset-fail message names the charset rule so the
 * user sees what went wrong.
 *
 * The lookup strategy is split into two stages so a charset fail
 * does not silently look like "unknown key" to the caller:
 *
 *   _pattern_shape_match() — returns true if the key's prefix/suffix
 *                            fit ANY template (used by the caller
 *                            to distinguish "shape match but
 *                            charset bad" from "no shape match").
 *
 *   _pattern_extract_id()  — extracts the <id> segment from a
 *                            known-matching template (charset
 *                            already validated here).
 */
static bool _pattern_shape_match(char const *key)
{
	for (unsigned t = 0; t < 3; ++t) {
		char const *suffix = nullptr;
		switch (t) {
		case 0: suffix = ".height";          break;
		case 1: suffix = ".position";        break;
		case 2: suffix = ".visible_widgets"; break;
		}
		Genode::size_t const suffix_len = Genode::strlen(suffix);
		Genode::size_t const key_len    = Genode::strlen(key);
		if (key_len <= suffix_len) continue;
		char const *prefix = "panel.";
		Genode::size_t const prefix_len = Genode::strlen(prefix);
		if (Genode::strcmp(key, prefix, prefix_len) != 0) continue;
		if (Genode::strcmp(key + key_len - suffix_len, suffix, suffix_len) != 0) continue;
		return true;
	}
	return false;
}


static bool _pattern_extract_id(char const *key, Genode::String<32> &id_out)
{
	char const *suffixes[] = { ".height", ".position", ".visible_widgets" };
	for (unsigned t = 0; t < 3; ++t) {
		Genode::size_t const suffix_len = Genode::strlen(suffixes[t]);
		Genode::size_t const key_len    = Genode::strlen(key);
		if (key_len <= suffix_len) continue;
		char const *prefix = "panel.";
		Genode::size_t const prefix_len = Genode::strlen(prefix);
		if (Genode::strcmp(key, prefix, prefix_len) != 0) continue;
		if (Genode::strcmp(key + key_len - suffix_len, suffixes[t], suffix_len) != 0) continue;

		Genode::size_t const id_len = key_len - prefix_len - suffix_len;
		if (id_len == 0 || id_len > 16) return false;
		for (Genode::size_t i = 0; i < id_len; ++i)
			if (!_id_charset_ok(key[prefix_len + i])) return false;

		char buf[17] { };
		for (Genode::size_t i = 0; i < id_len; ++i)
			buf[i] = key[prefix_len + i];
		id_out = Genode::String<32>(buf);
		return true;
	}
	return false;
}


/*
 * Flat-registry lookup. Walks the static Key_def array in
 * name-sorted order; returns the registry index on match, false
 * otherwise. Pattern templates are checked separately by
 * _instantiate_pattern().
 */
bool Sponge::Configd::Main::_find_key(char const *key, unsigned &idx) const
{
	for (unsigned i = 0; i < _num_keys; ++i)
		if (Genode::strcmp(_registry[i].name, key) == 0) {
			idx = i;
			return true;
		}
	return false;
}


/*
 * Look up an instantiated pattern entry. Walked AFTER the flat
 * registry; only concrete per-id names appear here (the templates
 * never do — they live in _pattern_registry[] and are cloned into
 * _instantiated[] on first write).
 */
bool Sponge::Configd::Main::_find_instantiated(char const *key, unsigned &idx) const
{
	for (unsigned i = 0; i < _num_instantiated; ++i)
		if (Genode::strcmp(_instantiated[i].name, key) == 0) {
			idx = i;
			return true;
		}
	return false;
}


/*
 * Pattern-key instantiation (Phase 16 W2 #3). Match `key` against
 * the templates; on match:
 *   (a) the <id> segment has already been validated by
 *       _pattern_extract_id (charset + length);
 *   (b) an instantiated slot is allocated (or the existing one
 *       returned, so re-writing the same per-id key is idempotent);
 *   (c) the template's Key_def fields are cloned into the slot, with
 *       `name` rewritten to the actual per-id key (so the broadcast
 *       and the persistent store see "panel.alpha.height", not
 *       "panel.<id>.height");
 *   (d) on charset / full-registry failure, return false with a
 *       structured `why` — the caller does NOT touch the value.
 *
 * Synchronous write-time instantiation (the plan's W2 #3 timing
 * rule): the broadcast and the store only see instantiated names;
 * a read of an uninstantiated template name finds nothing.
 */
bool Sponge::Configd::Main::_instantiate_pattern(char const *key, unsigned &idx,
                                                 Genode::String<256> &why)
{
	if (!_pattern_shape_match(key)) {
		/*
		 * The key fits no panel.<id>.<suffix> shape. Genuinely
		 * unknown — caller falls through to the F15 suggestion path.
		 */
		return false;
	}

	Genode::String<32> id_segment;
	if (!_pattern_extract_id(key, id_segment)) {
		/*
		 * The key has the right shape but the <id> segment is
		 * invalid (charset or length). The plan W2 #3 step b
		 * requires the structured error to mention the charset
		 * rule; emit it now and refuse to touch the value.
		 */
		why = Genode::String<256>("invalid <id> in key '", Genode::String<128>(key),
		                          "' (expected charset [a-z0-9_-]{1,16})");
		return false;
	}

	/*
	 * Re-writing an already-instantiated per-id key is a no-op
	 * at the structure level — return the existing slot so the
	 * caller can run the cloned validator on the new value.
	 */
	for (unsigned i = 0; i < _num_instantiated; ++i)
		if (Genode::strcmp(_instantiated[i].name, key) == 0) {
			idx = i;
			return true;
		}

	if (_num_instantiated >= MAX_PATTERN_KEYS) {
		why = Genode::String<256>("pattern-key registry full for key '",
		                          Genode::String<128>(key),
		                          "' (MAX_PATTERN_KEYS=", MAX_PATTERN_KEYS, ")");
		return false;
	}

	/*
	 * Find the template by suffix (the SHAPE-match above guarantees
	 * exactly one template fits) and clone its Key_def fields into
	 * the instantiated slot.
	 */
	char const *suffix_match = nullptr;
	unsigned    t_match      = 0;
	for (unsigned t = 0; t < _num_pattern_templates; ++t) {
		char const *suffix = nullptr;
		switch (t) {
		case 0: suffix = ".height";          break;
		case 1: suffix = ".position";        break;
		case 2: suffix = ".visible_widgets"; break;
		}
		Genode::size_t const suffix_len = Genode::strlen(suffix);
		Genode::size_t const key_len    = Genode::strlen(key);
		if (Genode::strcmp(key + key_len - suffix_len, suffix, suffix_len) == 0) {
			suffix_match = suffix;
			t_match = t;
			break;
		}
	}
	(void)suffix_match;
	Key_def const &tmpl = _pattern_registry[t_match];

	Key_def clone { };
	clone.is_enum       = tmpl.is_enum;
	for (unsigned i = 0; i < tmpl.num_enums; ++i)
		clone.enum_values[i] = tmpl.enum_values[i];
	clone.num_enums      = tmpl.num_enums;
	clone.default_value  = tmpl.default_value;
	clone.kind           = tmpl.kind;
	clone.min_value      = tmpl.min_value;
	clone.max_value      = tmpl.max_value;
	for (unsigned i = 0; i < tmpl.num_allowed; ++i)
		clone.allowed_paths[i] = tmpl.allowed_paths[i];
	clone.num_allowed    = tmpl.num_allowed;

	idx = _num_instantiated++;
	_instantiated[idx] = clone;
	/*
	 * Copy the per-id name into the parallel names array so the
	 * clone's `name` field can point to a stable, owned buffer
	 * (otherwise the pointer references the caller's stack frame
	 * and is undefined to read once _handle_request returns).
	 */
	_instantiated_names[idx] = Genode::String<128>(key);
	_instantiated[idx].name  = _instantiated_names[idx].string();
	return true;
}


/*
 * Structured shortcuts validator (Phase 16 W2, U16.4/D16.5/D16.10).
 * The value is a multi-line "action<TAB>key_sequence" list. The
 * validator walks every line; if any line fails, the whole value
 * fails (the key is stored verbatim when ALL lines pass — there is
 * no partial-success path). Every rejected line emits
 * Genode::warning AND appears in the structured error.
 *
 * Per-line grammar:
 *   line        := action '\t' sequence '\n'
 *   action      := 'launcher' | 'focus_next' | 'dismiss'
 *   sequence    := keyname ('-' keyname)*
 *   keyname     := one of Genode::Input::KEY_* (e.g. KEY_TAB)
 *
 * The validator uses Genode::Input::key_code() to look up the
 * keycode; KEY_UNKNOWN is the rejection signal.
 */
bool Sponge::Configd::Main::_shortcuts_valid(char const *value,
                                             Genode::String<256> &why) const
{
	/*
	 * Closed action-token enum. The exact list is part of the wire
	 * contract: every shortcuts binding declares an action from this
	 * set. Adding a new action token requires extending this enum
	 * AND the key_event subscriber code that dispatches it (Phase 17
	 * scope).
	 */
	struct Action { char const *name; };
	static Action const actions[] = {
		{ "launcher" }, { "focus_next" }, { "dismiss" }
	};
	static constexpr unsigned num_actions =
		sizeof(actions) / sizeof(actions[0]);

	Genode::size_t const length = Genode::strlen(value);
	Genode::size_t line_start { 0 };
	unsigned line_no { 0 };

	while (line_start < length) {
		Genode::size_t line_end = line_start;
		while (line_end < length && value[line_end] != '\n') ++line_end;
		++line_no;

		Genode::size_t first = line_start;
		Genode::size_t last  = line_end;
		while (first < last && (value[first] == ' ' || value[first] == '\t' ||
		       value[first] == '\r')) ++first;
		while (last > first && (value[last - 1] == ' ' ||
		       value[last - 1] == '\t' || value[last - 1] == '\r')) --last;

		if (first == last) {
			why = Genode::String<256>("empty line ",
			                          Genode::String<8>(line_no),
			                          " in shortcuts.bindings");
			Genode::warning("sponge_configd: shortcuts.bindings: ", why);
			return false;
		}

		/*
		 * Find the TAB that separates action from key sequence. There
		 * must be exactly one TAB on the line; lines with zero or
		 * multiple TABs are rejected.
		 */
		Genode::size_t tab_count { 0 };
		Genode::size_t tab_pos { 0 };
		for (Genode::size_t i = first; i < last; ++i)
			if (value[i] == '\t') {
				++tab_count;
				if (tab_count == 1) tab_pos = i;
			}
		if (tab_count != 1) {
			why = Genode::String<256>("line ", Genode::String<8>(line_no),
			                          " of shortcuts.bindings must have exactly one TAB "
			                          "between action and key sequence");
			Genode::warning("sponge_configd: shortcuts.bindings: ", why);
			return false;
		}

		char action[32] { };
		Genode::size_t const action_len = tab_pos - first;
		if (action_len == 0 || action_len >= sizeof(action)) {
			why = Genode::String<256>("missing action token on line ",
			                          Genode::String<8>(line_no),
			                          " of shortcuts.bindings");
			Genode::warning("sponge_configd: shortcuts.bindings: ", why);
			return false;
		}
		for (Genode::size_t i = 0; i < action_len; ++i)
			action[i] = value[first + i];

		bool action_ok { false };
		for (unsigned i = 0; i < num_actions; ++i)
			if (Genode::strcmp(action, actions[i].name) == 0) {
				action_ok = true;
				break;
			}
		if (!action_ok) {
			why = Genode::String<256>("unknown action token '",
			                          Genode::String<32>(action),
			                          "' on line ", Genode::String<8>(line_no),
			                          " of shortcuts.bindings (expected: launcher, "
			                          "focus_next, dismiss)");
			Genode::warning("sponge_configd: shortcuts.bindings: ", why);
			return false;
		}

		Genode::size_t const seq_first = tab_pos + 1;
		Genode::size_t const seq_last  = last;
		if (seq_first > seq_last) {
			why = Genode::String<256>("missing key sequence on line ",
			                          Genode::String<8>(line_no),
			                          " of shortcuts.bindings");
			Genode::warning("sponge_configd: shortcuts.bindings: ", why);
			return false;
		}

		/*
		 * Validate the key sequence: split on '-', each token must
		 * resolve via Genode::Input::key_code() to a non-KEY_UNKNOWN
		 * value. A small synonym table closes the gap between the
		 * conventional Linux/X11 names users expect (Super, Meta,
		 * Esc, Tab, Alt, Ctrl, Shift, ...) and the canonical
		 * Genode enum names (KEY_TAB, KEY_ESC, KEY_LEFTALT, ...).
		 * Without it the shipped "launcher\tSuper" / "focus_next\
		 * tAlt-Tab" bindings would be rejected by the validator
		 * even though the Genode keycodes exist (just under their
		 * canonical names).
		 */
		struct Synonym { char const *alias; char const *canonical; };
		static Synonym const synonyms[] = {
			{ "Super",     "KEY_LEFTMETA" },
			{ "Meta",      "KEY_LEFTMETA" },
			{ "MetaL",     "KEY_LEFTMETA" },
			{ "MetaR",     "KEY_RIGHTMETA"},
			{ "Esc",       "KEY_ESC"      },
			{ "Escape",    "KEY_ESC"      },
			{ "Tab",       "KEY_TAB"      },
			{ "Return",    "KEY_ENTER"    },
			{ "Enter",     "KEY_ENTER"    },
			{ "Backspace", "KEY_BACKSPACE"},
			{ "Alt",       "KEY_LEFTALT"  },
			{ "AltL",      "KEY_LEFTALT"  },
			{ "AltR",      "KEY_RIGHTALT" },
			{ "Ctrl",      "KEY_LEFTCTRL" },
			{ "CtrlL",     "KEY_LEFTCTRL" },
			{ "Shift",     "KEY_LEFTSHIFT"},
			{ "Space",     "KEY_SPACE"    },
		};

		Genode::size_t ks = seq_first;
		while (ks < seq_last) {
			Genode::size_t ke = ks;
			while (ke < seq_last && value[ke] != '-') ++ke;

			char keyname[32] { };
			Genode::size_t const kn_len = ke - ks;
			if (kn_len == 0 || kn_len >= sizeof(keyname)) {
				why = Genode::String<256>("empty key in sequence on line ",
				                          Genode::String<8>(line_no),
				                          " of shortcuts.bindings");
				Genode::warning("sponge_configd: shortcuts.bindings: ", why);
				return false;
			}
			for (Genode::size_t i = 0; i < kn_len; ++i)
				keyname[i] = value[ks + i];

			char const *resolved = keyname;
			for (unsigned s = 0;
			     s < sizeof(synonyms) / sizeof(synonyms[0]); ++s)
				if (Genode::strcmp(keyname, synonyms[s].alias) == 0) {
					resolved = synonyms[s].canonical;
					break;
				}

			Genode::String<22> const kn { resolved };
			if (Input::key_code(kn) == Input::KEY_UNKNOWN) {
				why = Genode::String<256>("unknown key '",
				                          Genode::String<32>(keyname),
				                          "' on line ", Genode::String<8>(line_no),
				                          " of shortcuts.bindings (expected: "
				                          "Genode Input-event key name like KEY_TAB)");
				Genode::warning("sponge_configd: shortcuts.bindings: ", why);
				return false;
			}

			if (ke == seq_last) break;
			ks = ke + 1;
		}

		line_start = line_end + 1;
	}

	if (line_no == 0) {
		why = Genode::String<256>("shortcuts.bindings value is empty");
		Genode::warning("sponge_configd: shortcuts.bindings: ", why);
		return false;
	}
	return true;
}


/*
 * Levenshtein-distance-based suggestion for typo errors (F15
 * validator parity, Phase 16 W2 #6). Walks every concrete known key
 * (flat + instantiated pattern entries — never templates, which
 * have no concrete name to suggest) and returns the candidate with
 * the smallest edit distance, capped at 3 (anything further is
 * probably deliberate). An empty string means "no close-enough
 * match".
 */
Genode::String<128> Sponge::Configd::Main::_suggest_similar(char const *key) const
{
	auto lev = [] (char const *a, char const *b) -> unsigned {
		Genode::size_t const la = Genode::strlen(a);
		Genode::size_t const lb = Genode::strlen(b);
		unsigned prev[128] { };
		unsigned curr[128] { };
		for (Genode::size_t i = 0; i <= lb; ++i) prev[i] = (unsigned)i;
		for (Genode::size_t i = 1; i <= la; ++i) {
			curr[0] = (unsigned)i;
			for (Genode::size_t j = 1; j <= lb; ++j) {
				unsigned sub = prev[j - 1]
				             + (a[i - 1] == b[j - 1] ? 0u : 1u);
				unsigned ins = curr[j - 1] + 1;
				unsigned del = prev[j]     + 1;
				unsigned best = sub;
				if (ins < best) best = ins;
				if (del < best) best = del;
				curr[j] = best;
			}
			for (Genode::size_t j = 0; j <= lb; ++j) prev[j] = curr[j];
		}
		return prev[lb];
	};

	Genode::String<128> best { };
	unsigned best_d = ~0U;

	auto consider = [&] (char const *candidate) {
		unsigned const d = lev(key, candidate);
		if (d < best_d) {
			best_d = d;
			best = Genode::String<128>(candidate);
		}
	};

	for (unsigned i = 0; i < _num_keys; ++i)
		consider(_registry[i].name);
	for (unsigned i = 0; i < _num_instantiated; ++i)
		consider(_instantiated[i].name);

	if (best_d > 3) return Genode::String<128>();
	return best;
}


bool Sponge::Configd::Main::_value_valid(Key_def const &d, char const *value,
                                         Genode::String<256> &why) const
{
	if (Genode::strcmp(value, "") == 0) {
		char const *const expected = d.kind == Key_def::Kind::EnumList
		                           ? "non-empty comma-separated list"
		                           : d.kind == Key_def::Kind::FormatString
		                           ? "non-empty printable ASCII string"
		                           : d.kind == Key_def::Kind::Shortcuts
		                           ? "non-empty multi-line binding list"
		                           : d.kind == Key_def::Kind::CommaList
		                           ? "non-empty comma-separated list"
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

	/*
	 * HexColor (Phase 16 W2): a #RRGGBB hex literal (7 chars, '#' +
	 * 6 hex digits). Any other length, missing '#', or non-hex char
	 * is rejected. The value is what the in-DE background widget
	 * paints; it's surfaced to the GUI via the configd broadcast and
	 * should round-trip exactly.
	 */
	if (d.kind == Key_def::Kind::HexColor) {
		Genode::size_t const length = Genode::strlen(value);
		if (length != 7 || value[0] != '#') {
			why = Genode::String<256>("invalid value '", Genode::String<128>(value),
			                          "' for key '", d.name,
			                          "' (expected: #RRGGBB hex literal, 7 characters)");
			return false;
		}
		for (Genode::size_t i = 1; i < 7; ++i) {
			char const c = value[i];
			bool const ok = (c >= '0' && c <= '9')
			             || (c >= 'a' && c <= 'f')
			             || (c >= 'A' && c <= 'F');
			if (!ok) {
				why = Genode::String<256>("invalid value '", Genode::String<128>(value),
				                          "' for key '", d.name,
				                          "' (expected: #RRGGBB hex literal, "
				                          "digits 0-9 a-f A-F)");
				return false;
			}
		}
		return true;
	}

	/*
	 * Allowlist (Phase 16 W2): the value must be one of the closed
	 * set of paths declared in Key_def::allowed_paths (the F10 path
	 * traversal defense — a writable allowlist is Phase 17+, per
	 * docs/13 Known Limitations).
	 */
	if (d.kind == Key_def::Kind::Allowlist) {
		for (unsigned i = 0; i < d.num_allowed; ++i)
			if (Genode::strcmp(value, d.allowed_paths[i]) == 0)
				return true;

		Genode::String<256> listing;
		for (unsigned i = 0; i < d.num_allowed; ++i) {
			if (i > 0) listing = Genode::String<256>(listing, ", ");
			listing = Genode::String<256>(listing, d.allowed_paths[i]);
		}
		why = Genode::String<256>("invalid value '", Genode::String<128>(value),
		                          "' for key '", d.name,
		                          "' (allowed: ",
		                          listing, ")");
		return false;
	}

	/*
	 * CommaList (Phase 16 W2, panel.ids): a non-empty comma-separated
	 * list of tokens that each match the charset [a-z0-9_-]{1,16}
	 * (the panel <id> charset, reused for the comma-list tokens so
	 * "panel.ids=alpha,beta" round-trips through every write).
	 */
	if (d.kind == Key_def::Kind::CommaList) {
		Genode::size_t const length = Genode::strlen(value);
		Genode::size_t start { 0 };
		while (start <= length) {
			Genode::size_t end = start;
			while (end < length && value[end] != ',') ++end;

			Genode::size_t first = start;
			Genode::size_t last  = end;
			while (first < last && (value[first] == ' ' || value[first] == '\t')) ++first;
			while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t')) --last;

			Genode::size_t const tok_len = last - first;
			if (tok_len == 0 || tok_len > 16) {
				why = Genode::String<256>("invalid token length in list for key '",
				                          d.name, "' (expected: each token 1..16 chars)");
				return false;
			}
			for (Genode::size_t i = 0; i < tok_len; ++i) {
				char const c = value[first + i];
				bool const ok = (c >= 'a' && c <= 'z')
				             || (c >= '0' && c <= '9')
				             || c == '_' || c == '-';
				if (!ok) {
					why = Genode::String<256>("invalid character '",
					                          Genode::String<8>(c),
					                          "' in list for key '", d.name,
					                          "' (expected: [a-z0-9_-]{1,16})");
					return false;
				}
			}

			if (end == length) break;
			start = end + 1;
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

	/*
	 * Shortcuts (Phase 16 W2, U16.4/D16.5): the value is a multi-line
	 * "action<TAB>key_sequence" list. The full per-line validator
	 * lives in _shortcuts_valid() to keep _value_valid readable; here
	 * we just dispatch.
	 */
	if (d.kind == Key_def::Kind::Shortcuts)
		return _shortcuts_valid(value, why);

	why = Genode::String<256>("invalid value '", Genode::String<128>(value),
	                          "' for key '", d.name,
	                          "' (expected: registered validation kind)");
	return false;
}


void Sponge::Configd::Main::_sorted_order_flat(unsigned *order) const
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


void Sponge::Configd::Main::_sorted_order_instantiated(unsigned *order) const
{
	for (unsigned i = 0; i < _num_instantiated; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_instantiated; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_instantiated; ++j)
			if (Genode::strcmp(_instantiated[order[j]].name,
			                   _instantiated[order[best]].name) < 0)
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
	/*
	 * The bake path applies a list of validated writes from a manifest.
	 * It walks the flat registry first, then the pattern templates
	 * (which clone-and-instantiate on the first match — so a baked
	 * "panel.<id>.height" line seeds the same validator + value slot
	 * as a user write). Unknown keys are skipped with a warning, never
	 * silently accepted.
	 */
	unsigned idx { 0 };
	if (_find_key(key, idx)) {
		Genode::String<256> why { };
		if (!_value_valid(_registry[idx], value, why)) {
			Genode::warning("sponge_configd: ", source, " skipped ", why);
			return false;
		}
		_values[idx] = Genode::String<128>(value);
		return true;
	}

	Genode::String<256> why { };
	unsigned inst_idx { 0 };
	if (_instantiate_pattern(key, inst_idx, why)) {
		if (!_value_valid(_instantiated[inst_idx], value, why)) {
			Genode::warning("sponge_configd: ", source, " skipped ", why);
			return false;
		}
		_instantiated_values[inst_idx] = Genode::String<128>(value);
		return true;
	}

	Genode::warning("sponge_configd: ", source, " skipped unknown key '", key, "'");
	return false;
}


/*
 * Phase 16 W4 (D16.1): The active channel flag selects which
 * result reporter to use. vct's `_handle_request` sets it to
 * `Result_channel::vct`; the DE-side `_handle_de_request` sets it
 * to `Result_channel::de_side`. The shared _do_get / _do_set /
 * _do_list / _report_* helpers then route their <result> XML to
 * the matching reporter (mirrors the pkgd launcher pattern in
 * repos/sponge/src/sponge_pkgd/main.cc:441-445).
 *
 * This flag is entry-point thread-local state. Only one
 * signal-handler invocation runs at a time (Genode's entrypoint
 * dispatcher serialises them), so a plain member is safe.
 */
enum class Result_channel { vct, de_side };

Result_channel _active_channel { Result_channel::vct };

Genode::Expanding_reporter &Sponge::Configd::Main::_active_result_reporter()
{
	return _active_channel == Result_channel::de_side
	     ? *_de_result_reporter : _result_reporter;
}


/*
 * Lazy-construct the DE-side channel (ROM + result reporter +
 * wire the signal handler). Called from `_handle_de_request` on
 * first invocation.
 */
void Sponge::Configd::Main::_ensure_de_channel()
{
	if (_de_request_rom.constructed())
		return;

	_de_request_rom.construct(_env, "de_config_request");
	_de_request_rom->sigh(_de_request_handler);
	_de_request_rom->update();

	_de_result_reporter.construct(_env, "result", "de_config_result");

	Genode::log("sponge_configd: de_config_request / de_config_result "
	            "channel live (D16.1 settings dialog writes)");
}


void Sponge::Configd::Main::_handle_request()
{
	_request_rom.update();

	if (!_request_rom.valid())
		return;

	_active_channel = Result_channel::vct;
	_handle_request_impl(_request_rom.xml());
}


void Sponge::Configd::Main::_handle_de_request()
{
	_ensure_de_channel();

	_de_request_rom->update();

	if (!_de_request_rom->valid())
		return;

	_active_channel = Result_channel::de_side;
	_handle_request_impl(_de_request_rom->xml());
}


/*
 * Shared body of both channel handlers. Mirrors pkgd's
 * `_handle_request_impl` (sponge_pkgd/main.cc). Validator parity is
 * enforced here (D16.9) — every channel's write goes through the
 * same `_apply_validated_value` chain.
 */
void Sponge::Configd::Main::_handle_request_impl(Genode::Xml_node const &req)
{
	try {
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
	if (_find_key(key, idx)) {
		_report_get_ok(key, _values[idx].string());
		return;
	}
	if (_find_instantiated(key, idx)) {
		_report_get_ok(key, _instantiated_values[idx].string());
		return;
	}

	Genode::String<128> const suggest = _suggest_similar(key);
	Genode::String<256> msg { "unknown key: ", Genode::String<128>(key) };
	if (suggest.length() > 0)
		msg = Genode::String<256>(msg, " (suggested: ", suggest, ")");
	_report_error("get", key, msg.string());
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

	/*
	 * Resolve the key to a concrete Key_def (flat or instantiated
	 * pattern). The flat lookup runs first; on miss the pattern
	 * template scan runs and, on match, instantiates the per-id
	 * slot synchronously before the value validator runs. Three
	 * failure modes are distinguished:
	 *   - charset violation (key shape matches a template but the
	 *     <id> segment isn't [a-z0-9_-]{1,16}) → the `why` from
	 *     _instantiate_pattern already names the charset rule.
	 *   - registry full (template matched but no instantiated slot)
	 *     → the `why` names the limit.
	 *   - genuinely unknown (no template shape matches) → F15
	 *     suggestion path surfaces the closest known key.
	 */
	unsigned idx { 0 };
	if (!_find_key(key, idx) && !_find_instantiated(key, idx)) {
		Genode::String<256> why { };
		if (!_instantiate_pattern(key, idx, why)) {
			/*
			 * Shape miss vs charset fail vs full registry: charset
			 * failures and full-registry errors carry a structured
			 * `why`; a shape miss (no template matched) falls
			 * through to the F15 suggestion path.
			 */
			if (why.length() > 0) {
				_report_error("set", key, why.string());
				return;
			}
			Genode::String<128> const suggest = _suggest_similar(key);
			Genode::String<256> msg { "unknown key: ", Genode::String<128>(key) };
			if (suggest.length() > 0)
				msg = Genode::String<256>(msg, " (suggested: ", suggest, ")");
			_report_error("set", key, msg.string());
			return;
		}
	}

	Key_def const &d = (idx < _num_instantiated)
	                  ? _instantiated[idx]
	                  : _registry[idx];

	Genode::String<256> why { };
	if (!_value_valid(d, value, why)) {
		_report_error("set", key, why.string());
		return;
	}

	if (idx < _num_instantiated)
		_instantiated_values[idx] = Genode::String<128>(value);
	else
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
 * name-sorted across flat + instantiated pattern entries (the
 * pattern templates NEVER appear — only the cloned per-id names do,
 * per the W2 #3 instantiation-timing rule). The root is "config"
 * (the reporter's node type) so a watcher reads it as a standard
 * config ROM. Deterministic: fixed attribute order, no volatile fields.
 */
void Sponge::Configd::Main::_generate_broadcast()
{
	unsigned flat_order[MAX_KEYS] { };
	_sorted_order_flat(flat_order);

	unsigned inst_order[MAX_PATTERN_KEYS] { };
	_sorted_order_instantiated(inst_order);

	_broadcast_reporter.generate_xml([&](Genode::Xml_generator &g) {
		for (unsigned n = 0; n < _num_keys; ++n) {
			unsigned const i = flat_order[n];
			g.node("key", [&] {
				g.attribute("name",  Genode::String<64>(_registry[i].name));
				g.attribute("value", _values[i]);
			});
		}
		for (unsigned n = 0; n < _num_instantiated; ++n) {
			unsigned const i = inst_order[n];
			g.node("key", [&] {
				g.attribute("name",  Genode::String<64>(_instantiated[i].name));
				g.attribute("value", _instantiated_values[i]);
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

	Genode::Vfs::File_system &vfs = _vfs_env->fs();

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
		Genode::Vfs::Vfs_handle::Read_result r
			{ Genode::Vfs::Vfs_handle::Read_error::DENIED };
		for (;;) {
			r = handle->read(Genode::Vfs::At { .pos = total },
			                 Genode::Byte_range_ptr(buf + total,
			                                        stat.size - total));
			if (r != Genode::Vfs::Vfs_handle::Read_error::RETRY)
				break;
			_vfs_env->io().commit_and_wait();
		}
		Genode::size_t const n = r.convert<Genode::size_t>(
			[](Genode::size_t bytes) { return bytes; },
			[](Genode::Vfs::Vfs_handle::Read_error) {
				return Genode::size_t(0); });
		if (n == 0) {
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
			if (Genode::strcmp(key.string(), "") == 0 ||
			    Genode::strcmp(val.string(), "") == 0)
				return;
			unsigned idx { 0 };
			if (_find_key(key.string(), idx)) {
				_values[idx] = val;
				++restored;
				return;
			}
			Genode::String<256> why { };
			if (_instantiate_pattern(key.string(), idx, why)) {
				_instantiated_values[idx] = val;
				++restored;
			}
		});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_configd: store is not valid XML — starting with defaults");
		for (unsigned i = 0; i < _num_keys; ++i) _values[i] = Genode::String<128>();
		_num_instantiated = 0;
		return;
	}

	Genode::log("sponge_configd: restored ", restored, " key(s) from store");
}


/*
 * Persist the flat + instantiated pattern entries to the store.
 * Output is name-sorted across both sets with a fixed attribute
 * order so the file is byte-stable for a given store (matching the
 * determinism contract of the broadcast generator). A failed write
 * is logged but never blocks the set: the in-memory state and the
 * broadcast still reflect the requested change, only the
 * across-reboot durability is lost for that one mutation.
 *
 * The <lz_diverged> mirrored key is NOT persisted — it is computed
 * live from the lz_model ROM on every broadcast and would just be
 * stale on the next boot.
 */
void Sponge::Configd::Main::_save_store()
{
	if (!_store_enabled()) return;

	unsigned flat_order[MAX_KEYS] { };
	_sorted_order_flat(flat_order);

	unsigned inst_order[MAX_PATTERN_KEYS] { };
	_sorted_order_instantiated(inst_order);

	char buf[STORE_BUF] { };
	Genode::size_t pos { 0 };
	auto append = [&buf, &pos](char const *s) {
		while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
	};
	append("<sponge-config version=\"1\">");
	for (unsigned i = 0; i < _num_keys; ++i) {
		unsigned const idx = flat_order[i];
		append("<entry name=\"");
		append(_registry[idx].name);
		append("\" value=\"");
		append(_values[idx].string());
		append("\"/>");
	}
	for (unsigned i = 0; i < _num_instantiated; ++i) {
		unsigned const idx = inst_order[i];
		append("<entry name=\"");
		append(_instantiated[idx].name);
		append("\" value=\"");
		append(_instantiated_values[idx].string());
		append("\"/>");
	}
	append("</sponge-config>");
	Genode::size_t const len = pos;

	Genode::Vfs::File_system &vfs = _vfs_env->fs();

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

	tmp_handle->ftruncate(len);

	{
		Genode::size_t off { 0 };
		bool ok { true };
		while (off < len) {
			Genode::Vfs::Vfs_handle::Write_result w
				{ Genode::Vfs::Vfs_handle::Write_error::DENIED };
			for (;;) {
				w = tmp_handle->write(Genode::Vfs::At { .pos = off },
				                      Genode::Const_byte_range_ptr(buf + off,
				                                                   len - off));
				if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
					break;
				_vfs_env->io().commit_and_wait();
			}
			Genode::size_t const n = w.convert<Genode::size_t>(
				[](Genode::size_t bytes) { return bytes; },
				[](Genode::Vfs::Vfs_handle::Write_error) {
					return Genode::size_t(0); });
			if (n == 0) { ok = false; break; }
			off += n;
		}

		while (tmp_handle->sync() == Genode::Vfs::Sync_result::RETRY)
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
	Genode::Expanding_reporter &reporter = _active_result_reporter();
	reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "get");
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("value",  Genode::String<128>(value));
	});
}


void Sponge::Configd::Main::_report_set_ok(char const *key, char const *value)
{
	Genode::Expanding_reporter &reporter = _active_result_reporter();
	reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "set");
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("value",  Genode::String<128>(value));
	});
}


void Sponge::Configd::Main::_report_list_ok()
{
	unsigned flat_order[MAX_KEYS] { };
	_sorted_order_flat(flat_order);

	unsigned inst_order[MAX_PATTERN_KEYS] { };
	_sorted_order_instantiated(inst_order);

	Genode::Expanding_reporter &reporter = _active_result_reporter();

	reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "list");
		g.attribute("count",  _num_keys + _num_instantiated);

		g.node("keys", [&] {
			for (unsigned n = 0; n < _num_keys; ++n) {
				unsigned const i = flat_order[n];
				g.node("key", [&] {
					g.attribute("name",  Genode::String<64>(_registry[i].name));
					g.attribute("value", _values[i]);
				});
			}
			for (unsigned n = 0; n < _num_instantiated; ++n) {
				unsigned const i = inst_order[n];
				g.node("key", [&] {
					g.attribute("name",  Genode::String<64>(_instantiated[i].name));
					g.attribute("value", _instantiated_values[i]);
				});
			}
		});
	});
}


void Sponge::Configd::Main::_report_error(char const *op, char const *key,
                                          char const *message)
{
	Genode::Expanding_reporter &reporter = _active_result_reporter();
	reporter.generate_xml([&](Genode::Xml_generator &g) {
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
Genode::size_t const Sponge::Configd::Main::STORE_BUF    = 16 * 1024;


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
	 * Phase 16 W4 (D16.1) — wire the DE-side `de_config_request` /
	 * `de_config_result` channel ONLY when the configd config ROM
	 * declares `<de_config/>`. The same config-gate pattern as
	 * `<lz_model/>` and `<bake/>`: scenarios that don't need DE-
	 * side writes (Phase 14 W6, Phase 15 W3, Phase 7 pkg-style
	 * scenarios without sponge-de) don't wire it and the
	 * underlying ROM session never opens. The phase-16 W4
	 * topologies (`sponge-de-*`) opt in.
	 *
	 * Reading the config ROM checks for `<de_config>` (an empty
	 * element, the same gate pattern as `<lz_model>`). When the
	 * gate is present, the DE-side ROM is opened and the signal
	 * handler is wired before any request is dispatched.
	 */
	_config_rom.update();
	bool       enable_de_channel = false;
	try {
		_config_rom.node().with_optional_sub_node("de_config",
			[&] (Genode::Node const &) { enable_de_channel = true; });
	} catch (Genode::Xml_node::Invalid_syntax) {
		/* malformed config ROM — leave the gate off */
	}
	if (enable_de_channel) {
		_ensure_de_channel();
	}

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
