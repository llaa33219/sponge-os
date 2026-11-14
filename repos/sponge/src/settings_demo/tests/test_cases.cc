/* SPDX-License-Identifier: Apache-2.0
 *
 * test_cases.cc — deterministic cases for common/store.h: charset,
 * value validation, bounded English error text, byte-stable store
 * generation, and strict store parsing. The exact code under test is
 * the code the providers ship (header-only common/store.h).
 */

#include "test_cases.h"

#include "../common/store.h"

#include <stdio.h>
#include <string.h>

using namespace Sponge::Settings_demo;

namespace {

unsigned failures = 0;

void check(bool ok, char const *what)
{
	if (ok)
		return;
	++failures;
	printf("FAIL: %s\n", what);
}

void check_eq_str(char const *got, char const *want, char const *what)
{
	if (strcmp(got, want) == 0)
		return;
	++failures;
	printf("FAIL: %s (got \"%s\" want \"%s\")\n", what, got, want);
}

void check_eq_uint(unsigned got, unsigned want, char const *what)
{
	if (got == want)
		return;
	++failures;
	printf("FAIL: %s (got %u want %u)\n", what, got, want);
}

Value_rules const greeting_rules { 1, 24 };
Value_rules const note_rules     { 0, 32 };

void charset_cases()
{
	check(char_allowed('A'), "charset accepts A");
	check(char_allowed('z'), "charset accepts z");
	check(char_allowed('0'), "charset accepts 0");
	check(char_allowed(' '), "charset accepts space");
	check(char_allowed('.'), "charset accepts dot");
	check(char_allowed(','), "charset accepts comma");
	check(char_allowed('!'), "charset accepts bang");
	check(char_allowed('?'), "charset accepts question");
	check(char_allowed('-'), "charset accepts dash");

	check(!char_allowed('<'), "charset rejects <");
	check(!char_allowed('&'), "charset rejects &");
	check(!char_allowed('"'), "charset rejects double quote");
	check(!char_allowed('\''), "charset rejects single quote");
	check(!char_allowed('\\'), "charset rejects backslash");
	check(!char_allowed(';'), "charset rejects semicolon");
	check(!char_allowed('|'), "charset rejects pipe");
	check(!char_allowed('$'), "charset rejects dollar");
	check(!char_allowed((char)0x80), "charset rejects non-ASCII");
	check(!char_allowed('\t'), "charset rejects tab");
	check(!char_allowed('\n'), "charset rejects newline");

	check(entry_name_char('a'), "entry name accepts a");
	check(entry_name_char('9'), "entry name accepts 9");
	check(entry_name_char('_'), "entry name accepts underscore");
	check(entry_name_char('-'), "entry name accepts hyphen");
	check(!entry_name_char('A'), "entry name rejects uppercase");
	check(!entry_name_char('.'), "entry name rejects dot");
	check(!entry_name_char('/'), "entry name rejects slash");
}

void validation_cases()
{
	check(validate_value("Hello", greeting_rules) == Value_error::ok,
	      "validate accepts Hello in 1..24");
	check(validate_value("Sponge OS", note_rules) == Value_error::ok,
	      "validate accepts note text in 0..32");
	check(validate_value("", note_rules) == Value_error::ok,
	      "validate accepts empty note (min 0)");
	check(validate_value("", greeting_rules) == Value_error::too_short,
	      "validate rejects empty greeting (min 1)");
	check(validate_value("ab", Value_rules { 3, 32 }) == Value_error::too_short,
	      "validate rejects short value (min 3)");

	char exact25[26];
	memset(exact25, 'x', 25); exact25[25] = '\0';
	check(validate_value(exact25, greeting_rules) == Value_error::too_long,
	      "validate rejects 25 chars for max 24");
	exact25[24] = '\0';
	check(validate_value(exact25, greeting_rules) == Value_error::ok,
	      "validate accepts exactly 24 chars for max 24");

	char exact33[34];
	memset(exact33, 'y', 33); exact33[33] = '\0';
	check(validate_value(exact33, note_rules) == Value_error::too_long,
	      "validate rejects 33 chars for max 32");

	check(validate_value("hel;lo", greeting_rules) == Value_error::bad_char,
	      "validate rejects semicolon in value");
	check(validate_value("a<b", greeting_rules) == Value_error::bad_char,
	      "validate rejects angle bracket in value");
	check(validate_value("a\tb", greeting_rules) == Value_error::bad_char,
	      "validate rejects tab in value");
}

void error_text_cases()
{
	char buf[ERROR_TEXT_MAX];

	fill_error_text(Value_error::too_short, greeting_rules, "Greeting",
	                buf, sizeof(buf));
	check_eq_str(buf, "Greeting must not be empty",
	             "too_short (min 1) text");

	fill_error_text(Value_error::too_short, Value_rules { 3, 32 }, "Note",
	                buf, sizeof(buf));
	check_eq_str(buf, "Note must be at least 3 characters",
	             "too_short (min 3) text");

	fill_error_text(Value_error::too_long, greeting_rules, "Greeting",
	                buf, sizeof(buf));
	check_eq_str(buf, "Greeting is too long (max 24 characters)",
	             "too_long text");

	fill_error_text(Value_error::bad_char, greeting_rules, "Greeting",
	                buf, sizeof(buf));
	check(strstr(buf, "Greeting") == buf, "bad_char text starts with label");
	check(strstr(buf, "invalid character") != nullptr,
	      "bad_char text names the problem");
	check(strlen(buf) < ERROR_TEXT_MAX, "error text is bounded");

	char tiny[2];
	fill_error_text(Value_error::bad_char, greeting_rules, "Greeting",
	                tiny, sizeof(tiny));
	check(tiny[1] == '\0', "error text NUL-terminates at cap");
}

void gen_cases()
{
	char buf[STORE_BUF];

	unsigned const len = gen_store(buf, sizeof(buf), "greeting", "Hello");
	check_eq_str(buf,
	             "<settings-demo version=\"1\">"
	             "<entry name=\"greeting\" value=\"Hello\"/></settings-demo>",
	             "gen_store exact bytes");
	check_eq_uint(len, (unsigned)strlen(buf), "gen_store length matches");

	unsigned const again = gen_store(buf, sizeof(buf), "greeting", "Hello");
	check_eq_uint(again, len, "gen_store is byte-stable");

	check_eq_uint(gen_store(buf, sizeof(buf), "greeting", "a<b"), 0,
	              "gen_store rejects bad value char");
	check(gen_store(buf, sizeof(buf), "note", "") > 0,
	      "gen_store accepts empty value (length is a rules concern)");
	check_eq_uint(gen_store(buf, sizeof(buf), "", "Hello"), 0,
	              "gen_store rejects empty entry name");
	check_eq_uint(gen_store(buf, sizeof(buf), "Greeting", "Hello"), 0,
	              "gen_store rejects uppercase entry name");
	check_eq_uint(gen_store(buf, sizeof(buf), "greeting/x", "Hello"), 0,
	              "gen_store rejects slash in entry name");

	char long_name[40];
	memset(long_name, 'a', 33); long_name[33] = '\0';
	check_eq_uint(gen_store(buf, sizeof(buf), long_name, "Hello"), 0,
	              "gen_store rejects 33-char entry name");

	char long_value[MAX_VALUE_LEN + 2];
	memset(long_value, 'z', 33); long_value[33] = '\0';
	check_eq_uint(gen_store(buf, sizeof(buf), "greeting", long_value), 0,
	              "gen_store rejects 33-char value");

	check_eq_uint(gen_store(buf, 8, "greeting", "Hello"), 0,
	              "gen_store rejects undersized buffer");
}

void parse_cases()
{
	char value[MAX_VALUE_LEN + 1] { };
	char buf[STORE_BUF];

	unsigned const len = gen_store(buf, sizeof(buf), "greeting", "Hello");
	check(parse_store(buf, len, "greeting", greeting_rules,
	                  value, sizeof(value)),
	      "parse accepts its own generated store");
	check_eq_str(value, "Hello", "parse round-trips the value");

	memset(value, 0, sizeof(value));
	check(parse_store(buf, len, "greeting", greeting_rules,
	                  value, 6),
	      "parse accepts exact-fit output buffer");
	check_eq_str(value, "Hello", "parse round-trips with tight buffer");
	check(!parse_store(buf, len, "greeting", greeting_rules, value, 5),
	      "parse rejects undersized output buffer");

	char const *two_entries =
		"<settings-demo version=\"1\">"
		"<entry name=\"greeting\" value=\"Hello\"/>"
		"<entry name=\"greeting\" value=\"Hi\"/></settings-demo>";
	check(!parse_store(two_entries, (unsigned)strlen(two_entries),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects two entry children");

	char const *no_entry =
		"<settings-demo version=\"1\"></settings-demo>";
	check(!parse_store(no_entry, (unsigned)strlen(no_entry),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects zero entry children");

	char const *wrong_root =
		"<other version=\"1\"><entry name=\"greeting\" value=\"Hello\"/></other>";
	check(!parse_store(wrong_root, (unsigned)strlen(wrong_root),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects wrong root element");

	char const *wrong_version =
		"<settings-demo version=\"2\"><entry name=\"greeting\" value=\"Hello\"/></settings-demo>";
	check(!parse_store(wrong_version, (unsigned)strlen(wrong_version),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects wrong version");

	char const *name_mismatch =
		"<settings-demo version=\"1\"><entry name=\"about\" value=\"Hello\"/></settings-demo>";
	check(!parse_store(name_mismatch, (unsigned)strlen(name_mismatch),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects entry name mismatch");

	char const *missing_value =
		"<settings-demo version=\"1\"><entry name=\"greeting\"/></settings-demo>";
	check(!parse_store(missing_value, (unsigned)strlen(missing_value),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects missing value attribute");

	char const *missing_name =
		"<settings-demo version=\"1\"><entry value=\"Hello\"/></settings-demo>";
	check(!parse_store(missing_name, (unsigned)strlen(missing_name),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects missing name attribute");

	char const *entity_value =
		"<settings-demo version=\"1\"><entry name=\"greeting\" value=\"a&amp;b\"/></settings-demo>";
	check(!parse_store(entity_value, (unsigned)strlen(entity_value),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects entity-encoded bad char");

	char const *bad_value =
		"<settings-demo version=\"1\"><entry name=\"greeting\" value=\"\"/></settings-demo>";
	check(!parse_store(bad_value, (unsigned)strlen(bad_value),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects empty value under min_len 1");

	check(parse_store(bad_value, (unsigned)strlen(bad_value),
	                  "note", note_rules, value, sizeof(value)) == false,
	      "parse still rejects name mismatch for note");

	char const *empty_note =
		"<settings-demo version=\"1\"><entry name=\"note\" value=\"\"/></settings-demo>";
	check(parse_store(empty_note, (unsigned)strlen(empty_note),
	                  "note", note_rules, value, sizeof(value)),
	      "parse accepts empty note value under min_len 0");
	check_eq_str(value, "", "parse returns the empty note value");

	char const *malformed = "this is not xml at all";
	check(!parse_store(malformed, (unsigned)strlen(malformed),
	                   "greeting", greeting_rules, value, sizeof(value)),
	      "parse rejects malformed XML without crashing");

	check(!parse_store(buf, 0, "greeting", greeting_rules,
	                   value, sizeof(value)),
	      "parse rejects zero-length buffer");
}

} /* anonymous namespace */


unsigned run_store_cases()
{
	charset_cases();
	validation_cases();
	error_text_cases();
	gen_cases();
	parse_cases();
	return failures;
}
