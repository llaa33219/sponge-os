/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test for vct's Settings wire adapter
 * (repos/sponge/src/vct/settings_wire.{h,cc}) — the focused cases the
 * vct settings commands rest on: installed-metadata entry collection,
 * category grouping (desktop/hardware/system/applications/other),
 * settings-open request shape, and settings-open result handling
 * (ok started/focused, refusals, budget figures, and the id /
 * settings-id identity aliases).
 *
 * NOT part of the Genode build (deliberately absent from target.mk).
 * It executes the shipped settings_wire.cc on the host, linked
 * against Genode's own public string/XML sources — the same pattern
 * as src/sponge_settings/tests/self_test.cc.
 *
 * BUILD & RUN (from the repository root; host g++ only):
 *
 *   repos/sponge/src/vct/tests/check.sh
 *
 * (check.sh runs this exact compile line; see its header.)
 *
 * Exit code 0 and a final "self-test: PASS (N checks)" line mean every
 * check matched. "self-test: FAIL" prints every divergence.
 *
 * The host stubs below (Genode::Log, Genode::Lock, Genode::raise)
 * exist only to satisfy the linker for symbols the tested code never
 * reaches; the 'raise' stub aborts loudly if that assumption is ever
 * wrong. Same pattern as src/sponge_settings/tests/self_test.cc.
 *
 * Note on -std: the Genode public headers use C++20 abbreviated
 * templates, so the host build compiles with -std=c++20; the tested
 * sources stay in the C++17 subset per AGENTS.md §3.1.
 */

#include "../settings_wire.h"

#include <base/error.h>
#include <base/lock.h>
#include <base/log.h>
#include <util/string.h>

#include <stdio.h>
#include <stdlib.h>

using namespace Sponge::Vct::Settings_wire;


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

void Genode::raise(Genode::Unexpected_error)
{
	fprintf(stderr, "self-test stub: Genode::raise(Unexpected_error) reached\n");
	abort();
}


/* ========================== harness ============================== */

namespace {

unsigned checks = 0, failures = 0;

void check(bool ok, char const *what)
{
	checks++;
	if (ok)
		return;
	failures++;
	printf("FAIL: %s\n", what);
}

void check_eq(long got, long expected, char const *what)
{
	checks++;
	if (got == expected)
		return;
	failures++;
	printf("FAIL: %s: expected %ld got %ld\n", what, expected, got);
}

void check_str(char const *got, char const *expected, char const *what)
{
	checks++;
	if (Genode::strcmp(got, expected) == 0)
		return;
	failures++;
	printf("FAIL: %s: expected '%s' got '%s'\n", what, expected, got);
}

Genode::Xml_node node_of(char const *doc)
{
	return Genode::Xml_node(doc, Genode::strlen(doc));
}


/* ===================== category enum ===================== */

void test_category_enum()
{
	bool recognized = false;

	check(category_from_attr("desktop", &recognized) == Category::DESKTOP &&
	      recognized, "category: desktop recognized");
	check(category_from_attr("hardware", &recognized) == Category::HARDWARE &&
	      recognized, "category: hardware recognized");
	check(category_from_attr("system", &recognized) == Category::SYSTEM &&
	      recognized, "category: system recognized");
	check(category_from_attr("applications", &recognized) == Category::APPLICATIONS &&
	      recognized, "category: applications recognized");
	check(category_from_attr("other", &recognized) == Category::OTHER &&
	      recognized, "category: other recognized");

	check(category_from_attr("tabbed", &recognized) == Category::OTHER &&
	      !recognized, "category: unknown falls back to other");
	check(category_from_attr("", &recognized) == Category::OTHER &&
	      !recognized, "category: absent falls back to other");

	check_str(category_label(Category::DESKTOP), "desktop", "label: desktop");
	check_str(category_label(Category::HARDWARE), "hardware", "label: hardware");
	check_str(category_label(Category::SYSTEM), "system", "label: system");
	check_str(category_label(Category::APPLICATIONS), "applications",
	          "label: applications");
	check_str(category_label(Category::OTHER), "other", "label: other");
}


/* ===================== list: collection + grouping ===================== */

char const *const GOLDEN_INSTALLED =
	"<installed count=\"2\">"
	"<packages>"

	"<package name=\"sponge-de\" version=\"1.0\">"
	"<settings id=\"panel\" title=\"Panel\" category=\"desktop\""
	" target=\"panel_settings\"/>"
	"<settings id=\"touch\" title=\"Touchpad\" category=\"hardware\""
	" target=\"touch_settings\"/>"
	"<settings id=\"fallback\" title=\"Fallback\" category=\"tabbed\""
	" target=\"misc_settings\"/>"
	"<settings id=\"nocat\" title=\"No Category\" target=\"misc_settings\"/>"
	"</package>"

	"<package name=\"sys-tools\" version=\"1.0\">"
	"<settings id=\"sys\" title=\"System Tuner\" category=\"system\""
	" target=\"tuner\"/>"
	"<settings id=\"app\" title=\"App Defaults\" category=\"applications\""
	" target=\"defaults\"/>"
	"<settings id=\"noname\" title=\"\" target=\"t\"/>"
	"</package>"

	"<package name=\"empty-pkg\" version=\"1.0\"/>"

	"</packages>"
	"</installed>";


void test_list_collection()
{
	Entry entries[MAX_ENTRIES] { };
	unsigned const n =
		collect_entries(node_of(GOLDEN_INSTALLED), entries, MAX_ENTRIES);

	check_eq(n, 6, "collect: six addressable entries (empty title dropped)");

	check_str(entries[0].pkg.string(), "sponge-de", "entry 0: package");
	check_str(entries[0].id.string(), "panel", "entry 0: settings-id");
	check_str(entries[0].title.string(), "Panel", "entry 0: title");
	check(entries[0].category == Category::DESKTOP, "entry 0: desktop category");

	check_str(entries[2].id.string(), "fallback", "entry 2: id kept");
	check(entries[2].category == Category::OTHER,
	      "entry 2: unknown category falls back to other");
	check(entries[3].category == Category::OTHER,
	      "entry 3: absent category falls back to other");
	check(entries[4].category == Category::SYSTEM, "entry 4: system category");
	check(entries[5].category == Category::APPLICATIONS,
	      "entry 5: applications category");

	Entry bounded[2] { };
	check_eq(collect_entries(node_of(GOLDEN_INSTALLED), bounded, 2), 2,
	         "collect: bounded at the caller's cap");

	/* the `list` result carries the same <packages> subtree */
	char const *const list_result =
		"<result status=\"ok\" op=\"list\" count=\"1\"><packages>"
		"<package name=\"sponge-de\">"
		"<settings id=\"panel\" title=\"Panel\" category=\"desktop\""
		" target=\"panel_settings\"/>"
		"</package></packages></result>";
	Entry from_result[4] { };
	check_eq(collect_entries(node_of(list_result), from_result, 4), 1,
	         "collect: accepts the list-result <packages> shape");
	check_str(from_result[0].id.string(), "panel",
	          "collect: list-result entry keeps id");
}


void test_list_grouping()
{
	Entry entries[MAX_ENTRIES] { };
	unsigned const n =
		collect_entries(node_of(GOLDEN_INSTALLED), entries, MAX_ENTRIES);

	unsigned order[MAX_ENTRIES] { };
	unsigned const m = group_order(entries, n, order, MAX_ENTRIES);

	check_eq(m, n, "group: every entry is placed");

	/* closed-enum order: desktop, hardware, system, applications,
	 * other; discovery order within one category */
	check_eq(order[0], 0, "group: desktop first (panel)");
	check_eq(order[1], 1, "group: hardware second (touch)");
	check_eq(order[2], 4, "group: system third (sys)");
	check_eq(order[3], 5, "group: applications fourth (app)");
	check_eq(order[4], 2, "group: other keeps discovery order (fallback)");
	check_eq(order[5], 3, "group: other keeps discovery order (nocat)");

	check_eq((long)entries[order[0]].category, (long)Category::DESKTOP,
	         "group: header order starts at desktop");
	check_eq((long)entries[order[m - 1]].category, (long)Category::OTHER,
	         "group: header order ends at other");

	unsigned small[2] { };
	check_eq(group_order(entries, n, small, 2), 2,
	         "group: bounded at the caller's cap");
}


/* ===================== open: request shape ===================== */

void test_open_request()
{
	char buf[256] { };
	auto const res = Genode::Xml_generator::generate(
		{ buf, sizeof(buf) }, "request", [&] (Genode::Xml_generator &g) {
			gen_settings_request(g, "sponge-de", "panel");
		});
	check(!res.failed(), "request: generates without buffer error");
	Genode::size_t len = Genode::strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
		buf[--len] = '\0';
	check_str(buf,
		"<request op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\"/>",
		"request: exact §14.5 shape (id primary, identity only)");
}


/* ===================== open: result handling ===================== */

Launch_result parse(char const *doc, char const *pkg, char const *id,
                    bool &answered)
{
	Launch_result out;
	answered = parse_settings_result(node_of(doc), pkg, id, out);
	return out;
}

void test_open_results()
{
	bool answered = false;

	Launch_result r = parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\" action=\"started\"/>",
		"sponge-de", "panel", answered);
	check(answered, "open ok/started: answered");
	check(r.status == Launch_status::OK_STARTED, "open ok/started: status");

	r = parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\" action=\"focused\"/>",
		"sponge-de", "panel", answered);
	check(answered, "open ok/focused: answered");
	check(r.status == Launch_status::OK_FOCUSED, "open ok/focused: status");

	r = parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check(answered, "open ok without action: answered");
	check(r.status == Launch_status::OK_STARTED,
	      "open ok without action: counts as fresh start");

	r = parse(
		"<result status=\"not-installed\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check(answered && r.status == Launch_status::NOT_INSTALLED,
	      "open refusal: not-installed");

	r = parse(
		"<result status=\"binary-missing\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check(answered && r.status == Launch_status::BINARY_MISSING,
	      "open refusal: binary-missing");

	r = parse(
		"<result status=\"insufficient-resources\" op=\"launch\""
		" target=\"settings\" pkg=\"sponge-de\" id=\"panel\""
		" need_ram=\"48000000\" avail_ram=\"1000000\""
		" need_caps=\"512\" avail_caps=\"64\"/>",
		"sponge-de", "panel", answered);
	check(answered && r.status == Launch_status::INSUFFICIENT_RESOURCES,
	      "open refusal: insufficient-resources");
	check_str(r.need_ram.string(), "48000000", "open refusal: need_ram figure");
	check_str(r.avail_ram.string(), "1000000", "open refusal: avail_ram figure");
	check_str(r.need_caps.string(), "512", "open refusal: need_caps figure");
	check_str(r.avail_caps.string(), "64", "open refusal: avail_caps figure");

	r = parse(
		"<result status=\"insufficient-resources\" op=\"launch\""
		" target=\"settings\" pkg=\"sponge-de\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check_str(r.need_ram.string(), "?",
	          "open refusal: absent figures render as '?'");

	r = parse(
		"<result status=\"error\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\" error=\"boom\"/>",
		"sponge-de", "panel", answered);
	check(answered && r.status == Launch_status::UNKNOWN,
	      "open structured error: mapped to UNKNOWN");
	check_str(r.error.string(), "boom", "open structured error: message kept");
}


void test_open_identity_aliases()
{
	bool answered = false;

	/* 'settings-id' is the docs-prose alias for the primary 'id' */
	Launch_result r = parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" settings-id=\"panel\" action=\"focused\"/>",
		"sponge-de", "panel", answered);
	check(answered, "alias: settings-id accepted on results");
	check(r.status == Launch_status::OK_FOCUSED, "alias: action still mapped");

	/* identity mismatch is never our answer */
	parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"theme\"/>",
		"sponge-de", "panel", answered);
	check(!answered, "identity: different id rejected");

	parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\" settings-id=\"theme\"/>",
		"sponge-de", "panel", answered);
	check(!answered, "identity: different settings-id alias rejected");

	parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"other-pkg\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check(!answered, "identity: different package rejected");

	parse(
		"<result status=\"ok\" op=\"launch\" target=\"settings\""
		" pkg=\"sponge-de\"/>",
		"sponge-de", "panel", answered);
	check(!answered,
	      "identity: absent identity rejected (shared result label)");

	/* plain launch results share vct's result label and must never
	 * satisfy a settings open */
	parse(
		"<result status=\"ok\" op=\"launch\" pkg=\"sponge-de\"/>",
		"sponge-de", "panel", answered);
	check(!answered, "shared label: plain launch result rejected");

	parse(
		"<result status=\"ok\" op=\"launch\" target=\"app\""
		" pkg=\"sponge-de\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check(!answered, "shared label: non-settings target rejected");

	parse(
		"<result status=\"ok\" op=\"install\" target=\"settings\""
		" pkg=\"sponge-de\" id=\"panel\"/>",
		"sponge-de", "panel", answered);
	check(!answered, "foreign op rejected");

	parse("<config/>", "sponge-de", "panel", answered);
	check(!answered, "non-result document rejected");
}

}  /* anonymous namespace */


int main()
{
	test_category_enum();
	test_list_collection();
	test_list_grouping();
	test_open_request();
	test_open_results();
	test_open_identity_aliases();

	if (failures != 0) {
		printf("self-test: FAIL (%u failed checks)\n", failures);
		return 1;
	}

	printf("self-test: PASS (%u checks)\n", checks);
	return 0;
}
