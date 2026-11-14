/* SPDX-License-Identifier: Apache-2.0
 *
 * store.h — pure value validation and package-private store XML for
 * the settings_demo providers (docs/12-package-format.md §14.6,
 * D16.10 tier (b)).
 *
 * Each provider owns exactly one small string value. It validates the
 * value itself ("each provider validates its own values in its own
 * code", D16.10 tier (b)) and persists it in a tiny byte-stable XML
 * file in the package-private store:
 *
 *   <settings-demo version="1"><entry name="NAME" value="VALUE"/></settings-demo>
 *
 * The format mirrors sponge_configd's store (fixed attribute order,
 * one entry per provider file) so the tmp+rename crash-consistency
 * recipe applies unchanged. Values are restricted to a charset that is
 * XML-attribute-safe by construction ([A-Za-z0-9 space . , ! ? -]),
 * so no escaping is needed and a hand-edited file with any other
 * character is rejected on load (fail-soft to the provider default).
 *
 * PURITY CONTRACT (the sponge_settings model.h precedent): no
 * Genode::Env, no sessions, no I/O, no exceptions escaping, no
 * mutable global state, no dynamic allocation. The only Genode header
 * used is the public <util/xml_node.h> (buffer parsing), so the exact
 * code runs in the providers and in tests/self_test.cc.
 *
 * Error contract: every validation failure maps to a bounded English
 * sentence via fill_error_text() (AGENTS.md §1.1: no jargon-only
 * errors). The buffer is NUL-terminated on both paths.
 */

#pragma once

#include <util/xml_node.h>

namespace Sponge::Settings_demo {

/* Absolute value cap shared by every provider entry. */
enum { MAX_VALUE_LEN = 32 };

/* Store document bound (the file is tiny: one entry). */
enum { STORE_BUF = 2048 };

/* Bound for the composed English error text. */
enum { ERROR_TEXT_MAX = 120 };

struct Value_rules
{
	unsigned min_len;
	unsigned max_len;   /* <= MAX_VALUE_LEN */
};

enum class Value_error { ok, too_short, too_long, bad_char };


/* ===================== charset ===================== */

/*
 * Allowed value characters: letters, digits, space, and the five
 * sentence punctuation marks. This set is deliberately closed and
 * XML-attribute-safe (no '<', '&', quotes, or backslash), which is
 * what lets gen_store() skip escaping entirely and parse_store()
 * reject anything else with one predicate.
 */
inline bool char_allowed(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
	    || (c >= '0' && c <= '9')
	    || c == ' ' || c == '.' || c == ',' || c == '!'
	    || c == '?' || c == '-';
}

/* Entry names use the §14.2 id charset ([a-z0-9_-]{1,32}). */
inline bool entry_name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
	    || c == '_' || c == '-';
}


/* ===================== validation ===================== */

inline unsigned str_len(char const *s)
{
	unsigned n = 0;
	while (s[n] != '\0') ++n;
	return n;
}

inline Value_error validate_value(char const *value, Value_rules rules)
{
	unsigned const len = str_len(value);

	if (len < rules.min_len) return Value_error::too_short;
	if (len > rules.max_len) return Value_error::too_long;

	for (unsigned i = 0; i < len; ++i)
		if (!char_allowed(value[i]))
			return Value_error::bad_char;

	return Value_error::ok;
}


namespace detail {

/* Returns false as soon as the buffer cannot hold the rest of 's'. */
inline bool append(char *buf, unsigned cap, unsigned &pos, char const *s)
{
	while (*s != '\0') {
		if (pos + 1 >= cap)
			return false;
		buf[pos++] = *s++;
	}
	return true;
}

inline bool append_uint(char *buf, unsigned cap, unsigned &pos, unsigned v)
{
	char digits[10];
	unsigned n = 0;
	do {
		digits[n++] = char('0' + v % 10);
		v /= 10;
	} while (v != 0 && n < sizeof(digits));
	while (n > 0) {
		if (pos + 1 >= cap)
			return false;
		buf[pos++] = digits[--n];
	}
	return true;
}

} /* namespace detail */


/*
 * Compose the bounded English explanation for a validation failure.
 * Always NUL-terminates (when cap > 0) and always returns 'buf'.
 * Examples:
 *   "Greeting must not be empty"
 *   "Note must be at least 3 characters"
 *   "Greeting is too long (max 24 characters)"
 *   "Greeting has an invalid character (allowed: A-Z a-z 0-9 space . , ! ? -)"
 */
inline char *fill_error_text(Value_error err, Value_rules rules,
                             char const *label, char *buf, unsigned cap)
{
	if (cap == 0)
		return buf;

	unsigned pos = 0;

	switch (err) {

	case Value_error::ok:
		detail::append(buf, cap, pos, "ok");
		break;

	case Value_error::too_short:
		detail::append(buf, cap, pos, label);
		if (rules.min_len <= 1)
			detail::append(buf, cap, pos, " must not be empty");
		else {
			detail::append(buf, cap, pos, " must be at least ");
			detail::append_uint(buf, cap, pos, rules.min_len);
			detail::append(buf, cap, pos, " characters");
		}
		break;

	case Value_error::too_long:
		detail::append(buf, cap, pos, label);
		detail::append(buf, cap, pos, " is too long (max ");
		detail::append_uint(buf, cap, pos, rules.max_len);
		detail::append(buf, cap, pos, " characters)");
		break;

	case Value_error::bad_char:
		detail::append(buf, cap, pos, label);
		detail::append(buf, cap, pos,
		               " has an invalid character "
		               "(allowed: A-Z a-z 0-9 space . , ! ? -)");
		break;
	}

	buf[pos] = '\0';
	return buf;
}


/* ===================== store document ===================== */

/*
 * Serialize the store document for one entry. Byte-stable for a given
 * (entry_name, value) — the determinism contract of the configd
 * store. Returns the byte count written (excluding the NUL), or 0 on
 * any invalid input or buffer overflow: the caller logs one bounded
 * warning and keeps the in-memory value (docs/12 §13.2 fail-soft).
 */
inline unsigned gen_store(char *buf, unsigned cap,
                          char const *entry_name, char const *value)
{
	if (cap == 0)
		return 0;

	unsigned name_len = 0;
	for (; entry_name[name_len] != '\0'; ++name_len)
		if (name_len >= 32 || !entry_name_char(entry_name[name_len]))
			return 0;

	if (name_len == 0)
		return 0;

	unsigned value_len = 0;
	for (; value[value_len] != '\0'; ++value_len)
		if (value_len >= MAX_VALUE_LEN || !char_allowed(value[value_len]))
			return 0;

	unsigned pos = 0;

	bool const fits =
		    detail::append(buf, cap, pos, "<settings-demo version=\"1\">"
		                                  "<entry name=\"")
		 && detail::append(buf, cap, pos, entry_name)
		 && detail::append(buf, cap, pos, "\" value=\"")
		 && detail::append(buf, cap, pos, value)
		 && detail::append(buf, cap, pos, "\"/></settings-demo>");

	if (!fits)
		return 0;

	buf[pos] = '\0';
	return pos;
}


/*
 * Parse and strictly validate a store document. Accepts only:
 *   - root <settings-demo version="1">
 *   - exactly one <entry> child
 *   - a name attribute equal to 'entry_name'
 *   - a present value attribute that passes validate_value()
 *
 * Any deviation — malformed XML, wrong root, wrong version, zero or
 * multiple entries, name mismatch, missing/invalid value — returns
 * false and leaves 'value_out' untouched. The caller then starts from
 * the provider default (docs/12 §13.2: "start with defaults and a
 * warning, never a crash").
 */
inline bool parse_store(char const *buf, unsigned len,
                        char const *entry_name, Value_rules rules,
                        char *value_out, unsigned cap)
{
	if (len == 0 || cap == 0)
		return false;

	char value[MAX_VALUE_LEN + 2] { };
	bool found = false;

	try {
		Genode::Xml_node const root(buf, len);

		if (!root.has_type("settings-demo"))
			return false;

		if (root.attribute_value("version", 0U) != 1)
			return false;

		unsigned entries = 0;

		root.for_each_sub_node("entry", [&] (Genode::Xml_node const &n) {
			++entries;

			if (entries > 1)
				return;

			if (!n.has_attribute("name") || !n.has_attribute("value"))
				return;

			Genode::String<48> const name =
				n.attribute_value("name", Genode::String<48>());

			if (str_len(name.string()) > 32 ||
			    Genode::strcmp(name.string(), entry_name) != 0)
				return;

			Genode::String<MAX_VALUE_LEN + 2> const val =
				n.attribute_value("value", Genode::String<MAX_VALUE_LEN + 2>());

			unsigned i = 0;
			for (; i <= MAX_VALUE_LEN && val.string()[i] != '\0'; ++i)
				value[i] = val.string()[i];
			value[i] = '\0';
			found = true;
		});

		if (entries != 1 || !found)
			return false;

	} catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}

	if (validate_value(value, rules) != Value_error::ok)
		return false;

	unsigned const value_len = str_len(value);
	if (value_len + 1 > cap)
		return false;

	for (unsigned i = 0; i <= value_len; ++i)
		value_out[i] = value[i];

	return true;
}

} /* namespace Sponge::Settings_demo */
