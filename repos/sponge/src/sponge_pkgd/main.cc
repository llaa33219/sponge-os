/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd — package backend daemon (Phase 4a/4b).
 *
 * A long-lived, signal-driven Genode component. It watches a "request"
 * ROM (relayed by report_rom from vct's request report), resolves the
 * requested package's metadata and dependency graph, and writes a
 * structured "result" report that report_rom relays back to vct as a
 * ROM. This Report/ROM channel is the settled vct<->backend design
 * (docs/04-components.md §5); there is no RPC stub or IDL.
 *
 * Resolution follows docs/12-package-format.md §6: a deterministic DFS
 * with cycle detection. Each package's metadata is staged in the boot
 * image as a ROM module named "pkg_<name>.xml" and opened here via
 * Attached_rom_dataspace.
 *
 * Operations:
 *   - explain (4a): produce the install plan, no side effects.
 *   - install (4b): resolve, add to the installed set, regenerate the
 *     pkg_runtime config (a nested init that hosts the components).
 *     Phase 7 (docs/12 §9.2.1): install registers packages as STOPPED
 *     unless their metadata declares <autostart/>, in which case they
 *     also get a <start> node immediately (preserves hello's old
 *     auto-start behavior).
 *   - launch (Phase 7): transition an installed-but-stopped package to
 *     running by adding its <start> node and regenerating. The result
 *     carries one of three outcomes: ok / not-installed / already-running.
 *     Phase 7 todo 10: the launch op is reachable from TWO input
 *     channels — "request" (vct + test probes) and "launcher_request"
 *     (the Sponge DE launcher menu) — because report_rom is single-
 *     writer per label and a long-lived launcher cannot share the same
 *     "request" label as short-lived vct invocations. Both channels
 *     feed the SAME _handle_request body and the SAME _do_launch: one
 *     launch backend, two transports (AGENTS.md §3.3 rule 5 — same
 *     backend interface, not a forked launch path).
 *   - remove  (4b): drop the package (and now-unused deps), regenerate.
 *
 * The pkg_runtime config is emitted via a second Expanding_reporter
 * ("runtime") that report_rom relays as pkg_runtime's "config" ROM.
 * The generator is deterministic (sorted start nodes, fixed attribute
 * order, no volatile fields) so init's config-diff leaves unchanged
 * children running across unrelated installs.
 *
 * Two sandboxes, two configs (docs/12-package-format.md §4.5): a
 * package whose metadata declares <kind>driver</kind> is emitted into
 * the driver_runtime config instead — a sibling nested init inside the
 * `system` sandbox whose config this daemon also owns. The split is the
 * capability boundary: driver_runtime's generated <parent-provides>
 * carries only the sessions the installed DRIVER packages declared,
 * pkg_runtime's carries only the sessions the installed APP packages
 * declared plus the fixed application set. No application package can
 * reach Platform/Irq/Io_mem/Io_port even by naming them
 * (AGENTS.md §1.2). Both configs are regenerated together on every
 * install/remove/launch, so the two sandboxes can never disagree about
 * the installed set.
 *
 * Persistence (Phase 4 follow-up #2 / docs/09-roadmap.md §6): the set
 * of explicitly-installed roots is mirrored to a tiny versioned XML
 * store on a File_system session, so installs survive a reboot. The
 * store holds ONLY the root names — the full installed set (roots +
 * transitive deps) is re-derived on load via the same pure-function
 * path (_sync_installed_from_roots) that install/remove already use,
 * so the state->config generator is untouched. Persistence is opt-in
 * per deployment: it activates only when this component's <config>
 * carries a <vfs> node (see docs/12-package-format.md §13). With no
 * such config (the Phase 4 scenarios) behaviour is byte-identical to
 * the in-memory daemon.
 *
 * Disk-served mode (Phase 8 P2, docs/14-boot-storage-architecture.md):
 * when <config> carries binary_prefix="bin/", generated <start> nodes
 * emit <binary name="bin/<binary>"/> so cached_fs_rom (chroot /system)
 * resolves them at /system/bin/<binary>. Without the attribute the
 * boot-module behaviour (<binary> = bare name) is unchanged.
 * Every generated child also gets the §4.6 ld.lib.so route uniformly.
 */

#include <base/attached_rom_dataspace.h>
#include <base/thread.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <sponge/pkg_config.h>
#include <util/hid.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>
#include <vfs/root.h>
#include "pkgd.h"


Genode::Expanding_reporter &Sponge::Pkgd::Main::_result()
{
	if (_active_channel == Result_channel::launcher)
		return _launcher_result_reporter;
	if (_active_channel == Result_channel::tty)
		return _tty_result_reporter;
	if (_active_channel == Result_channel::probe)
		return _probe_result_reporter;
	return _result_reporter;
}


/*
 * Session-routing helpers (_is_parent_route/_session_label, docs/12
 * §7.2 rules 1-2) moved to Sponge::Pkg_config (see
 * <sponge/pkg_config.h>) as part of the Phase 16 P1.2 emission-library
 * extraction. Emission calls them through the library.
 */


/* ===================== request handling ===================== */

void Sponge::Pkgd::Main::_handle_request()
{
	_active_channel = Result_channel::primary;

	_request_rom.update();
	if (!_request_rom.valid())
		return;

	try {
		_handle_request_impl(_request_rom.xml());
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("explain", Genode::String<128>(),
		              "malformed request ROM");
	}
}


void Sponge::Pkgd::Main::_handle_launcher_request()
{
	if (!_launcher_request_rom.constructed())
		return;

	_active_channel = Result_channel::launcher;

	_launcher_request_rom->update();
	if (!_launcher_request_rom->valid())
		return;

	try {
		_handle_request_impl(_launcher_request_rom->xml());
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("explain", Genode::String<128>(),
		              "malformed launcher_request ROM");
	}
}


void Sponge::Pkgd::Main::_handle_tty_request()
{
	if (!_tty_request_rom.constructed())
		return;

	_active_channel = Result_channel::tty;

	_tty_request_rom->update();
	if (!_tty_request_rom->valid())
		return;

	try {
		_handle_request_impl(_tty_request_rom->xml());
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("explain", Genode::String<128>(),
		              "malformed tty_request ROM");
	}
}


void Sponge::Pkgd::Main::_handle_probe_request()
{
	if (!_probe_request_rom.constructed())
		return;

	_active_channel = Result_channel::probe;

	_probe_request_rom->update();
	if (!_probe_request_rom->valid())
		return;

	try {
		_handle_request_impl(_probe_request_rom->xml());
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("explain", Genode::String<128>(),
		              "malformed probe_request ROM");
	}
}


void Sponge::Pkgd::Main::_handle_request_impl(Genode::Xml_node const &req)
{
	if (!req.has_type("request")) {
		_report_error("explain", Genode::String<128>(),
		              "request root is not <request>");
		return;
	}

	Genode::String<32>  const op  = req.attribute_value("op",
	                                         Genode::String<32>());
	Genode::String<128> const pkg = req.attribute_value("pkg",
	                                         Genode::String<128>());
	Genode::String<16>  const seq = req.attribute_value("seq",
	                                         Genode::String<16>());

	Genode::String<160> const sig(op, "|", pkg, "|", seq);
	if (sig == _last_request_sig) {
		/*
		 * Duplicate: re-processing would re-run the side effects, but
		 * answering nothing strands the caller until its timeout (the
		 * launcher's double-click case). Replay the cached answer
		 * instead — see _last_request_sig above.
		 */
		Genode::log("sponge_pkgd: duplicate request ", sig,
		            " — replaying the cached result");
		_replay_last_result();
		return;
	}
	_last_request_sig = sig;

	/* `list` takes no package; every other op requires one. */
	if (Genode::strcmp(op.string(), "list") == 0) {
		_do_list();
		return;
	}

	if (Genode::strcmp(pkg.string(), "") == 0) {
		_report_error(op.string(), pkg, "no package specified in request");
		return;
	}

	if (Genode::strcmp(op.string(), "explain") == 0) {
		_do_explain(pkg);
		return;
	}
	if (Genode::strcmp(op.string(), "install") == 0) {
		_do_install(pkg);
		return;
	}
	if (Genode::strcmp(op.string(), "remove") == 0) {
		_do_remove(pkg);
		return;
	}
	if (Genode::strcmp(op.string(), "launch") == 0) {
		_do_launch(pkg);
		return;
	}

	_report_error(op.string(), pkg, "unknown operation");
}


/* ===================== runtime-config generation ===================== */

/*
 * Emit the full pkg_runtime <config> from _installed.
 *
 * Determinism contract (the highest-risk constraint of Phase 4b):
 * the output for a given installed set is byte-identical every run.
 * Consequences:
 *   - <start> nodes are emitted in name-sorted order (so install order
 *     cannot perturb an already-running child's config);
 *   - every attribute is emitted in a fixed order;
 *   - no volatile fields (no timestamps, counters, or session ids).
 * init diffs the new config against the live tree, so unchanged
 * children keep running across an unrelated install.
 */
/*
 * Emit BOTH sandbox configs from _installed.
 *
 * One request produces two deterministic configs: the pkg_runtime one
 * for <kind>app</kind> packages and the driver_runtime one for
 * <kind>driver</kind> packages (docs/12 §4.5). They are emitted together
 * so no call site can ever publish one without the other and leave the
 * two sandboxes disagreeing about the installed set.
 */
void Sponge::Pkgd::Main::_generate_runtime_config()
{
	/* test-only (repro harness): delay the first generation to model the
	 * real-hardware gap where pkg_runtime attaches to the empty module
	 * SECONDS before pkgd's first write. Gated by <repro_delay_ms> in
	 * the config; inert in production. */
	static bool delayed = false;
	if (!delayed) {
		delayed = true;
		_config_rom.update();
		Genode::uint64_t ms = 0;
		if (_config_rom.valid()) {
			char const * const base = _config_rom.local_addr<char const>();
			Genode::size_t const sz = _config_rom.size();
			for (Genode::size_t i = 0; i + 14 < sz; ++i)
				if (Genode::strcmp(base + i, "repro_delay_ms", 14) == 0) {
					Genode::size_t j = i + 14;
					while (j < sz && (base[j] == ' ' || base[j] == ':'
					                  || base[j] == '"' || base[j] == '=')) ++j;
					while (j < sz && base[j] >= '0' && base[j] <= '9')
						ms = ms * 10 + (Genode::uint64_t)(base[j++] - '0');
					break;
				}
		}
		if (ms) {
			Genode::log("sponge_pkgd: repro delay ", ms, " ms before first write");
			/* preemptible stall: repeated config-ROM update RPCs; each is
			 * an IPC round trip on the entrypoint (schedulable), so the
			 * rest of the system keeps running. repro-only. */
			for (Genode::uint64_t i = 0; i < ms * 400; ++i) {
				_config_rom.update();
				for (Genode::uint64_t j = 0; j < 20000; ++j) { asm volatile("" ::: "memory"); }
			}
			Genode::log("sponge_pkgd: repro delay elapsed");
		}
	}

	++_runtime_config_writes;
	/* test-only (repro harness): pad the generated config past
	 * RUNTIME_CONFIG_BUFFER to force the writer-session close+reopen on
	 * the next write — reproducing the transient empty-config window
	 * that _runtime_reporter's comment above explains. Gated by
	 * <repro_pad> in the config; inert in production.
	 *
	 * The two knobs are independent substring probes over the config
	 * (the same raw-scan idiom as <repro_delay_ms> above): <repro_pad>
	 * pads every write by a constant, while <repro_pad_grow> pads write
	 * N by N * REPRO_PAD_GROW_BYTES so that EVERY write re-enters the
	 * Expanding_reporter growth path instead of only the first. The
	 * grow knob is what run/sponge-config-regen-survival.run uses to
	 * observe whether such a growth kills already-running pkg_runtime
	 * children.
	 *
	 * MANDATORY: both knobs must be written with a VALUE in the
	 * component's config ("repro_pad_grow: yes"), not as a bare token.
	 * A child config is delivered in HID (base/src/lib/ldso/main.cc:
	 * 'generate_xml' defaults to false), and Genode's HID attribute
	 * parser drops a valueless tag without complaint
	 * (base/src/lib/base/hid.cc:82-86 requires "key: value"). A bare
	 * "+ config | repro_pad_grow" therefore yields an EMPTY config for
	 * this component and the knob silently never fires. */
	bool repro_pad = false;
	bool repro_pad_grow = false;
	Genode::uint64_t repro_empty_from = 0;
	{
		_config_rom.update();
		if (_config_rom.valid()) {
			char const * const base = _config_rom.local_addr<char const>();
			Genode::size_t const sz = _config_rom.size();
			for (Genode::size_t i = 0; i + 16 < sz; ++i) {
				if (Genode::strcmp(base + i, "repro_pad", 9) == 0)
					repro_pad = true;
				if (Genode::strcmp(base + i, "repro_pad_grow", 14) == 0)
					repro_pad_grow = true;
				/* <repro_empty_from: N> — positive control for the
				 * configuration-regeneration survival scenario: from
				 * write N on, publish an EMPTY but valid config, which
				 * is precisely what pkg_runtime's init would see if
				 * the transient no-writer window were visible to it.
				 * It must tear down the running children, which proves
				 * that the scenario's detector is not blind. repro-only.
				 */
				if (Genode::strcmp(base + i, "repro_empty_from", 16) == 0) {
					Genode::size_t j = i + 16;
					while (j < sz && (base[j] == ' ' || base[j] == ':'
					                  || base[j] == '"' || base[j] == '=')) ++j;
					while (j < sz && base[j] >= '0' && base[j] <= '9')
						repro_empty_from = repro_empty_from * 10
						                 + (Genode::uint64_t)(base[j++] - '0');
					if (repro_empty_from)
						Genode::log("sponge_pkgd: repro_empty_from=",
						            repro_empty_from);
				}
			}
		}
	}
	if (repro_empty_from && _runtime_config_writes >= repro_empty_from) {
		Genode::log("sponge_pkgd: publishing EMPTY config (write ",
		            _runtime_config_writes, ") — control channel");
		_runtime_reporter.generate_xml([&](Genode::Xml_generator &) { });
		_emit_runtime_config(_driver_runtime_reporter, true);
		return;
	}
	if (repro_pad)
		pad_bytes = REPRO_PAD_BYTES;
	if (repro_pad_grow) {
		pad_bytes = _runtime_config_writes * REPRO_PAD_GROW_BYTES;
		/* Audit line: the pad is larger than RUNTIME_CONFIG_BUFFER from
		 * the first write on, so every write below provably overflows the
		 * Expanding_reporter buffer and re-enters its growth path (the
		 * close+reopen of the report session). Without this line the
		 * reproduction scenario could not distinguish "the knob was
		 * active and forced growth" from "the knob never matched and
		 * nothing was ever forced". repro-only. */
		Genode::log("sponge_pkgd: repro_pad_grow write=",
		            _runtime_config_writes, " emitted_pad>=", pad_bytes,
		            " buffer=", (Genode::size_t)RUNTIME_CONFIG_BUFFER);
	}

	_emit_runtime_config(_runtime_reporter,     false);
	_emit_runtime_config(_driver_runtime_reporter, true);
}


/*
 * Snapshot the state _emit_runtime_config has always read (_installed
 * in array order, the Phase 7 running gate, the is_driver() split) as
 * explicit view values for the pure sponge_pkg_config library. The
 * runtime_class token reproduces the pre-extraction membership filter
 * (`is_driver() != drivers`) exactly: "driver" versus "app". Member
 * storage keeps the snapshot off the requesting thread's small stack
 * (the pre-extraction code's lesson for its emit scratch buffers).
 */
void Sponge::Pkgd::Main::_refresh_config_views()
{
	_num_config_views = 0;
	for (unsigned i = 0; i < _num_installed && i < MAX_PACKAGES; ++i) {
		Package const &c = _installed[i];
		Sponge::Pkg_config::Package_view &v =
			_config_views[_num_config_views++];
		v.name          = c.name.string();
		v.binary        = c.binary.string();
		v.ram           = c.ram.string();
		v.caps          = c.caps;
		v.config_xml    = c.config_xml.string();
		v.runtime_class = c.is_driver() ? "driver" : "app";
		v.running       = _is_running(c.name.string());
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


/*
 * Emit one sandbox config through the pure sponge_pkg_config library
 * (Phase 16 P1.2 extraction; the pre-extraction generator lived here
 * and is preserved byte for byte by the library — see
 * <sponge/pkg_config.h> and lib/src/sponge_pkg_config/self_test.cc).
 * `drivers` selects which half of the installed set lands in it:
 * false = the pkg_runtime config (app packages), true = the
 * driver_runtime config (driver packages). The library shares every
 * other part of the generator between targets — the sorted <start>
 * order, the fixed attribute order, the absence of volatile fields —
 * so both configs carry the same determinism contract (the reason
 * unchanged children survive an unrelated install).
 */
void Sponge::Pkgd::Main::_emit_runtime_config(Genode::Expanding_reporter &reporter,
                                              bool drivers)
{
	_refresh_config_views();

	Sponge::Pkg_config::Emit_options options { };
	options.binary_prefix = _binary_prefix.string();

	/*
	 * test-only (repro harness): the pad budget lands only in the
	 * pkg_runtime (app) config, exactly as the pre-extraction code had
	 * it (`if (!drivers && pad_bytes)`). The driver emission always
	 * passes 0.
	 */
	options.pad_bytes = drivers ? 0 : pad_bytes;

	Sponge::Pkg_config::Target const target =
		drivers ? Sponge::Pkg_config::driver_target()
		        : Sponge::Pkg_config::app_target();

	reporter.generate_xml([&] (Genode::Xml_generator &g) {
		Sponge::Pkg_config::emit_target(g, target, _config_views,
		                                _num_config_views, options);
	});
}


/* ===================== installed broadcast (Phase 5c) ===================== */

void Sponge::Pkgd::Main::_generate_installed_report()
{
	/*
	 * Name-sorted (same selection sort as _generate_runtime_config and
	 * _report_list_ok) so the broadcast is byte-identical for a given
	 * installed set — long-lived watchers can cheaply diff consecutive
	 * versions without churn.
	 */
	unsigned order[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_installed; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_installed; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_installed; ++j) {
			if (Genode::strcmp(_installed[order[j]].name.string(),
			                   _installed[order[best]].name.string()) < 0)
				best = j;
		}
		if (best != i) {
			unsigned tmp = order[i]; order[i] = order[best]; order[best] = tmp;
		}
	}

	_installed_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("count", _num_installed);
		g.attribute("config_writes", _runtime_config_writes);

		g.node("packages", [&] {
			for (unsigned n = 0; n < _num_installed; ++n) {
				Package const &p = _installed[order[n]];
				g.node("package", [&] {
					g.attribute("name",    p.name);
					g.attribute("version", p.version);
					g.attribute("binary",  p.binary);
					/*
					 * Phase 7 lifecycle (docs/12 §9.2.1): per-package
					 * running="yes"|"no" so watchers (launcher, vct)
					 * can distinguish registered from running without
					 * inferring state from missing metadata.
					 */
					g.attribute("running",
					            _is_running(p.name.string()) ? "yes" : "no");
					/* Which sandbox hosts the package (docs/12 §4.5), so
					 * a long-lived watcher — the Sponge DE launcher above
					 * all — can render drivers apart from apps instead of
					 * offering a driver in the application launcher. */
					g.attribute("kind", p.kind.string());
					g.attribute("exclusive", p.exclusive_group.string());
					if (p.has_launcher)
						g.attribute("category", p.launcher_category);
					g.attribute("description", p.description);
				});
			}
		});
	});
}


/* ===================== result generation ===================== */

void Sponge::Pkgd::Main::_report_ok(Genode::String<128> const &pkg)
{
	/* The root package is the last entry in the install-ordered plan. */
	Package const &root = _plan[_num_plan > 0 ? _num_plan - 1 : 0];

	_last_op     = Genode::String<32>("explain");
	_last_pkg    = pkg;
	_last_status = Genode::String<32>("ok");
	_last_message = Genode::String<256>();

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "explain");
		g.attribute("pkg",    pkg);
		g.attribute("version", root.version);

		g.node("description", [&] {
			g.append_sanitized(root.description); });

		/* Step 1: dependencies (root's direct deps, in declared order). */
		g.node("dependencies", [&] {
			for (unsigned i = 0; i < root.num_deps; ++i) {
				g.node("dep", [&] {
					g.attribute("name", root.deps[i]);
				});
			}
		});

		/* Step 2: expanded component tree, install order (deps first). */
		g.node("components", [&] {
			for (unsigned i = 0; i < _num_plan; ++i) {
				Package const &c = _plan[i];
				g.node("component", [&] {
					g.attribute("name",    c.name);
					g.attribute("binary",  c.binary);
					g.attribute("ram",     c.ram);
					g.attribute("caps",    c.caps);
					g.attribute("requires", c.requires_sessions());
					g.attribute("new", c.reused ? "no" : "yes");
				});
			}
		});

		/* Step 3: session routes, grouped by component, in plan order. */
		g.node("routes", [&] {
			for (unsigned i = 0; i < _num_plan; ++i) {
				Package const &c = _plan[i];
				for (unsigned s = 0; s < c.num_sessions; ++s) {
					Package::Session const &session = c.sessions[s];
					g.node("route", [&] {
						g.attribute("component", c.name);
						g.attribute("session",   session.name);
						g.attribute("target",    session.route);
						if (session.readonly)
							g.attribute("readonly", "true");
						if (Genode::strcmp(session.subpath.string(), "") != 0)
							g.attribute("subpath", session.subpath);
					});
				}
			}
		});

		/* Step 4: launcher entry (omitted entirely if absent). */
		if (root.has_launcher) {
			g.node("launcher", [&] {
				g.attribute("category", root.launcher_category);
			});
		}
	});
}


void Sponge::Pkgd::Main::_report_error(char const *op,
                                       Genode::String<128> const &pkg,
                                       char const *message)
{
	_last_op      = Genode::String<32>(op);
	_last_pkg     = pkg;
	_last_status  = Genode::String<32>("error");
	_last_message = Genode::String<256>(message);
	_num_last_items = 0;

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "error");
		g.attribute("op",     op);
		g.attribute("pkg",    pkg);
		g.attribute("error",  message);
	});
}


/* ---- install / remove results ---- */

void Sponge::Pkgd::Main::_report_install_ok(Genode::String<128> const &pkg,
                                            Package const &root,
                                            Genode::String<64> const *added,
                                            unsigned num_added)
{
	_last_op     = Genode::String<32>("install");
	_last_pkg    = pkg;
	_last_status = Genode::String<32>("ok");
	_last_message = Genode::String<256>();
	_cache_items(added, num_added);

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status",  "ok");
		g.attribute("op",      "install");
		g.attribute("pkg",     pkg);
		g.attribute("version", root.version);

		g.node("components_added", [&] {
			for (unsigned i = 0; i < num_added; ++i)
				g.node("component", [&] { g.attribute("name", added[i]); });
		});

		if (root.has_launcher) {
			g.node("launcher", [&] {
				g.attribute("category", root.launcher_category);
			});
		}
	});
}


void Sponge::Pkgd::Main::_report_remove_ok(Genode::String<128> const &pkg,
Genode::String<64> const *removed,
                                            unsigned num_removed)
{
	_last_op     = Genode::String<32>("remove");
	_last_pkg    = pkg;
	_last_status = Genode::String<32>("ok");
	_last_message = Genode::String<256>();
	_cache_items(removed, num_removed);

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "remove");
		g.attribute("pkg",    pkg);

		g.node("components_removed", [&] {
			for (unsigned i = 0; i < num_removed; ++i)
				g.node("component", [&] { g.attribute("name", removed[i]); });
		});
	});
}


/*
 * Launch result (Phase 7, docs/12 §9.2.1). The outcome — one of
 * "ok" / "not-installed" / "already-running" — is encoded directly in
 * the `status` attribute so callers polling the result ROM can branch
 * on the existing status field without parsing a separate outcome
 * attribute. `status="ok"` is reserved for a successful transition;
 * the other two outcomes are non-error but non-success (the request
 * was well-formed and processed; no state changed).
 */
void Sponge::Pkgd::Main::_report_launch_ok(Genode::String<128> const &pkg,
                                           char const *outcome)
{
	_last_op     = Genode::String<32>("launch");
	_last_pkg    = pkg;
	_last_status = Genode::String<32>(outcome);
	_last_message = Genode::String<256>();
	_num_last_items = 0;

	Genode::log("sponge_pkgd: launch result ", pkg, " -> ", outcome,
	            " (channel=",
	            _active_channel == Result_channel::launcher ? "launcher"
            : _active_channel == Result_channel::tty ? "tty"
            : _active_channel == Result_channel::probe ? "probe" : "primary",
	            ")");
	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", outcome);
		g.attribute("op",     "launch");
		g.attribute("pkg",    pkg);
	});
}


void Sponge::Pkgd::Main::_report_list_ok()
{
	_last_op     = Genode::String<32>("list");
	_last_status = Genode::String<32>("ok");
	_last_message = Genode::String<256>();
	_num_last_items = 0;

	/* Name-sorted, matching the config generator's ordering, so `list`
	 * is a faithful view of exactly what pkgd will regenerate. */
	unsigned order[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_installed; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_installed; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_installed; ++j)
			if (Genode::strcmp(_installed[order[j]].name.string(),
			                   _installed[order[best]].name.string()) < 0)
				best = j;
		if (best != i) {
			unsigned tmp = order[i]; order[i] = order[best]; order[best] = tmp;
		}
	}

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "list");
		g.attribute("count",  _num_installed);

		g.node("packages", [&] {
			for (unsigned n = 0; n < _num_installed; ++n) {
				Package const &p = _installed[order[n]];
				g.node("package", [&] {
					g.attribute("name",    p.name);
					g.attribute("version", p.version);
					/* Phase 5c: additive attributes. vct's list
					 * renderers read only name+version so they keep
					 * working; the launcher reads `category` to group
					 * entries. Phase 7 adds `running` so list is a
					 * faithful view of the lifecycle state too. */
					g.attribute("binary", p.binary);
					g.attribute("running",
					            _is_running(p.name.string()) ? "yes" : "no");
					/* Which sandbox hosts the package (docs/12 §4.5), so
					 * a watcher can render drivers apart from apps. */
					g.attribute("kind", p.kind.string());
					/* Exclusive-group claim (docs/12 §4.6), empty for
					 * the overwhelming majority; a watcher uses it to
					 * tell the user which package must be removed before
					 * another one in the same group can start. */
					g.attribute("exclusive", p.exclusive_group.string());
					if (p.has_launcher)
						g.attribute("category", p.launcher_category);
					g.attribute("description", p.description);
				});
			}
		});
	});
}


void Sponge::Pkgd::Main::_cache_items(Genode::String<64> const *items, unsigned n)
{
	_num_last_items = 0;
	for (unsigned i = 0; i < n && i < MAX_PACKAGES; ++i)
		_last_items[_num_last_items++] = items[i];
}


/*
 * Re-emit the previous request's answer on whichever channel the
 * duplicate arrived on. Each branch re-invokes the SAME _report_* helper
 * that produced the original result with the SAME arguments, so the
 * replayed report is byte-identical to what a fresh identical request
 * would produce (a second launch of a running package answers
 * "already-running", not silence).
 */
void Sponge::Pkgd::Main::_replay_last_result()
{
	if (_last_status == Genode::String<32>("insufficient-resources")) {
		/*
		 * Replayed before the op branches: the op is "install" or
		 * "launch", and only _report_launch_ok would be picked by the
		 * generic branch below — which carries no figures. The cached
		 * budget is re-emitted verbatim so the second answer cites the
		 * same numbers as the first.
		 */
		_report_insufficient(_last_op.string(), _last_pkg,
		                     _last_need_ram, _last_avail_ram,
		                     _last_need_caps, _last_avail_caps);
		return;
	}
	if (_last_status == Genode::String<32>("exclusive-conflict")) {
		_report_conflict(_last_op.string(), _last_pkg,
		                 _last_peer, _last_group);
		return;
	}
	if (_last_status == Genode::String<32>("error")) {
		_report_error(_last_op.string(), _last_pkg, _last_message.string());
		return;
	}
	if (_last_op == Genode::String<32>("list")) {
		_report_list_ok();
		return;
	}
	if (_last_op == Genode::String<32>("explain")) {
		_report_ok(_last_pkg);
		return;
	}
	if (_last_op == Genode::String<32>("install")) {
Package const &root = _plan[_num_plan > 0 ? _num_plan - 1 : 0];

	/*
	 * Exclusive-group pre-flight (docs/12 §4.6). An <autostart/>
	 * package enters the running set the moment the install lands, so an
	 * install IS a spawn: `vct install fb_boot` on a medium where
	 * fb_vesa is already driving the display must be REFUSED rather than
	 * quietly produce two claimants of the PCI VGA class — the visible
	 * result of that race is a dark screen with no diagnosis anywhere.
	 *
	 * Refusing (rather than auto-stopping the incumbent) keeps the rule
	 * one-directional and auditable: a package is never taken away by
	 * an operation the user did not name. The remedy is in the message
	 * and in docs/15 §8.2 — `vct remove <old>` then install again.
	 */
	for (unsigned i = 0; i < _num_plan; ++i) {
		Package const &c = _plan[i];
		if (!c.has_autostart || _is_running(c.name.string()))
			continue;
		Genode::String<64> const peer = _exclusive_peer(c, c.name);
		if (c.exclusive())
			Genode::log("sponge_pkgd: exclusive-group check on '", c.name,
			            "' group='", c.exclusive_group, "' peer='", peer, "'");
		if (peer != Genode::String<64>()) {
			_report_conflict("install", c.name, peer,
			                 c.exclusive_group);
			return;
		}
	}
		_report_install_ok(_last_pkg, root, _last_items, _num_last_items);
		return;
	}
	if (_last_op == Genode::String<32>("remove")) {
		_report_remove_ok(_last_pkg, _last_items, _num_last_items);
		return;
	}
	if (_last_op == Genode::String<32>("launch")) {
		_report_launch_ok(_last_pkg, _last_status.string());
		return;
	}

	/*
	 * No recorded result — the previous request died before reporting.
	 * Answer with a bounded, honest error instead of the old silence.
	 */
	_report_error(_last_op.string(), _last_pkg,
	              Genode::String<128>("no result to replay for the previous ",
	                                   "request").string());
}


/* ===================== persistent store (optional) ===================== */

char const         Sponge::Pkgd::Main::STORE_PATH[]   = "/store/installed.xml";
unsigned const     Sponge::Pkgd::Main::STORE_VERSION  = 1;
Genode::size_t const Sponge::Pkgd::Main::STORE_BUF    = 4096;


/*
 * Build the Vfs environment only when the component <config> carries a
 * <vfs> node. Constructing Vfs::Root mounts the <fs/> plugin,
 * which opens a File_system session routed by the scenario (e.g. to an
 * lx_fs server backed by a host directory). With no <vfs> node the
 * store stays disabled and the daemon behaves exactly as the in-memory
 * Phase 4 build — this is what keeps the non-persistent scenarios and
 * any deployment that declines persistence working unchanged.
 */
void Sponge::Pkgd::Main::_init_store()
{
	_config_rom.update();
	if (!_config_rom.valid())
		return;

	try {
		_config_rom.node().with_optional_sub_node("vfs",
			[&](Genode::Node const &vfs_node) {
				_vfs_env.construct(_env, _heap, vfs_node);
				Genode::log("sponge_pkgd: persistent store enabled at ", STORE_PATH);
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — persistence disabled");
	}
}


/*
 * Launcher channel opt-in (Phase 7 todo 10). The launcher_request ROM
 * is opened ONLY when <config> carries <launcher_request/>. Genode
 * 26.05's session-denial path calls sleep_forever() (not a throw), so
 * an eager open in an unrouted scenario would hang the daemon. The
 * config gate is identical in spirit to _init_store's <vfs> gate.
 */
void Sponge::Pkgd::Main::_init_launcher_channel()
{
	_config_rom.update();
	if (!_config_rom.valid())
		return;

	try {
		_config_rom.node().with_optional_sub_node("launcher_request",
			[&](Genode::Node const &) {
				_launcher_request_rom.construct(_env, "launcher_request");
				_launcher_request_rom->sigh(_launcher_request_handler);
				_launcher_request_rom->update();
				Genode::log("sponge_pkgd: launcher_request channel enabled");
			});
		_config_rom.node().with_optional_sub_node("tty_request",
			[&](Genode::Node const &) {
				_tty_request_rom.construct(_env, "tty_request");
				_tty_request_rom->sigh(_tty_request_handler);
				_tty_request_rom->update();
				Genode::log("sponge_pkgd: tty_request channel enabled");
			});
		_config_rom.node().with_optional_sub_node("probe_request",
			[&](Genode::Node const &) {
				_probe_request_rom.construct(_env, "probe_request");
				_probe_request_rom->sigh(_probe_request_handler);
				_probe_request_rom->update();
				Genode::log("sponge_pkgd: probe_request channel enabled");
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — launcher channel disabled");
	}
}


/*
 * Restore _roots[] from the store. Every failure mode — missing file,
 * empty/oversized, unreadable, wrong root element, unsupported version,
 * malformed XML — resolves to the same safe state: an empty root set
 * plus a warning, never a crash (docs/12-package-format.md §13.2). The
 * full installed set is rebuilt afterwards by the caller via
 * _sync_installed_from_roots(), so loading and installing share one path.
 */
void Sponge::Pkgd::Main::_load_store()
{
	if (!_store_enabled()) return;

	Genode::Vfs::File_system &vfs = _vfs_env->fs();

	Genode::Vfs::Directory_service::Stat stat { };
	if (vfs.stat(STORE_PATH, stat) != Genode::Vfs::Directory_service::STAT_OK) {
		Genode::log("sponge_pkgd: no installed-set store — starting empty");
		return;
	}
	if (stat.size == 0 || stat.size > STORE_BUF) {
		Genode::warning("sponge_pkgd: store size ", stat.size,
		                " out of range — starting empty");
		return;
	}

	Genode::Vfs::Vfs_handle *handle { nullptr };
	if (vfs.open(STORE_PATH, Genode::Vfs::Directory_service::OPEN_MODE_RDONLY,
	             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_pkgd: store open failed — starting empty");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard guard(handle);

	char buf[STORE_BUF] { };
	Genode::size_t total { 0 };
	bool ok { true };
	while (total < stat.size) {
		Genode::Vfs::Vfs_handle::Read_result r
			{ Genode::Vfs::Vfs_handle::Read_error::DENIED };
		for (;;) {
			r = handle->read(Genode::Vfs::At { .pos = total },
			                 Genode::Byte_range_ptr(buf + total,
			                                        stat.size - total));
			if (r != Genode::Vfs::Vfs_handle::Read_error::RETRY)
				break;
			_vfs_env->io().commit_and_wait();
		}
		Genode::size_t const n = r.convert<Genode::size_t>(
			[](Genode::size_t bytes) { return bytes; },
			[](Genode::Vfs::Vfs_handle::Read_error) {
				return Genode::size_t(0); });
		if (n == 0) {
			ok = false; break;
		}
		total += n;
	}

	if (!ok || total == 0) {
		Genode::warning("sponge_pkgd: store unreadable — starting empty");
		return;
	}

	unsigned restored { 0 };
	try {
		Genode::Xml_node const root(buf, total);
		if (!root.has_type("sponge-installed")) {
			Genode::warning("sponge_pkgd: store root is not <sponge-installed> "
			                "— starting empty");
			return;
		}
		unsigned const version = root.attribute_value("version", 0U);
		if (version != STORE_VERSION) {
			Genode::warning("sponge_pkgd: store version ", version,
			                " unsupported (expected ", STORE_VERSION,
			                ") — starting empty");
			return;
		}
		root.for_each_sub_node("root", [&](Genode::Xml_node const &n) {
			Genode::String<64> const name =
				n.attribute_value("name", Genode::String<64>());
			if (Genode::strcmp(name.string(), "") != 0 &&
			    _num_roots < MAX_PACKAGES) {
				_roots[_num_roots++] = name;
				++restored;
			}
		});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: store is not valid XML — starting empty");
		_num_roots = 0;
		return;
	}

	Genode::log("sponge_pkgd: restored ", restored, " root(s) from store");
}


/*
 * Persist _roots[] to the store. Output is name-sorted with a fixed
 * attribute order so the file is byte-stable for a given root set
 * (matching the determinism contract of the config generator). A failed
 * write is logged but never blocks the install/remove: the in-memory
 * state and the broadcast still reflect the requested change, only the
 * across-reboot durability is lost for that one mutation.
 *
 * The version attribute is emitted as the literal "1" so the on-disk
 * format is grep-stable; if STORE_VERSION is bumped, update it here too.
 */
void Sponge::Pkgd::Main::_save_store()
{
	if (!_store_enabled()) return;

	unsigned order[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_roots; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_roots; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_roots; ++j)
			if (Genode::strcmp(_roots[order[j]].string(),
			                   _roots[order[best]].string()) < 0)
				best = j;
		if (best != i) {
			unsigned t = order[i]; order[i] = order[best]; order[best] = t;
		}
	}

	char buf[STORE_BUF] { };
	Genode::size_t pos { 0 };
	auto append = [&buf, &pos](char const *s) {
		while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
	};
	append("<sponge-installed version=\"1\">");
	for (unsigned i = 0; i < _num_roots; ++i) {
		append("<root name=\"");
		append(_roots[order[i]].string());
		append("\"/>");
	}
	append("</sponge-installed>");
	Genode::size_t const len = pos;

	Genode::Vfs::File_system &vfs = _vfs_env->fs();

	Genode::Vfs::Vfs_handle *handle { nullptr };
	if (vfs.open(STORE_PATH,
	             Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
	             | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
	             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_pkgd: cannot open store for write");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard guard(handle);

	handle->ftruncate(len);

	Genode::size_t off { 0 };
	bool ok { true };
	while (off < len) {
		Genode::Vfs::Vfs_handle::Write_result w
			{ Genode::Vfs::Vfs_handle::Write_error::DENIED };
		for (;;) {
			w = handle->write(Genode::Vfs::At { .pos = off },
			                  Genode::Const_byte_range_ptr(buf + off,
			                                               len - off));
			if (w != Genode::Vfs::Vfs_handle::Write_error::RETRY)
				break;
			_vfs_env->io().commit_and_wait();
		}
		Genode::size_t const n = w.convert<Genode::size_t>(
			[](Genode::size_t bytes) { return bytes; },
			[](Genode::Vfs::Vfs_handle::Write_error) {
				return Genode::size_t(0); });
		if (n == 0) { ok = false; break; }
		off += n;
	}

	while (handle->sync() == Genode::Vfs::Sync_result::RETRY)
		_vfs_env->io().commit_and_wait();

	if (!ok)
		Genode::warning("sponge_pkgd: store write incomplete");
}


/* ===================== component wiring ===================== */

/* ===================== bake manifest seeding (optional) ===================== */

/*
 * Open the bake-manifest ROM session only when this component's
 * <config> declares <bake/>. The ROM route must then be wired by the
 * scenario (mirroring sponge_configd's bake_manifest route); Genode's
 * denial path sleeps forever rather than throwing, so scenarios
 * without the route must not carry the node.
 */
void Sponge::Pkgd::Main::_init_bake()
{
	try {
		_config_rom.node().with_optional_sub_node("bake",
			[&] (Genode::Node const &) {
				_bake_manifest_rom.construct(_env, "bake_manifest");
			});
	} catch (Genode::Xml_node::Invalid_syntax) { }
}

/*
 * First-boot package seeding — the pkgd counterpart of configd's
 * boot-time _apply_bake_defaults(). The manifest (generated by
 * tool/bake from pkg/bake/<profile>.profile) lists the profile's
 * enabled packages as a "staged_packages" JSON array; each entry is
 * seeded as an install root so the launcher and pkg_runtime present
 * the pre-staged desktop on first boot. Seeds only when no roots are
 * known yet, so with a persistent store the user's removals stay
 * authoritative across reboots instead of being reverted.
 */
bool Sponge::Pkgd::Main::_seed_roots_from_bake()
{
	if (!_bake_manifest_rom.constructed() || !_bake_manifest_rom->valid())
		return false;
	if (_num_roots > 0)
		return false;

	char const *manifest = _bake_manifest_rom->local_addr<char const>();
	Genode::size_t const size = _bake_manifest_rom->size();

	auto skip_ws = [&] (Genode::size_t &p) {
		while (p < size && (manifest[p] == ' ' || manifest[p] == '\t' ||
		                    manifest[p] == '\r' || manifest[p] == '\n')) ++p;
	};

	char const *const key = "\"staged_packages\"";
	Genode::size_t const key_len = Genode::strlen(key);
	Genode::size_t i = 0;
	for (; i + key_len < size; ++i)
		if (Genode::strcmp(manifest + i, key, key_len) == 0)
			break;
	if (i + key_len >= size)
		return false;

	Genode::size_t p = i + key_len;
	skip_ws(p);
	if (p >= size || manifest[p++] != ':') return false;
	skip_ws(p);
	if (p >= size || manifest[p++] != '[') return false;

	unsigned seeded { 0 };
	for (;;) {
		skip_ws(p);
		if (p >= size || manifest[p] == ']') break;
		if (manifest[p++] != '"') break;

		char name[64] { };
		unsigned n { 0 };
		while (p < size && manifest[p] != '"') {
			if (n + 1 < sizeof(name))
				name[n++] = manifest[p];
			++p;
		}
		if (p >= size) break;
		++p;

		if (n > 0 && _num_roots < MAX_PACKAGES) {
			_roots[_num_roots++] = Genode::String<64>(name);
			++seeded;
		}

		skip_ws(p);
		if (p < size && manifest[p] == ',') { ++p; continue; }
		break;
	}

	if (seeded > 0) {
		Genode::log("sponge_pkgd: seeded ", seeded,
		            " baked package root(s) from the bake manifest");
		if (_store_enabled())
			_save_store();
	}
	return seeded > 0;
}


void Sponge::Pkgd::Main::_report_insufficient(char const *op,
                                              Genode::String<128> const &pkg,
                                              Genode::size_t need_ram,
                                              Genode::size_t avail_ram,
                                              unsigned need_caps,
                                              unsigned avail_caps)
{
	_last_op      = Genode::String<32>(op);
	_last_pkg     = pkg;
	_last_status  = Genode::String<32>("insufficient-resources");
	_last_message = Genode::String<256>();
	_num_last_items = 0;

	/* Keep the figures: the duplicate-replay recipe re-emits them. */
	_last_need_ram   = need_ram;
	_last_avail_ram  = avail_ram;
	_last_need_caps  = need_caps;
	_last_avail_caps = avail_caps;

	Genode::log("sponge_pkgd: ", op, " ", pkg,
	            " -> insufficient-resources (need_ram=", need_ram,
	            " avail_ram=", avail_ram,
	            " need_caps=", need_caps,
	            " avail_caps=", avail_caps, ")");

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "insufficient-resources");
		g.attribute("op",     op);
		g.attribute("pkg",    pkg);
		g.attribute("need_ram",   need_ram);
		g.attribute("avail_ram",  avail_ram);
		g.attribute("need_caps",  need_caps);
		g.attribute("avail_caps", avail_caps);
	});
}


/*
 * Refusal for the exclusive-group gate (docs/12 §4.6). Carries the
 * running peer and the group name so the caller can act on the answer
 * instead of guessing: `exclusive-conflict` is a normal status, not an
 * error, and the remedy is in the same message.
 */
void Sponge::Pkgd::Main::_report_conflict(char const *op,
                                          Genode::String<128> const &pkg,
                                          Genode::String<64> const &peer,
                                          Genode::String<16> const &group)
{
	_last_op      = Genode::String<32>(op);
	_last_pkg     = pkg;
	_last_status  = Genode::String<32>("exclusive-conflict");
	_last_message = Genode::String<256>();
	_last_peer    = peer;
	_last_group   = group;

	Genode::log("sponge_pkgd: ", op, " ", pkg, " -> exclusive-conflict (",
	            group, " is held by the running package ", peer, ")");

	/* `error` is carried as well as the structured fields so every
	 * generic renderer in vct (the --manual install path, the JSON
	 * fallback) has something to print without special-casing this
	 * status. */
	Genode::String<256> const msg("the '", group, "' resource is in use by ",
	                              peer, "; remove it before starting ", pkg);
	_last_message = msg;

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "exclusive-conflict");
		g.attribute("op",     op);
		g.attribute("pkg",    pkg);
		g.attribute("group",  group.string());
		g.attribute("peer",   peer.string());
		g.attribute("remedy", "remove the running package, then retry");
		g.attribute("error",  msg.string());
	});
}


/*
 * The boot-time bootstrap, run on the Bootstrap thread.
 *
 * Split out of the constructor because the constructor runs on
 * env.ep() and everything below can block on the storage chain - see the
 * Bootstrap comment. Everything here happens exactly once, before any
 * request handler is armed, so no request can observe a
 * half-bootstrapped daemon.
 */
void Sponge::Pkgd::Main::_bootstrap()
{
	Genode::log("sponge_pkgd: bootstrap thread started");
	_load_index();

	/* Optional persistent store: activate it if <config> declares a
	 * <vfs>, then reload the previously-installed root set (if any). In
	 * the non-persistent scenarios both calls are no-ops. */
	_init_store();
	_load_store();

	/*
	 * First-boot bake seeding: when <bake/> is configured and the
	 * restored root set is empty, seed the profile's staged_packages
	 * as roots so the pre-staged desktop lands without any probe or
	 * vct interaction (the same automation configd applies to config
	 * keys at boot).
	 */
	_init_bake();
	_seed_roots_from_bake();

	/*
	 * Staged-binary manifest (opt-in via <binaries/>). Loaded before the
	 * first request so _do_launch's pre-flight is armed from the start.
	 */
_init_binary_manifest();

	/*
	 * Resource admission control (opt-in via <admission/>). Armed before
	 * the first request so a launch never reaches _admits unarmed while
	 * the report is merely late — and never arms itself in a scenario
	 * that does not relay the state report (a denied ROM session is
	 * fatal under the base Env policy).
	 */
_init_admission();
	_init_driver_admission();

	/* Rebuild the full installed set from whatever roots we now have
	 * (empty on a fresh start, possibly restored on a reboot). This is
	 * the SAME path install/remove use, so the initial config and the
	 * installed broadcast below reflect the restored state for free. */
	if (_num_roots > 0)
		_sync_installed_from_roots();

	/*
	 * Phase 7 lifecycle (docs/12 §9.2.1): on a restored boot, re-enter
	 * every <autostart/> package into _running so its <start> node is
	 * emitted below. Explicitly-launched packages are NOT persisted
	 * (§13), so a non-autostart package re-runs only after a fresh
	 * launch — this is the documented Alpha semantics. On a fresh
	 * (empty) boot _running stays empty and the call is a no-op.
	 */
	_sync_running_state();

	/* Emit the initial pkg_runtime config before anything can request
	 * it. On a restored boot this already contains the previously-
	 * installed components, so pkg_runtime starts them without any
	 * user action. (Published here so init — which constructs children
	 * in config order — sees it before it constructs pkg_runtime.) */
	_generate_runtime_config();

	/* Publish the initial installed-set broadcast so watchers (e.g.
	 * sponge-de's launcher) read a well-formed <installed .../> right
	 * away — on a restored boot this already lists the survivors. */
	_generate_installed_report();

	/*
	 * Hand over to env.ep().
	 *
	 * Order matters and is the whole point of the split. The pending
	 * request is drained HERE, on the bootstrap thread, because at this
	 * point no signal handler is armed and nothing else can touch the
	 * resolver. Only once that is done is the primary sigh armed, from
	 * which moment env.ep() owns request handling and the bootstrap thread
	 * touches nothing the ep can reach. Arming the sigh before draining
	 * would let both threads run _handle_request() concurrently against
	 * the same unsynchronised _roots/_installed/_last_request_sig - the
	 * de-duplication replay makes that usually harmless, which is exactly
	 * why it must not be left to chance.
	 */
	_handle_request();
	_request_rom.sigh(_request_handler);
	_request_rom.update();

	/*
	 * Launcher channel: constructed ONLY when this component's <config>
	 * carries a <launcher_request/> element. Genode 26.05's session
	 * routing calls sleep_forever() on denial (not a catchable throw),
	 * so the ROM session MUST NOT be opened in scenarios that do not
	 * route it. The config gate is the same opt-in pattern _init_store
	 * uses for <vfs>.
	 *
	 * Last, because arming these sighs is the point at which env.ep() may
	 * start running handlers; nothing after this call touches resolver
	 * state.
	 */
	_init_launcher_channel();

	Genode::log("sponge_pkgd: bootstrap complete");
}


Sponge::Pkgd::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_pkgd: ready");

	_config_rom.update();
	if (_config_rom.valid()) {
		_binary_prefix = _config_rom.node().attribute_value(
		    "binary_prefix", Genode::String<32>());
		Genode::log("sponge_pkgd: binary_prefix='", _binary_prefix, "'");
	} else {
		Genode::warning("sponge_pkgd: config ROM not valid at construct");
	}

	/* The config ROM is served by init from this component's own inline
	 * <config> node, so reading it above cannot involve the storage
	 * chain. Everything that can - the vfs session, the store, the bake
	 * seeding - runs on its own thread instead, and env.ep() is free from
	 * here on. */
	_bootstrap_thread.construct(_env, *this);
	if (!_bootstrap_thread->start_or_log()) {
		/* No thread: a single-threaded daemon is the old behaviour, and
		 * is strictly better than a daemon whose storage work has not
		 * happened at all. */
		_bootstrap_thread.destruct();
		_bootstrap();
	}
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Pkgd::Main main { env };
}


/* The daemon carries the resolver state on its stack during request
 * handling; keep it comfortable. */
Genode::size_t Component::stack_size() { return 64 * 1024 * sizeof(Genode::addr_t); }
