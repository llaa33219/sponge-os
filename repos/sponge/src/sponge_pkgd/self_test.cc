/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test for sponge_pkgd's <settings> rules
 * (settings_rules.{h,cc} — docs/12-package-format.md §14).
 *
 * NOT part of the Genode build (deliberately absent from target.mk's
 * SRC_CC, exactly like the pure libraries' self_test.cc files). It
 * proves, deterministically:
 *
 *   1. Every §14.3 hard rejection: duplicate id, missing id / title /
 *      target, malformed id, absolute target (leading '/' or '\',
 *      drive forms), payload escape (".." or separators), shell /
 *      interpreter / command strings, and exclusive-on-settings
 *      (both the `exclusive` attribute and the <exclusive> claim
 *      child).
 *   2. The accept side: the §14.2 fields (id/title/category/icon/
 *      keywords/target), the unknown-category (and absent-category)
 *      fallback to "other", the warn-and-ignore unknown attribute /
 *      child cases, and two entries sharing one target binary (the
 *      §14.2 example shape).
 *   3. The §14.7 singleton open decision: unknown id -> not-installed,
 *      missing binary -> binary-missing, refused budget ->
 *      insufficient-resources, first open -> started, SECOND OPEN ->
 *      focused with no duplicate (the claim set is add-once and the
 *      focused path claims nothing new), the gate ordering (the
 *      singleton gate beats the binary gate beats the resource gate),
 *      and the decide()/decide_pre_resource() laziness property.
 *   4. The start-name derivation: deterministic, distinct per
 *      (package, settings-id), never equal to an app <start> name.
 *   5. The §14.7 crash/relaunch exit scan (settings_provider_state):
 *      an explicit `exited` attribute on the named <child> releases
 *      (including a clean `exited="0"`), while a running child, a
 *      missing child (reports can predate first paint) and another
 *      child's exit never do; the exit code is decoded.
 *
 * BUILD & RUN (from the repository root; host g++ only — this never
 * touches the Genode build tree):
 *
 *   g++ -std=c++20 -O1 \
 *       -I genode/repos/base/include \
 *       -I genode/repos/base/include/spec/64bit \
 *       -I genode/repos/base/include/spec/x86_64 \
 *       -I genode/repos/base/include/spec/x86 \
 *       -I genode/repos/base/src/include \
 *       -I repos/sponge/include \
 *       -I repos/sponge/src/sponge_pkgd \
 *       repos/sponge/src/sponge_pkgd/self_test.cc \
 *       repos/sponge/src/sponge_pkgd/settings_rules.cc \
 *       repos/sponge/lib/src/sponge_pkg_launch/pkg_launch.cc \
 *       genode/repos/base/src/lib/base/hid.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_pkgd_settings_self_test \
 *   && /tmp/sponge_pkgd_settings_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every check
 * matched. "self-test: FAIL" prints every divergence.
 *
 * The three host stubs further down (Genode::Log, Genode::Lock) exist
 * only to satisfy the linker for symbols hid.cc references from its
 * generator side (the invalid-attribute warning); the decode path
 * never logs and the test never triggers them. The two Genode::raise
 * shims keep Xml_node's throw mapping without the real raise's
 * logging/backtrace closure.
 */

#include "settings_rules.h"

#include <base/error.h>
#include <base/exception.h>
#include <base/lock.h>
#include <util/hid.h>

#include <stdio.h>
#include <string.h>

/*
 * Host-only shim: the real Genode::raise (base/src/lib/cxx/raise.cc)
 * drags in os/backtrace and its library closure. Xml_node raises
 * through these overloads only on malformed input, which this test
 * never feeds it — the shim keeps the throw mapping of the real
 * implementation minus the logging/backtrace.
 */
void Genode::raise(Genode::Unexpected_error e)
{
	switch (e) {
	case Genode::Unexpected_error::INDEX_OUT_OF_BOUNDS:
		throw Genode::Index_out_of_bounds();
	case Genode::Unexpected_error::NONEXISTENT_SUB_NODE:
		throw Genode::Nonexistent_sub_node();
	case Genode::Unexpected_error::ACCESS_UNCONSTRUCTED_OBJ:
		throw Genode::Access_unconstructed_obj();
	case Genode::Unexpected_error::IPC_BUFFER_EXCEEDED:
		break;
	}
	throw Genode::Ipc_error();
}

void Genode::raise(Genode::Alloc_error e)
{
	switch (e) {
	case Genode::Alloc_error::OUT_OF_RAM:  throw Genode::Out_of_ram();
	case Genode::Alloc_error::OUT_OF_CAPS: throw Genode::Out_of_caps();
	case Genode::Alloc_error::DENIED:      break;
	}
	throw Genode::Denied();
}


/* ===================== host stubs (link only) ===================== */

namespace {

struct Null_output : Genode::Output
{
	void out_char(char) override { }
	void out_string(char const *, Genode::size_t) override { }
};

Null_output null_output { };

}  /* anonymous namespace */

Genode::Log &Genode::Log::log()
{
	static Genode::Log instance { null_output };
	return instance;
}

void Genode::Log::_acquire(Type) { }
void Genode::Log::_release() { }

Genode::Lock::Lock(Genode::Lock::State initial)
:
	_spinlock_state(0), _state(initial), _last_applicant(0), _owner(nullptr)
{ }

using Sponge::Pkgd::Settings_entry;
using Sponge::Pkgd::Settings_outcome;
using Sponge::Pkgd::Settings_parse;
using Sponge::Pkgd::Settings_reject;
namespace Rules     = Sponge::Pkgd;
namespace Pkg_launch = Sponge::Pkg_launch;

unsigned failures = 0;

void check(bool ok, char const *what)
{
	if (ok)
		return;
	++failures;
	printf("FAIL: %s\n", what);
}


void check_reject(char const *xml, Settings_reject want, char const *what)
{
	Settings_entry e { };
	Settings_parse const p =
		Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
		                            e, nullptr, 0);
	if (p.reject == want)
		return;
	++failures;
	printf("FAIL: %s (reject=%d want=%d)\n", what, (int)p.reject, (int)want);
}


void check_accept(char const *xml, char const *what)
{
	Settings_entry e { };
	Settings_parse const p =
		Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
		                            e, nullptr, 0);
	if (p.reject == Settings_reject::none)
		return;
	++failures;
	printf("FAIL: %s (unexpected reject=%d: %s)\n",
	       what, (int)p.reject, Rules::settings_reject_reason(p.reject));
}


int main(int argc, char **argv)
{
	(void)argc; (void)argv;

	/* ============ 1. accept side (§14.2) ============ */

	{
		char const *const xml =
			"<settings id=\"panel\" title=\"Panel\" category=\"desktop\""
			" icon=\"panel.png\" target=\"panel_settings\">"
			"<keyword>height</keyword><keyword>widgets</keyword>"
			"</settings>";
		Settings_entry e { };
		Settings_parse const p =
			Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
			                            e, nullptr, 0);
		check(p.reject == Settings_reject::none, "full entry: accepted");
		check(strcmp(e.id.string(), "panel") == 0, "full entry: id");
		check(strcmp(e.title.string(), "Panel") == 0, "full entry: title");
		check(strcmp(e.category.string(), "desktop") == 0, "full entry: category");
		check(e.has_icon && strcmp(e.icon.string(), "panel.png") == 0,
		      "full entry: icon");
		check(strcmp(e.target.string(), "panel_settings") == 0,
		      "full entry: target");
		check(e.num_keywords == 2 &&
		      strcmp(e.keywords[0].string(), "height") == 0 &&
		      strcmp(e.keywords[1].string(), "widgets") == 0,
		      "full entry: keywords");
		check(!e.category_defaulted, "full entry: category not defaulted");
		check(!p.unknown_attribute && !p.unknown_child,
		      "full entry: no unknowns flagged");
	}

	{
		char const *const xml =
			"<settings id=\"a\" title=\"A\" target=\"t\"/>";
		Settings_entry e { };
		Settings_parse const p =
			Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
			                            e, nullptr, 0);
		check(p.reject == Settings_reject::none, "minimal entry: accepted");
		check(strcmp(e.category.string(), "other") == 0,
		      "minimal entry: absent category falls back to other");
		check(e.category_defaulted, "minimal entry: category_defaulted flagged");
		check(!e.has_icon && e.num_keywords == 0, "minimal entry: no icon/keywords");
	}

	{
		char const *const xml =
			"<settings id=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\" title=\"A\" target=\"t\"/>";
		Settings_entry e { };
		Settings_parse const p =
			Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
			                            e, nullptr, 0);
		check(p.reject == Settings_reject::none, "32-char id: accepted");
		check(Genode::strlen(e.id.string()) == 32,
		      "32-char id: identity is not truncated");
	}

	{
		char const *const cats[] = {
			"desktop", "hardware", "system", "applications", "other" };
		for (unsigned i = 0; i < 5; ++i) {
			char xml[256];
			snprintf(xml, sizeof(xml),
			         "<settings id=\"a\" title=\"A\" category=\"%s\""
			         " target=\"t\"/>", cats[i]);
			Settings_entry e { };
			Settings_parse const p =
				Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
				                            e, nullptr, 0);
			check(p.reject == Settings_reject::none, "known category: accepted");
			check(strcmp(e.category.string(), cats[i]) == 0,
			      "known category: preserved");
			check(!e.category_defaulted, "known category: not defaulted");
		}
	}

	{
		char const *const xml =
			"<settings id=\"a\" title=\"A\" category=\"banana\" target=\"t\"/>";
		Settings_entry e { };
		Settings_parse const p =
			Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
			                            e, nullptr, 0);
		check(p.reject == Settings_reject::none, "unknown category: accepted");
		check(strcmp(e.category.string(), "other") == 0,
		      "unknown category: falls back to other");
		check(e.category_defaulted, "unknown category: defaulted flagged");
	}

	{
		char const *const xml =
			"<settings id=\"a\" title=\"A\" target=\"t\" future=\"x\">"
			"<future/></settings>";
		Settings_entry e { };
		Settings_parse const p =
			Rules::parse_settings_entry(Genode::Xml_node(xml, strlen(xml)),
			                            e, nullptr, 0);
		check(p.reject == Settings_reject::none, "unknown attr/child: accepted");
		check(p.unknown_attribute, "unknown attribute flagged");
		check(p.unknown_child, "unknown child flagged");
	}

	{
		/* §14.2 example shape: two entries sharing one binary. */
		Settings_entry e1 { }, e2 { };
		char const *const x1 =
			"<settings id=\"panel\" title=\"Panel\" target=\"panel_settings\"/>";
		char const *const x2 =
			"<settings id=\"position\" title=\"Panel Position\""
			" target=\"panel_settings\"/>";
		Settings_parse const p1 =
			Rules::parse_settings_entry(Genode::Xml_node(x1, strlen(x1)),
			                            e1, nullptr, 0);
		Settings_parse const p2 =
			Rules::parse_settings_entry(Genode::Xml_node(x2, strlen(x2)),
			                            e2, &e1, 1);
		check(p1.reject == Settings_reject::none, "shared binary: entry 1 accepted");
		check(p2.reject == Settings_reject::none, "shared binary: entry 2 accepted");
		check(strcmp(e1.target.string(), e2.target.string()) == 0,
		      "shared binary: same target kept");
	}

	/* ============ 2. hard rejections (§14.3) ============ */

	check_reject("<settings title=\"A\" target=\"t\"/>",
	             Settings_reject::missing_id, "reject: missing id");

	check_reject("<settings id=\"\" title=\"A\" target=\"t\"/>",
	             Settings_reject::missing_id, "reject: empty id");

	check_reject("<settings id=\"Panel\" title=\"A\" target=\"t\"/>",
	             Settings_reject::malformed_id, "reject: uppercase id");

	check_reject("<settings id=\"a.b\" title=\"A\" target=\"t\"/>",
	             Settings_reject::malformed_id, "reject: dotted id");

	check_reject("<settings id=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""
	             " title=\"A\" target=\"t\"/>",
	             Settings_reject::malformed_id, "reject: 33-char id");

	check_reject("<settings id=\"a/b\" title=\"A\" target=\"t\"/>",
	             Settings_reject::malformed_id, "reject: separator id");

	check_reject("<settings id=\"a\" target=\"t\"/>",
	             Settings_reject::missing_title, "reject: missing title");

	check_reject("<settings id=\"a\" title=\"A\"/>",
	             Settings_reject::missing_target, "reject: missing target");

	check_reject("<settings id=\"a\" title=\"A\" target=\"\"/>",
	             Settings_reject::missing_target, "reject: empty target");

	check_reject("<settings id=\"a\" title=\"A\" target=\"/bin/panel\"/>",
	             Settings_reject::absolute_target, "reject: absolute /");

	check_reject("<settings id=\"a\" title=\"A\" target=\"\\\\evil\"/>",
	             Settings_reject::absolute_target, "reject: absolute backslash");

	check_reject("<settings id=\"a\" title=\"A\" target=\"C:\\\\x\\\\p\"/>",
	             Settings_reject::absolute_target, "reject: drive form");

	check_reject("<settings id=\"a\" title=\"A\" target=\"c:/x/p\"/>",
	             Settings_reject::absolute_target, "reject: drive form slash");

	check_reject("<settings id=\"a\" title=\"A\" target=\"..\"/>",
	             Settings_reject::payload_escape, "reject: dotdot");

	check_reject("<settings id=\"a\" title=\"A\" target=\"a..b\"/>",
	             Settings_reject::payload_escape, "reject: embedded dotdot");

	check_reject("<settings id=\"a\" title=\"A\" target=\"bin/p\"/>",
	             Settings_reject::payload_escape, "reject: separator /");

	check_reject("<settings id=\"a\" title=\"A\" target=\"bin\\\\p\"/>",
	             Settings_reject::payload_escape, "reject: separator backslash");

	check_reject("<settings id=\"a\" title=\"A\" target=\"sh -c x\"/>",
	             Settings_reject::command_string, "reject: interpreter string");

	check_reject("<settings id=\"a\" title=\"A\" target=\"a b\"/>",
	             Settings_reject::command_string, "reject: whitespace");

	check_reject("<settings id=\"a\" title=\"A\" target=\"x;rm\"/>",
	             Settings_reject::command_string, "reject: semicolon");

	check_reject("<settings id=\"a\" title=\"A\" target=\"x|y\"/>",
	             Settings_reject::command_string, "reject: pipe");

	check_reject("<settings id=\"a\" title=\"A\" target=\"x&amp;y\"/>",
	             Settings_reject::command_string, "reject: ampersand");

	check_reject("<settings id=\"a\" title=\"A\" target=\"$x\"/>",
	             Settings_reject::command_string, "reject: dollar");

	check_reject("<settings id=\"a\" title=\"A\" target=\"&#96;x&#96;\"/>",
	             Settings_reject::command_string, "reject: backticks");

	check_reject("<settings id=\"a\" title=\"A\" target=\"x&quot;y\"/>",
	             Settings_reject::command_string, "reject: quote");

	check_reject("<settings id=\"a\" title=\"A\" target=\"x&apos;y\"/>",
	             Settings_reject::command_string, "reject: apostrophe");

	check_reject("<settings id=\"a\" title=\"A\" target=\"~root\"/>",
	             Settings_reject::command_string, "reject: tilde");

	{
		/* 64+ chars, all in the bare-name set: overlong is still not
		 * one representable bare payload binary name. */
		char xml[512];
		char name[80];
		memset(name, 'a', 64);
		name[64] = 0;
		snprintf(xml, sizeof(xml),
		         "<settings id=\"a\" title=\"A\" target=\"%s\"/>", name);
		check_reject(xml, Settings_reject::malformed_target,
		             "reject: overlong target");
	}

	check_reject("<settings id=\"a\" title=\"A\" target=\"t\""
	             " exclusive=\"fb\"/>",
	             Settings_reject::exclusive_claim, "reject: exclusive attribute");

	check_reject("<settings id=\"a\" title=\"A\" target=\"t\""
	             " exclusive=\"\"/>",
	             Settings_reject::exclusive_claim,
	             "reject: exclusive attribute (empty value)");

	check_reject("<settings id=\"a\" title=\"A\" target=\"t\">"
	             "<exclusive group=\"fb\"/></settings>",
	             Settings_reject::exclusive_claim,
	             "reject: exclusive claim child");

	{
		/* Duplicate id in one package (the second entry is the
		 * offending one); a different id afterwards is fine. */
		Settings_entry e1 { }, e2 { }, e3 { };
		char const *const x1 =
			"<settings id=\"panel\" title=\"Panel\" target=\"t\"/>";
		char const *const x2 =
			"<settings id=\"panel\" title=\"Other\" target=\"t2\"/>";
		char const *const x3 =
			"<settings id=\"position\" title=\"P\" target=\"t3\"/>";
		Settings_parse const p1 =
			Rules::parse_settings_entry(Genode::Xml_node(x1, strlen(x1)),
			                            e1, nullptr, 0);
		Settings_parse const p2 =
			Rules::parse_settings_entry(Genode::Xml_node(x2, strlen(x2)),
			                            e2, &e1, 1);
		Settings_parse const p3 =
			Rules::parse_settings_entry(Genode::Xml_node(x3, strlen(x3)),
			                            e3, &e1, 1);
		check(p1.reject == Settings_reject::none, "dup id: first accepted");
		check(p2.reject == Settings_reject::duplicate_id, "dup id: second rejected");
		check(strcmp(e2.id.string(), "panel") == 0,
		      "dup id: rejected entry still named for the error");
		check(p3.reject == Settings_reject::none,
		      "dup id: distinct id after a rejection accepted");
	}

	/* ============ 3. singleton open decision (§14.7) ============ */

	{
		using Pkg_launch::Resource_verdict;
		Resource_verdict const adm  = Resource_verdict::admitted();
		Resource_verdict const ref  = Resource_verdict::refused(1, 2, 3, 4);
		Resource_verdict const unp  = Resource_verdict::quota_unparsable();

		check(Rules::settings_decide(false, true, adm, false) ==
		      Settings_outcome::not_installed, "open: unknown id -> not-installed");
		check(Rules::settings_decide(false, true, adm, true) ==
		      Settings_outcome::not_installed,
		      "open: unknown id beats claim (stale) -> not-installed");
		check(Rules::settings_decide(true, false, adm, true) ==
		      Settings_outcome::focused,
		      "open: second open beats binary gate -> focused");
		check(Rules::settings_decide(true, false, adm, false) ==
		      Settings_outcome::binary_missing, "open: missing binary -> binary-missing");
		check(Rules::settings_decide(true, true, ref, false) ==
		      Settings_outcome::insufficient_resources,
		      "open: refused budget -> insufficient-resources");
		check(Rules::settings_decide(true, true, unp, false) ==
		      Settings_outcome::insufficient_resources,
		      "open: unparsable quota -> insufficient-resources");
		check(Rules::settings_decide(true, true, adm, false) ==
		      Settings_outcome::started, "open: first open -> started");
		check(Rules::settings_decide(true, true, adm, true) ==
		      Settings_outcome::focused, "open: second open -> focused");

		/* pre_resource laziness: a refusal before the resource gate
		 * answers the same outcome for EVERY resource verdict. */
		Settings_outcome const pre =
			Rules::settings_decide_pre_resource(true, false, false);
		check(pre == Settings_outcome::binary_missing,
		      "pre_resource: binary gate answer");
		check(Rules::settings_decide(true, false, adm, false) == pre &&
		      Rules::settings_decide(true, false, ref, false) == pre &&
		      Rules::settings_decide(true, false, unp, false) == pre,
		      "pre_resource: dominates the resource verdict");
		check(Rules::settings_decide_pre_resource(true, true, false) ==
		      Settings_outcome::proceed,
		      "pre_resource: proceed means the resource gate decides");
	}

	{
		/* Second-open "no duplicate": the claim set is the emission
		 * driver (one <start> view per claim) and add-once. */
		char const *pkgs[2] { };
		char const *ids[2]  { };
		unsigned n { 0 };

		check(!Rules::settings_claim_in_set(pkgs, ids, n, "sponge-de", "panel"),
		      "claim: empty set has no claim");

		pkgs[n] = "sponge-de"; ids[n] = "panel"; ++n;
		check(Rules::settings_claim_in_set(pkgs, ids, n, "sponge-de", "panel"),
		      "claim: first open claims");
		check(Rules::settings_decide(true, true,
		                             Pkg_launch::Resource_verdict::admitted(),
		                             Rules::settings_claim_in_set(
			                             pkgs, ids, n, "sponge-de", "panel")) ==
		      Settings_outcome::focused,
		      "claim: second open -> focused (no new claim, no new start)");

		check(!Rules::settings_claim_in_set(pkgs, ids, n, "sponge-de", "position"),
		      "claim: distinct id is a distinct identity");
		check(!Rules::settings_claim_in_set(pkgs, ids, n, "other", "panel"),
		      "claim: distinct package is a distinct identity");
	}

	/* ============ 4. start-name derivation ============ */

	{
		Genode::String<64> const a =
			Rules::settings_start_name("sponge-de", "panel");
		Genode::String<64> const b =
			Rules::settings_start_name("sponge-de", "panel");
		Genode::String<64> const c =
			Rules::settings_start_name("sponge-de", "position");
		Genode::String<64> const d =
			Rules::settings_start_name("other", "panel");

		check(strcmp(a.string(), "sponge-de.panel") == 0,
		      "start name: deterministic derivation");
		check(strcmp(a.string(), b.string()) == 0,
		      "start name: stable");
		check(strcmp(a.string(), c.string()) != 0,
		      "start name: distinct id -> distinct name");
		check(strcmp(a.string(), d.string()) != 0,
		      "start name: distinct package -> distinct name");
		check(strcmp(a.string(), "sponge-de") != 0,
		      "start name: never aliases the app start name");

		check(Rules::settings_start_name_valid("sponge-de", "panel"),
		      "start name: normal identity fits the component-name bound");

		char long_pkg[64];
		for (unsigned i = 0; i < 63; ++i) long_pkg[i] = 'p';
		long_pkg[63] = 0;
		check(!Rules::settings_start_name_valid(long_pkg, "x"),
		      "start name: 63-char package plus separator overflows and is rejected");

		char pkg30[31], pkg31[32], id32[33];
		for (unsigned i = 0; i < 30; ++i) pkg30[i] = 'p';
		pkg30[30] = 0;
		for (unsigned i = 0; i < 31; ++i) pkg31[i] = 'p';
		pkg31[31] = 0;
		for (unsigned i = 0; i < 32; ++i) id32[i] = 'i';
		id32[32] = 0;
		check(Rules::settings_start_name_valid(pkg30, id32),
		      "start name: 30+1+32=63 chars is accepted at the exact boundary");
		check(strlen(Rules::settings_start_name(pkg30, id32).string()) == 63,
		      "start name: exact-boundary identity is not truncated");
		check(!Rules::settings_start_name_valid(pkg31, id32),
		      "start name: 31+1+32=64 chars is rejected before truncation");
	}

	/* ============ 5. §14.7 crash/relaunch exit scan ============ */

	{
		/* Sandbox-state-report HID shape: root line, + child
		 * sub-nodes (the 'name' attribute prints tagless as the
		 * first attribute), '-' end marker. */
		char const *const report =
			"state | version: 1\n"
			"+ ram | avail: 32M\n"
			"+ caps | avail: 400\n"
			"+ child sponge-de.panel | binary: panel_settings\n"
			"+ child sponge-de.pos | binary: panel_settings | exited: 1\n"
			"-\n";
		Genode::size_t const len = strlen(report);

		Rules::Settings_provider_state st =
			Rules::settings_provider_state(report, len, "sponge-de.pos");
		check(st.exited && st.code == 1,
		      "exit scan: explicit exited -> released with code");

		st = Rules::settings_provider_state(report, len, "sponge-de.panel");
		check(!st.exited,
		      "exit scan: running child (no exited attr) is not an exit");

		st = Rules::settings_provider_state(report, len, "sponge-de.nope");
		check(!st.exited,
		      "exit scan: absent name is never an exit (report can predate first paint)");

		st = Rules::settings_provider_state(report, len, "other.pos");
		check(!st.exited,
		      "exit scan: another child's exit never releases this name");
	}

	{
		char const *const report =
			"state\n"
			"+ child sponge-de.panel | binary: panel_settings | exited: 0\n"
			"-\n";

		Rules::Settings_provider_state const st =
			Rules::settings_provider_state(
				report, strlen(report), "sponge-de.panel");
		check(st.exited && st.code == 0,
		      "exit scan: clean exited=0 is still an exit");
	}

	{
		/* The 'name' attribute may carry its tag explicitly. */
		char const *const report =
			"state\n"
			"+ child name: other.thing | exited: 7\n"
			"-\n";

		Rules::Settings_provider_state const st =
			Rules::settings_provider_state(
				report, strlen(report), "other.thing");
		check(st.exited && st.code == 7,
		      "exit scan: tagged name attribute matched");
	}

	{
		/* Only top-level <child> nodes count; a child-looking node
		 * nested under another node is not a sandbox child. */
		char const *const report =
			"state\n"
			"+ session foo\n"
			"  + child deep.thing | exited: 5\n"
			"-\n";

		Rules::Settings_provider_state const st =
			Rules::settings_provider_state(
				report, strlen(report), "deep.thing");
		check(!st.exited,
		      "exit scan: nested child-looking node is not a sandbox child");
	}

	{
		/* Malformed or empty input never reports an exit. */
		Rules::Settings_provider_state st =
			Rules::settings_provider_state("", 0, "sponge-de.pos");
		check(!st.exited, "exit scan: empty report is not an exit");

		char const *const garbage = "not a hid document";
		st = Rules::settings_provider_state(
			garbage, strlen(garbage), "sponge-de.pos");
		check(!st.exited, "exit scan: malformed report is not an exit");
	}

	if (failures) {
		printf("self-test: FAIL (%u divergence(s))\n", failures);
		return 1;
	}
	printf("self-test: PASS (settings parse + reject + singleton decision"
	       " + exit scan)\n");
	return 0;
}
