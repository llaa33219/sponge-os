/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd - shared types and the daemon class declaration.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): the
 * parsed-package type and the Main class declaration moved here
 * VERBATIM from main.cc so the implementation can live in
 * responsibility modules while main.cc keeps the channel and
 * lifecycle wiring. Declarations and in-class one-liners only - no
 * behavior, no I/O.
 *
 * Module map (docs/plans/phase16-runtime-overhaul.md P1.3):
 *   main.cc     - request/result channels (dedup/replay), runtime-
 *                 config and installed-broadcast emission wiring,
 *                 bootstrap thread + constructor
 *   registry.cc - installed/running state (roots, closure, running
 *                 set, exclusive-group peer lookup)
 *   metadata.cc - pkg_index/pkg_<name>.xml parsing, DFS resolution
 *   admission.cc - resource admission math + ROM arming
 *   ops.cc      - install/remove/launch + binary-manifest pre-flight
 *   store.cc    - persistent installed-set store + bake seeding
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/heap.h>
#include <base/signal.h>
#include <base/thread.h>
#include <os/reporter.h>
#include <sponge/pkg_config.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_node.h>
#include <vfs/root.h>

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
	Genode::String<16384> config_xml;

	bool                has_autostart  { false };
	bool                has_launcher   { false };

	/* Which sandbox owns this package's <start> node: pkg_runtime ("app")
	 * or driver_runtime ("driver"). The split is what keeps hardware
	 * capabilities out of the application runtime (AGENTS.md §1.2);
	 * docs/12-package-format.md §4.5. */
	Genode::String<16>  kind            { "app" };

	bool is_driver() const
	{ return Genode::strcmp(kind.string(), "driver") == 0; }

	/*
	 * Exclusive group (docs/12-package-format.md §4.6). Empty means the
	 * package claims no exclusive hardware; a non-empty value means at
	 * most one RUNNING package may carry the same group at a time. The
	 * framebuffer drivers are the first such group: the PCI VGA class is
	 * offered to exactly one policy, so a second fb driver would take
	 * the display away rather than add a capability.
	 */
	Genode::String<16>  exclusive_group { };

	bool exclusive() const
	{ return Genode::strcmp(exclusive_group.string(), "") != 0; }
	bool                resizeable     { true };   /* Phase 16 W9: default-yes; opt-out
	                                                  is <resizeable>no</resizeable>. The
	                                                  closed-form token is yes/no (D16.10). */
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
		 * Terminal vct channel pair: the in-terminal `vct` command
		 * (a shell script writing to /dev/vct) reaches pkgd through
		 * the vct_tty bridge component. report_rom is single-writer
		 * per label, so the bridge gets its own pair. Third transport,
		 * same backend (AGENTS.md §3.3 rule 5).
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _tty_request_rom { };
		Genode::Expanding_reporter     _tty_result_reporter { _env, "result", "tty_result" };

		/*
		 * Launch-probe channel: launch_probe replays the launcher's
		 * exact click-to-launch backend (<request op="launch"/>) every
		 * boot — the automated verification for the user-facing launch
		 * path (report_rom is single-writer per label, hence a fourth
		 * transport; the handle body is shared).
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _probe_request_rom { };
		Genode::Expanding_reporter     _probe_result_reporter { _env, "result", "probe_result" };

		/*
		 * Runtime config report: report_rom relays this to the nested
		 * pkg_runtime init's "config" ROM. pkgd owns the ENTIRE
		 * pkg_runtime config and regenerates it on every install/remove.
		 * init diffs the new config against the live tree, so unchanged
		 * children are not restarted even though the report is whole.
		 *
		 * The report buffer MUST be pre-sized so growth never happens in
		 * production. Expanding_reporter's growth path
		 * (_increase_report_buffer(), os/reporter.h) destructs and
		 * reconstructs the Report session; while that window is open the
		 * "runtime" report carries no payload, so report_rom relays an
		 * empty "config" ROM to pkg_runtime and pkg_runtime's own init
		 * applies an EMPTY config — tearing down every <start> node it
		 * holds. A single launch of one big package must not be able to
		 * kill all running apps.
		 *
		 * Size derivation (measured against _generate_runtime_config's
		 * actual output, modelled with Genode's own Xml_generator
		 * serialization rules — tab indent, one node per line, metadata
		 * <config> blocks appended verbatim):
		 *   - all 11 shipped packages (desktop profile 8 + ncurses +
		 *     nano + pkg_gui_demo), binary_prefix="bin/": 22797 bytes
		 *   - the desktop bake profile alone (8 packages):   20639 bytes
		 * The dominant term is the verbatim per-package <config> block
		 * (falkon 3386, terminal 4117, pdf_view 2975 bytes); the fixed
		 * scaffolding (start/binary/resource/route + the parent-provides
		 * header) is ~460 bytes per package.
		 * 32768 gives ~44% headroom over the measured 11-package
		 * worst case, so no realistic installed set reaches it.
		 *
		 * The <repro_pad> knob (test-only, below) still forces growth
		 * on purpose; see REPRO_PAD_BYTES for its sizing.
		 */
		static constexpr Genode::size_t RUNTIME_CONFIG_BUFFER = 32768;

		/*
		 * test-only (repro harness): the <repro_pad> pad budget. The
		 * knob emits pad_bytes/64 <p v="...."/> nodes, each of which
		 * serializes to MORE than 64 bytes, so the emitted padding is
		 * at least pad_bytes (see the emit site). 32000 therefore
		 * guarantees an overflow of the 32768-byte buffer once the
		 * fixed config scaffolding is added, which re-triggers the
		 * close+reopen of the report session this harness exists to
		 * reproduce.
		 *
		 * Both repro knobs are inert unless written WITH A VALUE
		 * ("repro_pad: yes"); see the scan site for why.
		 */
		static constexpr unsigned REPRO_PAD_BYTES = 32000;

		/*
		 * test-only (repro harness): the <repro_pad_grow> increment.
		 * <repro_pad> alone pads EVERY regeneration with a constant
		 * amount, so only the first write crosses the buffer boundary —
		 * the Expanding_reporter grows once and every later write then
		 * fits. <repro_pad_grow> instead makes write N pad N times the
		 * increment, so EVERY regeneration crosses the boundary and
		 * forces the report-session close+reopen that the
		 * configuration-regeneration survival scenario
		 * (run/sponge-config-regen-survival.run) needs to observe.
		 *
		 * Sizing: RUNTIME_CONFIG_BUFFER is 32768. The first write pads
		 * 32768 bytes, so it already exceeds the buffer (and grows it
		 * to 36864); the second pads 65536 (grows to 69632), the third
		 * 98304 (grows to 101376). Each write therefore re-enters the
		 * growth path. The pad grows linearly with the write counter
		 * without an upper bound, which is intentional: the knob is
		 * inert unless a scenario sets it, and a repro run issues only
		 * a handful of requests (a few hundred KiB of report buffer on
		 * a 32 MiB pkgd quota).
		 */
		static constexpr unsigned REPRO_PAD_GROW_BYTES = 32768;

		Genode::Expanding_reporter _runtime_reporter {
			_env, "config", "runtime",
			{ .value = RUNTIME_CONFIG_BUFFER } };

		/*
		 * driver_runtime config report: the second sandbox. A driver
		 * package (<kind>driver</kind>, docs/12 §4.5) is emitted HERE
		 * instead of into the pkg_runtime config, into a nested init
		 * that is a sibling of pkg_runtime inside the `system` sandbox.
		 *
		 * Splitting the two configs is the minimum-privilege boundary:
		 * driver_runtime's generated <parent-provides> carries the
		 * hardware services (Platform/Irq/Io_mem/Io_port/...) that only
		 * the installed driver packages declared, while pkg_runtime's
		 * fixed list stays exactly as it was. An application package can
		 * therefore never obtain a Platform or Io_port session, no
		 * matter what it asks for.
		 *
		 * Size derivation: the driver set is small and each driver's
		 * <config> block is a handful of lines (the largest, pc_nic, is
		 * ~1 KiB). 8192 covers the whole Sculpt PC driver inventory
		 * (pc_nic + pc_wifi + usb_net + intel_gpu + pc_intel_fb +
		 * pc_discover + rom_reporter) with ~4x headroom, and matches the
		 * pre-sized-buffer discipline documented on _runtime_reporter
		 * above (no growth path, so no transient empty-config window).
		 */
		static constexpr Genode::size_t DRIVER_CONFIG_BUFFER = 8192;

		Genode::Expanding_reporter _driver_runtime_reporter {
			_env, "config", "driver_runtime",
			{ .value = DRIVER_CONFIG_BUFFER } };

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

		Genode::Signal_handler<Main> _tty_request_handler {
			_env.ep(), *this, &Main::_handle_tty_request };

		Genode::Signal_handler<Main> _probe_request_handler {
			_env.ep(), *this, &Main::_handle_probe_request };

		/*
		 * De-duplication of the request ROM. ROM signals can be
		 * delivered more than once for the same content (e.g. once on
		 * session setup, once on relay), and install/remove mutate
		 * state — so a naive re-process would turn a real install into
		 * a no-op re-install and make vct observe an empty result. We
		 * skip RE-PROCESSING any request whose op|pkg|seq signature
		 * matches the last one we already handled.
		 *
		 * Skipping the re-processing must NOT skip the answer: the Sponge
		 * DE launcher sends no seq, so a double-click is a genuine
		 * duplicate. Returning early without emitting anything left the
		 * launcher polling a result ROM that would never change until its
		 * 6 s timeout, with no user feedback at all. So the last result
		 * is cached as a REPLAY RECIPE (not as bytes) and re-emitted on
		 * a duplicate. A recipe — not a byte snapshot — is what makes the
		 * replay byte-identical: it re-invokes the very same _report_*
		 * helper with the same inputs.
		 *
		 * _last_* are written by the _report_* helpers themselves, so the
		 * cache can never drift from what was actually emitted. A matching
		 * signature also guarantees no other request ran in between (every
		 * handled request rewrites _last_request_sig), so the derived state
		 * those helpers read (_plan, _installed, _running) is unchanged.
		 */
		Genode::String<160> _last_request_sig { };

		Genode::String<32>  _last_op      { };
		Genode::String<128> _last_pkg     { };
		Genode::String<32>  _last_status  { };   /* "ok" | "error" | launch outcome */
		Genode::String<256> _last_message { };   /* error text, empty otherwise      */
		Genode::String<64>  _last_items[MAX_PACKAGES] { };  /* added / removed */
		unsigned            _num_last_items { 0 };

		/*
		 * Cached refusal numbers for the duplicate-request replay. A
		 * replayed "insufficient-resources" answer is only useful if it
		 * cites the same budget as the original, so the recipe keeps the
		 * four figures rather than re-deriving them (re-deriving would
		 * race the state report and could replay a DIFFERENT number for
		 * the same request).
		 */
		Genode::size_t _last_need_ram   { 0 };
		Genode::size_t _last_avail_ram  { 0 };
		unsigned       _last_need_caps  { 0 };
		unsigned       _last_avail_caps { 0 };

		/* Cached for the same reason: the peer and group a replayed
		 * exclusive-conflict must name, not re-derive. */
		Genode::String<64> _last_peer  { };
		Genode::String<16> _last_group { };

		/*
		 * Selects which result reporter the _report_* helpers write to
		 * during an in-flight request. Set at the top of each handler
		 * entry point. Signals dispatch serially, so no concurrency.
		 */
		enum class Result_channel { primary, launcher, tty, probe };
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

		/*
		 * Real-hardware observability: how many times the runtime
		 * config was generated+written. Surfaced in the installed
		 * report (config_writes) and mirrored on the panel — on
		 * hardware where C: stays 0, writes>=2 proves pkgd DID
		 * regenerate with children and the second write was lost in
		 * the report_rom -> pkg_runtime relay (vs writes==1: pkgd
		 * never got past the first empty generation).
		 */
		unsigned _runtime_config_writes { 0 };
		unsigned pad_bytes { 0 };

		/*
		 * Dirty-scoped emission invariant (D16R.4): a package
		 * operation marks exactly the runtime targets its RESOLVED
		 * plan kinds touch — never just the request string — and
		 * _generate_runtime_config() publishes a target only when its
		 * mark is set, clearing the mark on emission. Bootstrap marks
		 * BOTH targets so the initial publish reaches both sandboxes.
		 * The marks are exact rather than approximate because the
		 * generator's per-target membership filter makes each config a
		 * function of its own kind's packages alone; an unmarked target
		 * therefore really is byte-unchanged and is not republished.
		 * Emitted reports stay byte-identical to the pre-split
		 * generator for the same package state.
		 */
		bool _app_config_dirty    { false };
		bool _driver_config_dirty { false };

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
		Genode::Constructible<Genode::Vfs::Root> _vfs_env { };

		/*
		 * Thread that runs the boot-time part of the work which TOUCHES
		 * STORAGE.
		 *
		 * Component::construct() runs on env.ep()'s dispatch thread, so a
		 * File_system session request issued from the constructor blocks
		 * the entrypoint for as long as the storage chain takes to answer.
		 * That is the same defect docs/evidence/logdisk-field-diagnosis.md
		 * §3(a) records for the log sink, and pkgd repeats it three times:
		 * _init_store() opens the vfs File_system, _load_store() stats and
		 * reads it, _seed_roots_from_bake()/_init_bake() write it. On a
		 * slow or stalled USB boot device none of that comes back, the
		 * sandbox heartbeat counts skips, and pkgd is abandoned and
		 * respawned - repeatedly, because it blocks in exactly the same
		 * place every incarnation.
		 *
		 * Moving the bootstrap off the constructor leaves env.ep() free to
		 * answer from the first instruction after member construction, so
		 * a child that is merely WAITING ON ITS STORE is not a child with
		 * a wedged entrypoint. env.ep() still dispatches the storage
		 * chain's completion signals - only the blocking calls move.
		 *
		 * The two never run concurrently: the bootstrap completes before
		 * the request handlers are armed.
		 */
		struct Bootstrap : Genode::Thread
		{
			Sponge::Pkgd::Main &_main;

			Bootstrap(Genode::Env &env, Sponge::Pkgd::Main &main)
			: Thread(env, "pkgd-store", Stack_size { 64 * 1024 }), _main(main) { }

			void entry() override
			{
				_main._bootstrap();
				Genode::sleep_forever();
			}

			bool start_or_log()
			{
				if (start() != Start_result::OK) {
					Genode::error("sponge_pkgd: storage thread could not be\
 created - falling back to the entrypoint");
					return false;
				}
				return true;
			}
		};

		Genode::Constructible<Bootstrap> _bootstrap_thread { };

		/*
		 * Bake-manifest ROM (optional, config-gated by <bake/>): the
		 * first-boot package seed source (see _seed_roots_from_bake).
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _bake_manifest_rom { };

		/*
		 * Binary-manifest ROM (optional, config-gated by <binaries/>):
		 * the set of package binaries actually staged in this image,
		 * used by _do_launch's pre-flight check (see _binary_available).
		 *
		 * Why a manifest instead of probing the binary ROM directly: a
		 * missing boot module is a FATAL ROM-session denial under Genode's
		 * base Env policy — base/component.cc answers a denied session
		 * with "stop because parent denied ..." + sleep_forever(), it does
		 * NOT throw. So a probe-style open would kill sponge_pkgd exactly
		 * in the case it is meant to diagnose, and would additionally fail
		 * on every launch in disk-served mode, where the binaries live in
		 * pkg_runtime's rom_sys domain (chroot /system), not in pkgd's.
		 *
		 * This is the same denial-safe, index-gated pattern pkg_index.xml
		 * already uses for package metadata, generalized from metadata to
		 * binaries. With no <binaries/> node the check is inert and launch
		 * behaves exactly as before — the manual escape hatch
		 * (AGENTS.md §1.1).
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _binary_manifest_rom { };
		Genode::String<96> _staged_binaries[MAX_PACKAGES] { };
		unsigned           _num_staged_binaries       { 0 };
		bool               _binary_manifest_loaded    { false };

		/*
		 * Resource admission control (optional, config-gated by
		 * <admission/>). pkg_runtime's init silently CLAMPS a child to
		 * whatever quota it has left
		 * (genode/repos/os/src/lib/sandbox/child.cc:483-486,
		 * `ram_quota = avail_ram`), so a launch that does not fit is
		 * answered "ok" and then evaporates with nothing but a kernel
		 * warning. Before adding a package to the running set, compare
		 * its declared <quota ram= caps=> against pkg_runtime's OWN
		 * remaining budget, which is exactly what its sandbox state
		 * report carries as the top-level <ram avail=/> and
		 * <caps avail=/> nodes (sandbox/types.h, Resource_info::generate).
		 *
		 * Why the report and not a configured copy of the grant: pkgd
		 * does not know what the top-level init granted pkg_runtime —
		 * duplicating that number in pkgd's config would be a second
		 * source of truth that silently rots. `avail` is a fact about
		 * the running system, it needs no grant arithmetic, and it also
		 * accounts for quota pkg_runtime has already donated to other
		 * children. That is the number the admission decision is about.
		 *
		 * The ROM subscription is opt-in because a DENIED ROM session is
		 * fatal under Genode's base Env policy (sleep_forever, not a
		 * throw — the same reason <binaries/> and <launcher_request/>
		 * are gated), so a scenario that does not relay the report must
		 * not carry the node.
		 */
		Genode::Constructible<Genode::Attached_rom_dataspace> _state_rom { };
		bool               _admission_armed { false };

		/* driver_runtime's sandbox state report. Same shape and same
		 * reason as _state_rom, but for the driver sandbox: a driver
		 * package's declared quota has to fit driver_runtime's own
		 * remaining budget, not pkg_runtime's. Opened only when the
		 * deployment writes BOTH <admission/> and <driver_admission/>,
		 * because the relay has to exist in the scenario — a denied ROM
		 * session is fatal under the base Env policy. */
		Genode::Constructible<Genode::Attached_rom_dataspace> _driver_state_rom { };
		bool               _driver_admission_armed { false };

		/*
		 * Admission safety margin. A refusal threshold of exactly
		 * `avail >= need` would admit a razor-thin package that init then
		 * has to clamp — recreating the very silent failure the gate
		 * exists to prevent, and doing so non-deterministically. The
		 * margin covers what a child costs on top of its declared quota:
		 * init's own bookkeeping for it (PD session, CNode slab, child
		 * object, the parsed <start> node) plus the report's own staleness
		 * window — the state report is periodic with delay_ms=2000, so a
		 * reading can be up to one period behind a just-started sibling.
		 * 8M / 64 caps is ~8% of a typical package quota and two orders
		 * of magnitude above the per-child overhead, while staying far
		 * below the 11% product headroom documented in docs/17 §6 — the
		 * gate must not eat the headroom the quota ledger budgets for.
		 */
		static constexpr Genode::size_t ADMISSION_MARGIN_RAM  = 8u * 1024 * 1024;
		static constexpr unsigned       ADMISSION_MARGIN_CAPS = 64;

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
		void _handle_tty_request();
		void _handle_probe_request();

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

		/* Runs on the Bootstrap thread, never on env.ep(). */
		void _bootstrap();
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

		/* ---- bake manifest seeding (optional, config-gated) ---- */
		void _init_bake();
		bool _seed_roots_from_bake();

		/* ---- binary manifest (optional, config-gated) ---- */
		void _init_binary_manifest();
		bool _binary_available(Package const &p);

		/* ---- resource admission control (optional, config-gated) ---- */
		void _init_admission();
		void _init_driver_admission();
		static bool _read_budget(Genode::Attached_rom_dataspace &state,
		                         Genode::size_t &avail_ram,
		                         unsigned &avail_caps);
		bool _admission_state(Genode::size_t &avail_ram, unsigned &avail_caps);
		bool _admits(Package const &p, Genode::size_t need_ram,
		             Genode::size_t &avail_ram,
		             unsigned need_caps, unsigned &avail_caps);
		static bool _quota_bytes(Genode::String<16> const &q, Genode::size_t &out);

		/* ---- launcher channel (optional, config-gated) ---- */
		void _init_launcher_channel();

		/* ---- runtime-config generation (deterministic) ---- */
		void _generate_runtime_config();
		void _emit_runtime_config(Genode::Expanding_reporter &reporter,
		                          bool drivers);

		/*
		 * Emission snapshot: the pure sponge_pkg_config library takes
		 * explicit view values, so each emission first snapshots
		 * _installed/_running into _config_views (the runtime_class
		 * token reproduces the pre-extraction is_driver() split).
		 * Member storage — never static, never on the small ep stack.
		 */
		void _refresh_config_views();
		Sponge::Pkg_config::Package_view _config_views[MAX_PACKAGES] { };
		unsigned                       _num_config_views  { 0 };

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
		void _report_insufficient(char const *op, Genode::String<128> const &pkg,
		                          Genode::size_t need_ram, Genode::size_t avail_ram,
		                          unsigned need_caps, unsigned avail_caps);
		void _report_conflict(char const *op, Genode::String<128> const &pkg,
		                      Genode::String<64> const &peer,
		                      Genode::String<16> const &group);
		Genode::String<64> _exclusive_peer(Package const &p,
		                                  Genode::String<64> const &self);

		/*
		 * Re-emit the cached result of the previous request on the
		 * channel the duplicate arrived on. Byte-identical by
		 * construction: it re-invokes the same _report_* helper that
		 * produced the original answer, with the same arguments.
		 */
		void _replay_last_result();
		void _cache_items(Genode::String<64> const *items, unsigned n);

		/* ---- small helpers ---- */
		Genode::Expanding_reporter &_result();
		static bool _contains(Genode::String<64> const *set, unsigned n,
		                      char const *name);
		static Genode::String<256> _cycle_message(Genode::String<64> const *visiting,
		                                           unsigned n, char const *repeat);
};
