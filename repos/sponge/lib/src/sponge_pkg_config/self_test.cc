/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side golden self-test for sponge_pkg_config (Phase 16 P1.2).
 *
 * NOT part of the Genode build (deliberately absent from
 * lib/mk/sponge_pkg_config.mk), so it lives outside the shared build
 * run. It proves the P1.2 byte-identity claim two ways:
 *
 *   1. Equivalence: the library's emit_target is compared byte for
 *      byte against `ref_emit` below — a verbatim copy of the
 *      pre-extraction _emit_runtime_config generator (sponge_pkgd
 *      main.cc:2092-2356 on the P0.3 audit tree), including its
 *      Genode::String<32> emit-table truncation semantics.
 *
 *   2. Golden snapshots: the same fixture outputs are compared
 *      against embedded snapshot strings captured from that reference
 *      implementation, so any future edit that changes the bytes
 *      fails loudly even if it changes both copies together.
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
 *       repos/sponge/lib/src/sponge_pkg_config/self_test.cc \
 *       repos/sponge/lib/src/sponge_pkg_config/pkg_config.cc \
 *       genode/repos/base/src/lib/base/xml_generator.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_pkg_config_self_test \
 *   && /tmp/sponge_pkg_config_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every fixture
 * matched. "self-test: FAIL" prints the first divergence.
 *
 * `--dump` prints the library output for every fixture/target pair
 * (used to capture the snapshot goldens embedded below).
 */

#include <sponge/pkg_config.h>

#include <stdio.h>
#include <string.h>


/* ===================== reference implementation ===================== */
/*
 * Verbatim transcription of the pre-extraction generator
 * (repos/sponge/src/sponge_pkgd/main.cc, Main::_emit_runtime_config
 * plus its _session_label/_is_parent_route helpers, P0.3 audit tree
 * be273423bc). The only adaptations are the ones the function
 * signature had to gain: the installed set, the running flag, the
 * binary prefix and the pad budget arrive as arguments instead of
 * Main members. Field types stay Genode::String<NN> exactly like the
 * parse-side Package type, so truncation semantics are identical.
 * If this copy and pkg_config.cc are ever edited together, the
 * snapshot goldens below still catch the change.
 */

namespace {

struct Ref_session
{
	Genode::String<32> name;
	Genode::String<32> route;
	bool               readonly { false };
	Genode::String<64> label;
};

struct Ref_package
{
	Genode::String<64>    name;
	Genode::String<64>    binary;
	Genode::String<16>    ram;
	unsigned              caps  { 0 };
	Genode::String<16384> config_xml;
	bool                  is_driver { false };
	bool                  running   { false };

	Ref_session sessions[16] { };
	unsigned    num_sessions  { 0 };
};

constexpr unsigned REF_MAX_PACKAGES = 32;

/* Pre-extraction Main::_session_label (verbatim). */
Genode::String<96> ref_session_label(
	char const *pkg_name, Ref_session const &s)
{
	if (Genode::strcmp(s.label.string(), "") != 0)
		return Genode::String<96>(s.label);

	if (s.readonly && Genode::strcmp(s.name.string(), "File_system") == 0)
		return Genode::String<96>(pkg_name, "-ro");

	return Genode::String<96>();
}

/* Pre-extraction Main::_is_parent_route (verbatim). */
bool ref_is_parent_route(char const *route)
{
	if (route == nullptr || route[0] == '\0') return true;
	if (Genode::strcmp(route, "parent") == 0) return true;

	static char const *const OUTER[] = {
		"nitpicker", "vfs", "event_filter", "wm",
		"report_rom", "timer", "input_drv"
	};
	for (char const *o : OUTER)
		if (Genode::strcmp(route, o) == 0)
			return true;
	return false;
}

/*
 * Pre-extraction Main::_emit_runtime_config body (verbatim): selection
 * sort by name, then the generate_xml lambda with the report node, the
 * repro padding, the parent-provides union, the fixed route/default
 * scaffolding and the sorted <start> nodes.
 */
void ref_emit(Genode::Xml_generator &g, bool drivers,
              Ref_package const *installed, unsigned num_installed,
              Genode::String<32> binary_prefix, unsigned pad_bytes)
{
	/* Selection-sort the installed set by name into a stable index order. */
	unsigned order[REF_MAX_PACKAGES] { };
	for (unsigned i = 0; i < num_installed; ++i) order[i] = i;
	for (unsigned i = 0; i < num_installed; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < num_installed; ++j) {
			if (Genode::strcmp(installed[order[j]].name.string(),
			                   installed[order[best]].name.string()) < 0)
				best = j;
		}
		if (best != i) {
			unsigned tmp = order[i];
			order[i] = order[best];
			order[best] = tmp;
		}
	}

	{
		g.node("report", [&] {
			g.attribute("delay_ms", "2000");
			g.attribute("init_ram", "yes");
			g.attribute("init_caps", "yes");
			g.attribute("child_ram", "yes");
		});
		if (!drivers && pad_bytes)
			g.node("repro_padding", [&] {
				for (unsigned i = 0; i < pad_bytes / 64; ++i)
					g.node("p", [&] {
						g.attribute("v", "................................"
						                "................................"); });
			});

		g.node("parent-provides", [&] {
			static char const *const APP_BASE[] = {
				"ROM", "PD", "RM", "CPU", "LOG", "Timer", "Gui",
				"Input", "Report", "File_system", "Nic", "Rtc" };
			static char const *const DRIVER_BASE[] = {
				"ROM", "PD", "RM", "CPU", "LOG" };

			char const *const *fixed = drivers ? DRIVER_BASE : APP_BASE;
			unsigned const num_fixed = drivers ?
				(sizeof(DRIVER_BASE)/sizeof(DRIVER_BASE[0]))
				: (sizeof(APP_BASE)/sizeof(APP_BASE[0]));

			static Genode::String<32> emitted[64] { };
			static unsigned      num_emitted  { 0 };
			num_emitted = 0;
			for (unsigned i = 0; i < num_fixed; ++i)
				emitted[num_emitted++] = Genode::String<32>(fixed[i]);
			for (unsigned i = 0; i < num_installed; ++i) {
				if (installed[i].is_driver != drivers)
					continue;
				for (unsigned s = 0; s < installed[i].num_sessions; ++s) {
					Genode::String<32> const &n =
						installed[i].sessions[s].name;
					if (n == Genode::String<32>() || num_emitted >= 64)
						continue;
					bool dup { false };
					for (unsigned k = 0; k < num_emitted; ++k)
						if (emitted[k] == n) { dup = true; break; }
					if (dup)
						continue;
					emitted[num_emitted++] = n;
				}
			}
			for (unsigned i = 0; i < num_emitted; ++i)
				g.node("service", [&] {
					g.attribute("name", emitted[i].string()); });
		});

		g.node("default-route", [&] {
			g.node("any-service", [&] {
				g.node("parent");
				g.node("any-child");
			});
		});

		g.node("default", [&] {
			g.attribute("caps", "1000");
		});

		for (unsigned n = 0; n < num_installed; ++n) {
			Ref_package const &c = installed[order[n]];

			if (c.is_driver != drivers)
				continue;

			if (!c.running)
				continue;

			g.node("start", [&] {
				g.attribute("name", c.name);
				g.attribute("caps", c.caps);

				Genode::String<96> const bin_path(binary_prefix,
				                                  c.binary);
				if (Genode::strcmp(bin_path.string(),
				                   c.name.string()) != 0) {
					g.node("binary", [&] {
						g.attribute("name", bin_path);
					});
				}

				g.node("resource", [&] {
					g.attribute("name", "RAM");
					g.attribute("quantum", c.ram);
				});

				if (Genode::strcmp(c.config_xml.string(), "") != 0)
					g.append(c.config_xml.string());

			g.node("route", [&] {
					g.node("service", [&] {
						g.attribute("name", "ROM");
						g.attribute("label_last", "ld.lib.so");
						g.node("parent");
					});

					for (unsigned s = 0; s < c.num_sessions; ++s) {
							Ref_session const &session = c.sessions[s];

							bool const parent_route =
								ref_is_parent_route(session.route.string());

							Genode::String<96> const label =
								ref_session_label(c.name.string(), session);

							g.node("service", [&] {
								g.attribute("name", session.name);
								if (Genode::strcmp(label.string(), "") != 0)
									g.attribute("label", label);
								if (parent_route) {
									g.node("parent");
								} else {
									g.node("child", [&] {
										g.attribute("name", session.route);
									});
								}
							});
						}
						g.node("any-service", [&] { g.node("parent"); });
					});
			});
		}
	}
}

/* Wrap ref_emit in the "<config>...</config>" document the reporter
 * node type produces (Expanding_reporter type is "config"). */
Genode::size_t ref_emit_config(char *dst, Genode::size_t dst_len, bool drivers,
                               Ref_package const *installed,
                               unsigned num_installed,
                               Genode::String<32> binary_prefix,
                               unsigned pad_bytes)
{
	Genode::Xml_generator::Result const result =
		Genode::Xml_generator::generate(
			Genode::Byte_range_ptr(dst, dst_len), "config",
			[&] (Genode::Xml_generator &g) {
				ref_emit(g, drivers, installed, num_installed,
				         binary_prefix, pad_bytes);
			});

	return result.convert<Genode::size_t>(
		[] (Genode::size_t used) { return used; },
		[] (Genode::Buffer_error) { return (Genode::size_t)0; });
}


/* ===================== fixture conversion ===================== */

/*
 * Mirror a fixture into the library's view values. Session names stay
 * at parse width (Genode::String<32>), honoring the view contract in
 * <sponge/pkg_config.h>.
 */
void to_views(Ref_package const *installed, unsigned num_installed,
              Sponge::Pkg_config::Package_view *views, unsigned &num_views)
{
	num_views = 0;
	for (unsigned i = 0; i < num_installed &&
	                    i < Sponge::Pkg_config::MAX_PACKAGES; ++i) {
		Ref_package const &c = installed[i];
		Sponge::Pkg_config::Package_view &v = views[num_views++];
		v.name          = c.name.string();
		v.binary        = c.binary.string();
		v.ram           = c.ram.string();
		v.caps          = c.caps;
		v.config_xml    = c.config_xml.string();
		v.runtime_class = c.is_driver ? "driver" : "app";
		v.running       = c.running;
		v.num_sessions  = 0;
		for (unsigned s = 0; s < c.num_sessions &&
		                    s < Sponge::Pkg_config::MAX_SESSIONS; ++s) {
			Sponge::Pkg_config::Session_view &sv =
				v.sessions[v.num_sessions++];
			sv.name     = c.sessions[s].name.string();
			sv.route    = c.sessions[s].route.string();
			sv.readonly = c.sessions[s].readonly;
			sv.label    = c.sessions[s].label.string();
		}
	}
}


/* ===================== fixtures ===================== */

struct Fixture
{
	char const        *name;
	Ref_package const *packages;
	unsigned           num_packages;
	char const        *binary_prefix;
	unsigned           pad_bytes;
};

/*
 * The long session name is exactly the parse width (31 chars, plus
 * terminating null): a name wider than that cannot occur in
 * sponge_pkgd's state and is outside the view contract.
 */
Ref_package const FIX_MIXED[] = {
	{ .name = "zeta", .binary = "zeta_bin", .ram = "8M", .caps = 128,
	  .config_xml = "<config><libc/></config>",
	  .is_driver = false, .running = true,
	  .sessions = {
		{ .name = "Gui", .route = "nitpicker" },
		{ .name = "File_system", .route = "vfs", .readonly = true },
		{ .name = "ROM", .route = "parent" } },
	  .num_sessions = 3 },

	{ .name = "alpha", .binary = "alpha", .ram = "4M", .caps = 64,
	  .is_driver = false, .running = false,
	  .sessions = {
		{ .name = "Gui", .route = "" },
		{ .name = "Terminal", .route = "term-bridge" },
		{ .name = "Custom", .route = "nitpicker", .label = "my-label" } },
	  .num_sessions = 3 },

	{ .name = "mid", .binary = "mid", .ram = "2M", .caps = 32,
	  .is_driver = false, .running = true,
	  .sessions = {
		{ .name = "Gui", .route = "parent" },
		{ .name = "Sess", .route = "" },
		{ .name = "WideNameAAAAAAAAAAAAAAAAAAAAA", .route = "wm" } },
	  .num_sessions = 3 },

	{ .name = "beta", .binary = "nic_drv", .ram = "16M", .caps = 256,
	  .is_driver = true, .running = true,
	  .sessions = {
		{ .name = "Platform", .route = "" },
		{ .name = "Nic", .route = "" } },
	  .num_sessions = 2 },

	{ .name = "gamma", .binary = "gamma", .ram = "4M", .caps = 16,
	  .config_xml = "<config a=\"b\"/>",
	  .is_driver = true, .running = false,
	  .sessions = {
		{ .name = "Platform", .route = "" },
		{ .name = "Dma", .route = "" } },
	  .num_sessions = 2 },
};

Ref_package const FIX_PAD[] = {
	{ .name = "solo", .binary = "solo", .ram = "1M", .caps = 8,
	  .is_driver = false, .running = true,
	  .sessions = { { .name = "Gui", .route = "parent" } },
	  .num_sessions = 1 },
};

Fixture const FIXTURES[] = {
	{ "mixed",  FIX_MIXED, 5, "bin/", 0 },
	{ "pad",    FIX_PAD,   1, "bin/", 128 },
	{ "boot",   FIX_MIXED, 5, "",     0 },
};

constexpr unsigned NUM_FIXTURES = sizeof(FIXTURES) / sizeof(FIXTURES[0]);

/*
 * Snapshot goldens (fixture "mixed", binary_prefix "bin/"), captured
 * from the reference implementation and cross-checked against it on
 * every run. The driver snapshot also covers the empty-config_xml
 * path; the app snapshot covers verbatim inline config, the
 * <pkg>-ro label, the dedup of "Gui"/"ROM"/"Platform" against the
 * fixed base services, the child route ("term-bridge"), the explicit
 * label, the 31-char session name, and the running gate (alpha is
 * installed but stopped: no <start>, but its sessions still shape
 * <parent-provides>).
 */
char const *const GOLDEN_MIXED_APP =
	"<config>\n"
	"	<report delay_ms=\"2000\" init_ram=\"yes\" init_caps=\"yes\" child_ram=\"yes\"/>\n"
	"	<parent-provides>\n"
	"		<service name=\"ROM\"/>\n"
	"		<service name=\"PD\"/>\n"
	"		<service name=\"RM\"/>\n"
	"		<service name=\"CPU\"/>\n"
	"		<service name=\"LOG\"/>\n"
	"		<service name=\"Timer\"/>\n"
	"		<service name=\"Gui\"/>\n"
	"		<service name=\"Input\"/>\n"
	"		<service name=\"Report\"/>\n"
	"		<service name=\"File_system\"/>\n"
	"		<service name=\"Nic\"/>\n"
	"		<service name=\"Rtc\"/>\n"
	"		<service name=\"Terminal\"/>\n"
	"		<service name=\"Custom\"/>\n"
	"		<service name=\"Sess\"/>\n"
	"		<service name=\"WideNameAAAAAAAAAAAAAAAAAAAAA\"/>\n"
	"	</parent-provides>\n"
	"	<default-route>\n"
	"		<any-service>\n"
	"			<parent/>\n"
	"			<any-child/>\n"
	"		</any-service>\n"
	"	</default-route>\n"
	"	<default caps=\"1000\"/>\n"
	"	<start name=\"mid\" caps=\"32\">\n"
	"		<binary name=\"bin/mid\"/>\n"
	"		<resource name=\"RAM\" quantum=\"2M\"/>\n"
	"		<route>\n"
	"			<service name=\"ROM\" label_last=\"ld.lib.so\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"Gui\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"Sess\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"WideNameAAAAAAAAAAAAAAAAAAAAA\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<any-service>\n"
	"				<parent/>\n"
	"			</any-service>\n"
	"		</route>\n"
	"	</start>\n"
	"	<start name=\"zeta\" caps=\"128\">\n"
	"		<binary name=\"bin/zeta_bin\"/>\n"
	"		<resource name=\"RAM\" quantum=\"8M\"/><config><libc/></config>\n"
	"		<route>\n"
	"			<service name=\"ROM\" label_last=\"ld.lib.so\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"Gui\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"File_system\" label=\"zeta-ro\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"ROM\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<any-service>\n"
	"				<parent/>\n"
	"			</any-service>\n"
	"		</route>\n"
	"	</start>\n"
	"</config>\n";
char const *const GOLDEN_MIXED_DRIVER =
	"<config>\n"
	"	<report delay_ms=\"2000\" init_ram=\"yes\" init_caps=\"yes\" child_ram=\"yes\"/>\n"
	"	<parent-provides>\n"
	"		<service name=\"ROM\"/>\n"
	"		<service name=\"PD\"/>\n"
	"		<service name=\"RM\"/>\n"
	"		<service name=\"CPU\"/>\n"
	"		<service name=\"LOG\"/>\n"
	"		<service name=\"Platform\"/>\n"
	"		<service name=\"Nic\"/>\n"
	"		<service name=\"Dma\"/>\n"
	"	</parent-provides>\n"
	"	<default-route>\n"
	"		<any-service>\n"
	"			<parent/>\n"
	"			<any-child/>\n"
	"		</any-service>\n"
	"	</default-route>\n"
	"	<default caps=\"1000\"/>\n"
	"	<start name=\"beta\" caps=\"256\">\n"
	"		<binary name=\"bin/nic_drv\"/>\n"
	"		<resource name=\"RAM\" quantum=\"16M\"/>\n"
	"		<route>\n"
	"			<service name=\"ROM\" label_last=\"ld.lib.so\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"Platform\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"Nic\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<any-service>\n"
	"				<parent/>\n"
	"			</any-service>\n"
	"		</route>\n"
	"	</start>\n"
	"</config>\n";
char const *const GOLDEN_PAD_APP =
	"<config>\n"
	"	<report delay_ms=\"2000\" init_ram=\"yes\" init_caps=\"yes\" child_ram=\"yes\"/>\n"
	"	<repro_padding>\n"
	"		<p v=\"................................................................\"/>\n"
	"		<p v=\"................................................................\"/>\n"
	"	</repro_padding>\n"
	"	<parent-provides>\n"
	"		<service name=\"ROM\"/>\n"
	"		<service name=\"PD\"/>\n"
	"		<service name=\"RM\"/>\n"
	"		<service name=\"CPU\"/>\n"
	"		<service name=\"LOG\"/>\n"
	"		<service name=\"Timer\"/>\n"
	"		<service name=\"Gui\"/>\n"
	"		<service name=\"Input\"/>\n"
	"		<service name=\"Report\"/>\n"
	"		<service name=\"File_system\"/>\n"
	"		<service name=\"Nic\"/>\n"
	"		<service name=\"Rtc\"/>\n"
	"	</parent-provides>\n"
	"	<default-route>\n"
	"		<any-service>\n"
	"			<parent/>\n"
	"			<any-child/>\n"
	"		</any-service>\n"
	"	</default-route>\n"
	"	<default caps=\"1000\"/>\n"
	"	<start name=\"solo\" caps=\"8\">\n"
	"		<binary name=\"bin/solo\"/>\n"
	"		<resource name=\"RAM\" quantum=\"1M\"/>\n"
	"		<route>\n"
	"			<service name=\"ROM\" label_last=\"ld.lib.so\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<service name=\"Gui\">\n"
	"				<parent/>\n"
	"			</service>\n"
	"			<any-service>\n"
	"				<parent/>\n"
	"			</any-service>\n"
	"		</route>\n"
	"	</start>\n"
	"</config>\n";


/* ===================== comparison harness ===================== */

unsigned failures = 0;

void expect_eq(char const *what, char const *a, char const *b)
{
	if (strcmp(a, b) == 0)
		return;

	++failures;
	printf("FAIL: %s\n", what);
	printf("--- expected/reference ---\n%s\n", a);
	printf("--- actual ---\n%s\n", b);

	/* first diverging line */
	char const *pa = a, *pb = b;
	unsigned line = 1;
	while (*pa && *pb && *pa == *pb) {
		if (*pa == '\n') ++line;
		++pa; ++pb;
	}
	printf("first divergence at line %u (offset %ld / %ld): "
	       "expected \"%.40s\" vs actual \"%.40s\"\n",
	       line, (long)(pa - a), (long)(pb - b), pa, pb);
}

char ref_buf[64 * 1024];
char lib_buf[64 * 1024];

}  /* anonymous namespace */


int main(int argc, char **argv)
{
	bool const dump = (argc > 1 && strcmp(argv[1], "--dump") == 0);

	Sponge::Pkg_config::Package_view views[Sponge::Pkg_config::MAX_PACKAGES] { };

	for (unsigned f = 0; f < NUM_FIXTURES; ++f) {
		Fixture const &fx = FIXTURES[f];

		unsigned num_views = 0;
		to_views(fx.packages, fx.num_packages, views, num_views);

		for (int drv = 0; drv <= 1; ++drv) {
			bool const drivers = (drv == 1);

			Sponge::Pkg_config::Target const target =
				drivers ? Sponge::Pkg_config::driver_target()
				        : Sponge::Pkg_config::app_target();

			Sponge::Pkg_config::Emit_options options { };
			options.binary_prefix = fx.binary_prefix;
			options.pad_bytes     = drivers ? 0 : fx.pad_bytes;

			Genode::String<32> const ref_prefix(fx.binary_prefix);

			Genode::size_t const ref_used =
				ref_emit_config(ref_buf, sizeof(ref_buf), drivers,
				                fx.packages, fx.num_packages,
				                ref_prefix, options.pad_bytes);
			Genode::size_t const lib_used =
				Sponge::Pkg_config::emit_config(lib_buf, sizeof(lib_buf),
				                                target, views, num_views,
				                                options);

			if (ref_used == 0 || lib_used == 0) {
				printf("FAIL: %s/%s buffer exhausted "
				       "(ref %zu, lib %zu)\n",
				       fx.name, target.name, ref_used, lib_used);
				++failures;
				continue;
			}

			char what[128];
			snprintf(what, sizeof(what),
			         "equivalence: fixture %s / target %s",
			         fx.name, target.name);
			expect_eq(what, ref_buf, lib_buf);

			if (dump)
				printf("=== %s / %s ===\n%s\n",
				       fx.name, target.name, lib_buf);
		}
	}

	/* Snapshot goldens (fixture "mixed" / "pad"). */
	{
		unsigned num_views = 0;
		to_views(FIX_MIXED, 5, views, num_views);

		Sponge::Pkg_config::Emit_options options { };
		options.binary_prefix = "bin/";

		Sponge::Pkg_config::emit_config(lib_buf, sizeof(lib_buf),
		                                Sponge::Pkg_config::app_target(),
		                                views, num_views, options);
		expect_eq("golden: mixed/app", GOLDEN_MIXED_APP, lib_buf);

		Sponge::Pkg_config::emit_config(lib_buf, sizeof(lib_buf),
		                                Sponge::Pkg_config::driver_target(),
		                                views, num_views, options);
		expect_eq("golden: mixed/driver", GOLDEN_MIXED_DRIVER, lib_buf);

		to_views(FIX_PAD, 1, views, num_views);
		options.pad_bytes = 128;
		Sponge::Pkg_config::emit_config(lib_buf, sizeof(lib_buf),
		                                Sponge::Pkg_config::app_target(),
		                                views, num_views, options);
		expect_eq("golden: pad/app", GOLDEN_PAD_APP, lib_buf);
	}

	if (failures) {
		printf("self-test: FAIL (%u divergence(s))\n", failures);
		return 1;
	}
	printf("self-test: PASS (equivalence + golden snapshots)\n");
	return 0;
}
