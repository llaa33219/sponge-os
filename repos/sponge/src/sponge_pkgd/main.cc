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

	Queued_request entry { };
	entry.op      = op;
	entry.pkg     = pkg;
	entry.seq     = seq;
	entry.sig     = Genode::String<160>(op, "|", pkg, "|", seq);
	entry.channel = _active_channel;

	_submit_request(entry);
}


/*
 * Strict global FIFO (P3.3): when the pipeline is idle the request
 * drains inline (the common path — the pre-P3.3 synchronous
 * behaviour); while a flush is in flight every request queues in
 * arrival order. A full queue answers a loud recipe-free busy error.
 */
void Sponge::Pkgd::Main::_submit_request(Queued_request const &req)
{
	if (_queue_count == 0 && !_pending_tail.active) {
		_dispatch_request(req);
		return;
	}

	if (_queue_count >= REQUEST_QUEUE_MAX) {
		_report_busy(req.op.string(), req.pkg);
		return;
	}

	_request_queue[(_queue_head + _queue_count) % REQUEST_QUEUE_MAX] = req;
	++_queue_count;

	Genode::log("sponge_pkgd: queued ", req.op, " ", req.pkg,
	            " (", _queue_count, " pending)");
}


/*
 * Process one request (the "dequeue" of the FIFO). The de-duplication
 * runs HERE so that the replay recipe of the previous request is
 * complete — its tail ran before the drain continued — and so a
 * duplicate that arrived while the pipeline was busy is replayed
 * instead of re-processed.
 */
void Sponge::Pkgd::Main::_dispatch_request(Queued_request const &req)
{
	_active_channel = req.channel;

	if (req.sig == _last_request_sig) {
		/*
		 * Duplicate: re-processing would re-run the side effects, but
		 * answering nothing strands the caller until its timeout (the
		 * launcher's double-click case). Replay the cached answer
		 * instead — see _last_request_sig above.
		 */
		Genode::log("sponge_pkgd: duplicate request ", req.sig,
		            " — replaying the cached result");
		_replay_last_result();
		return;
	}
	_last_request_sig = req.sig;

	Genode::String<32>  const &op  = req.op;
	Genode::String<128> const &pkg = req.pkg;

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


void Sponge::Pkgd::Main::_drain_queue()
{
	while (_queue_count > 0 && !_pending_tail.active) {
		Queued_request const req =
			_request_queue[_queue_head];
		_queue_head = (_queue_head + 1) % REQUEST_QUEUE_MAX;
		--_queue_count;
		_dispatch_request(req);
	}
}


/*
 * Flush-completion signal, submitted by the Bootstrap worker after
 * writing the job payload. Runs on env.ep(): the deferred tail first
 * (flush-before-broadcast), then the FIFO drain continues — no later
 * request can answer before the mutation whose flush just completed.
 */
void Sponge::Pkgd::Main::_on_flush_done()
{
	_finish_tail();
	_drain_queue();
}


/*
 * Loud overload refusal. RECIPE-FREE on purpose: the replay recipe and
 * _last_request_sig must keep describing the last PROCESSED request,
 * or a retried request would replay "busy" instead of being processed
 * fresh.
 */
void Sponge::Pkgd::Main::_report_busy(char const *op,
                                      Genode::String<128> const &pkg)
{
	Genode::warning("sponge_pkgd: request queue full — refusing ", op,
	                " ", pkg, " (busy, retry)");

	_result().generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "error");
		g.attribute("op",     op);
		g.attribute("pkg",    pkg);
		g.attribute("error",  "busy: request queue full — retry");
	});
}


/* ===================== runtime-config generation ===================== */

/*
 * Emit the sandbox configs dirtied by the current operation from
 * _installed (dirty-scoped emission, D16R.4 — see the dirty invariant
 * on _slot_dirty in pkgd.h).
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
 * One request may produce up to NUM_SLOTS deterministic configs: the
 * app-class one(s) for <kind>app</kind> packages (the single
 * pkg_runtime config in legacy mode; the simple/terminal/browser
 * class configs in grouped mode, per <runtime_class>, docs/12 §4.5)
 * and the driver_runtime one for <kind>driver</kind> packages. Which
 * of the active targets are published is decided by the per-slot dirty
 * marks the operation set from its resolved plan — an app-only
 * operation publishes only its app-class config(s), a driver-only
 * operation only the driver_runtime one, and a plan that mixes kinds
 * both. Bootstrap marks every active target. A target that is not
 * dirty is never published: its content is a function of its own
 * members alone (the generator's membership filter), so an unmarked
 * target is byte-unchanged and republishing it would be pure config
 * churn against the other runtime (the failure domain D16R.4 closes).
 */
bool Sponge::Pkgd::Main::_generate_runtime_config()
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

	_emit_failure = Genode::String<256>();
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
	Genode::uint64_t repro_malformed_from = 0;
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
				/* <repro_malformed_from: N> — negative control for the
				 * emission-validation gate (run/sponge-config-emission-
				 * invalid.run): from write N on, the RENDERED app config
				 * is corrupted at the reporter write site with a raw tag
				 * truncated mid-attribute. The knob corrupts the
				 * rendering only — it never disables or bypasses
				 * validation and never touches the pure emission library.
				 * Same value requirement as the knobs above ("repro_
				 * malformed_from: 2"; a bare token is dropped by the HID
				 * parser). The 20-byte compare is guarded because the
				 * loop bound above only guarantees 16 bytes in range. */
				if (i + 20 <= sz &&
				    Genode::strcmp(base + i, "repro_malformed_from", 20) == 0) {
					Genode::size_t j = i + 20;
					while (j < sz && (base[j] == ' ' || base[j] == ':'
					                  || base[j] == '"' || base[j] == '=')) ++j;
					while (j < sz && base[j] >= '0' && base[j] <= '9')
						repro_malformed_from = repro_malformed_from * 10
						                     + (Genode::uint64_t)(base[j++] - '0');
					if (repro_malformed_from)
						Genode::log("sponge_pkgd: repro_malformed_from=",
						            repro_malformed_from);
				}
			}
		}
	}
	/*
	 * Consume the per-slot dirty marks set by the operation from its
	 * resolved plan (or every ACTIVE slot at bootstrap — see the
	 * dirty invariant on _slot_dirty in pkgd.h). Clearing here means
	 * every later generation is driven purely by what the next
	 * operation dirties — unless this write fails below, which restores
	 * the marks (abort_write) so the unpublished delta stays queued.
	 */
	bool emit[NUM_SLOTS] { };
	for (unsigned s = 0; s < NUM_SLOTS; ++s) {
		emit[s] = _slot_dirty[s];
		_slot_dirty[s] = false;
	}

	/* test-only (repro harness): <repro_malformed_from: N> marks the
	 * current write; _render_runtime_config corrupts the rendered app
	 * config accordingly. The audit line is mandatory (the repro-family
	 * rule): without it a run cannot distinguish "the knob fired" from
	 * "the knob silently never matched". repro-only. */
	_malform_this_write = (repro_malformed_from != 0 &&
	                       _runtime_config_writes >= repro_malformed_from);
	if (_malform_this_write)
		Genode::log("sponge_pkgd: repro_malformed write=",
		            _runtime_config_writes,
		            " injecting truncated app config (negative control)");

	/*
	 * Failure seam shared by every rejection below: publish NOTHING for
	 * this write, restore the consumed dirty marks so convergence is
	 * not lost (the next generation re-attempts the whole delta), and
	 * let the caller answer with the bounded reason in _emit_failure.
	 * The loud structured log is emitted at the rejection site itself
	 * (render or validate), where the target and cause are known.
	 * The restore spans the FULL dirty set including driver — no
	 * partial publication and no lost delta on any failure.
	 */
	auto abort_write = [&] () -> bool {
		for (unsigned s = 0; s < NUM_SLOTS; ++s)
			_slot_dirty[s] |= emit[s];
		return false;
	};

	if (repro_empty_from && _runtime_config_writes >= repro_empty_from) {
		/*
		 * test-only (repro harness) control channel: from write N on,
		 * publish an EMPTY <config> — what the runtime's init would see
		 * if the transient no-writer window were visible to it. This
		 * is the ONLY validation bypass in the seam: an empty <config>
		 * is deliberately outside the validated subset (it would be
		 * MISSING_REQUIRED), and publishing it is the whole point of
		 * the control, so the bypass is scoped exactly to this knob
		 * (and to the simple/legacy-app target it always corrupts)
		 * and can never leak into a production write. Every other
		 * dirty slot of the write is still rendered and validated
		 * before anything is published.
		 */
		for (unsigned s = 0; s < NUM_SLOTS; ++s) {
			if (s == SLOT_APP || !emit[s])
				continue;
			if (!_render_runtime_config(s) || !_validate_runtime_config(s))
				return abort_write();
		}

		if (emit[SLOT_APP]) {
			Genode::log("sponge_pkgd: publishing EMPTY config (write ",
			            _runtime_config_writes, ") — control channel "
			            "(test-only validation bypass)");
			_slot_reporter(SLOT_APP).generate_xml(
				[&](Genode::Xml_generator &) { });
		}
		for (unsigned s = 0; s < NUM_SLOTS; ++s) {
			if (s == SLOT_APP || !emit[s])
				continue;
			_publish_runtime_config(s);
		}
		return true;
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

	/*
	 * P1.5 render -> validate -> publish. Render EVERY dirty slot to
	 * its scratch and validate ALL of them BEFORE publishing ANY:
	 * a write that mixes slots must not publish one sandbox's config
	 * once another sandbox's document has been disproved. Only after
	 * every document passed the gate are the validated bytes published
	 * through the slot's Expanding_reporter, in slot order (app/simple
	 * first, then terminal, browser, driver — the legacy pair keeps
	 * the pre-seam publication order app-then-driver).
	 */
	for (unsigned s = 0; s < NUM_SLOTS; ++s)
		if (emit[s] && !_render_runtime_config(s))
			return abort_write();

	for (unsigned s = 0; s < NUM_SLOTS; ++s)
		if (emit[s] && !_validate_runtime_config(s))
			return abort_write();

	for (unsigned s = 0; s < NUM_SLOTS; ++s)
		if (emit[s])
			_publish_runtime_config(s);
	return true;
}


/*
 * Snapshot the state _render_runtime_config has always read (_installed
 * in array order, the Phase 7 running gate, the kind split) as explicit
 * view values for the pure sponge_pkg_config library. The
 * runtime_class token is mode-dependent and IS the membership filter
 * (a pure string match against the target's token):
 *
 *   - legacy mode reproduces the pre-extraction is_driver() split
 *     byte for byte: "driver" versus "app" for EVERY non-driver,
 *     whatever its parsed class;
 *   - grouped mode carries the actual class token ("simple",
 *     "terminal", "browser") for apps and "driver" for drivers
 *     (kind outranks class, docs/12 §4.5).
 *
 * Member storage keeps the snapshot off the requesting thread's small
 * stack (the pre-extraction code's lesson for its emit scratch buffers).
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
		if (c.is_driver())
			v.runtime_class = "driver";
		else if (_grouped_runtimes)
			v.runtime_class = c.runtime_class.string();
		else {
			v.runtime_class = "app";
			/*
			 * The class is ignored for placement in legacy mode;
			 * log once that it requires grouped mode (the optional
			 * log-once of the P2.4 commit-1 contract).
			 */
			if (!_class_log_done &&
			    Genode::strcmp(c.runtime_class.string(), "simple") != 0) {
				_class_log_done = true;
				Genode::log("sponge_pkgd: <runtime_class>'",
				            c.runtime_class, "' of ", c.name,
				            " requires <grouped_runtimes/> — placing "
				            "it in the legacy app runtime");
			}
		}
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
 * Slot routing (the active target list — see the SLOT_* constants in
 * pkgd.h). Driver packages always land in SLOT_DRIVER: <kind> is the
 * security boundary and outranks <runtime_class> (docs/12 §4.5). In
 * grouped mode an app package routes by its parsed class token; in
 * legacy mode every app package lands in SLOT_APP regardless of the
 * class — the exact pre-P2.4 app/driver placement.
 */
unsigned Sponge::Pkgd::Main::_emission_slot(Package const &p) const
{
	if (p.is_driver())
		return SLOT_DRIVER;

	if (_grouped_runtimes) {
		if (Genode::strcmp(p.runtime_class.string(), "terminal") == 0)
			return SLOT_TERMINAL;
		if (Genode::strcmp(p.runtime_class.string(), "browser") == 0)
			return SLOT_BROWSER;
	}
	return SLOT_APP;
}


Sponge::Pkg_config::Target Sponge::Pkgd::Main::_slot_target(unsigned slot) const
{
	switch (slot) {
	case SLOT_APP:
		/* grouped: the simple class target; legacy: the single app
		 * runtime target (byte-identical pre-P2.4 emission). */
		return _grouped_runtimes ? Sponge::Pkg_config::simple_target()
		                         : Sponge::Pkg_config::app_target();
	case SLOT_TERMINAL: return Sponge::Pkg_config::terminal_target();
	case SLOT_BROWSER:  return Sponge::Pkg_config::browser_target();
	default:            return Sponge::Pkg_config::driver_target();
	}
}


char const *Sponge::Pkgd::Main::_slot_name(unsigned slot) const
{
	switch (slot) {
	case SLOT_APP:      return _grouped_runtimes ? "simple" : "app";
	case SLOT_TERMINAL: return "terminal";
	case SLOT_BROWSER:  return "browser";
	default:            return "driver";
	}
}


Genode::Expanding_reporter &Sponge::Pkgd::Main::_slot_reporter(unsigned slot)
{
	switch (slot) {
	case SLOT_APP:
		return _grouped_runtimes ? *_runtime_simple_reporter
		                         : *_runtime_reporter;
	case SLOT_TERMINAL: return *_runtime_terminal_reporter;
	case SLOT_BROWSER:  return *_runtime_browser_reporter;
	default:            return _driver_runtime_reporter;
	}
}


/*
 * Attribute-safe copy of a bounded reason. The rejection reason is
 * emitted as an XML attribute (<result error="..."/>), so it must not
 * carry '"', '<', '>', '&' or control bytes — a raw '<' would corrupt
 * the result document the probes parse (the validator's own detail
 * quotes the offending construct and does contain '<'). The text stays
 * readable: the four structural characters fold to their look-alikes
 * and control bytes to '?'.
 */
static void scrub_reason(char *dst, Genode::size_t cap, char const *src)
{
	if (cap == 0)
		return;

	Genode::size_t n = 0;
	for (char const *s = src; *s && n + 1 < cap; ++s) {
		char c = *s;
		switch (c) {
		case '<': c = '('; break;
		case '>': c = ')'; break;
		case '&': c = '+'; break;
		case '"': c = '\''; break;
		default:
			unsigned char const u = (unsigned char)c;
			if (u < 0x20 || u > 0x7e)
				c = '?';
			break;
		}
		dst[n++] = c;
	}
	dst[n] = '\0';
}


/*
 * P1.5 render step: render one slot's sandbox config through the pure
 * sponge_pkg_config library (Phase 16 P1.2 extraction) into the
 * slot's growable member scratch — the complete
 * "<config>...</config>" document the validate step reads and the
 * publish step republishes. `slot` selects both the library target
 * and the membership filter (via the runtime_class view token): the
 * legacy app target (or the simple class target in grouped mode), the
 * terminal/browser class targets, or the driver target. The library
 * shares every other part of the generator between targets — the
 * sorted <start> order, the fixed attribute order, the absence of
 * volatile fields — so every config carries the same determinism
 * contract (the reason unchanged children survive an unrelated
 * install).
 *
 * Buffer_error is covered, never swallowed: the scratch grows and the
 * render is retried (the Expanding_reporter growth idiom), and a
 * document that outlives RENDER_SCRATCH_CEILING or the heap fails
 * loudly as a render failure — nothing is ever silently dropped or
 * truncated.
 */
bool Sponge::Pkgd::Main::_render_runtime_config(unsigned slot)
{
	Render_scratch &scratch = _slot_render[slot];

	_refresh_config_views();

	Sponge::Pkg_config::Emit_options options { };
	options.binary_prefix = _binary_prefix.string();

	/*
	 * test-only (repro harness): the pad budget lands only in the
	 * simple/legacy-app target (SLOT_APP), exactly as the
	 * pre-extraction code had it (`if (!drivers && pad_bytes)`). Every
	 * other slot's emission always passes 0.
	 */
	options.pad_bytes = (slot == SLOT_APP) ? pad_bytes : 0;

	Sponge::Pkg_config::Target const target = _slot_target(slot);

	char const *const target_name = _slot_name(slot);

	for (;;) {
		if (!scratch.buf) {
			Genode::size_t const initial =
				(slot == SLOT_DRIVER) ? DRIVER_CONFIG_BUFFER
				                      : RUNTIME_CONFIG_BUFFER;

			void *p = _heap.try_alloc(initial).convert<void *>(
				[&] (Genode::Allocator::Allocation &a) {
					a.deallocate = false;
					return a.ptr;
				},
				[&] (Genode::Alloc_error) -> void * { return nullptr; });

			if (!p) {
				_emit_failure = Genode::String<256>(
					"runtime-config ", target_name,
					" render failed: out of memory for the ", initial,
					"-byte render scratch");
				Genode::error("sponge_pkgd: ", _emit_failure);
				return false;
			}
			scratch.buf = (char *)p;
			scratch.cap = initial;
		}

		Genode::size_t const used =
			Genode::Xml_generator::generate(
				Genode::Byte_range_ptr(scratch.buf, scratch.cap), "config",
				[&] (Genode::Xml_generator &g) {
					Sponge::Pkg_config::emit_target(
						g, target, _config_views, _num_config_views, options);
					/* test-only (repro harness): <repro_malformed_from: N>
					 * corrupts the RENDERED document (never the pure
					 * library) with a raw tag truncated mid-attribute —
					 * the "malformed raw <config> append" the emission
					 * validator's threat model names. Simple/legacy-app
					 * target only (SLOT_APP), the same scope as
					 * pad_bytes. The append lands after the renderer, so
					 * the document that reaches the gate is not
					 * well-formed XML and the write is rejected
					 * (run/sponge-config-emission-invalid.run). Inert
					 * unless the knob is armed. */
					if (slot == SLOT_APP && _malform_this_write)
						g.append("<start name=\"repro_malformed_tail");
				}).convert<Genode::size_t>(
					[] (Genode::size_t n) { return n; },
					[] (Genode::Buffer_error) { return (Genode::size_t)0; });

		if (used != 0) {
			/*
			 * Xml_generator::generate frames the document as
			 * "<config>..." + "</config>" + "\n" + "\0"; 'used' counts
			 * the framing. The publish step appends the BODY between a
			 * fresh <config> root, so verify the fixed framing (the
			 * root carries no attributes) before trusting its offsets.
			 */
			Genode::size_t const doc_len = used >= 2 ? used - 2 : 0;

			if (doc_len >= 8 + 9 &&
			    Genode::strcmp(scratch.buf, "<config>", 8) == 0 &&
			    Genode::strcmp(scratch.buf + doc_len - 9,
			                   "</config>", 9) == 0) {
				scratch.used = doc_len;
				return true;
			}

			/* Unreachable for emit_target output; fail loudly rather
			 * than republish bytes whose framing was not verified. */
			_emit_failure = Genode::String<256>(
				"runtime-config ", target_name,
				" render failed: unexpected document framing");
			Genode::error("sponge_pkgd: ", _emit_failure);
			return false;
		}

		/* Buffer_error: grow the scratch and retry, bounded. */
		if (scratch.cap >= RENDER_SCRATCH_CEILING) {
			_emit_failure = Genode::String<256>(
				"runtime-config ", target_name,
				" render failed: document exceeds the ",
				(unsigned long)RENDER_SCRATCH_CEILING,
				"-byte render ceiling (buffer exhausted)");
			Genode::error("sponge_pkgd: ", _emit_failure);
			return false;
		}

		Genode::size_t grown = scratch.cap * 2;
		if (grown > RENDER_SCRATCH_CEILING)
			grown = RENDER_SCRATCH_CEILING;

		void *p = _heap.try_alloc(grown).convert<void *>(
			[&] (Genode::Allocator::Allocation &a) {
				a.deallocate = false;
				return a.ptr;
			},
			[&] (Genode::Alloc_error) -> void * { return nullptr; });

		if (!p) {
			_emit_failure = Genode::String<256>(
				"runtime-config ", target_name,
				" render failed: out of memory growing the render scratch to ",
				grown, " bytes");
			Genode::error("sponge_pkgd: ", _emit_failure);
			return false;
		}

		_heap.free(scratch.buf, scratch.cap);
		scratch.buf = (char *)p;
		scratch.cap = grown;
	}
}


/*
 * P1.5 validate step: the committed pure validator answers "is this
 * document exactly a well-formed instance of the runtime-config subset
 * sponge_pkg_config renders?" — on failure the structured reason is
 * bounded into _emit_failure (attribute-safe) and logged loudly. The
 * document checked is exactly the bytes the publish step republishes.
 */
bool Sponge::Pkgd::Main::_validate_runtime_config(unsigned slot)
{
	Render_scratch const &scratch = _slot_render[slot];
	char const *const target_name = _slot_name(slot);

	Sponge::Runtime_config_validate::Result const r =
		Sponge::Runtime_config_validate::validate(
			scratch.buf, (unsigned)scratch.used, _validate_ws);

	if (r.ok)
		return true;

	char detail[Sponge::Runtime_config_validate::Result::DETAIL_CAPACITY] { };
	scrub_reason(detail, sizeof(detail), r.detail);

	/* 'char const *': Genode deletes print(Output&, char *) to guard
	 * against accidentally printing mutable buffers. */
	char const *const safe_detail = detail;

	_emit_failure = Genode::String<256>(
		"runtime-config ", target_name, " rejected by the publication "
		"gate: ", safe_detail);

	Genode::error("sponge_pkgd: ", _emit_failure,
	              " (validation code=", (unsigned)r.code, ") — nothing "
	              "published for this write");
	return false;
}


/*
 * P1.5 publish step: publish the VALIDATED bytes through the existing
 * Expanding_reporter. generate_xml opens the <config> root itself, so
 * append only the validated document BODY (everything the renderer
 * wrote between the root open tag and </config>) — a nested <config>
 * root would not be the document that passed the gate. The republished
 * document is byte-identical to the validated one (the root carries no
 * attributes, so the framing is fixed and was verified at render time),
 * and the reporter's growth semantics are untouched: the append takes
 * the same Buffer_error -> grow loop every other generate_xml write
 * takes (the repro-pad knobs still force it on purpose).
 */
void Sponge::Pkgd::Main::_publish_runtime_config(unsigned slot)
{
	Render_scratch const &scratch = _slot_render[slot];

	/* "<config>" (8) ... "</config>" (9) — see _render_runtime_config. */
	char const *const body        = scratch.buf + 8;
	Genode::size_t const body_len = scratch.used - 8 - 9;

	_slot_reporter(slot).generate_xml([&] (Genode::Xml_generator &g) {
		g.append(body, body_len);
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


/*
 * Grouped-runtime opt-in (P2.4, explicit <grouped_runtimes/> marker in
 * this component's <config> — the same config-marker pattern as
 * <admission/>, <binaries/>, <launcher_request/>). Without it pkgd stays
 * in legacy mode byte-for-byte: one app target published on the
 * "runtime" report plus the "driver_runtime" one. With it the app
 * target splits into the simple/terminal/browser class targets,
 * published as "runtime_simple"/"runtime_terminal"/"runtime_browser"
 * while the driver target is unchanged.
 *
 * The runtime-config report channels are CONSTRUCTED here — exactly
 * the active set of the mode — so no mode opens a report channel it
 * never publishes (explicit initialization, consistent with the
 * Constructible opt-in channels above): grouped mode never creates
 * "runtime", legacy mode never creates the three class channels.
 */
void Sponge::Pkgd::Main::_init_grouped_runtimes()
{
	using Buffer = Genode::Expanding_reporter::Initial_buffer_size;

	try {
		_config_rom.node().with_optional_sub_node("grouped_runtimes",
			[&] (Genode::Node const &) {
				_grouped_runtimes = true;
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — grouped "
		                "runtimes disabled (legacy mode)");
	}

	if (_grouped_runtimes) {
		_runtime_simple_reporter.construct(
			_env, "config", "runtime_simple",
			Buffer { .value = RUNTIME_CONFIG_BUFFER });
		_runtime_terminal_reporter.construct(
			_env, "config", "runtime_terminal",
			Buffer { .value = RUNTIME_CONFIG_BUFFER });
		_runtime_browser_reporter.construct(
			_env, "config", "runtime_browser",
			Buffer { .value = RUNTIME_CONFIG_BUFFER });
		Genode::log("sponge_pkgd: grouped runtimes enabled — app "
		            "packages route by <runtime_class> into "
		            "runtime_simple/runtime_terminal/runtime_browser "
		            "(driver unchanged)");
	} else {
		_runtime_reporter.construct(
			_env, "config", "runtime",
			Buffer { .value = RUNTIME_CONFIG_BUFFER });
	}
}


/*
 * Launcher channel opt-in (Phase 7 todo 10). The launcher_request ROM
 * is opened ONLY when <config> carries <launcher_request/>. Genode
 * 26.05's session-denial path calls sleep_forever() (not a throw), so
 * an eager open in an unrouted scenario would hang the daemon. The
 * config gate is identical in spirit to _init_store's <vfs> gate.
 *
 * Strict two-phase sequence (the cross-channel arming window is closed
 * on purpose):
 *
 *   Phase 1 (_init_launcher_channel) - construct every enabled
 *   optional ROM and drain it with the matching _handle_*_request(),
 *   with NO sigh armed anywhere. A request posted before the ROM
 *   session existed is never signalled on it (the session's signal
 *   context did not exist when the content was written), so without
 *   the drain it sits unconsumed until a LATER write changes the
 *   module and the handler then processes only the newer content - the
 *   first request is lost. Draining with no handler armed anywhere
 *   means no launcher/tty/probe handler can run while any other
 *   optional channel is draining: every drain is single-threaded on
 *   the bootstrap thread.
 *
 *   Phase 2 (_arm_launcher_channel) - only after ALL drains are done
 *   (the optional ones above AND the primary channel's _handle_request
 *   drain), arm each constructed channel's sigh and perform the
 *   existing update/log. Splitting the arm step out of this function
 *   is what makes the invariant hold for the primary channel too: an
 *   optional handler armed here would otherwise run on env.ep() while
 *   the bootstrap thread is still draining the primary channel -
 *   two threads inside _handle_request_impl against the same
 *   unsynchronised _roots/_installed/_last_request_sig. Anything that
 *   arrives after the arming signals the armed handler as before, and
 *   the request de-duplication (_last_request_sig) turns a re-delivery
 *   of the drained content into a cached replay rather than a repeated
 *   side effect.
 */
void Sponge::Pkgd::Main::_init_launcher_channel()
{
	_config_rom.update();
	if (!_config_rom.valid())
		return;

	try {
		/* Phase 1: construct + drain. No sigh is armed yet. */
		_config_rom.node().with_optional_sub_node("launcher_request",
			[&](Genode::Node const &) {
				_launcher_request_rom.construct(_env, "launcher_request");
				_handle_launcher_request();
			});
		_config_rom.node().with_optional_sub_node("tty_request",
			[&](Genode::Node const &) {
				_tty_request_rom.construct(_env, "tty_request");
				_handle_tty_request();
			});
		_config_rom.node().with_optional_sub_node("probe_request",
			[&](Genode::Node const &) {
				_probe_request_rom.construct(_env, "probe_request");
				_handle_probe_request();
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — launcher channel disabled");
	}
}


/*
 * Phase 2 of the channel handoff (see _init_launcher_channel): arm +
 * update + log every channel phase 1 constructed (a ROM is constructed
 * exactly when its config gate was present). Called only once ALL
 * drains - optional AND primary - have completed, so from here on the
 * armed handlers may run on env.ep() and the bootstrap thread touches
 * nothing the ep can reach.
 */
void Sponge::Pkgd::Main::_arm_launcher_channel()
{
	if (_launcher_request_rom.constructed()) {
		_launcher_request_rom->sigh(_launcher_request_handler);
		_launcher_request_rom->update();
		Genode::log("sponge_pkgd: launcher_request channel enabled");
	}
	if (_tty_request_rom.constructed()) {
		_tty_request_rom->sigh(_tty_request_handler);
		_tty_request_rom->update();
		Genode::log("sponge_pkgd: tty_request channel enabled");
	}
	if (_probe_request_rom.constructed()) {
		_probe_request_rom->sigh(_probe_request_handler);
		_probe_request_rom->update();
		Genode::log("sponge_pkgd: probe_request channel enabled");
	}
}


/* ===================== component wiring ===================== */


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

	/*
	 * Grouped-runtime opt-in (P2.4) FIRST: it decides the active
	 * target list — which runtime-config report channels this daemon
	 * constructs and how every dirty mark below routes — and it arms
	 * nothing itself, so nothing else can observe a half-decided mode.
	 */
	_init_grouped_runtimes();

	_load_index();

	/* P3.3: the metadata cache is the dispatch path's only metadata
	 * source — copy it here, before any handler is armed. */
	_load_metadata_cache();

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
	 * Resource admission control (opt-in via <admission/> and, in
	 * grouped mode, <simple_admission/>/<terminal_admission/>/
	 * <browser_admission/>). Armed before the first request so a launch
	 * never reaches _admits unarmed while a report is merely late — and
	 * never arms itself in a scenario that does not relay the state
	 * report (a denied ROM session is fatal under the base Env policy).
	 */
	_init_admission();
	_init_class_admission();
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

	/* Emit the initial runtime configs before anything can request
	 * them. On a restored boot these already contain the previously-
	 * installed components, so the runtimes start them without any
	 * user action. (Published here so init — which constructs children
	 * in config order — sees them before it constructs the runtimes.)
	 * Bootstrap dirties EVERY ACTIVE target (the dirty invariant in
	 * pkgd.h): every sandbox needs its initial config whether or not
	 * the restored set happens to span its members. */
	for (unsigned s = 0; s < NUM_SLOTS; ++s)
		if (_slot_active(s))
			_slot_dirty[s] = true;
	if (!_generate_runtime_config()) {
		/*
		 * Bootstrap has no request context: fail loudly and publish
		 * nothing (the seam already restored the dirty marks), but
		 * stay alive so the next operation re-attempts the whole delta
		 * and converges. No <result> is fabricated here — there is no
		 * caller to answer.
		 */
		Genode::error("sponge_pkgd: bootstrap runtime-config publication "
		              "failed (", _emit_failure, ") — nothing published; "
		              "dirty marks kept for convergence");
	}

	/* Publish the initial installed-set broadcast so watchers (e.g.
	 * sponge-de's launcher) read a well-formed <installed .../> right
	 * away — on a restored boot this already lists the survivors. */
	_generate_installed_report();

	/*
	 * Hand over to env.ep().
	 *
	 * Order matters and is the whole point of the split. ALL FOUR
	 * request channels are drained HERE, on the bootstrap thread,
	 * before ANY sigh is armed:
	 *
	 *   - _init_launcher_channel drains the three optional channels
	 *     (launcher_request, tty_request, probe_request) in its phase
	 *     1, with NO sigh armed anywhere. The drain exists because a
	 *     request posted before the ROM session existed is never
	 *     signalled on it (the session's signal context did not exist
	 *     when the content was written) and would otherwise be lost
	 *     while a LATER write's signal made the handler process only
	 *     the newer content;
	 *   - the primary channel is drained next (its sigh is still
	 *     unarmed at that point);
	 *   - only THEN _arm_launcher_channel arms the optional sighs and
	 *     the primary sigh is armed - the handoff point from which
	 *     env.ep() owns request handling and the bootstrap thread
	 *     touches nothing the ep can reach. Arming the optional
	 *     handlers before the primary drain (the pre-correction order)
	 *     left a window where an optional handler ran on env.ep()
	 *     while this thread was still inside the primary drain -
	 *     two threads in _handle_request_impl against the same
	 *     unsynchronised _roots/_installed/_last_request_sig. The
	 *     de-duplication replay makes a REPLAY harmless, which is
	 *     exactly why a concurrent first execution must not be left
	 *     to chance.
	 *
	 * The optional channels stay opt-in: _init_launcher_channel
	 * constructs a ROM ONLY when this component's <config> carries the
	 * matching element (<launcher_request/>, <tty_request/>,
	 * <probe_request/>). Genode 26.05's session routing calls
	 * sleep_forever() on denial (not a catchable throw), so a ROM
	 * session MUST NOT be opened in scenarios that do not route it -
	 * the same opt-in pattern _init_store uses for <vfs>.
	 */
	_init_launcher_channel();
	_handle_request();
	_arm_launcher_channel();
	_request_rom.sigh(_request_handler);
	_request_rom.update();

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
		 * happened at all. The flag switches the store I/O waits to
		 * the entrypoint-sound primitive (_store_io_wait). */
		_single_threaded = true;
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
