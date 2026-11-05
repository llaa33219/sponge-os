/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Sponge::Pkg_config — pure runtime-config
 * generation for Sponge OS package sandboxes.
 *
 * The generator is a verbatim extraction of sponge_pkgd's
 * _emit_runtime_config/_generate_runtime_config templating (Phase 16
 * P1.2, docs/plans/phase16-runtime-overhaul.md; pre-extraction source
 * repos/sponge/src/sponge_pkgd/main.cc:1959-2356 on the P0.3 audit
 * tree). The determinism contract, the emission order, the fixed
 * attribute order, and the buffer bounds are preserved exactly — see
 * <sponge/pkg_config.h> for the contracts and
 * lib/src/sponge_pkg_config/self_test.cc for the byte-identity proof
 * against a verbatim reference copy of the old algorithm. The one
 * deliberate deviation from the reference is the hardware capability
 * boundary (docs/12-package-format.md §4.5): non-driver targets drop
 * the hardware service names from <parent-provides> and the matching
 * <parent/> routes, which the pre-extraction generator emitted.
 *
 * Purity: no Env, no Report/ROM sessions, no File_system, no hardware
 * access, no exceptions, no mutable global state. Every output buffer
 * is owned by the caller.
 */

#include <sponge/pkg_config.h>

using namespace Sponge;
using namespace Sponge::Pkg_config;


/*
 * Session-label materialization (docs/12-package-format.md §7.2 rule 2):
 *   - an explicit <session label="..."> is forwarded as-is;
 *   - a read-only File_system session with no explicit label gets the
 *     "<pkg>-ro" suffix (the convention the outer vfs policy matches);
 *   - everything else has no label attribute (empty string).
 */
Genode::String<96> Sponge::Pkg_config::session_label(
	char const *pkg_name, Session_view const &s)
{
	if (s.label && Genode::strcmp(s.label, "") != 0)
		return Genode::String<96>(s.label);

	if (s.readonly && s.name && Genode::strcmp(s.name, "File_system") == 0)
		return Genode::String<96>(pkg_name, "-ro");

	return Genode::String<96>();
}


/*
 * Outer-system service/component names that live outside the package
 * sandbox and therefore route via <parent/>, never <child> (docs/12
 * §7.2 rule 1). The set is the union of (a) the literal "parent", and
 * (b) every named outer-system service the run scenarios actually wire
 * (nitpicker/vfs/event_filter/wm/report_rom/timer/input_drv). A
 * default-route that is none of these is assumed to name a sibling
 * package inside the sandbox and routes via <child name="..."/>.
 */
bool Sponge::Pkg_config::is_parent_route(char const *route)
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
 * Hardware capability service names (docs/12-package-format.md §4.5,
 * AGENTS.md §1.2): the sessions only a driver package may obtain.
 * Both casings that occur in the tree are listed - the shipped driver
 * metadata and Genode's service_name() spell them Platform/Dma/IRQ/
 * IO_MEM/IO_PORT, while docs/12 and the target presets use the
 * API-type casing Platform/Dma/Irq/Io_mem/Io_port. Matching is exact
 * so every other service name keeps its current casing and behaviour.
 */
static bool is_hardware_service(char const *name)
{
	if (name == nullptr || name[0] == '\0') return false;

	static char const *const HW[] = {
		"IO_MEM", "Io_mem",
		"IO_PORT", "Io_port",
		"IRQ",    "Irq",
		"Platform",
		"Dma"
	};
	for (char const *h : HW)
		if (Genode::strcmp(name, h) == 0)
			return true;
	return false;
}


/*
 * The fixed <parent-provides> set of each sandbox (docs/12-package-format.md
 * §7.2). The difference between the two lists IS the capability boundary
 * (AGENTS.md §1.2):
 *
 *   - the app target keeps the fixed application set of docs/12 §7.2
 *     unchanged and unions in the sessions the installed APP packages
 *     declared. It carries no hardware service at all, so an
 *     application package cannot obtain Platform/Irq/Io_mem/Io_port
 *     even by naming them.
 *   - the driver target carries only the five services a component
 *     cannot construct without (ROM/PD/RM/CPU/LOG) plus exactly the
 *     sessions the installed DRIVER packages declared. Nothing
 *     hardware-related is pre-granted: with no driver installed the
 *     list is the bare five.
 */
static char const *const APP_BASE_SERVICES[] = {
	"ROM", "PD", "RM", "CPU", "LOG", "Timer", "Gui",
	"Input", "Report", "File_system", "Nic", "Rtc" };

/*
 * RM is a distinct core service (base/src/core/main.cc
 * Core_service<Rm_session_component>), NOT subsumed by PD. Without it
 * in parent-provides, a package child that constructs an Rm_connection
 * (e.g. falkon/WebEngine for mmap / qtwebengine_shm) is stopped with
 * "no route to service RM" (sandbox/route_model.h:272). PD only hands
 * out the component's own address_space/stack_area/linker_area region
 * maps; additional region maps require a separate RM session routed to
 * core.
 *
 * Nic + Rtc use Genode's canonical service-name casing
 * (case-sensitive). The prior "NIC" never matched a real request;
 * without Rtc here, a networked+rtc package child (e.g. falkon) is
 * stopped at construction with "parent denied Rtc-session".
 */
static char const *const DRIVER_BASE_SERVICES[] = {
	"ROM", "PD", "RM", "CPU", "LOG" };


Target Sponge::Pkg_config::app_target()
{
	Target t { };
	t.name          = "app";
	t.runtime_class = "app";
	t.base_services.names = APP_BASE_SERVICES;
	t.base_services.count = sizeof(APP_BASE_SERVICES)
	                      / sizeof(APP_BASE_SERVICES[0]);
	return t;
}


Target Sponge::Pkg_config::driver_target()
{
	Target t { };
	t.name          = "driver";
	t.runtime_class = "driver";
	t.base_services.names = DRIVER_BASE_SERVICES;
	t.base_services.count = sizeof(DRIVER_BASE_SERVICES)
	                      / sizeof(DRIVER_BASE_SERVICES[0]);
	return t;
}


/*
 * The three app runtime-class presets (docs/12-package-format.md §4.5,
 * <runtime_class>simple|terminal|browser</runtime_class>, default
 * simple). All three carry the exact APP_BASE_SERVICES set of
 * 'app_target': the capability boundary is identical per class, so a
 * simple/terminal/browser package cannot obtain Platform/Dma/Irq/
 * Io_mem/Io_port even by naming them. Only the membership token
 * differs — 'emit_target' selects a package into exactly one target by
 * the 'runtime_class' string match and needs no change for these.
 */
Target Sponge::Pkg_config::simple_target()
{
	Target t { };
	t.name          = "simple";
	t.runtime_class = "simple";
	t.base_services.names = APP_BASE_SERVICES;
	t.base_services.count = sizeof(APP_BASE_SERVICES)
	                      / sizeof(APP_BASE_SERVICES[0]);
	return t;
}


Target Sponge::Pkg_config::terminal_target()
{
	Target t { };
	t.name          = "terminal";
	t.runtime_class = "terminal";
	t.base_services.names = APP_BASE_SERVICES;
	t.base_services.count = sizeof(APP_BASE_SERVICES)
	                      / sizeof(APP_BASE_SERVICES[0]);
	return t;
}


Target Sponge::Pkg_config::browser_target()
{
	Target t { };
	t.name          = "browser";
	t.runtime_class = "browser";
	t.base_services.names = APP_BASE_SERVICES;
	t.base_services.count = sizeof(APP_BASE_SERVICES)
	                      / sizeof(APP_BASE_SERVICES[0]);
	return t;
}


/*
 * Emit one target's sandbox config.
 *
 * The generator is deterministic (sorted <start> nodes, fixed
 * attribute order, no volatile fields) so init's config-diff leaves
 * unchanged children running across unrelated installs. The selection
 * of packages into the target is the string match on 'runtime_class'
 * (the pre-extraction split was `is_driver() != drivers` — exactly
 * "driver" versus "app"); the lifecycle gate is 'running'.
 */
void Sponge::Pkg_config::emit_target(Genode::Xml_generator &g,
                                     Target const &target,
                                     Package_view const *packages,
                                     unsigned num_packages,
                                     Emit_options const &options)
{
	if (packages == nullptr)
		num_packages = 0;

	/* Buffer bound: the sort index holds MAX_PACKAGES entries. */
	if (num_packages > MAX_PACKAGES)
		num_packages = MAX_PACKAGES;

	/*
	 * Hardware capability boundary (docs/12-package-format.md §4.5):
	 * an application package cannot obtain Platform/Dma/Irq/Io_mem/
	 * Io_port no matter what it declares. Every non-driver target
	 * therefore drops those service names from the <parent-provides>
	 * union and from the matching <parent/> routes below; the driver
	 * target keeps them verbatim.
	 */
	bool const hw_filter =
		Genode::strcmp(target.runtime_class, "driver") != 0;

	/* Selection-sort the installed set by name into a stable index order. */
	unsigned order[MAX_PACKAGES] { };
	for (unsigned i = 0; i < num_packages; ++i) order[i] = i;
	for (unsigned i = 0; i < num_packages; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < num_packages; ++j) {
			if (Genode::strcmp(packages[order[j]].name,
			                   packages[order[best]].name) < 0)
				best = j;
		}
		if (best != i) {
			unsigned tmp = order[i];
			order[i] = order[best];
			order[best] = tmp;
		}
	}

	/*
	 * Sandbox state reporting (bring-up observability + admission
	 * control): makes the sandbox emit a periodic <state> report
	 * listing every spawned child and its state. Relayed to sponge-de
	 * as the panel-title C:<n> readout — on hardware where children
	 * never produce windows, C: discriminates "the sandbox never
	 * spawned them" (config delivery broken) from "they spawned but
	 * died before opening a Gui session".
	 *
	 * init_ram/init_caps add the sandbox's OWN top-level <ram>/<caps>
	 * nodes (quota/used/avail for the sandbox itself, not per child).
	 * Those avail figures are what the admission math compares a
	 * package's declared quota against — without init_caps the caps
	 * dimension of the budget would simply be absent from the report.
	 */
	g.node("report", [&] {
		g.attribute("delay_ms", "2000");
		g.attribute("init_ram", "yes");
		g.attribute("init_caps", "yes");
		g.attribute("child_ram", "yes");
	});

	if (options.pad_bytes)
		g.node("repro_padding", [&] {
			/*
			 * One node per 64 pad bytes, each carrying a
			 * 64-character attribute value. Such a node
			 * serializes to clearly more than 64 bytes (two
			 * tabs, the node and attribute syntax, the 64
			 * characters, and the line break), so the EMITTED
			 * padding is always at least pad_bytes. That makes
			 * "this write overflows the caller's report buffer"
			 * a fact provable from the knob's audit line
			 * instead of an estimate that depends on the
			 * serializer's formatting.
			 */
			for (unsigned i = 0; i < options.pad_bytes / 64; ++i)
				g.node("p", [&] {
					g.attribute("v", "................................"
					                "................................"); });
		});

	g.node("parent-provides", [&] {
		/*
		 * Union in the declared session names of the packages that
		 * belong to THIS target (e.g. the terminal package's
		 * Terminal bridge session, or a driver's Platform session):
		 * a service name missing here stops the forwarding child
		 * with "parent denied <svc>-session". The pointer table is
		 * deliberately small (MAX_PROVIDES pointers, no copies): the
		 * pre-extraction code used a static String<32> table because
		 * of the requesting thread's small stack; the names are only
		 * compared and emitted, never modified, and are already at
		 * String<32> width from parse.
		 */
		char const *emitted[MAX_PROVIDES] { };
		unsigned    num_emitted          { 0 };

		for (unsigned i = 0; i < target.base_services.count; ++i) {
			if (num_emitted >= MAX_PROVIDES)
				continue;
			emitted[num_emitted++] = target.base_services.names[i];
		}
		for (unsigned i = 0; i < num_packages; ++i) {
			if (Genode::strcmp(packages[i].runtime_class,
			                   target.runtime_class) != 0)
				continue;
			for (unsigned s = 0; s < packages[i].num_sessions; ++s) {
				if (s >= MAX_SESSIONS)
					continue;
				char const *n = packages[i].sessions[s].name;
				if (n == nullptr || n[0] == '\0' || num_emitted >= MAX_PROVIDES)
					continue;
				if (hw_filter && is_hardware_service(n))
					continue;
				bool dup { false };
				for (unsigned k = 0; k < num_emitted; ++k)
					if (Genode::strcmp(emitted[k], n) == 0) { dup = true; break; }
				if (dup)
					continue;
				emitted[num_emitted++] = n;
			}
		}
		for (unsigned i = 0; i < num_emitted; ++i)
			g.node("service", [&] {
				g.attribute("name", emitted[i]); });
	});

	g.node("default-route", [&] {
		g.node("any-service", [&] {
			g.node("parent");
			g.node("any-child");
		});
	});

	g.node("default", [&] {
		/*
		 * GUI-safe caps floor (docs/09-roadmap.md §11.1 lesson,
		 * Metis A3). The previous caps="100" silently exhausted
		 * on seL4 mid-Qt-init, hanging with zero diagnostics.
		 * Each <start> still carries its own per-package <quota>
		 * caps (honored below); this floor protects any child
		 * that lacks an explicit value. No default ram: each
		 * <start> carries its own <resource name="RAM">, and a
		 * non-zero default ram would make sandbox/child.cc flag
		 * the explicit resource as an "ambiguous RAM-quota
		 * definition".
		 */
		g.attribute("caps", "1000");
	});

	for (unsigned n = 0; n < num_packages; ++n) {
		Package_view const &c = packages[order[n]];

		/*
		 * Target split: a package is emitted into the target whose
		 * runtime_class token matches its own. This is the single
		 * place the sandboxes' membership is decided.
		 */
		if (Genode::strcmp(c.runtime_class, target.runtime_class) != 0)
			continue;

		/*
		 * Phase 7 lifecycle gate (docs/12-package-format.md §9.2.1):
		 * emit <start> only for running packages. Installed-but-
		 * stopped packages are deliberately absent from the sandbox
		 * config, so init never constructs them until a launch adds
		 * them.
		 */
		if (!c.running)
			continue;

		g.node("start", [&] {
			g.attribute("name", c.name);
			g.attribute("caps", c.caps);

			/*
			 * (a) <binary> when the effective binary path differs
			 * from the package name (docs/12 §4.1). In disk-served
			 * mode (binary_prefix set, docs/14 §4.4) the path is
			 * "bin/<binary>" so cached_fs_rom resolves it from
			 * /system/bin/. In boot-module mode the prefix is empty
			 * and the legacy single-name form is preserved.
			 */
			Genode::String<96> const bin_path(options.binary_prefix,
			                                  c.binary);
			if (Genode::strcmp(bin_path.string(),
			                   c.name) != 0) {
				g.node("binary", [&] {
					g.attribute("name", bin_path);
				});
			}

			g.node("resource", [&] {
				g.attribute("name", "RAM");
				g.attribute("quantum", c.ram);
			});

			/*
			 * (b) Inline <config> from metadata, emitted verbatim
			 * via raw append (Xml_generator::append is the non-
			 * sanitized path). config_xml already includes its own
			 * <config>...</config> wrapper.
			 */
			if (Genode::strcmp(c.config_xml, "") != 0)
				g.append(c.config_xml);

			g.node("route", [&] {
				/*
				 * §4.6 ld.lib.so rule (docs/14-boot-storage-architecture.md):
				 * every dynamically linked child routes ld.lib.so to
				 * <parent/>, cascading up to core's boot-module ROM.
				 * Harmless in boot-module mode (same target); load-
				 * bearing in disk-served mode (prevents a chicken-and-
				 * egg: the component that loads a dynamically linked
				 * binary must itself already be loaded).
				 */
				g.node("service", [&] {
					g.attribute("name", "ROM");
					g.attribute("label_last", "ld.lib.so");
					g.node("parent");
				});

				for (unsigned s = 0; s < c.num_sessions; ++s) {
					if (s >= MAX_SESSIONS)
						continue;
					Session_view const &session = c.sessions[s];

					bool const parent_route =
						is_parent_route(session.route);

					if (hw_filter && parent_route &&
					    is_hardware_service(session.name))
						continue;

					Genode::String<96> const label =
						session_label(c.name, session);

					/*
					 * (c)+(e) service route: <parent/> for
					 * outer-system routes (rule 1), <child> for
					 * siblings; label materialized per rule 2.
					 */
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


Genode::size_t Sponge::Pkg_config::emit_config(char *dst,
                                               Genode::size_t dst_len,
                                               Target const &target,
                                               Package_view const *packages,
                                               unsigned num_packages,
                                               Emit_options const &options)
{
	Genode::Xml_generator::Result const result =
		Genode::Xml_generator::generate(
			Genode::Byte_range_ptr(dst, dst_len), "config",
			[&] (Genode::Xml_generator &g) {
				emit_target(g, target, packages, num_packages, options);
			});

	return result.convert<Genode::size_t>(
		[] (Genode::size_t used) { return used; },
		[] (Genode::Buffer_error) { return (Genode::size_t)0; });
}
