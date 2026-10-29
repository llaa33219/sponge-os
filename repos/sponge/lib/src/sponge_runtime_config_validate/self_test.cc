/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test for sponge_runtime_config_validate (Phase 16
 * P1.5 publication seam).
 *
 * NOT part of the Genode build (deliberately absent from
 * lib/mk/sponge_runtime_config_validate.mk), so it lives outside the
 * shared build run. It proves the render -> validate -> publish gate:
 *
 *   1. Positive: every configuration sponge_pkg_config renders for
 *      the app and driver targets and for the simple/terminal/browser
 *      runtime-class targets (mixed app+driver fixture, mixed-class
 *      fixture, empty installed set, empty-class set, boot-module and
 *      disk-served binary prefixes, the repro-padding knob) validates
 *      OK. Hand-written literals
 *      additionally pin the accepted shapes independently of the
 *      emitter (including the documented <repro_padding> test-only
 *      exception).
 *
 *   2. Negative: malformed XML, structural nodes/attributes outside
 *      the pkgd-emitted subset, bad lexemes, missing required fields,
 *      and duplicate <start>/service names are rejected with the
 *      expected structured code and a non-empty detail reason. A
 *      raw-append breakout (hostile metadata <config> fragment) is
 *      rejected too.
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
 *       repos/sponge/lib/src/sponge_runtime_config_validate/self_test.cc \
 *       repos/sponge/lib/src/sponge_runtime_config_validate/runtime_config_validate.cc \
 *       repos/sponge/lib/src/sponge_pkg_config/pkg_config.cc \
 *       genode/repos/base/src/lib/base/xml_generator.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_runtime_config_validate_self_test \
 *   && /tmp/sponge_runtime_config_validate_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every case
 * behaved as expected. "self-test: FAIL" prints the first divergence.
 */

#include <sponge/runtime_config_validate.h>
#include <sponge/pkg_config.h>

#include <stdio.h>
#include <string.h>
#include <string>

using Sponge::Runtime_config_validate::Code;
using Sponge::Runtime_config_validate::Result;
using Sponge::Runtime_config_validate::Workspace;

namespace {

/* ===================== comparison harness ===================== */

unsigned cases    = 0;
unsigned failures = 0;

void fail(char const *what, char const *why, Result const &r)
{
	++failures;
	printf("FAIL: %s\n", what);
	printf("  %s\n", why);
	printf("  code=%d detail=%s\n", (int)r.code, r.detail);
}

void expect_ok(char const *what, std::string const &xml)
{
	++cases;

	Workspace ws;
	Result const r = Sponge::Runtime_config_validate::validate(
		xml.data(), (unsigned)xml.size(), ws);

	if (!r.ok || r.code != Code::OK)
		fail(what, "expected validation OK", r);
	else if (r.detail[0] != '\0')
		fail(what, "expected empty detail on OK", r);
}

void expect_err(char const *what, Code expected, std::string const &xml)
{
	++cases;

	Workspace ws;
	Result const r = Sponge::Runtime_config_validate::validate(
		xml.data(), (unsigned)xml.size(), ws);

	if (r.ok)
		fail(what, "expected rejection, but validation passed", r);
	else if (r.code != expected)
		fail(what, "unexpected error code", r);
	else if (r.detail[0] == '\0')
		fail(what, "expected a non-empty detail reason", r);
}


/* ===================== rendered fixtures ===================== */

/*
 * App+driver mixed set (mirrors sponge_pkg_config's self-test
 * fixtures): verbatim inline config with arbitrary attributes (opaque
 * content), the <pkg>-ro label rule, a sibling <child> route, an
 * explicit label, a 31-char session name, a stopped package (its
 * sessions shape <parent-provides> without a <start>), and driver
 * packages that only land in the driver target.
 */
Sponge::Pkg_config::Package_view const FIX_MIXED[] = {
	{ .name = "zeta", .binary = "zeta_bin", .ram = "8M", .caps = 128,
	  .config_xml = "<config a=\"b\"><vfs><any/></vfs></config>",
	  .runtime_class = "app", .running = true,
	  .sessions = {
		{ .name = "Gui", .route = "nitpicker" },
		{ .name = "File_system", .route = "vfs", .readonly = true },
		{ .name = "ROM", .route = "parent" } },
	  .num_sessions = 3 },

	{ .name = "alpha", .binary = "alpha", .ram = "4M", .caps = 64,
	  .runtime_class = "app", .running = false,
	  .sessions = {
		{ .name = "Gui", .route = "" },
		{ .name = "Terminal", .route = "term-bridge" },
		{ .name = "Custom", .route = "nitpicker", .label = "my-label" } },
	  .num_sessions = 3 },

	{ .name = "mid", .binary = "mid", .ram = "2M", .caps = 32,
	  .runtime_class = "app", .running = true,
	  .sessions = {
		{ .name = "Gui", .route = "parent" },
		{ .name = "Sess", .route = "" },
		{ .name = "WideNameAAAAAAAAAAAAAAAAAAAAA", .route = "wm" } },
	  .num_sessions = 3 },

	{ .name = "beta", .binary = "nic_drv", .ram = "16M", .caps = 256,
	  .runtime_class = "driver", .running = true,
	  .sessions = {
		{ .name = "Platform", .route = "" },
		{ .name = "Nic", .route = "" } },
	  .num_sessions = 2 },

	{ .name = "gamma", .binary = "gamma", .ram = "4M", .caps = 16,
	  .runtime_class = "driver", .running = false,
	  .sessions = {
		{ .name = "Platform", .route = "" },
		{ .name = "Dma", .route = "" } },
	  .num_sessions = 2 },
};

Sponge::Pkg_config::Package_view const FIX_PAD[] = {
	{ .name = "solo", .binary = "solo", .ram = "1M", .caps = 8,
	  .runtime_class = "app", .running = true,
	  .sessions = { { .name = "Gui", .route = "parent" } },
	  .num_sessions = 1 },
};

/* Hostile metadata fragment: breaks out of the <start> structure. */
Sponge::Pkg_config::Package_view const FIX_BREAKOUT[] = {
	{ .name = "pwn", .binary = "pwn", .ram = "1M", .caps = 8,
	  .config_xml = "</start><alias name=\"x\" child=\"y\"/>",
	  .runtime_class = "app", .running = true,
	  .num_sessions = 0 },
};

/* Unbalanced inline fragment: makes the rendered document malformed. */
Sponge::Pkg_config::Package_view const FIX_MALFORMED[] = {
	{ .name = "broken", .binary = "broken", .ram = "1M", .caps = 8,
	  .config_xml = "<config><oops>",
	  .runtime_class = "app", .running = true,
	  .num_sessions = 0 },
};

/*
 * Runtime-class fixture (docs/12-package-format.md §4.5
 * <runtime_class>): one member per simple/terminal/browser class plus
 * a legacy "app"-token and a driver package. Exercises the class
 * target presets end to end: the class-private session union, the
 * <pkg>-ro label rule, a sibling <child> route, an explicit label,
 * verbatim inline config, and a binary != name <binary> node.
 */
Sponge::Pkg_config::Package_view const FIX_CLASSES[] = {
	{ .name = "sim_a", .binary = "sim_a", .ram = "4M", .caps = 64,
	  .runtime_class = "simple", .running = true,
	  .sessions = {
		{ .name = "Gui", .route = "parent" },
		{ .name = "SimpleOnly", .route = "" } },
	  .num_sessions = 2 },

	{ .name = "sim_b", .binary = "sim_b_bin", .ram = "2M", .caps = 32,
	  .runtime_class = "simple", .running = false,
	  .sessions = {
		{ .name = "SimpleStopped", .route = "" } },
	  .num_sessions = 1 },

	{ .name = "ter_a", .binary = "ter_a", .ram = "8M", .caps = 128,
	  .config_xml = "<config><libc/></config>",
	  .runtime_class = "terminal", .running = true,
	  .sessions = {
		{ .name = "Terminal", .route = "term-bridge" },
		{ .name = "TerminalOnly", .route = "wm", .label = "term-label" } },
	  .num_sessions = 2 },

	{ .name = "bro_a", .binary = "bro_a_bin", .ram = "32M", .caps = 512,
	  .runtime_class = "browser", .running = true,
	  .sessions = {
		{ .name = "BrowserOnly", .route = "nitpicker" },
		{ .name = "File_system", .route = "vfs", .readonly = true } },
	  .num_sessions = 2 },

	{ .name = "legacy_app", .binary = "legacy_app", .ram = "1M", .caps = 8,
	  .runtime_class = "app", .running = true,
	  .sessions = { { .name = "LegacyOnly", .route = "" } },
	  .num_sessions = 1 },

	{ .name = "drv_hw", .binary = "drv_hw", .ram = "16M", .caps = 256,
	  .runtime_class = "driver", .running = true,
	  .sessions = {
		{ .name = "Platform", .route = "" },
		{ .name = "Dma", .route = "" } },
	  .num_sessions = 2 },
};

constexpr unsigned NUM_CLASS_PACKAGES =
	sizeof(FIX_CLASSES) / sizeof(FIX_CLASSES[0]);

char render_buf[65536];

std::string render(Sponge::Pkg_config::Target const &target,
                   Sponge::Pkg_config::Package_view const *packages,
                   unsigned num_packages,
                   Sponge::Pkg_config::Emit_options const &options)
{
	Genode::size_t const used = Sponge::Pkg_config::emit_config(
		render_buf, sizeof(render_buf), target, packages,
		num_packages, options);

	if (used == 0) {
		printf("FAIL: render buffer exhausted\n");
		++failures;
		return std::string("<config/>");
	}
	/* 'used' includes the terminating NUL */
	return std::string(render_buf, used - 1);
}


/* ===================== hand-written literals ===================== */

char const *const HDR =
	"<config>\n"
	"  <report delay_ms=\"2000\" init_ram=\"yes\" init_caps=\"yes\""
	" child_ram=\"yes\"/>\n"
	"  <parent-provides>\n"
	"    <service name=\"ROM\"/>\n"
	"    <service name=\"File_system\"/>\n"
	"  </parent-provides>\n"
	"  <default-route>"
	"<any-service><parent/><any-child/></any-service>"
	"</default-route>\n"
	"  <default caps=\"1000\"/>\n";

char const *const FTR = "</config>\n";

std::string valid_start(char const *name, char const *caps)
{
	std::string s;
	s += "  <start name=\"";
	s += name;
	s += "\" caps=\"";
	s += caps;
	s += "\">\n"
	     "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	     "    <route>\n"
	     "      <service name=\"ROM\" label_last=\"ld.lib.so\">"
	     "<parent/></service>\n"
	     "      <any-service><parent/></any-service>\n"
	     "    </route>\n"
	     "  </start>\n";
	return s;
}

std::string doc(std::string const &body)
{
	return std::string(HDR) + body + FTR;
}


/*64-character pad value (the shape the emitter uses) */
std::string pad64(char fill = '.')
{
	return std::string(64, fill);
}

}  /* anonymous namespace */


int main()
{
	using Sponge::Pkg_config::Emit_options;
	using Sponge::Pkg_config::Package_view;
	using Sponge::Pkg_config::Target;

	/* ============ 1. rendered fixtures validate OK ============ */

	{
		Target const app = Sponge::Pkg_config::app_target();
		Target const drv = Sponge::Pkg_config::driver_target();
		Target const simple   = Sponge::Pkg_config::simple_target();
		Target const terminal = Sponge::Pkg_config::terminal_target();
		Target const browser  = Sponge::Pkg_config::browser_target();

		for (int pass = 0; pass < 2; ++pass) {
			/* pass 0: disk-served ("bin/"), pass 1: boot modules */
			Emit_options options { };
			options.binary_prefix = pass == 0 ? "bin/" : "";

			expect_ok("rendered: mixed/app",
			          render(app, FIX_MIXED, 5, options));
			expect_ok("rendered: mixed/driver",
			          render(drv, FIX_MIXED, 5, options));
			expect_ok("rendered: empty/app",
			          render(app, nullptr, 0, options));
			expect_ok("rendered: empty/driver",
			          render(drv, nullptr, 0, options));

			/*
			 * Class targets (P2.4 pure-emission presets): each
			 * class renders and validates over the mixed-class
			 * fixture; a class with no member (browser over the
			 * simple-only slice) renders the empty-class shape
			 * and must validate too.
			 */
			expect_ok("rendered: classes/simple",
			          render(simple, FIX_CLASSES, NUM_CLASS_PACKAGES,
			                 options));
			expect_ok("rendered: classes/terminal",
			          render(terminal, FIX_CLASSES, NUM_CLASS_PACKAGES,
			                 options));
			expect_ok("rendered: classes/browser",
			          render(browser, FIX_CLASSES, NUM_CLASS_PACKAGES,
			                 options));
			expect_ok("rendered: classes/empty (browser, no member)",
			          render(browser, FIX_CLASSES, 2, options));
			expect_ok("rendered: classes/empty (terminal, no member)",
			          render(terminal, FIX_CLASSES, 2, options));
		}

		/* the repro-padding knob (test-only) passes as documented */
		Emit_options pad_options { };
		pad_options.binary_prefix = "bin/";
		pad_options.pad_bytes     = 128;
		expect_ok("rendered: pad/app (repro_padding accepted)",
		          render(app, FIX_PAD, 1, pad_options));

		/*
		 * Regression pin for a return-status bug: repro_pad_grow pads
		 * write N with N * REPRO_PAD_GROW_BYTES (pkgd.h) and the
		 * emitter turns that into pad_bytes/64 <p> nodes WITHOUT an
		 * upper bound, so a shape-valid <repro_padding> may carry far
		 * more than 1024 children. The validator used to reject the
		 * overflow ("more than 1024 <p> nodes" -> INVALID_VALUE),
		 * which failed the regen-survival scenario's write 3 (98304
		 * pad bytes = 1536 nodes) although the document is exactly the
		 * documented test-only exception. Render that exact budget and
		 * require OK; the shape gates (below) must keep rejecting
		 * genuine violations regardless of node count.
		 */
		{
			static char pad3_buf[262144];

			Emit_options pad3 { };
			pad3.binary_prefix = "bin/";
			pad3.pad_bytes     = 98304;

			Genode::size_t const used = Sponge::Pkg_config::emit_config(
				pad3_buf, sizeof(pad3_buf), app, FIX_PAD, 1, pad3);

			if (used == 0) {
				++cases;
				fail("rendered: repro_pad_grow write-3 pad accepted",
				     "render buffer exhausted", Result());
			} else {
				expect_ok("rendered: repro_pad_grow write-3 pad accepted "
				          "(1536 <p> nodes)",
				          std::string(pad3_buf, used - 2));
			}
		}
	}

	/* ============ 2. hand-written positive literals ============ */

	expect_ok("literal: minimal document",
	          doc(valid_start("alpha", "64")));

	expect_ok("literal: no <start> at all",
	          doc(""));

	expect_ok("literal: opaque inline <config> with attrs",
	          doc("  <start name=\"zeta\" caps=\"128\">\n"
	              "    <binary name=\"bin/zeta_bin\"/>\n"
	              "    <resource name=\"RAM\" quantum=\"8M\"/>\n"
	              "    <config a=\"b\"><vfs><any/></vfs></config>\n"
	              "    <route>\n"
	              "      <service name=\"File_system\" label=\"zeta-ro\">"
	              "<child name=\"files\"/></service>\n"
	              "      <any-service><parent/></any-service>\n"
	              "    </route>\n"
	              "  </start>\n"));

	expect_ok("literal: repro_padding shape (test-only exception)",
	          doc("  <repro_padding>\n"
	              "    <p v=\"" + pad64() + "\"/>\n"
	              "    <p v=\"" + pad64('#') + "\"/>\n"
	              "  </repro_padding>\n" + valid_start("alpha", "64")));

	expect_ok("literal: comment and whitespace tolerated",
	          doc("<!-- stage -->\n  " + valid_start("alpha", "64") +
	              "  <!-- end -->\n"));

	/* ============ 3. malformed XML ============ */

	expect_err("malformed: truncated document", Code::XML_SYNTAX,
	           "<config><report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>");

	expect_err("malformed: mismatched end tag", Code::XML_SYNTAX,
	           "<config><report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"></config>");

	expect_err("malformed: trailing content after root", Code::XML_SYNTAX,
	           doc(valid_start("alpha", "64")) + "junk");

	expect_err("malformed: second root element", Code::XML_SYNTAX,
	           doc(valid_start("alpha", "64")) + "<config/>");

	expect_err("malformed: bad entity in attribute", Code::XML_SYNTAX,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1&2\"/>\n"
	           "</config>\n");

	expect_err("malformed: unquoted attribute value", Code::XML_SYNTAX,
	           "<config><report delay_ms=2000/></config>");

	expect_err("malformed: unterminated attribute quote", Code::XML_SYNTAX,
	           "<config><report delay_ms=\"2000/></config>");

	expect_err("malformed: doctype rejected", Code::XML_SYNTAX,
	           "<!DOCTYPE config><config/>");

	expect_err("malformed: processing instruction rejected",
	           Code::XML_SYNTAX,
	           "<?xml version=\"1.0\"?><config/>");

	expect_err("malformed: unterminated comment", Code::XML_SYNTAX,
	           "<config><!-- oops</config>");

	expect_err("malformed: stray end tag after root", Code::XML_SYNTAX,
	           doc(valid_start("alpha", "64")) + "</config>");

	/* ============ 4. unknown structural nodes ============ */

	expect_err("unknown node: top-level <alias>", Code::UNKNOWN_NODE,
	           doc("  <alias name=\"x\" child=\"y\"/>\n"));

	expect_err("unknown node: top-level <heartbeat>", Code::UNKNOWN_NODE,
	           doc("  <heartbeat rate_ms=\"100\"/>\n"));

	expect_err("unknown node: second <report>", Code::UNKNOWN_NODE,
	           std::string(HDR) +
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n" + FTR);

	expect_err("unknown node: <start> child <heartbeat>",
	           Code::UNKNOWN_NODE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <heartbeat/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown node: <start> child <provides>", Code::UNKNOWN_NODE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <provides><service name=\"X\"/></provides>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown node: per-service default-route entry",
	           Code::UNKNOWN_NODE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<service name=\"Gui\"><parent/></service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("unknown node: repro_padding child <q>",
	           Code::UNKNOWN_NODE,
	           doc("  <repro_padding><q/></repro_padding>\n"));

	expect_err("unknown node: route entry with two targets",
	           Code::UNKNOWN_NODE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"Gui\">"
	               "<parent/><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown node: route <any-service> with <any-child>",
	           Code::UNKNOWN_NODE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><any-child/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown node: root element not <config>", Code::UNKNOWN_NODE,
	           "<runtime/>");

	expect_err("unknown node: parent-provides child <any-service>",
	           Code::UNKNOWN_NODE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><any-service/></parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	/* ============ 5. unknown attributes ============ */

	expect_err("unknown attr: report 'buffer'", Code::UNKNOWN_ATTRIBUTE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\" buffer=\"4K\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("unknown attr: start 'priority'", Code::UNKNOWN_ATTRIBUTE,
	           doc("  <start name=\"alpha\" caps=\"64\" priority=\"1\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown attr: default 'ram'", Code::UNKNOWN_ATTRIBUTE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\" ram=\"1M\"/>\n"
	           "</config>\n");

	expect_err("unknown attr: route service 'label_prefix'",
	           Code::UNKNOWN_ATTRIBUTE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"Gui\" label_prefix=\"a\">"
	               "<parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown attr: resource 'preserve'", Code::UNKNOWN_ATTRIBUTE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\""
	               " preserve=\"x\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("unknown attr: root config 'verbose'",
	           Code::UNKNOWN_ATTRIBUTE,
	           "<config verbose=\"yes\">\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("unknown attr: parent-provides service 'label'",
	           Code::UNKNOWN_ATTRIBUTE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides>"
	           "<service name=\"ROM\" label=\"x\"/></parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("unknown attr: <p> extra attribute", Code::UNKNOWN_ATTRIBUTE,
	           doc("  <repro_padding>"
	               "<p v=\"" + pad64() + "\" extra=\"x\"/>"
	               "</repro_padding>\n"));

	/* ============ 6. invalid values / bad lexemes ============ */

	expect_err("bad lexeme: delay_ms '0'", Code::INVALID_VALUE,
	           "<config>\n"
	           "  <report delay_ms=\"0\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("bad lexeme: init_ram 'maybe'", Code::INVALID_VALUE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"maybe\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("bad lexeme: default caps '0'", Code::INVALID_VALUE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"0\"/>\n"
	           "</config>\n");

	expect_err("bad lexeme: start caps '12x'", Code::INVALID_VALUE,
	           doc("  <start name=\"alpha\" caps=\"12x\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("bad lexeme: quantum '48MB'", Code::INVALID_VALUE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"48MB\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("bad lexeme: quantum '0K' (zero)", Code::INVALID_VALUE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"0K\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("bad value: resource name 'CAP' (subset is RAM)",
	           Code::INVALID_VALUE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"CAP\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("bad lexeme: empty service name", Code::INVALID_VALUE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("bad lexeme: service name with space", Code::INVALID_VALUE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"Bad Name\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("bad lexeme: service name over 31 chars", Code::INVALID_VALUE,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides>"
	           "<service name=\"WideNameAAAAAAAAAAAAAAAAAAAAAAAAA\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("bad lexeme: start name upper-case", Code::INVALID_VALUE,
	           doc(valid_start("Zeta", "64")));

	expect_err("bad lexeme: child name with space", Code::INVALID_VALUE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"Gui\">"
	               "<child name=\"bad name\"/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("bad lexeme: empty label", Code::INVALID_VALUE,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"Gui\" label=\"\">"
	               "<parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("bad shape: repro_padding <p v> wrong length",
	           Code::INVALID_VALUE,
	           doc("  <repro_padding><p v=\"short\"/></repro_padding>\n"));

	expect_err("bad lexeme: text at structural level", Code::INVALID_VALUE,
	           std::string(HDR) + "junk" + FTR);

	/* ============ 7. missing required fields ============ */

	expect_err("missing: <parent-provides>", Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("missing: <report>", Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("missing: <default-route>", Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("missing: <default>", Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "</config>\n");

	expect_err("missing: report attribute child_ram", Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\"/>\n"
	           "  <parent-provides><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("missing: start attribute name", Code::MISSING_REQUIRED,
	           doc("  <start caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("missing: start attribute caps", Code::MISSING_REQUIRED,
	           doc("  <start name=\"alpha\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("missing: start <resource>", Code::MISSING_REQUIRED,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("missing: start <route>", Code::MISSING_REQUIRED,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "  </start>\n"));

	expect_err("missing: route <any-service>", Code::MISSING_REQUIRED,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("missing: route entry target", Code::MISSING_REQUIRED,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"Gui\"/>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	expect_err("missing: parent-provides service name",
	           Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides><service/></parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("missing: empty <parent-provides>", Code::MISSING_REQUIRED,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides/>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	expect_err("missing: binary attribute name", Code::MISSING_REQUIRED,
	           doc("  <start name=\"alpha\" caps=\"64\">\n"
	               "    <binary/>\n"
	               "    <resource name=\"RAM\" quantum=\"4M\"/>\n"
	               "    <route>\n"
	               "      <service name=\"ROM\"><parent/></service>\n"
	               "      <any-service><parent/></any-service>\n"
	               "    </route>\n"
	               "  </start>\n"));

	/* ============ 8. duplicate names ============ */

	expect_err("duplicate: <start> name", Code::DUPLICATE_NAME,
	           doc(valid_start("alpha", "64") +
	               valid_start("alpha", "32")));

	expect_err("duplicate: parent-provides service name",
	           Code::DUPLICATE_NAME,
	           "<config>\n"
	           "  <report delay_ms=\"2000\" init_ram=\"yes\""
	           " init_caps=\"yes\" child_ram=\"yes\"/>\n"
	           "  <parent-provides>"
	           "<service name=\"ROM\"/><service name=\"ROM\"/>"
	           "</parent-provides>\n"
	           "  <default-route>"
	           "<any-service><parent/><any-child/></any-service>"
	           "</default-route>\n"
	           "  <default caps=\"1000\"/>\n"
	           "</config>\n");

	/* ============ 9. raw-append breakout is rejected ============ */

	/*
	 * Hostile metadata <config> fragments are appended verbatim by
	 * the emitter (raw Xml_generator::append). A forged end tag
	 * truncates the <start> and the validator rejects the result —
	 * here as MISSING_REQUIRED because the real <route> lands after
	 * the forged close. A malformed fragment simply makes the whole
	 * document invalid syntax.
	 */
	expect_err("raw append: breakout rejected",
	           Code::MISSING_REQUIRED,
	           render(Sponge::Pkg_config::app_target(),
	                  FIX_BREAKOUT, 1, Emit_options { }));

	expect_err("raw append: malformed fragment rejected",
	           Code::XML_SYNTAX,
	           render(Sponge::Pkg_config::app_target(),
	                  FIX_MALFORMED, 1, Emit_options { }));

	/* ============ 10. publication seam (P1.5) pins ============ */

	/*
	 * The exact repro_malformed corruption — a raw tag truncated
	 * mid-attribute, appended after the renderer, app config only —
	 * must be rejected with a non-empty detail that does NOT echo the
	 * tail token. run/sponge-config-emission-invalid.run matches
	 * "repro_malformed_tail" ANYWHERE in the serial log as proof of a
	 * silent publication, so a rejection reason quoting the token
	 * would false-positive that detector; the same reason is also
	 * serialized into <result error="..."/>, whose attribute must stay
	 * parseable. The raw-'<' pin records why sponge_pkgd's
	 * attribute-safe scrub of the detail is load-bearing (the detail
	 * legitimately quotes the offending construct).
	 */
	{
		static char tail_buf[65536];

		Genode::size_t const used =
			Genode::Xml_generator::generate(
				Genode::Byte_range_ptr(tail_buf, sizeof(tail_buf)),
				"config",
				[&] (Genode::Xml_generator &g) {
					Sponge::Pkg_config::emit_target(
						g, Sponge::Pkg_config::app_target(),
						FIX_PAD, 1, Emit_options { });
					g.append("<start name=\"repro_malformed_tail");
				}).convert<Genode::size_t>(
					[] (Genode::size_t n) { return n; },
					[] (Genode::Buffer_error) { return (Genode::size_t)0; });

		++cases;
		if (used == 0) {
			fail("seam: malformed tail render",
			     "render buffer exhausted", Result());
		} else {
			Workspace ws;
			Result const r = Sponge::Runtime_config_validate::validate(
				tail_buf, (unsigned)(used - 2), ws);

			if (r.ok)
				fail("seam: malformed tail rejected",
				     "expected rejection", r);
			else if (strstr(r.detail, "repro_malformed_tail") != nullptr)
				fail("seam: detail must not echo the detector token",
				     "would false-positive the publication detector", r);
			else if (r.detail[0] == '\0')
				fail("seam: non-empty detail", "detail empty", r);
			else if (strchr(r.detail, '<') == nullptr)
				fail("seam: detail quotes the offending construct",
				     "the attribute-scrub pin lost its reason", r);
		}
	}

	/*
	 * Body-append republish (the seam's publish step) is byte-
	 * identical to the validated document: Xml_generator::generate
	 * opens a fresh <config> root and the BODY of the rendered
	 * document (everything between the root open tag and </config>) is
	 * appended raw — no nested <config> root, no drift, no truncation.
	 * Pins the exact framing the seam derives (doc = used - 2; body =
	 * [8, doc_len - 9)).
	 */
	{
		static char rendered[65536], republished[65536];

		Genode::size_t const used_r = Sponge::Pkg_config::emit_config(
			rendered, sizeof(rendered), Sponge::Pkg_config::app_target(),
			FIX_MIXED, 5, Emit_options { });

		++cases;
		if (used_r < 2 + 8 + 9) {
			fail("seam: republish render",
			     "render buffer exhausted", Result());
		} else {
			Genode::size_t const doc_len  = used_r - 2;
			char const *const   body      = rendered + 8;
			Genode::size_t const body_len = doc_len - 8 - 9;

			Genode::size_t const used_p =
				Genode::Xml_generator::generate(
					Genode::Byte_range_ptr(republished, sizeof(republished)),
					"config",
					[&] (Genode::Xml_generator &g) {
						g.append(body, body_len);
					}).convert<Genode::size_t>(
						[] (Genode::size_t n) { return n; },
						[] (Genode::Buffer_error) {
							return (Genode::size_t)0;
						});

			if (used_p != used_r ||
			    memcmp(rendered, republished, used_r) != 0)
				fail("seam: republish is byte-identical",
				     "body-append diverged from the rendered document",
				     Result());
		}
	}

	if (failures) {
		printf("self-test: FAIL (%u of %u case(s))\n", failures, cases);
		return 1;
	}
	printf("self-test: PASS (%u cases)\n", cases);
	return 0;
}
