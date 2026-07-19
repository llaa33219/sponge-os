/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of InitStateReader.
 *
 * Parses the tabular state report produced by Genode's sandbox state
 * reporter. The report format is line-oriented, e.g.
 *
 *     state
 *     + ram | quota: 25050364 | used: 332K | avail: 24710396
 *     + child vct | binary: vct
 *       + ram  | assigned: 8M | quota: 8300338 | used: 0 | avail: 8300338
 *       + caps | assigned: 200 | quota: 178 | used: 1 | avail: 177
 */

#include "init_state.h"

#include <base/log.h>

using namespace Sponge;
using namespace Sponge::Vct;


static unsigned long _parse_value(char const *line, char const *key)
{
	Genode::size_t const key_len = Genode::strlen(key);

	for (char const *p = line; *p; ) {

		while (*p == ' ') ++p;
		if (Genode::memcmp(p, key, key_len) != 0 || p[key_len] != ':') {
			while (*p && *p != '|') ++p;
			if (*p == '|') ++p;
			continue;
		}

		p += key_len + 1;
		while (*p == ' ') ++p;

		unsigned long value = 0;
		Genode::Num_bytes::parse(Genode::Span(p, Genode::strlen(p)), value);
		return value;
	}
	return 0;
}


static void _extract_string(char const *line, char const *key,
                            char *out, Genode::size_t out_len)
{
	Genode::size_t const key_len = Genode::strlen(key);

	for (char const *p = line; *p; ) {

		while (*p == ' ') ++p;
		if (Genode::memcmp(p, key, key_len) != 0 || p[key_len] != ':') {
			while (*p && *p != '|') ++p;
			if (*p == '|') ++p;
			continue;
		}

		p += key_len + 1;
		while (*p == ' ') ++p;

		Genode::size_t i = 0;
		while (*p && *p != '|' && *p != ' ' && i < out_len - 1)
			out[i++] = *p++;
		out[i] = 0;
		return;
	}
	out[0] = 0;
}


static bool _line_starts_with(char const *line, char const *prefix)
{
	while (*prefix) {
		if (*line != *prefix) return false;
		++line; ++prefix;
	}
	return true;
}


static char const *_next_line(char const *p)
{
	while (*p && *p != '\n') ++p;
	return (*p == '\n') ? p + 1 : p;
}


static unsigned long _line_indent(char const *line)
{
	unsigned long n = 0;
	while (line[n] == ' ') ++n;
	return n;
}


static bool _line_type_is(char const *l, char const *type)
{
	while (*type) {
		if (*l != *type)
			return false;
		++l; ++type;
	}
	while (*l == ' ')
		++l;
	return *l == '|';
}


void InitStateReader::_parse_content()
{
	if (!_state_rom->valid())
		return;

	char const *p = _state_rom->local_addr<char>();
	if (!p)
		return;

	Child *current_child = nullptr;

	for (char const *line = p; *line; line = _next_line(line)) {

		unsigned long const indent = _line_indent(line);
		char const *l = line + indent;

		if (!_line_starts_with(l, "+ "))
			continue;

		l += 2;

		if (indent == 0) {

			if (_line_type_is(l, "ram")) {
				_ram_quota = _parse_value(l, "quota");
				_ram_used  = _parse_value(l, "used");
				_ram_avail = _parse_value(l, "avail");
			}
			else if (_line_type_is(l, "caps")) {
				_cap_quota = _parse_value(l, "quota");
				_cap_used  = _parse_value(l, "used");
				_cap_avail = _parse_value(l, "avail");
			}
			else if (_line_starts_with(l, "child ")) {
				if (_num_children < MAX_CHILDREN) {
					current_child = &_children[_num_children++];
					current_child->name = Genode::String<64>();
					current_child->binary = Genode::String<64>();
					current_child->state = Genode::String<16>("ok");

					char name_buf[64] = { 0 };
					char const *name_start = l + 6; /* skip "child " */
					while (*name_start == ' ') ++name_start;
					Genode::size_t i = 0;
					while (*name_start && *name_start != '|' && *name_start != ' ' && i < sizeof(name_buf) - 1)
						name_buf[i++] = *name_start++;
					name_buf[i] = 0;
					current_child->name = name_buf;

					char binary_buf[64] = { 0 };
					_extract_string(l, "binary", binary_buf, sizeof(binary_buf));
					current_child->binary = binary_buf;

					char state_buf[16] = { 0 };
					_extract_string(l, "state", state_buf, sizeof(state_buf));
					if (state_buf[0])
						current_child->state = state_buf;
				}
				else {
					current_child = nullptr;
				}
			}
		}
		else if (indent == 2 && current_child) {

			if (_line_type_is(l, "ram")) {
				current_child->ram_assigned = _parse_value(l, "assigned");
				current_child->ram_quota    = _parse_value(l, "quota");
				current_child->ram_used     = _parse_value(l, "used");
				current_child->ram_avail    = _parse_value(l, "avail");
			}
			else if (_line_type_is(l, "caps")) {
				current_child->cap_assigned = _parse_value(l, "assigned");
				current_child->cap_quota    = _parse_value(l, "quota");
				current_child->cap_used     = _parse_value(l, "used");
				current_child->cap_avail    = _parse_value(l, "avail");
			}
		}
	}
}


InitStateReader::InitStateReader(Genode::Env &env)
{
	try {
		_state_rom.construct(env, "state");
		_state_rom->update();
		_timer.construct(env);

		/*
		 * The init state report is generated asynchronously by the nested
		 * init. Early reports may contain only the RAM summary before child
		 * entries are filled in. Retry until at least one child appears
		 * (there is always at least one child — vct itself — in any scenario
		 * that wires the state report), or until the retry budget is spent.
		 */
		/* The system sub-init generates its state report asynchronously
		 * with a coalescing delay (delay_ms, typically 1s). The first
		 * report often contains only the RAM summary before child entries
		 * are filled in. Wait long enough for the report that includes
		 * child state, then poll until it appears. */
		_timer->msleep(1200);
		for (unsigned i = 0; i < 30; ++i) {
			_state_rom->update();
			_parse_content();
			if (_num_children > 0)
				break;
			_timer->msleep(200);
		}

		_available = _state_rom->valid();
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::warning("vct: init state report unavailable (ROM connection failed)");
	}
	catch (Genode::Service_denied) {
		Genode::warning("vct: init state report unavailable (Report/Rom service denied)");
	}
	catch (Genode::Out_of_ram) {
		Genode::warning("vct: init state report unavailable (out of RAM)");
	}
	catch (Genode::Out_of_caps) {
		Genode::warning("vct: init state report unavailable (out of caps)");
	}
}
