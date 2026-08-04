/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
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
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>
#include <vfs/simple_env.h>

namespace Sponge::Pkgd {

class Main;
struct Package;

namespace { }

}  /* namespace Sponge::Pkgd */


/* ===================== parsed package metadata ===================== */

struct Sponge::Pkgd::Package
{
	static constexpr unsigned MAX_DEPS     = 16;
	static constexpr unsigned MAX_SESSIONS = 16;

	Genode::String<64>  name;
	Genode::String<32>  version;
	Genode::String<256> description;

	Genode::String<64>  binary;   /* defaults to name */
	Genode::String<16>  ram;      /* "48M"            */
	unsigned            caps;     /* 512 default      */

	/*
	 * Inline <config> element from metadata, serialized to a string at
	 * parse time as a complete "<config>...</config>" block (empty when
	 * the metadata carries no <config>). Emitted verbatim into the
	 * generated <start> node via Xml_generator::append (raw, non-
	 * sanitized) so Qt/libc fragments such as <libc>/<vfs>/<tar> pass
	 * through literally (docs/12-package-format.md §7.2 rule 4).
	 */
	Genode::String<3072> config_xml;

	bool                has_autostart  { false };
	bool                has_launcher   { false };
	Genode::String<32>  launcher_category;

	struct Session
	{
		Genode::String<32> name;        /* "Gui"              */
		Genode::String<32> route;       /* "nitpicker"        */
		bool               readonly;    /* File_system only   */
		Genode::String<64> subpath;     /* "/app/nano"        */
		Genode::String<64> label;       /* optional Genode label hint */
	};

	Session   sessions[MAX_SESSIONS] { };
	unsigned  num_sessions           { 0 };

	Genode::String<64> deps[MAX_DEPS] { };
	unsigned           num_deps       { 0 };

	bool valid { false };  /* required fields present */

	/* Set during resolution: true when this package is already in the
	 * installed set (explain marks it "already present, reused"). */
	bool reused { false };

	/* Comma-joined session names, e.g. "Gui, Input, File_system".
	 * Built at result-generation time for the explain step 2 line. */
	Genode::String<128> requires_sessions() const
	{
		if (num_sessions == 0)
			return Genode::String<128>();

		char buf[128] { };
		Genode::size_t pos = 0;
		for (unsigned i = 0; i < num_sessions && pos + 2 < sizeof(buf); ++i) {
			char const *s = sessions[i].name.string();
			if (i > 0 && pos + 2 < sizeof(buf)) {
				buf[pos++] = ',';
				buf[pos++] = ' ';
			}
			while (*s && pos + 1 < sizeof(buf))
				buf[pos++] = *s++;
		}
		buf[pos] = 0;
		return Genode::String<128>(buf);
	}
};


/* ===================== dependency resolver ===================== */

/*
 * Deterministic DFS topological expansion with cycle detection
 * (docs/12-package-format.md §6).
 *
 * The plan is built in install order: dependencies appear before the
 * packages that depend on them. Each package is parsed at most once
 * (single-pass). The "installed" set is empty in Phase 4a — that is the
 * seam where 4b will consult the live init tree to reuse components.
 */
class Sponge::Pkgd::Main
{
	public:

		explicit Main(Genode::Env &env);

	private:

		static constexpr unsigned MAX_PACKAGES = 32;

		Genode::Env &_env;

		/* Request ROM: report_rom relays vct's "request" report here. */
		Genode::Attached_rom_dataspace _request_rom { _env, "request" };

		/* Result report: report_rom relays this to vct's "result" ROM. */
		Genode::Expanding_reporter _result_reporter { _env, "result", "result" };

		/*
		 * Launcher request/result channel pair (Phase 7 todo 10).
		 * report_rom is single-writer per label, so a long-lived
		 * launcher (sponge-de) cannot share the "request" label with
		 * short-lived vct invocations. pkgd exposes a SECOND input/
		 * output pair for the launcher menu. Both channels feed the
		 * SAME _handle_request_impl body and the SAME _do_launch —
		 * one backend, two transports (AGENTS.md §3.3 rule 5).
		 *
		 * _launcher_request_rom is Constructible because scenarios that
		 * do not route the "launcher_request" ROM deny the session at
		 * the parent, which is fatal if opened eagerly. The constructor
		 * tries to construct it and falls back gracefully.
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _launcher_request_rom { };
		Genode::Expanding_reporter     _launcher_result_reporter { _env, "result", "launcher_result" };

		/*
		 * Runtime config report: report_rom relays this to the nested
		 * pkg_runtime init's "config" ROM. pkgd owns the ENTIRE
		 * pkg_runtime config and regenerates it on every install/remove.
		 * init diffs the new config against the live tree, so unchanged
		 * children are not restarted even though the report is whole.
		 */
		Genode::Expanding_reporter _runtime_reporter { _env, "config", "runtime" };

		/*
		 * Installed-set broadcast (Phase 5c). Long-lived watchers
		 * (sponge-de's launcher) cannot share the request/result
		 * channel with vct because report_rom is single-writer per
		 * label. pkgd therefore mirrors sponge_configd's "config"
		 * broadcast pattern: it republishes the rich installed list
		 * as an "installed" ROM whenever the set changes, so watchers
		 * observe the live state without issuing requests. The
		 * request/result path is unchanged for vct and the probes.
		 */
		Genode::Expanding_reporter _installed_reporter { _env, "installed", "installed" };

		Genode::Signal_handler<Main> _request_handler {
			_env.ep(), *this, &Main::_handle_request };

		Genode::Signal_handler<Main> _launcher_request_handler {
			_env.ep(), *this, &Main::_handle_launcher_request };

		/*
		 * De-duplication of the request ROM. ROM signals can be
		 * delivered more than once for the same content (e.g. once on
		 * session setup, once on relay), and install/remove mutate
		 * state — so a naive re-process would turn a real install into
		 * a no-op re-install and make vct observe an empty result. We
		 * skip any request whose op|pkg|seq signature matches the last
		 * one we already handled.
		 */
		Genode::String<160> _last_request_sig { };

		/*
		 * Selects which result reporter the _report_* helpers write to
		 * during an in-flight request. Set at the top of each handler
		 * entry point. Signals dispatch serially, so no concurrency.
		 */
		enum class Result_channel { primary, launcher };
		Result_channel _active_channel { Result_channel::primary };

		/*
		 * Known-package set, loaded once from the pkg_index.xml boot
		 * module. Genode boot modules are not enumerable, and a request
		 * for a missing module is a fatal ROM-session denial under the
		 * base Env policy (component.cc), so we consult this index
		 * before opening any pkg_<name>.xml ROM. The index is a derived
		 * staging artifact (generated at boot-image assembly from the
		 * actual pkg/ contents), not the per-system runtime index that
		 * docs/12-package-format.md §5.3 rules out.
		 */
		Genode::String<64> _known[MAX_PACKAGES] { };
		unsigned           _num_known            { 0 };
		bool               _index_loaded         { false };

		/* Constructed per package during resolution (sequential DFS,
		 * so a single slot is enough). Starts deconstructed so that a
		 * missing ROM surfaces only when we actually look it up. */
		Genode::Constructible<Genode::Attached_rom_dataspace> _meta_rom { };

		/* resolution state */
		Package          _plan[MAX_PACKAGES] { };
		unsigned         _num_plan            { 0 };

		Genode::String<64> _visiting[MAX_PACKAGES] { };
		unsigned           _num_visiting          { 0 };

		Genode::String<64> _done[MAX_PACKAGES] { };
		unsigned           _num_done           { 0 };

		bool               _ok    { false };
		Genode::String<256> _error { };

		/*
		 * Installed state. _roots are the explicitly-installed package
		 * names; _installed is the full transitive closure (roots +
		 * deps). On remove, unused deps are GC'd by re-deriving
		 * _installed from the surviving _roots.
		 *
		 * The state->config path is a pure function: every report and
		 * the pkg_runtime config are derived from _roots+_installed
		 * alone, with no hidden inputs. Persistence (when enabled)
		 * mirrors ONLY _roots to disk and reloads them on construct;
		 * _installed is then rebuilt by the same _sync_installed_from_
		 * roots call the install/remove paths use, so the generator is
		 * untouched either way.
		 */
		Package            _installed[MAX_PACKAGES] { };
		unsigned           _num_installed           { 0 };
		Genode::String<64> _roots[MAX_PACKAGES]     { };
		unsigned           _num_roots               { 0 };

		/*
		 * Running set (Phase 7, docs/12-package-format.md §9.2.1).
		 * Subset of installed package names that currently have a
		 * <start> node in pkg_runtime. Membership is determined by:
		 *   - <autostart/> packages: in _running automatically when
		 *     installed (or restored on boot);
		 *   - explicitly launched packages: added by _do_launch;
		 *   - everyone else: installed-but-stopped, NOT in _running.
		 * _sync_running_state() is the single re-derivation point
		 * called after install/remove/launch/restore so the invariant
		 * "_running ⊆ installed_names, autostart ∪ explicitly-launched"
		 * always holds before the config + broadcast are regenerated.
		 */
		Genode::String<64> _running[MAX_PACKAGES]   { };
		unsigned           _num_running             { 0 };

		/*
		 * Optional persistent installed-set store (Phase 4 follow-up
		 * #2). Activated only when this component's <config> ROM
		 * contains a <vfs> node; otherwise _vfs_env stays deconstructed
		 * and _load_store()/_save_store() are no-ops, leaving behaviour
		 * identical to the in-memory Phase 4 daemon. The store itself
		 * is a single small XML file (see docs/12-package-format.md
		 * §13) on whatever File_system session the <vfs> node mounts.
		 */
		static char        const STORE_PATH[];
		static unsigned    const STORE_VERSION;
		static Genode::size_t const STORE_BUF;

		Genode::Attached_rom_dataspace         _config_rom { _env, "config" };
		Genode::Heap                           _heap       { _env.ram(), _env.rm() };
		Genode::Constructible<Genode::Vfs::Simple_env> _vfs_env { };

		/*
		 * Disk-served binary path prefix (Phase 8 P2, docs/14 §4.4/§4.6).
		 * When non-empty (e.g. "bin/"), generated <binary> names are
		 * prefixed so cached_fs_rom (chroot /system) resolves them at
		 * /system/bin/<binary>. Empty in the boot-module model — the
		 * binary name is the bare module name (backward-compatible).
		 */
		Genode::String<32> _binary_prefix { };

		/* ---- request handling ---- */
		void _handle_request();
		void _handle_launcher_request();

		/*
		 * Core request-processing body shared by both input channels.
		 * `result` selects which reporter receives the <result/> so the
		 * launcher reads "launcher_result" and vct/probes read "result"
		 * — report_rom is single-writer per label, so the two output
		 * reports cannot be merged (see _launcher_request_rom above).
		 */
		void _handle_request_impl(Genode::Xml_node const &req);
		void _do_explain(Genode::String<128> const &pkg);
		void _do_install(Genode::String<128> const &pkg);
		void _do_remove(Genode::String<128> const &pkg);
		void _do_launch(Genode::String<128> const &pkg);
		void _do_list();

		/* ---- resolution ---- */
		void _resolve(char const *root);
		void _visit(char const *name);

		void _load_index();
		bool _load_package(char const *name, Package &out);
		void _parse_package(Genode::Xml_node const &pkg, char const *name, Package &out);

		/* ---- installed-set management ---- */
		bool _installed_contains(char const *name) const;
		void _add_root(char const *name);
		void _sync_installed_from_roots();

		/* ---- running-set management (Phase 7 lifecycle) ---- */
		bool _is_running(char const *name) const;
		bool _add_running(char const *name);
		void _sync_running_state();

		/* ---- persistent store (optional) ---- */
		bool _store_enabled() const { return _vfs_env.constructed(); }
		void _init_store();
		void _load_store();
		void _save_store();

		/* ---- launcher channel (optional, config-gated) ---- */
		void _init_launcher_channel();

		/* ---- runtime-config generation (deterministic) ---- */
		void _generate_runtime_config();

		/* ---- installed-broadcast generation (Phase 5c) ---- */
		void _generate_installed_report();

		/* ---- result generation ---- */
		void _report_ok(Genode::String<128> const &pkg);
		void _report_install_ok(Genode::String<128> const &pkg,
		                        Package const &root,
		                        Genode::String<64> const *added, unsigned num_added);
		void _report_remove_ok(Genode::String<128> const &pkg,
		                       Genode::String<64> const *removed, unsigned num_removed);
		void _report_launch_ok(Genode::String<128> const &pkg,
		                       char const *outcome);
		void _report_list_ok();
		void _report_error(char const *op, Genode::String<128> const &pkg,
		                   char const *message);

		/* ---- small helpers ---- */
		Genode::Expanding_reporter &_result();
		static bool _contains(Genode::String<64> const *set, unsigned n,
		                      char const *name);
		static Genode::String<256> _cycle_message(Genode::String<64> const *visiting,
		                                           unsigned n, char const *repeat);

		/*
		 * Session-routing helpers (docs/12-package-format.md §7.2).
		 * _is_parent_route: a declared default-route routes via <parent/>
		 * when it is the literal "parent" OR names an outer-system
		 * service (nitpicker/vfs/event_filter/...) that lives outside
		 * pkg_runtime — never as a <child name="nitpicker"/> (rule 1).
		 * _session_label: materialize the label attribute per rule 2
		 * (explicit label wins; read-only File_system gets "<pkg>-ro").
		 */
		static bool _is_parent_route(char const *route);
		static Genode::String<96> _session_label(char const *pkg_name,
		                                         Package::Session const &s);
};


bool Sponge::Pkgd::Main::_contains(Genode::String<64> const *set, unsigned n,
                                   char const *name)
{
	for (unsigned i = 0; i < n; ++i)
		if (Genode::strcmp(set[i].string(), name) == 0)
			return true;
	return false;
}


Genode::Expanding_reporter &Sponge::Pkgd::Main::_result()
{
	return _active_channel == Result_channel::launcher
	     ? _launcher_result_reporter : _result_reporter;
}


Genode::String<256> Sponge::Pkgd::Main::_cycle_message(
	Genode::String<64> const *visiting, unsigned n, char const *repeat)
{
	/* "dependency cycle: a -> b -> c -> a" */
	char buf[256] { "dependency cycle: " };
	Genode::size_t pos = Genode::strlen(buf);
	for (unsigned i = 0; i < n; ++i) {
		char const *s = visiting[i].string();
		while (*s && pos + 4 < sizeof(buf)) buf[pos++] = *s++;
		if (pos + 4 < sizeof(buf)) { buf[pos++] = ' '; buf[pos++] = '-'; buf[pos++] = '>'; buf[pos++] = ' '; }
	}
	char const *s = repeat;
	while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
	buf[pos] = 0;
	return Genode::String<256>(buf);
}


/*
 * Outer-system service/component names that live outside pkg_runtime and
 * therefore route via <parent/>, never <child> (docs/12 §7.2 rule 1). The
 * set is the union of (a) the literal "parent", and (b) every named
 * outer-system service the run scenarios actually wire (nitpicker/vfs/
 * event_filter/wm/report_rom/timer/input_drv). A default-route that is
 * none of these is assumed to name a sibling package inside pkg_runtime
 * and routes via <child name="..."/>.
 */
bool Sponge::Pkgd::Main::_is_parent_route(char const *route)
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
 * Materialize a session label per docs/12 §7.2 rule 2:
 *   - an explicit <session label="..."> is forwarded as-is;
 *   - a read-only File_system session with no explicit label gets the
 *     "<pkg>-ro" suffix (the convention the outer vfs policy matches);
 *   - everything else has no label attribute (empty string).
 */
Genode::String<96> Sponge::Pkgd::Main::_session_label(
	char const *pkg_name, Package::Session const &s)
{
	if (Genode::strcmp(s.label.string(), "") != 0)
		return Genode::String<96>(s.label);

	if (s.readonly && Genode::strcmp(s.name.string(), "File_system") == 0)
		return Genode::String<96>(pkg_name, "-ro");

	return Genode::String<96>();
}


/* ===================== index + metadata loading ===================== */

void Sponge::Pkgd::Main::_load_index()
{
	/* The pkg_index.xml boot module lists every pkg_<name>.xml that
	 * exists in the image. Loading it must succeed; a scenario that
	 * forgets to stage it is misconfigured. */
	try {
		Genode::Attached_rom_dataspace index(_env, "pkg_index.xml");
		index.update();
		if (!index.valid())
			return;

		Genode::Xml_node const root(index.local_addr<char>(), index.size());
		if (!root.has_type("packages"))
			return;

		root.for_each_sub_node("pkg", [&](Genode::Xml_node const &p) {
			if (_num_known < MAX_PACKAGES)
				_known[_num_known++] = p.attribute_value("name",
				                                Genode::String<64>());
		});
		_index_loaded = true;
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::warning("sponge_pkgd: pkg_index.xml not available — "
		                "every package will be reported as not found");
	}
}


bool Sponge::Pkgd::Main::_load_package(char const *name, Package &out)
{
	/* Never request a metadata ROM for a package that has no staged
	 * module: a missing boot module is a fatal ROM-session denial under
	 * Genode's base Env policy, not a catchable error. The index is the
	 * gate that keeps resolution denial-safe. */
	if (!_index_loaded || !_contains(_known, _num_known, name)) {
		if (_num_visiting == 0)
			_error = Genode::String<256>("package not found: ", name);
		else
			_error = Genode::String<256>(_visiting[_num_visiting - 1],
			                            " requires ", name,
			                            ", which is not in the repository");
		_ok = false;
		return false;
	}

	Genode::String<96> const rom_label("pkg_", name, ".xml");

	try {
		_meta_rom.construct(_env, rom_label.string());
		_meta_rom->update();
	} catch (Genode::Rom_connection::Rom_connection_failed) {
		/* Should not happen: the index guarantees the module exists.
		 * Treat defensively as a malformed repository. */
		_error = Genode::String<256>("package not found: ", name);
		_ok = false;
		return false;
	}

	if (!_meta_rom->valid()) {
		_error = Genode::String<256>("package not found: ", name);
		_ok = false;
		return false;
	}

	try {
		Genode::Xml_node const root(_meta_rom->local_addr<char>(),
		                            _meta_rom->size());
		if (!root.has_type("package")) {
			_error = Genode::String<256>("malformed metadata for package: ", name,
			                            " (root is not <package>)");
			_ok = false;
			return false;
		}
		_parse_package(root, name, out);
	} catch (Genode::Xml_node::Invalid_syntax) {
		_error = Genode::String<256>("malformed metadata for package: ", name);
		_ok = false;
		return false;
	}

	if (!out.valid) {
		if (_error.length() == 0)
			_error = Genode::String<256>("malformed metadata for package: ", name);
		_ok = false;
		return false;
	}

	return true;
}


void Sponge::Pkgd::Main::_parse_package(Genode::Xml_node const &pkg,
                                        char const *name, Package &out)
{
	bool has_name { false };
	bool has_version { false };
	bool has_description { false };

	pkg.for_each_sub_node([&](Genode::Xml_node const &child) {
		if (child.has_type("name")) {
			out.name = child.decoded_content<Genode::String<64>>();
			has_name = true;
		}
		else if (child.has_type("version")) {
			out.version = child.decoded_content<Genode::String<32>>();
			has_version = true;
		}
		else if (child.has_type("description")) {
			out.description = child.decoded_content<Genode::String<256>>();
			has_description = true;
		}
		else if (child.has_type("binary")) {
			out.binary = child.decoded_content<Genode::String<64>>();
		}
		else if (child.has_type("quota")) {
			out.ram  = child.attribute_value("ram",  Genode::String<16>("32M"));
			out.caps = child.attribute_value("caps", 512U);
		}
		else if (child.has_type("config")) {
			/*
			 * Serialize the metadata <config> element (with its inner
			 * XML) into out.config_xml as a complete "<config>...</config>"
			 * block, emitted verbatim into the <start> node later. We
			 * rebuild the element via Xml_generator::generate +
			 * append_node_content so arbitrary Qt/libc fragments
			 * (<libc>, <vfs>, <tar>, ...) are preserved structurally.
			 */
			char cfg_buf[3072] { };
			Genode::Xml_generator::Result const cfg_res =
				Genode::Xml_generator::generate(
					Genode::Byte_range_ptr(cfg_buf, sizeof(cfg_buf)),
					Genode::Xml_generator::Tag_name("config"),
					[&](Genode::Xml_generator &g) {
						/* append_node_content is nodiscard: failure
						 * (max-depth exceeded) just yields a shorter
						 * serialized fragment, logged below. */
						(void)g.append_node_content(child,
						                            { .value = 16 });
					});
			if (cfg_res.ok())
				out.config_xml = Genode::String<3072>(cfg_buf);
			else
				Genode::warning("pkg: <config> for ", name,
				                " exceeded serialization buffer");
		}
		else if (child.has_type("autostart")) {
			out.has_autostart = true;
		}
		else if (child.has_type("launcher")) {
			out.has_launcher      = true;
			out.launcher_category = child.attribute_value("category",
			                            Genode::String<32>());
		}
		else if (child.has_type("dependencies")) {
			child.for_each_sub_node("pkg", [&](Genode::Xml_node const &dep) {
				if (out.num_deps < Package::MAX_DEPS) {
					out.deps[out.num_deps++] =
					    dep.decoded_content<Genode::String<64>>();
				}
			});
		}
		else if (child.has_type("sessions")) {
			child.for_each_sub_node("session", [&](Genode::Xml_node const &s) {
				if (out.num_sessions < Package::MAX_SESSIONS) {
					Package::Session &session = out.sessions[out.num_sessions++];
					session.name      = s.attribute_value("name",
					                    Genode::String<32>());
					session.route     = s.attribute_value("default-route",
					                    Genode::String<32>());
					session.readonly  = s.attribute_value("readonly", false);
					session.subpath   = s.attribute_value("subpath",
					                    Genode::String<64>());
					session.label     = s.attribute_value("label",
					                    Genode::String<64>());
				}
			});
		}
		else {
			/* Unknown element: warn and ignore (docs/12 §4.4). */
			Genode::warning("pkg: unknown element <",
			                child.type().string(), "> in package ", name);
		}
	});

	/* binary defaults to name (docs/12 §4.1). */
	if (Genode::strcmp(out.binary.string(), "") == 0)
		out.binary = Genode::String<64>(name);

	if (Genode::strcmp(out.ram.string(), "") == 0)
		out.ram = Genode::String<16>("32M");
	if (out.caps == 0)
		out.caps = 512;

	/* Required fields (docs/12 §4.4: reject on missing name/version/description). */
	if (!has_name || !has_version || !has_description) {
		char const *missing = !has_name        ? "name"
		                    : !has_version     ? "version"
		                    :                    "description";
		_error = Genode::String<256>("package '", Genode::String<64>(name),
		                            "' is missing required element <", missing, ">");
		out.valid = false;
		return;
	}

	out.valid = true;
}


/* ===================== DFS resolution ===================== */

void Sponge::Pkgd::Main::_resolve(char const *root)
{
	_ok = true;
	_error = Genode::String<256>();
	_num_plan = 0;
	_num_visiting = 0;
	_num_done = 0;

	_visit(root);
}


void Sponge::Pkgd::Main::_visit(char const *name)
{
	if (!_ok) return;

	if (_contains(_done, _num_done, name))
		return;

	/* Already-installed packages are reused, never re-installed
	 * (docs/12 §6.1). The resolver still loads their metadata so the
	 * explain plan can show their sessions, but marks them reused. */
	bool const reused = _installed_contains(name);

	if (_contains(_visiting, _num_visiting, name)) {
		_error = _cycle_message(_visiting, _num_visiting, name);
		_ok = false;
		return;
	}

	Package p { };
	if (!_load_package(name, p))
		return;

	p.reused = reused;

	if (_num_visiting < MAX_PACKAGES)
		_visiting[_num_visiting++] = Genode::String<64>(name);

	/* Visit dependencies in declared order (deterministic). */
	for (unsigned i = 0; i < p.num_deps && _ok; ++i)
		_visit(p.deps[i].string());

	if (_num_visiting > 0)
		_num_visiting--;

	if (_num_done < MAX_PACKAGES)
		_done[_num_done++] = Genode::String<64>(name);

	if (_num_plan < MAX_PACKAGES)
		_plan[_num_plan++] = p;
}


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
	if (sig == _last_request_sig)
		return;
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


void Sponge::Pkgd::Main::_do_explain(Genode::String<128> const &pkg)
{
	_resolve(pkg.string());

	if (!_ok) {
		_report_error("explain", pkg, _error.string());
		return;
	}

	_report_ok(pkg);
}


void Sponge::Pkgd::Main::_do_list()
{
	_report_list_ok();
}


/* ===================== installed-set management ===================== */

bool Sponge::Pkgd::Main::_installed_contains(char const *name) const
{
	for (unsigned i = 0; i < _num_installed; ++i)
		if (Genode::strcmp(_installed[i].name.string(), name) == 0)
			return true;
	return false;
}


void Sponge::Pkgd::Main::_add_root(char const *name)
{
	for (unsigned i = 0; i < _num_roots; ++i)
		if (Genode::strcmp(_roots[i].string(), name) == 0)
			return;
	if (_num_roots < MAX_PACKAGES)
		_roots[_num_roots++] = Genode::String<64>(name);
}


/*
 * Rebuild _installed as the transitive closure of all installed roots.
 * Called after every install/remove so unused deps are garbage-collected
 * on remove. Each root is re-resolved (metadata is static); the union is
 * de-duplicated by name. Order within _installed does not matter because
 * the config generator sorts by name.
 */
void Sponge::Pkgd::Main::_sync_installed_from_roots()
{
	Package fresh[MAX_PACKAGES] { };
	unsigned num_fresh { 0 };

	for (unsigned r = 0; r < _num_roots; ++r) {
		_resolve(_roots[r].string());
		if (!_ok)
			continue;

		for (unsigned j = 0; j < _num_plan; ++j) {
			Package const &p = _plan[j];
			bool present { false };
			for (unsigned k = 0; k < num_fresh; ++k)
				if (Genode::strcmp(fresh[k].name.string(), p.name.string()) == 0) {
					present = true;
					break;
				}
			if (!present && num_fresh < MAX_PACKAGES)
				fresh[num_fresh++] = p;
		}
	}

	for (unsigned i = 0; i < num_fresh && i < MAX_PACKAGES; ++i)
		_installed[i] = fresh[i];
	_num_installed = num_fresh;
}


/* ===================== running-set management (Phase 7) ===================== */

bool Sponge::Pkgd::Main::_is_running(char const *name) const
{
	for (unsigned i = 0; i < _num_running; ++i)
		if (Genode::strcmp(_running[i].string(), name) == 0)
			return true;
	return false;
}


/*
 * Add a name to the running set. Returns true iff it was newly added
 * (false on duplicate or overflow). Used by _do_launch and the
 * autostart path in _sync_running_state.
 */
bool Sponge::Pkgd::Main::_add_running(char const *name)
{
	if (_is_running(name))
		return false;
	if (_num_running < MAX_PACKAGES) {
		_running[_num_running++] = Genode::String<64>(name);
		return true;
	}
	return false;
}


/*
 * Re-derive _running from the current _installed set so the invariant
 * "_running ⊆ installed_names" survives every state change. Drops any
 * running entry whose package was uninstalled; adds every autostart
 * package that is currently installed (idempotent — _add_running
 * deduplicates). Explicit (non-autostart) launches of still-installed
 * packages are preserved.
 */
void Sponge::Pkgd::Main::_sync_running_state()
{
	/* Drop running entries for packages that are no longer installed. */
	unsigned w { 0 };
	for (unsigned i = 0; i < _num_running; ++i) {
		if (_installed_contains(_running[i].string())) {
			_running[w++] = _running[i];
		}
	}
	_num_running = w;

	/* Ensure every installed <autostart/> package is in the running set. */
	for (unsigned i = 0; i < _num_installed; ++i) {
		if (_installed[i].has_autostart)
			_add_running(_installed[i].name.string());
	}
}


void Sponge::Pkgd::Main::_do_install(Genode::String<128> const &pkg)
{
	char const *const name = pkg.string();

	_resolve(name);
	if (!_ok) {
		_report_error("install", pkg, _error.string());
		return;
	}

	Package const &root = _plan[_num_plan > 0 ? _num_plan - 1 : 0];

	/* Snapshot the installed names before the change, to report exactly
	 * what this install added. */
	Genode::String<64> before[MAX_PACKAGES] { };
	unsigned num_before { 0 };
	for (unsigned i = 0; i < _num_installed && i < MAX_PACKAGES; ++i)
		before[num_before++] = _installed[i].name;

	_add_root(name);
	_sync_installed_from_roots();
	_sync_running_state();   /* picks up <autostart/> roots (docs/12 §9.2.1) */
	_save_store();   /* flush before broadcast so the change survives a reboot */
	_generate_runtime_config();
	_generate_installed_report();

	Genode::String<64> added[MAX_PACKAGES] { };
	unsigned num_added { 0 };
	for (unsigned i = 0; i < _num_installed && i < MAX_PACKAGES; ++i) {
		Genode::String<64> const &n = _installed[i].name;
		bool was { false };
		for (unsigned b = 0; b < num_before; ++b)
			if (Genode::strcmp(before[b].string(), n.string()) == 0) {
				was = true;
				break;
			}
		if (!was && num_added < MAX_PACKAGES)
			added[num_added++] = n;
	}

	_report_install_ok(pkg, root, added, num_added);
}


void Sponge::Pkgd::Main::_do_remove(Genode::String<128> const &pkg)
{
	char const *const name = pkg.string();

	if (!_installed_contains(name)) {
		_report_error("remove", pkg,
		              Genode::String<128>("package not installed: ", name).string());
		return;
	}

	/* Snapshot before, to report what removal dropped. */
	Genode::String<64> before[MAX_PACKAGES] { };
	unsigned num_before { 0 };
	for (unsigned i = 0; i < _num_installed && i < MAX_PACKAGES; ++i)
		before[num_before++] = _installed[i].name;

	/* Drop the root; re-derive the closure so unused deps are GC'd. */
	for (unsigned i = 0; i < _num_roots; ++i) {
		if (Genode::strcmp(_roots[i].string(), name) == 0) {
		_roots[i] = _roots[_num_roots - 1];
		_num_roots--;
		break;
	}
	}
	_sync_installed_from_roots();
	_sync_running_state();   /* drops the removed name from _running too */
	_save_store();   /* flush before broadcast so the change survives a reboot */
	_generate_runtime_config();
	_generate_installed_report();

	Genode::String<64> removed[MAX_PACKAGES] { };
	unsigned num_removed { 0 };
	for (unsigned b = 0; b < num_before; ++b) {
		if (!_installed_contains(before[b].string()) &&
		    num_removed < MAX_PACKAGES)
			removed[num_removed++] = before[b];
	}

	_report_remove_ok(pkg, removed, num_removed);
}


/*
 * Phase 7 launch operation (docs/12-package-format.md §9.2.1).
 *
 * Transitions an installed-but-stopped package to running by adding its
 * name to _running and regenerating the pkg_runtime config so init
 * starts the new <start> node. Three bounded outcomes:
 *   - not-installed: the package is not in the installed set
 *     (no implicit install — caller must install first);
 *   - already-running: the package already has a <start> node
 *     (idempotent launch is a no-op);
 *   - ok: the package transitioned installed -> running.
 *
 * No stop operation exists in Alpha, so once running the package stays
 * running until it is removed (which drops both the root and any
 * running entry, regenerating pkg_runtime without the <start> node).
 */
void Sponge::Pkgd::Main::_do_launch(Genode::String<128> const &pkg)
{
	char const *const name = pkg.string();

	if (!_installed_contains(name)) {
		_report_launch_ok(pkg, "not-installed");
		return;
	}

	if (_is_running(name)) {
		_report_launch_ok(pkg, "already-running");
		return;
	}

	_add_running(name);
	/*
	 * _save_store() persists only the installed root set; _running is
	 * intentionally NOT persisted (docs/12 §9.2.1 + §13). On reboot,
	 * autostart packages re-enter _running via _sync_running_state;
	 * explicitly-launched packages re-enter only via a fresh launch.
	 */
	_generate_runtime_config();
	_generate_installed_report();

	_report_launch_ok(pkg, "ok");
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
void Sponge::Pkgd::Main::_generate_runtime_config()
{
	/* Selection-sort the installed set by name into a stable index order. */
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
			unsigned tmp = order[i];
			order[i] = order[best];
			order[best] = tmp;
		}
	}

	_runtime_reporter.generate_xml([&](Genode::Xml_generator &g) {
		/*
		 * Extended parent-provides (docs/12 §7.2): the original
		 * ROM/PD/CPU/LOG/Timer set could not resolve <parent/> routes
		 * for Gui/Input/Report/File_system/NIC. Phase 7 adds exactly
		 * those five (Timer is retained, not duplicated).
		 */
		g.node("parent-provides", [&] {
			g.node("service", [&] { g.attribute("name", "ROM"); });
			g.node("service", [&] { g.attribute("name", "PD"); });
			/*
			 * RM is a distinct core service (base/src/core/main.cc
			 * Core_service<Rm_session_component>), NOT subsumed by PD.
			 * Without it in parent-provides, a package child that
			 * constructs an Rm_connection (e.g. falkon/WebEngine for
			 * mmap / qtwebengine_shm) is stopped with
			 * "no route to service RM" (sandbox/route_model.h:272).
			 * PD only hands out the component's own address_space/
			 * stack_area/linker_area region maps; additional region
			 * maps require a separate RM session routed to core.
			 */
			g.node("service", [&] { g.attribute("name", "RM"); });
			g.node("service", [&] { g.attribute("name", "CPU"); });
			g.node("service", [&] { g.attribute("name", "LOG"); });
			g.node("service", [&] { g.attribute("name", "Timer"); });
			g.node("service", [&] { g.attribute("name", "Gui"); });
			g.node("service", [&] { g.attribute("name", "Input"); });
			g.node("service", [&] { g.attribute("name", "Report"); });
			g.node("service", [&] { g.attribute("name", "File_system"); });
			/*
			 * Nic + Rtc use Genode's canonical service-name casing
			 * (case-sensitive). The prior "NIC" never matched a real
			 * request; without Rtc here, a networked+rtc package child
			 * (e.g. falkon) is stopped at construction with
			 * "parent denied Rtc-session".
			 */
			g.node("service", [&] { g.attribute("name", "Nic"); });
			g.node("service", [&] { g.attribute("name", "Rtc"); });
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

		for (unsigned n = 0; n < _num_installed; ++n) {
			Package const &c = _installed[order[n]];

			/*
			 * Phase 7 lifecycle gate (docs/12-package-format.md §9.2.1):
			 * emit <start> only for packages in _running. Installed-but-
			 * stopped packages are deliberately absent from pkg_runtime,
			 * so init never constructs them until a launch adds them.
			 */
			if (!_is_running(c.name.string()))
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
				Genode::String<96> const bin_path(_binary_prefix,
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

				/*
				 * (b) Inline <config> from metadata, emitted verbatim
				 * via raw append (Xml_generator::append is the non-
				 * sanitized path). config_xml already includes its own
				 * <config>...</config> wrapper.
				 */
				if (Genode::strcmp(c.config_xml.string(), "") != 0)
					g.append(c.config_xml.string());

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
						Package::Session const &session = c.sessions[s];

						bool const parent_route =
							_is_parent_route(session.route.string());

						Genode::String<96> const label =
							_session_label(c.name.string(), session);

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
	Genode::log("sponge_pkgd: launch result ", pkg, " -> ", outcome,
	            " (channel=",
	            _active_channel == Result_channel::launcher ? "launcher" : "primary",
	            ")");
	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", outcome);
		g.attribute("op",     "launch");
		g.attribute("pkg",    pkg);
	});
}


void Sponge::Pkgd::Main::_report_list_ok()
{
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
					if (p.has_launcher)
						g.attribute("category", p.launcher_category);
					g.attribute("description", p.description);
				});
			}
		});
	});
}


/* ===================== persistent store (optional) ===================== */

char const         Sponge::Pkgd::Main::STORE_PATH[]   = "/store/installed.xml";
unsigned const     Sponge::Pkgd::Main::STORE_VERSION  = 1;
Genode::size_t const Sponge::Pkgd::Main::STORE_BUF    = 4096;


/*
 * Build the Vfs environment only when the component <config> carries a
 * <vfs> node. Constructing Vfs::Simple_env mounts the <fs/> plugin,
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

	Genode::Vfs::File_system &vfs = _vfs_env->root_dir();

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
		handle->seek(total);
		handle->fs().queue_read(handle, stat.size - total);
		Genode::size_t n { 0 };
		Genode::Vfs::File_io_service::Read_result r;
		while ((r = handle->fs().complete_read(handle,
		            Genode::Byte_range_ptr(buf + total, sizeof(buf) - total),
		            n)) == Genode::Vfs::File_io_service::READ_QUEUED)
			_vfs_env->io().commit_and_wait();
		if (r != Genode::Vfs::File_io_service::READ_OK || n == 0) {
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

	Genode::Vfs::File_system &vfs = _vfs_env->root_dir();

	Genode::Vfs::Vfs_handle *handle { nullptr };
	if (vfs.open(STORE_PATH,
	             Genode::Vfs::Directory_service::OPEN_MODE_WRONLY
	             | Genode::Vfs::Directory_service::OPEN_MODE_CREATE,
	             &handle, _heap) != Genode::Vfs::Directory_service::OPEN_OK) {
		Genode::warning("sponge_pkgd: cannot open store for write");
		return;
	}
	Genode::Vfs::Vfs_handle::Guard guard(handle);

	handle->fs().ftruncate(handle, len);

	Genode::size_t off { 0 };
	bool ok { true };
	while (off < len) {
		handle->seek(off);
		Genode::size_t n { 0 };
		Genode::Vfs::File_io_service::Write_result const w =
			handle->fs().write(handle,
			    Genode::Const_byte_range_ptr(buf + off, len - off), n);
		if (w == Genode::Vfs::File_io_service::WRITE_OK) {
			if (n == 0) { ok = false; break; }
			off += n;
		} else if (w == Genode::Vfs::File_io_service::WRITE_ERR_WOULD_BLOCK) {
			_vfs_env->io().commit_and_wait();
		} else {
			ok = false; break;
		}
	}

	handle->fs().queue_sync(handle);
	while (handle->fs().complete_sync(handle) ==
	       Genode::Vfs::File_io_service::SYNC_QUEUED)
		_vfs_env->io().commit_and_wait();

	if (!ok)
		Genode::warning("sponge_pkgd: store write incomplete");
}


/* ===================== component wiring ===================== */

Sponge::Pkgd::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_pkgd: ready");

	_load_index();

	_config_rom.update();
	if (_config_rom.valid()) {
		_binary_prefix = _config_rom.node().attribute_value(
		    "binary_prefix", Genode::String<32>());
		Genode::log("sponge_pkgd: binary_prefix='", _binary_prefix, "'");
	} else {
		Genode::warning("sponge_pkgd: config ROM not valid at construct");
	}

	/* Optional persistent store: activate it if <config> declares a
	 * <vfs>, then reload the previously-installed root set (if any). In
	 * the non-persistent scenarios both calls are no-ops. */
	_init_store();
	_load_store();

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

	_request_rom.sigh(_request_handler);
	_request_rom.update();

	/*
	 * Launcher channel: constructed ONLY when this component's <config>
	 * carries a <launcher_request/> element. Genode 26.05's session
	 * routing calls sleep_forever() on denial (not a catchable throw),
	 * so the ROM session MUST NOT be opened in scenarios that do not
	 * route it. The config gate is the same opt-in pattern _init_store
	 * uses for <vfs>.
	 */
	_init_launcher_channel();

	/* Process a request that arrived before the signal handler was wired. */
	_handle_request();
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Pkgd::Main main { env };
}


/* The daemon carries the resolver state on its stack during request
 * handling; keep it comfortable. */
Genode::size_t Component::stack_size() { return 64 * 1024 * sizeof(Genode::addr_t); }
