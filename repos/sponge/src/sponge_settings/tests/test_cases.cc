/* SPDX-License-Identifier: Apache-2.0
 *
 * test_cases.cc — deterministic pure test cases for sponge_settings
 * (see test_cases.h).
 */

#include "test_cases.h"

#include <base/node.h>
#include <util/string.h>
#include <util/xml_generator.h>

#include "../model.h"

using namespace Sponge::Settings;


namespace {


struct Checker
{
	unsigned checks = 0, failures = 0;
	Check_fn report;

	void check(bool ok, char const *what)
	{
		checks++;
		if (!ok) failures++;
		report(ok, what);
	}

	void check_eq(long got, long expected, char const *what)
	{
		checks++;
		if (got == expected) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<256> const msg(what, ": expected ", expected,
		                              " got ", got);
		report(false, msg.string());
	}

	void check_str(char const *got, char const *expected, char const *what)
	{
		checks++;
		if (Genode::strcmp(got, expected) == 0) {
			report(true, what);
			return;
		}
		failures++;
		Genode::String<256> const msg(what, ": expected '", expected,
		                              "' got '", got, "'");
		report(false, msg.string());
	}
};


template <typename FN>
char const *gen_xml(char *buf, Genode::size_t buf_len,
                    char const *type, FN const &fn)
{
	auto const res = Genode::Xml_generator::generate({ buf, buf_len }, type, fn);
	if (res.failed()) {
		buf[0] = '\0';
		return buf;
	}
	return buf;
}


Genode::Node node_of(char const *doc)
{
	return Genode::Node {
		Genode::Const_byte_range_ptr(doc, Genode::strlen(doc)) };
}


char const *const GOLDEN_INSTALLED =
	"<installed count=\"3\">"
	"<packages>"

	"<package name=\"sponge-de\" version=\"1.0\">"
	"<settings id=\"panel\" title=\"Panel\" category=\"desktop\""
	" icon=\"panel.png\" target=\"panel_settings\">"
	"<keyword>height</keyword>"
	"<keyword>widgets</keyword>"
	"</settings>"
	"<settings id=\"position\" title=\"Panel Position\""
	" category=\"desktop\" target=\"panel_settings\">"
	"<keyword>edge</keyword>"
	"</settings>"
	"</package>"

	"<package name=\"hw-tools\" version=\"2.0\">"
	"<settings id=\"touch\" title=\"Touchpad\" category=\"hardware\""
	" target=\"touch_settings\"/>"
	"</package>"

	"<package name=\"misc\" version=\"1.0\">"
	"<settings id=\"editor\" title=\"Editor\" category=\"tabbed\""
	" target=\"nano_settings\"/>"
	"<settings id=\"dup\" title=\"Dup One\" target=\"a\"/>"
	"<settings id=\"dup\" title=\"Dup Two\" target=\"b\"/>"
	"<settings id=\"BadCaps\" title=\"Bad\" target=\"c\"/>"
	"<settings id=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""
	" title=\"Too Long\" target=\"d\"/>"
	"<settings id=\"notarget\" title=\"No Target\"/>"
	"<settings id=\"notitle\" target=\"e\"/>"
	"</package>"

	"<package name=\"empty-pkg\" version=\"1.0\"/>"

	"</packages>"
	"</installed>";


void test_discovery(Checker &c)
{
	Model model;
	model.parse(node_of(GOLDEN_INSTALLED));

	c.check_eq(model.num_entries, 5, "golden set: five addressable entries");
	c.check_eq(model.dropped_invalid, 4,
	           "golden set: bad id case, 33-char id, missing target, "
	           "missing title are dropped");
	c.check_eq(model.dropped_duplicate, 1,
	           "golden set: duplicate (package, id) dropped");
	c.check_eq(model.dropped_overflow, 0, "golden set: nothing overflows");
	c.check_eq(model.category_fallbacks, 2,
	           "golden set: unknown 'tabbed' and the absent category "
	           "fall back and are counted");

	Entry const *panel = model.find("sponge-de", "panel");
	c.check(panel != nullptr, "identity (sponge-de, panel) resolves");
	if (panel) {
		c.check_str(panel->pkg.string(), "sponge-de", "entry keeps package");
		c.check_str(panel->title.string(), "Panel", "entry keeps title");
		c.check(panel->category == Category::DESKTOP,
		        "category desktop parses to the closed enum");
		c.check(panel->has_icon(), "icon-if-present: declared icon kept");
		c.check_str(panel->icon.string(), "panel.png", "icon name kept");
		c.check_str(panel->target.string(), "panel_settings",
		            "target kept for diagnostics only");
		c.check_eq(panel->num_keywords, 2, "two keywords kept");
		c.check_str(panel->keywords[0].string(), "height", "keyword 1");
		c.check_str(panel->keywords[1].string(), "widgets", "keyword 2");
	}

	Entry const *position = model.find("sponge-de", "position");
	c.check(position != nullptr, "identity (sponge-de, position) resolves");
	if (position) {
		c.check(!position->has_icon(), "absent icon stays absent");
		c.check_eq(position->num_keywords, 1, "one keyword kept");
	}

	c.check(model.entries[2].category == Category::HARDWARE,
	        "category hardware parses to the closed enum");
	c.check(model.entries[3].category == Category::OTHER,
	        "unknown category falls back to other");
	c.check(model.entries[4].category == Category::OTHER,
	        "absent category falls back to other");

	c.check(model.find("sponge-de", "dup") == nullptr,
	        "dropped duplicate is not addressable");
	c.check(model.find("nope", "panel") == nullptr,
	        "unknown package is not addressable");
}


void test_id_bounds(Checker &c)
{
	char const *doc =
		"<installed><packages><package name=\"p\">"
		"<settings id=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""
		" title=\"T\" target=\"bin\"/>"
		"<settings id=\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\""
		" title=\"T\" target=\"bin\"/>"
		"<settings id=\"has/slash\" title=\"T\" target=\"bin\"/>"
		"<settings id=\"UPPER\" title=\"T\" target=\"bin\"/>"
		"</package></packages></installed>";

	Model model;
	model.parse(node_of(doc));

	c.check_eq(model.num_entries, 1,
	           "id bounds: exactly the 32-char [a-z0-9_-] id survives");
	c.check(model.find("p", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != nullptr,
	        "id bounds: 32-char id is addressable");
	c.check_eq(model.dropped_invalid, 3,
	           "id bounds: 33-char, slash, uppercase ids rejected");
}


void test_keyword_cap(Checker &c)
{
	char const *doc =
		"<installed><packages><package name=\"p\">"
		"<settings id=\"k\" title=\"T\" target=\"bin\">"
		"<keyword>a</keyword><keyword>b</keyword><keyword>c</keyword>"
		"<keyword>d</keyword><keyword>e</keyword><keyword>f</keyword>"
		"</settings></package></packages></installed>";

	Model model;
	model.parse(node_of(doc));

	c.check_eq(model.num_entries, 1, "keyword cap: entry accepted");
	c.check_eq(model.entries[0].num_keywords, Entry::MAX_KEYWORDS,
	           "keyword cap: extra keywords dropped, count bounded");
	c.check_str(model.entries[0].keywords[3].string(), "d",
	            "keyword cap: first keywords are kept");
}


void test_overflow(Checker &c)
{
	char doc[16384];
	unsigned n = 0;

	auto append = [&] (char const *s) {
		while (*s != '\0' && n + 1 < sizeof(doc))
			doc[n++] = *s++;
	};

	append("<installed><packages><package name=\"p\">");

	for (unsigned i = 0; i < Model::MAX_ENTRIES + 2; ++i) {
		char digits[12];
		unsigned len = 1;
		for (unsigned v = i; v >= 10; v /= 10)
			len++;
		digits[len] = '\0';
		for (unsigned v = i, pos = len; pos > 0; v /= 10)
			digits[--pos] = (char)('0' + v % 10);

		append("<settings id=\"e");
		append(digits);
		append("\" title=\"T\" target=\"bin\"/>");
	}

	append("</package></packages></installed>");
	doc[n] = '\0';

	Model model;
	model.parse(node_of(doc));

	c.check_eq(model.num_entries, Model::MAX_ENTRIES,
	           "overflow: capacity holds exactly MAX_ENTRIES");
	c.check_eq(model.dropped_overflow, 2,
	           "overflow: the two extras are counted, not stored");
}


void test_categories(Checker &c)
{
	bool recognized = false;

	c.check(category_from_attr("desktop", &recognized) == Category::DESKTOP
	     && recognized, "category: desktop");
	c.check(category_from_attr("hardware", &recognized) == Category::HARDWARE
	     && recognized, "category: hardware");
	c.check(category_from_attr("system", &recognized) == Category::SYSTEM
	     && recognized, "category: system");
	c.check(category_from_attr("applications", &recognized) == Category::APPLICATIONS
	     && recognized, "category: applications");
	c.check(category_from_attr("other", &recognized) == Category::OTHER
	     && recognized, "category: other");

	c.check(category_from_attr("", &recognized) == Category::OTHER
	     && !recognized, "category: absent falls back to other");
	c.check(category_from_attr("tabbed", &recognized) == Category::OTHER
	     && !recognized, "category: unknown falls back to other");

	c.check_str(category_label(Category::DESKTOP), "Desktop",
	            "category label: desktop");
	c.check_str(category_label(Category::OTHER), "Other",
	            "category label: other");
}


void test_request_shape(Checker &c)
{
	char buf[256];
	char const *doc = gen_xml(buf, sizeof(buf), "request",
		[&] (Genode::Xml_generator &g) {
			gen_settings_request(g, "sponge-de", "panel");
		});

	c.check_str(doc,
		"<request op=\"launch\" target=\"settings\" pkg=\"sponge-de\""
		" id=\"panel\"/>\n",
		"request wire shape is identity-only with the settings "
		"discriminator");

	Genode::Node const root = node_of(doc);
	c.check(root.has_type("request"), "request root type");
	c.check_str(root.attribute_value("op", Genode::String<16>()).string(),
	            "launch", "request op");
	c.check_str(root.attribute_value("target", Genode::String<16>()).string(),
	            "settings", "request target discriminator");
	c.check_str(root.attribute_value("id", Genode::String<16>()).string(),
	            "panel", "request carries the settings-id identity");
	c.check(!root.has_attribute("seq"),
	        "request carries no seq (launcher precedent)");
}


void test_result_shapes(Checker &c)
{
	Launch_result r;

	c.check(parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\" target=\"settings\""
		        " pkg=\"sponge-de\" id=\"panel\""
		        " action=\"started\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::OK_STARTED,
		"result: ok action=started maps to OK_STARTED");

	c.check(parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\" target=\"settings\""
		        " pkg=\"sponge-de\" id=\"panel\""
		        " action=\"focused\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::OK_FOCUSED,
		"result: ok action=focused maps to OK_FOCUSED");

	c.check(parse_settings_result(
		node_of("<result status=\"not-installed\" op=\"launch\""
		        " pkg=\"sponge-de\" id=\"panel\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::NOT_INSTALLED,
		"result: not-installed");

	c.check(parse_settings_result(
		node_of("<result status=\"binary-missing\" op=\"launch\""
		        " pkg=\"sponge-de\" id=\"panel\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::BINARY_MISSING,
		"result: binary-missing");

	c.check(parse_settings_result(
		node_of("<result status=\"insufficient-resources\" op=\"launch\""
		        " pkg=\"sponge-de\" id=\"panel\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::INSUFFICIENT_RESOURCES,
		"result: insufficient-resources");

	c.check(parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\" target=\"settings\""
		        " pkg=\"sponge-de\" settings-id=\"panel\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::OK_STARTED,
		"result: docs-prose settings-id alias is accepted");

	c.check(parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\" target=\"settings\""
		        " pkg=\"sponge-de\" id=\"panel\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::OK_STARTED,
		"result: ok without action defaults to started");

	c.check(parse_settings_result(
		node_of("<result status=\"surprise\" op=\"launch\""
		        " pkg=\"sponge-de\" id=\"panel\"/>"),
		"sponge-de", "panel", r)
	 && r.status == Launch_status::UNKNOWN,
		"result: unknown status is preserved as UNKNOWN");

	c.check(!parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\""
		        " pkg=\"other-pkg\" id=\"panel\"/>"),
		"sponge-de", "panel", r),
		"result: pkg mismatch is not our answer");

	c.check(!parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\""
		        " pkg=\"sponge-de\" id=\"other\"/>"),
		"sponge-de", "panel", r),
		"result: id mismatch is not our answer");

	c.check(!parse_settings_result(
		node_of("<result status=\"ok\" op=\"launch\""
		        " pkg=\"sponge-de\" settings-id=\"other\"/>"),
		"sponge-de", "panel", r),
		"result: alias mismatch is not our answer");

	c.check(!parse_settings_result(
		node_of("<install status=\"ok\"/>"), "sponge-de", "panel", r),
		"result: foreign root is not our answer");

	c.check(!parse_settings_result(
		node_of("<result status=\"ok\" op=\"explain\""
		        " pkg=\"sponge-de\" settings-id=\"panel\"/>"),
		"sponge-de", "panel", r),
		"result: non-launch op is not our answer");

	c.check(parse_settings_result(
		node_of("<result status=\"ok\"/>"), "sponge-de", "panel", r)
	 && r.status == Launch_status::OK_STARTED,
		"result: identity-less result answers the single pending open");

	c.check(Genode::strlen(status_text(Launch_status::PACKAGE_MANAGER_UNAVAILABLE)) > 0,
	        "status text: package-manager-unavailable has a bounded surface");
}


Model grouped_model()
{
	char const *doc =
		"<installed><packages>"
		"<package name=\"a\">"
		"<settings id=\"one\" title=\"One\" category=\"system\" target=\"t\"/>"
		"<settings id=\"two\" title=\"Two\" category=\"desktop\" target=\"t\"/>"
		"</package>"
		"<package name=\"b\">"
		"<settings id=\"three\" title=\"Three\" category=\"desktop\" target=\"t\"/>"
		"<settings id=\"four\" title=\"Four\" target=\"t\"/>"
		"</package>"
		"</packages></installed>";

	Model model;
	model.parse(node_of(doc));
	return model;
}


void test_layout(Checker &c)
{
	Model const model = grouped_model();

	c.check_eq(model.num_entries, 4, "layout: four entries");
	c.check(model.entries[3].category == Category::OTHER,
	        "layout: absent category lands in other");

	Layout::Row rows[Layout::MAX_ROWS];
	unsigned const n = Layout::build_rows(model, rows, Layout::MAX_ROWS);

	c.check_eq(n, 7, "layout: three category headers + four entries");

	c.check(rows[0].kind == Layout::Row::Kind::CATEGORY
	     && rows[0].category == Category::DESKTOP
	     && rows[1].kind == Layout::Row::Kind::ENTRY
	     && rows[1].entry == 1
	     && rows[2].kind == Layout::Row::Kind::ENTRY
	     && rows[2].entry == 2,
	        "layout: desktop header precedes its entries in discovery order");

	c.check(rows[3].kind == Layout::Row::Kind::CATEGORY
	     && rows[3].category == Category::SYSTEM
	     && rows[4].entry == 0,
	        "layout: system follows desktop (closed-enum order)");

	c.check(rows[5].kind == Layout::Row::Kind::CATEGORY
	     && rows[5].category == Category::OTHER
	     && rows[6].kind == Layout::Row::Kind::ENTRY
	     && rows[6].entry == 3
	     && rows[6].y == 3 * Layout::HEADER_H + 3 * Layout::ENTRY_H,
	        "layout: other trails last (closed-enum order)");

	int const expected_h = 3 * Layout::HEADER_H + 4 * Layout::ENTRY_H;
	c.check_eq(Layout::content_height(model), expected_h,
	           "layout: content height sums headers and rows");

	c.check_eq(Layout::clamp_scroll(model, -5, 100), 0,
	           "layout: negative scroll clamps to zero");
	c.check_eq(Layout::clamp_scroll(model, 5000, 100), expected_h - 100,
	           "layout: oversized scroll clamps to content");
	c.check_eq(Layout::clamp_scroll(model, 5000, expected_h), 0,
	           "layout: no scroll space without overflow");

	c.check_eq(Layout::hit_entry(model, 0, 1000, 1), -1,
	           "layout: category header is not an entry hit");
	c.check_eq(Layout::hit_entry(model, 0, 1000,
	                             Layout::HEADER_H + 1), 1,
	           "layout: first desktop row hit maps to its entry");
	c.check_eq(Layout::hit_entry(model, Layout::ENTRY_H, 1000,
	                             Layout::HEADER_H + 1), 2,
	           "layout: hit-testing follows the scroll offset");
	c.check_eq(Layout::hit_entry(model, 0, 1000, 999999), -1,
	           "layout: out-of-range click is a miss");
}


} /* anonymous namespace */


unsigned Sponge::Settings::run_test_cases(Check_fn report)
{
	Checker c;
	c.report = report;

	test_discovery(c);
	test_id_bounds(c);
	test_keyword_cap(c);
	test_overflow(c);
	test_categories(c);
	test_request_shape(c);
	test_result_shapes(c);
	test_layout(c);

	return c.failures;
}
