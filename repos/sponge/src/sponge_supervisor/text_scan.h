/*
 * \brief  Tolerant text scanning helpers for Sponge control components
 * \author Sponge OS contributors
 * \date   2026
 *
 * A Genode component's `config` ROM reaches it in one of two
 * serialisations, depending on how the linker delivered it
 * (genode/repos/base/src/lib/ldso/include/config.h:36):
 *
 *   XML: <config role="console" color="#204080"/>
 *   HID: config
 *        + | role: console | color: #204080
 *
 * Reading the config through Xml_node alone therefore fails closed on
 * HID input, and through Hid_node alone fails closed on XML input. Both
 * failure modes are silent - a component that reads its own config with
 * the wrong parser keeps running with DEFAULT behaviour, which is the
 * worst possible outcome for a component whose job is to report other
 * components' faults.
 *
 * So for the handful of scalar attributes these components need, we
 * scan the raw config text and accept either spelling. The helpers are
 * deliberately dumb and total: no parser state, no failure mode beyond
 * "attribute not found", and the caller always supplies a default.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <base/stdint.h>
#include <util/string.h>

namespace Sponge {

	namespace Detail {

		/**
		 * Compare 'text' at 'at' against the NUL-terminated 'what'
		 */
		inline bool tag_at(char const *text, Genode::size_t sz,
		                   Genode::size_t at, char const *what)
			{
				Genode::size_t n = Genode::strlen(what);

				if (at + n > sz)
					return false;

				for (Genode::size_t i = 0; i < n; i++)
					if (text[at + i] != what[i])
						return false;

				return true;
			}

		inline Genode::size_t copy_bounded(char *dst, char const *src,
		                                   Genode::size_t n, Genode::size_t cap)
			{
				Genode::size_t i = 0;
				for (; i < n && i + 1 < cap; i++)
					dst[i] = src[i];
				dst[i] = '\0';
				return i;
			}
	}

	/**
	 * Look up a scalar attribute in a raw config text, accepting both
	 * the XML spelling `key="value"` and the HID spelling `key: value`.
	 *
	 * \param cfg  config text
	 * \param sz   length of 'cfg'
	 * \param key  attribute name (without the separator)
	 * \param[out] value  start of the value
	 * \param[out] len    length of the value
	 *
	 * \return true when the attribute was found
	 */
	inline bool scan_attr(char const *cfg, Genode::size_t sz,
	                      char const *key,
	                      char const *&value, Genode::size_t &len)
	{
		Genode::size_t const key_len = Genode::strlen(key);

		for (Genode::size_t i = 0; i + key_len < sz; i++) {

			if (Genode::strcmp(cfg + i, key, key_len) != 0)
				continue;

			/* the attribute name must not be the tail of a longer name */
			if (i > 0) {
				char const p = cfg[i - 1];
				bool const name_char = (p >= 'a' && p <= 'z') ||
				                       (p >= 'A' && p <= 'Z') ||
				                       (p >= '0' && p <= '9') ||
				                       p == '_' || p == '-' || p == ':';
				if (name_char)
					continue;
			}

			Genode::size_t j = i + key_len;

			/* skip the separator: '=' (XML) or ':' (HID) */
			if (j >= sz || (cfg[j] != '=' && cfg[j] != ':'))
				continue;

			j++;
			while (j < sz && cfg[j] == ' ') j++;

			if (j >= sz)
				return false;

			/* quoted value (XML, and HID whenever the value needs it) */
			if (cfg[j] == '"') {
				j++;
				value = cfg + j;
				len   = 0;
				while (j + len < sz && cfg[j + len] != '"')
					len++;
				return true;
			}

			/* bare value (HID): runs to the next segment or line break */
			value = cfg + j;
			len   = 0;
			while (j + len < sz) {
				char const c = cfg[j + len];
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
				    c == '|' || c == '"' || c == '<' || c == '/')
					break;
				len++;
			}
			return true;
		}

		return false;
	}

	/**
	 * Copy the first 'cap'-1 bytes of a scanned value into a buffer,
	 * NUL-terminating it. Returns the number of bytes written.
	 */
	inline Genode::size_t copy_value(char *dst, char const *value,
	                                 Genode::size_t len, Genode::size_t cap)
	{
		return Detail::copy_bounded(dst, value, len, cap);
	}

	/**
	 * Extract the XML element named 'name' from 'text', wrapper tag
	 * included.
	 *
	 * Nesting of the same tag name is accounted for so a template may
	 * legally contain a nested element of its own type. ATTRIBUTES and
	 * self-closing tags are accounted for too: a config produced by
	 * `hid format --output-xml` is full of `<config verbose="no">` and
	 * `<config/>` nodes, and a scanner that recognises only the bare
	 * `<config>` counts an opening tag that is never closed - which
	 * truncates the forwarded config at the first nested `</config>` and
	 * hands init a config that silently starts three children instead of
	 * eleven. (Measured: a 13662-byte template was forwarded as 3325
	 * bytes, and the desktop simply never came up.)
	 *
	 * The returned span is what gets written to the report verbatim: the
	 * supervisor must not reformat the config it forwards, because a
	 * reformat is indistinguishable from a semantic change to whoever
	 * reads it.
	 */
	inline bool element_text(char const *text, Genode::size_t sz,
	                         char const *name,
	                         char const *&out, Genode::size_t &out_len)
	{
		Genode::size_t const n = Genode::strlen(name);

		/* an open tag is "<name" followed by '>', whitespace or '/' */
		auto is_open = [&] (Genode::size_t at) {

			if (at == 0 || text[at - 1] != '<' || !Detail::tag_at(text, sz, at, name))
				return false;

			if (at + n >= sz)
				return false;

			char const c = text[at + n];

			return c == '>' || c == '/' || c == ' ' || c == '\t' ||
			       c == '\n' || c == '\r';
		};

		auto is_close = [&] (Genode::size_t at) {

			if (at == 0 || text[at - 1] != '<' || text[at] != '/')
				return false;

			return Detail::tag_at(text, sz, at + 1, name);
		};

		/*
		 * Advance to just past the '>' ending the tag that starts at 'at',
		 * stepping over quoted attribute values so a '>' inside one cannot
		 * end the tag early.
		 */
		auto skip_tag = [&] (Genode::size_t at) {

			bool quoted = false;
			char q = 0;

			while (at < sz) {
				char const c = text[at];

				if (quoted) {
					if (c == q) quoted = false;
				} else if (c == '"' || c == '\'') {
					quoted = true; q = c;
				} else if (c == '>') {
					return at + 1;
				}
				at++;
			}

			return sz;
		};

		Genode::size_t depth = 0;
		Genode::size_t start = 0;
		bool have_start = false;

		for (Genode::size_t i = 0; i < sz; i++) {

			if (text[i] != '<')
				continue;

			if (is_open(i + 1)) {

				Genode::size_t const tag_end = skip_tag(i + 1 + n);

				/* "<name .../>" is a leaf and changes no depth */
				bool const leaf = (tag_end >= 2 && text[tag_end - 2] == '/');

				if (!have_start) {
					start = i;
					have_start = true;
				}

				if (!leaf)
					depth++;

				i = tag_end - 1;
				continue;
			}

			if (is_close(i + 1)) {

				Genode::size_t const tag_end = skip_tag(i + 1 + n + 1);

				if (depth > 0 && --depth == 0) {
					out     = text + start;
					out_len = tag_end - start;
					return true;
				}

				i = tag_end - 1;
			}
		}

		return false;
	}

	/**
	 * One restart-version slot: a marker name and the decimal values it
	 * renders to.
	 *
	 * 'name' is the marker's name part - nullptr is the GLOBAL slot,
	 * which renders the plain "@KILL@" marker. 'value' is what the
	 * marker renders to in THIS substitution; 'previous' is the value
	 * the node carried before, so the caller can count how many nodes a
	 * publish actually bumped (the publish log's 'bumped=' witness).
	 */
	struct Version_slot {
		char const *name;
		unsigned    value;
		unsigned    previous;
	};

	namespace Detail {

		inline void render_decimal(unsigned value, char *digits,
		                           Genode::size_t &digits_len)
		{
			char tmp[16];
			Genode::size_t t = 0;

			digits_len = 0;
			do { tmp[t++] = char('0' + (value % 10)); value /= 10; } while (value);
			while (t) digits[digits_len++] = tmp[--t];
		}
	}

	/**
	 * Replace every restart marker in 'text' with its rendered version.
	 *
	 * Marker grammar (P3.4):
	 *
	 *   "@KILL@"         the GLOBAL slot - every node carrying it
	 *                    shares one version counter
	 *   "@KILL:<name>@"  the named slot '<name>' - a per-node counter,
	 *                    so a containment action can bump ONE child
	 *                    while every other node stays byte-identical
	 *
	 * (Start_model::matches() compares only name+version
	 * (genode/repos/os/src/lib/sandbox/config_model.h:171), so a node
	 * whose rendered version does not change is never rebuilt.)
	 *
	 * 'bumped' accumulates the number of marker sites whose rendered
	 * value DIFFERS from 'previous' - the witness that distinguishes a
	 * targeted containment (bumped=1) from a global one (bumped=all).
	 *
	 * Returns the number of bytes written (at most 'cap'-1 plus a NUL),
	 * or 0 when a marker names a slot the caller did not provide - the
	 * caller must then refuse to publish rather than forward a template
	 * whose restart lever is half-rendered.
	 */
	inline Genode::size_t substitute_versions(char const *text, Genode::size_t len,
	                                          Version_slot const *slots,
	                                          Genode::size_t num_slots,
	                                          unsigned &bumped,
	                                          char *dst, Genode::size_t cap)
	{
		static char const PREFIX[] = "@KILL";

		Genode::size_t const prefix_len = 5;

		Genode::size_t o = 0;
		Genode::size_t i = 0;

		bumped = 0;

		while (i < len) {

			bool const at_marker =
				(i + prefix_len <= len) &&
				Detail::tag_at(text, len, i, PREFIX);

			if (!at_marker) {

				if (o + 2 >= cap)
					return 0;

				dst[o++] = text[i++];
				continue;
			}

			/* the marker tail: "@KILL@" or "@KILL:<name>@" */
			Genode::size_t const tail = i + prefix_len;

			if (tail >= len)
				return 0;

			char const kind = text[tail];

			if (kind != '@' && kind != ':')
				return 0;

			char const *name = nullptr;
			Genode::size_t name_len = 0;
			Genode::size_t consumed = 0;

			if (kind == '@') {
				consumed = prefix_len + 1;
			} else {
				name     = text + tail + 1;
				name_len = 0;
				while (tail + 1 + name_len < len &&
				       text[tail + 1 + name_len] != '@')
					name_len++;

				if (tail + 1 + name_len >= len)
					return 0;

				consumed = prefix_len + 1 + name_len + 1;
			}

			Version_slot const *slot = nullptr;

			for (Genode::size_t s = 0; s < num_slots; s++) {

				bool const global = (slots[s].name == nullptr);

				if (global != (kind == '@'))
					continue;

				if (global) { slot = &slots[s]; break; }

				Genode::size_t const n = Genode::strlen(slots[s].name);

				if (n != name_len)
					continue;

				bool same = true;
				for (Genode::size_t k = 0; k < n; k++)
					if (slots[s].name[k] != name[k]) { same = false; break; }

				if (same) { slot = &slots[s]; break; }
			}

			if (!slot)
				return 0;

			char digits[16];
			Genode::size_t digits_len = 0;

			Detail::render_decimal(slot->value, digits, digits_len);

			if (o + digits_len + 1 >= cap)
				return 0;

			for (Genode::size_t k = 0; k < digits_len; k++)
				dst[o + k] = digits[k];

			o += digits_len;
			i += consumed;

			if (slot->value != slot->previous)
				bumped++;
		}

		dst[o] = '\0';
		return o;
	}
}