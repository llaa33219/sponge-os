/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_validate — pure validation logic shared by the first-party
 * settings providers (Criterion 18).
 *
 * These are the CLIENT-SIDE gates every provider runs before it
 * writes anything through its own configd label. They mirror the
 * authoritative sponge_configd validators (sponge_configd/main.cc
 * `_value_valid` / `_shortcuts_valid`, the closed typed registry)
 * so a malformed value is refused locally with a bounded English
 * error and no request is sent ("provider-owned validation",
 * docs/12 §14.6). configd remains the authority: its structured
 * error still surfaces whenever the two disagree.
 *
 * Pure logic only: no sessions, no timers, no XML, no heap. Plain
 * char buffers and the header-inline Genode string/keycode helpers
 * (util/string.h, input/keycodes.h), so the exact code runs both in
 * the providers and in the host self-test (tests/self_test.cc) —
 * the tested logic IS the shipped logic (the sponge_configd
 * key_domain_acl self-test precedent).
 *
 * Error contract: every validator takes a `why` buffer and fills it
 * with a bounded, user-facing English sentence on failure (never a
 * jargon-only token, AGENTS.md §1.1 convenience rule). The buffer is
 * NUL-terminated on both paths.
 */

#pragma once

#include <input/keycodes.h>
#include <util/string.h>

namespace Sponge::Settings {

enum {
	VAL_MAX = 128,   /* configd request parse bound (String<128>) */
	WHY_MAX = 160,   /* bounded English error text */
};

using Why = char[WHY_MAX];


/* ===================== primitives ===================== */

inline void why_clear(char *why, unsigned cap)
{
	if (cap > 0)
		why[0] = 0;
}

inline void why_set(char *why, unsigned cap, char const *text)
{
	why_clear(why, cap);
	if (cap == 0)
		return;

	unsigned i = 0;
	for (; text[i] && i + 1 < cap; ++i)
		why[i] = text[i];
	why[i] = 0;
}

/*
 * XML attribute safety for the Report/ROM transport. The request
 * path writes values as raw XML attributes (Genode's Xml_generator
 * does not escape attribute values), so '"', '<', '>' and '&' would
 * break the document. Rejected for EVERY value, every provider.
 */
inline bool xml_attr_safe(char const *s)
{
	for (unsigned i = 0; s[i]; ++i)
		if (s[i] == '"' || s[i] == '<' || s[i] == '>' || s[i] == '&')
			return false;
	return true;
}

inline bool xml_attr_safe_checked(char const *s, char *why, unsigned cap)
{
	if (xml_attr_safe(s))
		return true;

	why_set(why, cap,
	        "value must not contain \" < > & (XML attribute safety)");
	return false;
}

/* Bounded length: configd's request parser keeps at most 127 chars. */
inline bool length_ok(char const *s, char *why, unsigned cap)
{
	if (Genode::strlen(s) < VAL_MAX)
		return true;

	why_set(why, cap, "value is too long (max 127 characters)");
	return false;
}


/* ===================== value kinds (registry parity) ===================== */

/*
 * UintRange parity (configd Kind::UintRange): pure decimal digits,
 * no sign, no whitespace, parsed value within [min, max].
 */
inline bool uint_in_range(char const *s, unsigned min, unsigned max,
                          unsigned &out, char *why, unsigned cap)
{
	if (Genode::strcmp(s, "") == 0) {
		why_set(why, cap, "value must not be empty (expected a number)");
		return false;
	}

	unsigned parsed = 0;
	for (unsigned i = 0; s[i]; ++i) {
		char const c = s[i];
		if (c < '0' || c > '9') {
			why_set(why, cap,
			        "value must be a base-10 unsigned integer (digits only)");
			return false;
		}

		unsigned const digit = (unsigned)(c - '0');
		if (parsed > ((~0U) - digit) / 10U) {
			why_set(why, cap, "value is too large (number overflow)");
			return false;
		}
		parsed = parsed * 10U + digit;
	}

	if (parsed < min || parsed > max) {
		/* bounded composed message: "value '...' out of range [a..b]" */
		char buf[WHY_MAX] { };
		unsigned pos = 0;
		auto emit = [&] (char const *t) {
			for (unsigned i = 0; t[i] && pos + 1 < WHY_MAX; ++i)
				buf[pos++] = t[i];
		};
		auto emit_num = [&] (unsigned n) {
			char digits[16] { };
			unsigned d = 0;
			do { digits[d++] = char('0' + n % 10U); n /= 10U; } while (n);
			while (d)
				buf[pos++] = digits[--d];
		};
		emit("value out of range [");
		emit_num(min);
		emit("..");
		emit_num(max);
		emit("]");
		buf[pos] = 0;
		why_set(why, cap, buf);
		return false;
	}

	out = parsed;
	why_clear(why, cap);
	return true;
}

/*
 * Enum parity (configd Kind::Enum): exact membership in a closed
 * token set. `name` is used only for the error sentence.
 */
inline bool enum_choice(char const *s, char const *const *choices,
                        unsigned n, char *why, unsigned cap)
{
	for (unsigned i = 0; i < n; ++i)
		if (Genode::strcmp(s, choices[i]) == 0) {
			why_clear(why, cap);
			return true;
		}

	char buf[WHY_MAX] { };
	unsigned pos = 0;
	auto emit = [&] (char const *t) {
		for (unsigned i = 0; t[i] && pos + 1 < WHY_MAX; ++i)
			buf[pos++] = t[i];
	};
	emit("value must be one of: ");
	for (unsigned i = 0; i < n; ++i) {
		if (i > 0)
			emit(", ");
		emit(choices[i]);
	}
	buf[pos] = 0;
	why_set(why, cap, buf);
	return false;
}

/*
 * EnumList parity (configd Kind::EnumList): comma-separated tokens,
 * each an exact member of the closed set. Whitespace around a token
 * is trimmed (all four whitespace classes, as in configd). Empty
 * tokens are refused. Duplicate tokens are accepted, exactly like
 * the configd gate.
 */
inline bool enum_list(char const *s, char const *const *tokens, unsigned n,
                      char *why, unsigned cap)
{
	if (Genode::strcmp(s, "") == 0) {
		why_set(why, cap,
		        "value must not be empty (expected a comma-separated list)");
		return false;
	}

	auto whitespace = [] (char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r';
	};

	Genode::size_t const length = Genode::strlen(s);
	unsigned start = 0;
	while (start <= length) {
		unsigned end = start;
		while (end < length && s[end] != ',')
			++end;

		unsigned first = start;
		unsigned last  = end;
		while (first < last && whitespace(s[first]))
			++first;
		while (last > first && whitespace(s[last - 1]))
			--last;

		char token[VAL_MAX] { };
		unsigned const token_length = last - first;
		for (unsigned i = 0; i < token_length && i + 1 < sizeof(token); ++i)
			token[i] = s[first + i];

		bool known = false;
		if (token_length > 0 && token_length < sizeof(token))
			for (unsigned i = 0; i < n; ++i)
				if (Genode::strcmp(token, tokens[i]) == 0) {
					known = true;
					break;
				}

		if (!known) {
			char buf[WHY_MAX] { };
			unsigned pos = 0;
			auto emit = [&] (char const *t) {
				for (unsigned i = 0; t[i] && pos + 1 < WHY_MAX; ++i)
					buf[pos++] = t[i];
			};
			emit("unknown token '");
			emit(token);
			emit("' in list (expected one of: ");
			for (unsigned i = 0; i < n; ++i) {
				if (i > 0)
					emit(", ");
				emit(tokens[i]);
			}
			emit(")");
			buf[pos] = 0;
			why_set(why, cap, buf);
			return false;
		}

		if (end == length)
			break;
		start = end + 1;
	}

	why_clear(why, cap);
	return true;
}

/*
 * HexColor parity (configd Kind::HexColor): exactly "#RRGGBB".
 */
inline bool hex_color(char const *s, char *why, unsigned cap)
{
	Genode::size_t const length = Genode::strlen(s);
	if (length != 7 || s[0] != '#') {
		why_set(why, cap,
		        "expected a #RRGGBB hex color (7 characters)");
		return false;
	}
	for (unsigned i = 1; i < 7; ++i) {
		char const c = s[i];
		bool const ok = (c >= '0' && c <= '9')
		             || (c >= 'a' && c <= 'f')
		             || (c >= 'A' && c <= 'F');
		if (!ok) {
			why_set(why, cap,
			        "expected a #RRGGBB hex color (digits 0-9 a-f A-F)");
			return false;
		}
	}
	why_clear(why, cap);
	return true;
}

/*
 * Allowlist parity (configd Kind::Allowlist): exact membership in a
 * closed set of paths (the F10 path-traversal defense).
 */
inline bool allowlisted_path(char const *s, char const *const *paths,
                             unsigned n, char *why, unsigned cap)
{
	for (unsigned i = 0; i < n; ++i)
		if (Genode::strcmp(s, paths[i]) == 0) {
			why_clear(why, cap);
			return true;
		}

	char buf[WHY_MAX] { };
	unsigned pos = 0;
	auto emit = [&] (char const *t) {
		for (unsigned i = 0; t[i] && pos + 1 < WHY_MAX; ++i)
			buf[pos++] = t[i];
	};
	emit("path not allowed (allowed: ");
	for (unsigned i = 0; i < n; ++i) {
		if (i > 0)
			emit(", ");
		emit(paths[i]);
	}
	emit(")");
	buf[pos] = 0;
	why_set(why, cap, buf);
	return false;
}

/* Panel <id> charset (configd `_id_charset_ok` + length 1..16). */
inline bool id_token(char const *s)
{
	Genode::size_t const length = Genode::strlen(s);
	if (length == 0 || length > 16)
		return false;

	for (unsigned i = 0; i < length; ++i) {
		char const c = s[i];
		bool const ok = (c >= 'a' && c <= 'z')
		             || (c >= '0' && c <= '9')
		             || c == '_' || c == '-';
		if (!ok)
			return false;
	}
	return true;
}

/*
 * CommaList parity (configd Kind::CommaList, panel.ids): non-empty
 * comma-separated list of [a-z0-9_-]{1,16} tokens, whitespace
 * around tokens trimmed, empty tokens refused.
 */
inline bool comma_list_ids(char const *s, char *why, unsigned cap)
{
	if (Genode::strcmp(s, "") == 0) {
		why_set(why, cap,
		        "value must not be empty (expected a comma-separated id list)");
		return false;
	}

	Genode::size_t const length = Genode::strlen(s);
	unsigned start = 0;
	while (start <= length) {
		unsigned end = start;
		while (end < length && s[end] != ',')
			++end;

		unsigned first = start;
		unsigned last  = end;
		while (first < last && (s[first] == ' ' || s[first] == '\t'))
			++first;
		while (last > first && (s[last - 1] == ' ' || s[last - 1] == '\t'))
			--last;

		unsigned const tok_len = last - first;
		if (tok_len == 0 || tok_len > 16) {
			why_set(why, cap,
			        "each id must be 1..16 characters ([a-z0-9_-])");
			return false;
		}

		char token[32] { };
		for (unsigned i = 0; i < tok_len; ++i)
			token[i] = s[first + i];

		if (!id_token(token)) {
			why_set(why, cap,
			        "id must use only a-z 0-9 _ - ([a-z0-9_-]{1,16})");
			return false;
		}

		if (end == length)
			break;
		start = end + 1;
	}

	why_clear(why, cap);
	return true;
}

/*
 * theme.active parity (configd Kind::String): any non-empty value.
 * The extra bounds here are transport bounds, not policy: length
 * and XML-attribute safety apply to every written value.
 */
inline bool theme_name(char const *s, char *why, unsigned cap)
{
	if (Genode::strcmp(s, "") == 0) {
		why_set(why, cap, "theme name must not be empty");
		return false;
	}
	if (!length_ok(s, why, cap))
		return false;
	return xml_attr_safe_checked(s, why, cap);
}


/* ===================== shortcuts (structured value parity) ===================== */

/* The closed action token set (configd `_shortcuts_valid`). */
inline bool shortcut_action(char const *s)
{
	return Genode::strcmp(s, "launcher") == 0
	    || Genode::strcmp(s, "focus_next") == 0
	    || Genode::strcmp(s, "dismiss") == 0;
}

/*
 * Key-sequence token resolution: the conventional alias table plus
 * the canonical Genode key names, exactly as configd's
 * `_shortcuts_valid` resolves them.
 */
inline char const *resolve_key_alias(char const *keyname)
{
	struct Synonym { char const *alias; char const *canonical; };
	static Synonym const synonyms[] = {
		{ "Super",     "KEY_LEFTMETA"  },
		{ "Meta",      "KEY_LEFTMETA"  },
		{ "MetaL",     "KEY_LEFTMETA"  },
		{ "MetaR",     "KEY_RIGHTMETA" },
		{ "Esc",       "KEY_ESC"       },
		{ "Escape",    "KEY_ESC"       },
		{ "Tab",       "KEY_TAB"       },
		{ "Return",    "KEY_ENTER"     },
		{ "Enter",     "KEY_ENTER"     },
		{ "Backspace", "KEY_BACKSPACE" },
		{ "Alt",       "KEY_LEFTALT"   },
		{ "AltL",      "KEY_LEFTALT"   },
		{ "AltR",      "KEY_RIGHTALT"  },
		{ "Ctrl",      "KEY_LEFTCTRL"  },
		{ "CtrlL",     "KEY_LEFTCTRL"  },
		{ "Shift",     "KEY_LEFTSHIFT" },
		{ "Space",     "KEY_SPACE"     },
	};

	for (unsigned i = 0; i < sizeof(synonyms) / sizeof(synonyms[0]); ++i)
		if (Genode::strcmp(keyname, synonyms[i].alias) == 0)
			return synonyms[i].canonical;

	return keyname;
}

/*
 * One key sequence ("Alt-Tab", "Super"): '-'-separated key tokens,
 * each an alias or a canonical Input::key_code name.
 */
inline bool key_sequence(char const *s, char *why, unsigned cap)
{
	if (Genode::strcmp(s, "") == 0) {
		why_set(why, cap, "key sequence must not be empty");
		return false;
	}

	Genode::size_t const seq_last = Genode::strlen(s);
	unsigned ks = 0;
	while (ks < seq_last) {
		unsigned ke = ks;
		while (ke < seq_last && s[ke] != '-')
			++ke;

		char keyname[32] { };
		unsigned const kn_len = ke - ks;
		if (kn_len == 0 || kn_len >= sizeof(keyname)) {
			why_set(why, cap,
			        "key sequence must not contain empty keys");
			return false;
		}
		for (unsigned i = 0; i < kn_len; ++i)
			keyname[i] = s[ks + i];

		char const *resolved = resolve_key_alias(keyname);
		Input::Key_name const kn { resolved };
		if (Input::key_code(kn) == Input::KEY_UNKNOWN) {
			char buf[WHY_MAX] { };
			unsigned pos = 0;
			auto emit = [&] (char const *t) {
				for (unsigned i = 0; t[i] && pos + 1 < WHY_MAX; ++i)
					buf[pos++] = t[i];
			};
			emit("unknown key '");
			emit(keyname);
			emit("' (expected a key name like KEY_TAB, or an alias "
			     "like Super, Alt, Esc)");
			buf[pos] = 0;
			why_set(why, cap, buf);
			return false;
		}

		if (ke == seq_last)
			break;
		ks = ke + 1;
	}

	why_clear(why, cap);
	return true;
}

/*
 * One binding line ("action<TAB>key_sequence"): exactly one TAB, a
 * closed action token, and a valid key sequence (configd
 * `_shortcuts_valid` per-line rules).
 */
inline bool binding_line(char const *s, char *why, unsigned cap)
{
	Genode::size_t const length = Genode::strlen(s);

	Genode::size_t first = 0;
	Genode::size_t last  = length;
	while (first < last && (s[first] == ' ' || s[first] == '\t' ||
	       s[first] == '\r'))
		++first;
	while (last > first && (s[last - 1] == ' ' || s[last - 1] == '\t' ||
	       s[last - 1] == '\r'))
		--last;

	if (first == last) {
		why_set(why, cap, "binding line must not be empty");
		return false;
	}

	unsigned tab_count = 0;
	Genode::size_t tab_pos = 0;
	for (Genode::size_t i = first; i < last; ++i)
		if (s[i] == '\t') {
			++tab_count;
			if (tab_count == 1)
				tab_pos = i;
		}
	if (tab_count != 1) {
		why_set(why, cap,
		        "binding line must have exactly one TAB between "
		        "action and key sequence");
		return false;
	}

	char action[32] { };
	Genode::size_t const action_len = tab_pos - first;
	if (action_len == 0 || action_len >= sizeof(action)) {
		why_set(why, cap, "binding line is missing its action token");
		return false;
	}
	for (Genode::size_t i = 0; i < action_len; ++i)
		action[i] = s[first + i];

	if (!shortcut_action(action)) {
		char buf[WHY_MAX] { };
		unsigned pos = 0;
		auto emit = [&] (char const *t) {
			for (unsigned i = 0; t[i] && pos + 1 < WHY_MAX; ++i)
				buf[pos++] = t[i];
		};
		emit("unknown action '");
		emit(action);
		emit("' (expected: launcher, focus_next, dismiss)");
		buf[pos] = 0;
		why_set(why, cap, buf);
		return false;
	}

	char sequence[VAL_MAX] { };
	Genode::size_t const seq_len = last - (tab_pos + 1);
	if (seq_len == 0 || seq_len >= sizeof(sequence)) {
		why_set(why, cap, "binding line is missing its key sequence");
		return false;
	}
	for (Genode::size_t i = 0; i < seq_len; ++i)
		sequence[i] = s[tab_pos + 1 + i];

	return key_sequence(sequence, why, cap);
}

/*
 * Whole shortcuts.bindings value parity (configd `_shortcuts_valid`):
 * a non-empty multi-line list of binding lines. Any bad line rejects
 * the whole value (the legacy tab's reject-the-whole-write rule).
 */
inline bool bindings_value(char const *s, char *why, unsigned cap)
{
	Genode::size_t const length = Genode::strlen(s);
	unsigned line_start = 0;
	unsigned line_no    = 0;

	while (line_start < length) {
		unsigned line_end = line_start;
		while (line_end < length && s[line_end] != '\n')
			++line_end;
		++line_no;

		char line[VAL_MAX] { };
		unsigned const line_len = line_end - line_start;
		if (line_len >= sizeof(line)) {
			why_set(why, cap, "binding line is too long (max 127 characters)");
			return false;
		}
		for (unsigned i = 0; i < line_len; ++i)
			line[i] = s[line_start + i];

		if (!binding_line(line, why, cap))
			return false;

		line_start = line_end + 1;
	}

	if (line_no == 0) {
		why_set(why, cap,
		        "bindings must not be empty (expected action<TAB>key lines)");
		return false;
	}

	why_clear(why, cap);
	return true;
}


/* ===================== unified value-spec dispatch ===================== */

/*
 * One value constraint, used by rows and by the closed quick-pair
 * vocabulary so both validate through the same gate. `choices` is
 * the closed token set for KIND_ENUM / KIND_LIST / KIND_PATH.
 */
enum class Value_kind { ANY, UINT, ENUM, LIST, PATH };

struct Value_spec
{
	Value_kind          kind { Value_kind::ANY };
	char const *const  *choices { nullptr };
	unsigned            n_choices { 0 };
	unsigned            min { 0 };
	unsigned            max { 0 };
	int                 validator { 0 };  /* V_* id, see below */

	/*
	 * V_* ids for the value shapes that do not fold into the kinds
	 * above. 0 = none beyond the shared bounds; the rest name an
	 * sp_validate function exactly.
	 */
	enum {
		V_NONE = 0,
		V_HEX_COLOR = 1,
		V_THEME     = 2,
		V_ID_CSV    = 3,
		V_BINDINGS  = 4,
		V_KEYSEQ    = 5,
	};
};

inline bool validate_value(Value_spec const &spec, char const *value,
                           char *why, unsigned cap)
{
	why_clear(why, cap);

	if (!length_ok(value, why, cap))
		return false;

	if (!xml_attr_safe_checked(value, why, cap))
		return false;

	switch (spec.validator) {
	case Value_spec::V_HEX_COLOR: return hex_color(value, why, cap);
	case Value_spec::V_THEME:     return theme_name(value, why, cap);
	case Value_spec::V_ID_CSV:    return comma_list_ids(value, why, cap);
	case Value_spec::V_BINDINGS:  return bindings_value(value, why, cap);
	case Value_spec::V_KEYSEQ:    return key_sequence(value, why, cap);
	default: break;
	}

	switch (spec.kind) {
	case Value_kind::ANY:
		if (Genode::strcmp(value, "") == 0) {
			why_set(why, cap, "value must not be empty");
			return false;
		}
		return true;

	case Value_kind::UINT: {
		unsigned parsed = 0;
		return uint_in_range(value, spec.min, spec.max, parsed, why, cap);
	}

	case Value_kind::ENUM:
		return enum_choice(value, spec.choices, spec.n_choices, why, cap);

	case Value_kind::LIST:
		return enum_list(value, spec.choices, spec.n_choices, why, cap);

	case Value_kind::PATH:
		return allowlisted_path(value, spec.choices, spec.n_choices, why, cap);
	}

	why_set(why, cap, "internal error: unknown value kind");
	return false;
}

}  /* namespace Sponge::Settings */
