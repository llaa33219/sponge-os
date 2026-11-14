/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_logic_tests — case implementation. See sp_logic_tests.h for the
 * contract. Pure cases against the production sp_validate/sp_rows
 * logic; no Genode runtime.
 */

#include "sp_logic_tests.h"

#include "../common/sp_rows.h"
#include "../common/sp_validate.h"

namespace Sponge::Settings_tests {

using namespace Sponge::Settings;

namespace {

Report_fn _report = nullptr;

unsigned checks = 0;

void expect(bool ok, char const *what)
{
	++checks;
	_report(ok, what);
}

void expect_str(bool ok, char const *what, char const *got)
{
	++checks;
	if (ok) {
		_report(true, what);
	} else {
		char buf[256] { };
		unsigned pos = 0;
		for (unsigned i = 0; what[i] && pos + 1 < 200; ++i)
			buf[pos++] = what[i];
		buf[pos++] = ':';
		buf[pos++] = ' ';
		for (unsigned i = 0; got[i] && pos + 1 < 255; ++i)
			buf[pos++] = got[i];
		buf[pos] = 0;
		_report(false, buf);
	}
}

char why[WHY_MAX] { };

void test_uint_range()
{
	unsigned v = 0;

	expect(uint_in_range("28", 16, 128, v, why, WHY_MAX) && v == 28,
	       "uint: 28 in [16..128]");
	expect(!uint_in_range("0", 16, 128, v, why, WHY_MAX),
	       "uint: 0 below range refused");
	expect(!uint_in_range("129", 16, 128, v, why, WHY_MAX),
	       "uint: 129 above range refused");
	expect(!uint_in_range("2a", 16, 128, v, why, WHY_MAX),
	       "uint: non-digit refused");
	expect(!uint_in_range("", 16, 128, v, why, WHY_MAX),
	       "uint: empty refused");
	expect(!uint_in_range("-5", 16, 128, v, why, WHY_MAX),
	       "uint: sign refused");
	expect(uint_in_range("0028", 16, 128, v, why, WHY_MAX) && v == 28,
	       "uint: leading zeros parse like configd");
	expect(!uint_in_range("42949672960", 16, 128, v, why, WHY_MAX),
	       "uint: overflow refused");
	expect(why[0] != 0 && !uint_in_range("1", 16, 128, v, why, WHY_MAX),
	       "uint: bounded error text produced");
}

void test_enum()
{
	char const *const positions[] = { "top", "bottom" };

	expect(enum_choice("top", positions, 2, why, WHY_MAX),
	       "enum: top accepted");
	expect(enum_choice("bottom", positions, 2, why, WHY_MAX),
	       "enum: bottom accepted");
	expect(!enum_choice("left", positions, 2, why, WHY_MAX),
	       "enum: left refused (provider-side deferral)");
	expect(!enum_choice("", positions, 2, why, WHY_MAX),
	       "enum: empty refused");

	char const *const modes[] = { "floating", "tiling" };
	expect(enum_choice("tiling", modes, 2, why, WHY_MAX),
	       "enum: wm tiling accepted");
	expect(!enum_choice("stacking", modes, 2, why, WHY_MAX),
	       "enum: unknown wm mode refused");
}

void test_enum_list()
{
	char const *const widgets[] = { "clock", "launcher", "tasklist" };

	expect(enum_list("clock,launcher", widgets, 3, why, WHY_MAX),
	       "list: clock,launcher accepted");
	expect(enum_list("clock, launcher ,tasklist", widgets, 3, why, WHY_MAX),
	       "list: whitespace trimmed like configd");
	expect(enum_list("tasklist", widgets, 3, why, WHY_MAX),
	       "list: single token accepted");
	expect(!enum_list("clock,menu", widgets, 3, why, WHY_MAX),
	       "list: unknown token rejects the value");
	expect(!enum_list("", widgets, 3, why, WHY_MAX),
	       "list: empty value refused");
	expect(!enum_list("clock,,launcher", widgets, 3, why, WHY_MAX),
	       "list: empty token refused");
}

void test_hex_color()
{
	expect(hex_color("#1e1e2e", why, WHY_MAX),
	       "hex: #1e1e2e accepted");
	expect(hex_color("#AABBCC", why, WHY_MAX),
	       "hex: uppercase accepted");
	expect(!hex_color("#1e1e2", why, WHY_MAX),
	       "hex: short value refused");
	expect(!hex_color("1e1e2e", why, WHY_MAX),
	       "hex: missing # refused");
	expect(!hex_color("#GGGGGG", why, WHY_MAX),
	       "hex: non-hex refused");
	expect(!hex_color("", why, WHY_MAX),
	       "hex: empty refused");
}

void test_allowlist()
{
	char const *const images[] = { "/system/background/default.png" };

	expect(allowlisted_path("/system/background/default.png",
	                        images, 1, why, WHY_MAX),
	       "path: shipped image accepted");
	expect(!allowlisted_path("/etc/passwd", images, 1, why, WHY_MAX),
	       "path: foreign path refused");
	expect(!allowlisted_path("../secret.png", images, 1, why, WHY_MAX),
	       "path: traversal refused");
	expect(!allowlisted_path("", images, 1, why, WHY_MAX),
	       "path: empty refused");
}

void test_ids()
{
	expect(comma_list_ids("alpha", why, WHY_MAX),
	       "ids: alpha accepted");
	expect(comma_list_ids("alpha,beta", why, WHY_MAX),
	       "ids: alpha,beta accepted");
	expect(id_token("a1_b-2"),
	       "ids: charset [a-z0-9_-] accepted");
	expect(!id_token("Alpha"),
	       "ids: uppercase refused");
	expect(!id_token(""),
	       "ids: empty token refused");
	expect(!id_token("0123456789abcdefg"),
	       "ids: 17 chars refused");
	expect(!comma_list_ids("alpha,,beta", why, WHY_MAX),
	       "ids: empty token refused");
	expect(!comma_list_ids("", why, WHY_MAX),
	       "ids: empty list refused (configd parity)");
}

void test_theme()
{
	expect(theme_name("light", why, WHY_MAX),
	       "theme: light accepted");
	expect(theme_name("My Theme 2", why, WHY_MAX),
	       "theme: open string accepted (configd String parity)");
	expect(!theme_name("", why, WHY_MAX),
	       "theme: empty refused");
}

void test_transport_bounds()
{
	char long_value[200] { };
	for (unsigned i = 0; i < 180; ++i)
		long_value[i] = 'x';

	expect(!length_ok(long_value, why, WHY_MAX),
	       "bound: 180-char value refused");
	expect(xml_attr_safe("plain value"),
	       "bound: plain value is xml-safe");
	expect(!xml_attr_safe("a\"b"),
	       "bound: quote refused");
	expect(!xml_attr_safe("a<b"),
	       "bound: '<' refused");
	expect(!xml_attr_safe("a&b"),
	       "bound: '&' refused");
}

void test_bindings()
{
	expect(bindings_value("launcher\tSuper\nfocus_next\tAlt-Tab\n"
	                      "dismiss\tEscape", why, WHY_MAX),
	       "bindings: shipped default table accepted");
	expect(bindings_value("launcher\tSuper", why, WHY_MAX),
	       "bindings: single line accepted");
	expect(!bindings_value("", why, WHY_MAX),
	       "bindings: empty table refused");
	expect(!bindings_value("launcher Super", why, WHY_MAX),
	       "bindings: missing TAB refuses the whole write");
	expect(!bindings_value("launcher\tSuper\tX", why, WHY_MAX),
	       "bindings: two TABs refuse the whole write");
	expect(!bindings_value("launch\tSuper", why, WHY_MAX),
	       "bindings: unknown action refuses the whole write");
	expect(!bindings_value("launcher\tBogus", why, WHY_MAX),
	       "bindings: unknown key refuses the whole write");
	expect(!bindings_value("launcher\tSuper\n\n", why, WHY_MAX),
	       "bindings: empty line refuses the whole write");

	expect(key_sequence("Super", why, WHY_MAX),
	       "keys: Super alias resolves");
	expect(key_sequence("Alt-Tab", why, WHY_MAX),
	       "keys: Alt-Tab resolves");
	expect(key_sequence("KEY_LEFTCTRL-KEY_F1", why, WHY_MAX),
	       "keys: canonical names resolve");
	expect(key_sequence("Esc", why, WHY_MAX),
	       "keys: Esc alias resolves");
	expect(key_sequence("Alt-", why, WHY_MAX),
	       "keys: trailing dash accepted (configd parity quirk: the "
	       "trailing empty token is never examined)");
	expect(!key_sequence("-Tab", why, WHY_MAX),
	       "keys: leading empty key refused");
	expect(!key_sequence("Alt--Tab", why, WHY_MAX),
	       "keys: doubled dash refused");
	expect(!key_sequence("Hyper", why, WHY_MAX),
	       "keys: unknown alias refused");

	expect(shortcut_action("launcher") && shortcut_action("focus_next") &&
	       shortcut_action("dismiss") && !shortcut_action("close"),
	       "actions: closed enum pinned");
}

void test_validate_value_dispatch()
{
	char const *const widgets[] = { "clock", "launcher", "tasklist" };

	Value_spec uint_s { };
	uint_s.kind = Value_kind::UINT;
	uint_s.min  = 16;
	uint_s.max  = 128;

	expect(validate_value(uint_s, "40", why, WHY_MAX),
	       "spec: UINT 40 accepted");
	expect(!validate_value(uint_s, "4", why, WHY_MAX),
	       "spec: UINT 4 refused");

	Value_spec list_s { };
	list_s.kind      = Value_kind::LIST;
	list_s.choices   = widgets;
	list_s.n_choices = 3;

	expect(validate_value(list_s, "clock,tasklist", why, WHY_MAX),
	       "spec: LIST accepted");
	expect(!validate_value(list_s, "clock,nope", why, WHY_MAX),
	       "spec: LIST rejects unknown token");

	Value_spec hex_s { };
	hex_s.validator = Value_spec::V_HEX_COLOR;

	expect(validate_value(hex_s, "#102030", why, WHY_MAX),
	       "spec: V_HEX_COLOR accepted");
	expect(!validate_value(hex_s, "102030", why, WHY_MAX),
	       "spec: V_HEX_COLOR rejects bad shape");
}

void test_rows()
{
	char const *const widgets[] = { "clock", "launcher", "tasklist" };

	Row toggles { };
	toggles.kind      = Row_kind::TOGGLES;
	toggles.spec.kind = Value_kind::LIST;
	toggles.spec.choices   = widgets;
	toggles.spec.n_choices = 3;
	row_set_text(toggles, "clock,tasklist");
	row_sync_state_from_text(toggles);

	expect(toggles.mask == 0b101,
	       "row: toggles mask derived from csv");
	row_text_from_mask(toggles);
	expect_str(Genode::strcmp(toggles.text, "clock,tasklist") == 0,
	           "row: mask rebuilds canonical csv", toggles.text);

	char const *const positions[] = { "top", "bottom" };
	Row choice { };
	choice.kind      = Row_kind::ENUM;
	choice.spec.kind = Value_kind::ENUM;
	choice.spec.choices   = positions;
	choice.spec.n_choices = 2;
	row_set_text(choice, "bottom");
	row_sync_state_from_text(choice);

	expect(choice.choice == 1,
	       "row: enum index derived from text");

	row_set_text(choice, "left");
	row_sync_state_from_text(choice);
	expect(choice.choice == -1,
	       "row: external enum value kept as custom");

	Row text { };
	text.kind = Row_kind::TEXT;
	row_set_text(text, "abc");
	row_set_applied(text);
	row_set_text(text, "xyz");
	row_set_text(text, text.applied);
	expect_str(Genode::strcmp(text.text, "abc") == 0,
	           "row: applied snapshot restores on revert", text.text);
}

}  /* anonymous namespace */


unsigned run_logic_tests(Report_fn report)
{
	_report = report;
	checks = 0;

	test_uint_range();
	test_enum();
	test_enum_list();
	test_hex_color();
	test_allowlist();
	test_ids();
	test_theme();
	test_transport_bounds();
	test_bindings();
	test_validate_value_dispatch();
	test_rows();

	return checks;
}

}  /* namespace Sponge::Settings_tests */
