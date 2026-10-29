/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkg_launch — pure launch model for Sponge OS package
 * sandboxes.
 *
 * This library models the launch decision sponge_pkgd makes before a
 * package's <start> node is admitted into a runtime config: the exact
 * gate ordering and the running-set rules that surround it. It was
 * extracted from sponge_pkgd's _do_launch and registry.cc's
 * running-set functions (Phase 16 P1.2,
 * docs/plans/phase16-runtime-overhaul.md) so the decision becomes
 * testable and reusable without touching pkgd's I/O. The
 * pre-extraction behavior is preserved exactly; all side effects
 * (report generation, _add_running mutation, dirty marks, runtime
 * config regeneration, logging) stay in sponge_pkgd's shell.
 *
 * PURITY CONTRACT (hard):
 *
 *   No Genode::Env, no sessions, no I/O, no hardware access, no
 *   exceptions, no mutable global state. The only Genode header used
 *   is the pure <util/string.h>. Every input is a caller-owned value
 *   or a non-owning view of caller-owned strings; every output lands
 *   in caller-owned memory (or is a pointer into the inputs).
 *
 * DECISION ORDER (the model — do not reorder):
 *
 *   not-installed -> already-running -> exclusive-conflict ->
 *   binary-missing -> insufficient-resources -> ok
 *
 *   1. not-installed: the requested name is not in the installed set.
 *      No implicit install — the caller must install first.
 *   2. already-running: the name is already in the running set
 *      (idempotent launch is a no-op).
 *   3. exclusive-conflict: a RUNNING peer holds the package's
 *      exclusive group (docs/12-package-format.md §4.6). The decision
 *      carries the peer and group the caller must report. Staged-but-
 *      stopped peers never conflict (running-only semantics).
 *   4. binary-missing: the package's effective binary (binary_prefix
 *      + <binary>) is not in the staged-binary manifest. Precedes
 *      the resource gate: when BOTH apply, binary-missing wins. A
 *      manifest that is not loaded is FAIL-OPEN (the check is inert
 *      and the gate passes) — the manual escape hatch (AGENTS.md
 *      §1.1) sponge_pkgd's <binaries/> opt-in already documents.
 *   5. insufficient-resources: the resource/admission verdict is an
 *      INPUT computed by the caller's admission code (sponge_pkgd's
 *      admission.cc) — this library never re-derives it. A quota the
 *      admission code could not parse refuses with zero figures; a
 *      refused budget echoes the four figures the admission code
 *      read.
 *   6. ok: the caller may transition the package installed -> running.
 *
 * RESOURCE GATE LAZINESS: decide_pre_resource() evaluates exactly the
 * gates 1-4 and answers ok when the resource gate is now decisive.
 * Callers whose admission input has side effects (sponge_pkgd's
 * _admits logs and reads a state ROM) call it first, compute the
 * verdict only on ok, then call decide() — the two-call sequence is
 * behavior-identical to decide() with a precomputed verdict whenever
 * an earlier gate fires (a property the self-test asserts), and keeps
 * the pre-extraction control flow's laziness (an earlier refusal never
 * consulted the budget).
 *
 * START-MODEL IDENTITY (INVARIANT — documented, tested; do not change
 * emission):
 *
 *   Genode's sandbox keeps a running child across a config
 *   regeneration iff the child's <start> node still MATCHES — and
 *   Sandbox::Start_model::matches() compares ONLY the `name` and
 *   `version` attributes (genode/repos/os/src/lib/sandbox/
 *   config_model.h:178). sponge_pkgd's emission (sponge_pkg_config)
 *   writes NO version attribute on <start> nodes, so the version is
 *   always empty on both sides and identity reduces to the NAME
 *   alone. That is why the running set here is keyed by name only and
 *   why name-stable regeneration preserves every running child (the
 *   finding behind docs/evidence/config-regen-survival.md). A caller
 *   that ever starts emitting <start> versions must re-key this
 *   model; the self-test pins both halves of the argument
 *   (Start_model::matches transcription + name-keyed set semantics).
 *
 * VIEW CONTRACT:
 *
 *   All strings are non-null, null-terminated pointers into
 *   caller-owned storage that outlives the call. Returned pointers
 *   (Decision::peer, Decision::group, exclusive_peer()) point INTO
 *   the inputs — the caller copies them before mutating the input
 *   arrays. Empty string means "none".
 */

#pragma once

#include <util/string.h>

namespace Sponge::Pkg_launch {

/*
 * Set bounds mirrored from sponge_pkgd's parse/state capacities
 * (pkgd.h Main::MAX_PACKAGES). Callers exceeding these bounds with
 * their counts get the extra entries ignored.
 */
enum { MAX_PACKAGES = 32 };

/*
 * The bounded launch outcomes, in the exact vocabulary sponge_pkgd's
 * result channel already emits (the shell maps each onto its existing
 * report helper; the mapping is deliberately NOT part of this
 * library).
 */
enum class Outcome {
	not_installed,
	already_running,
	exclusive_conflict,
	binary_missing,
	insufficient_resources,
	ok,
};

/*
 * One installed package as the launch model sees it. Field widths
 * follow sponge_pkgd's parsed Package (pkgd.h).
 */
struct Installed_view
{
	char const *name            { "" };
	char const *binary          { "" };  /* <binary>, defaults to name
	                                     * at parse time              */
	char const *exclusive_group { "" };  /* "" = no exclusive claim   */
	bool        has_autostart   { false }; /* read by sync_running    */
};

/*
 * The staged-binary manifest as the launch model sees it: the set of
 * EFFECTIVE binary paths (binary_prefix composed with <binary>, see
 * sponge_pkgd's _init_binary_manifest) actually staged in this image.
 *
 * 'loaded' false means the caller never opted a manifest in
 * (sponge_pkgd: no <binaries/> node) — the binary gate is then inert.
 */
struct Manifest_view
{
	bool         loaded        { false };
	char const  *binary_prefix { "" };
	char const  *staged[MAX_PACKAGES] { };  /* effective paths        */
	unsigned     num_staged    { 0 };
};

/*
 * Everything decide() needs, as pure values. The installed set and the
 * running set are caller-owned arrays; the running set is keyed by
 * NAME (see the START-MODEL IDENTITY invariant).
 */
struct Launch_input
{
	char const    *name { "" };           /* requested package name   */

	Installed_view installed[MAX_PACKAGES] { };
	unsigned       num_installed { 0 };

	char const    *running[MAX_PACKAGES] { };
	unsigned       num_running { 0 };

	Manifest_view  manifest { };
};

/*
 * The resource-gate input — computed by the CALLER's admission code
 * (sponge_pkgd's admission.cc: quota parsing, budget read,
 * ADMISSION_MARGIN_* comparison), never re-derived here. The launch
 * model only orders this gate behind the others and echoes its
 * figures into the decision.
 */
struct Resource_verdict
{
	enum class Kind {
		admitted,          /* fits, or admission is unarmed/fail-open */
		quota_unparsable,  /* <quota ram=...> did not parse           */
		refused,           /* does not fit the remaining budget       */
	};

	Kind         kind { Kind::admitted };

	/* Figures to echo on a refusal (zeros for quota_unparsable). */
	Genode::size_t need_ram   { 0 };
	Genode::size_t avail_ram  { 0 };
	unsigned       need_caps  { 0 };
	unsigned       avail_caps { 0 };

	static Resource_verdict admitted();
	static Resource_verdict quota_unparsable();
	static Resource_verdict refused(Genode::size_t need_ram,
	                                Genode::size_t avail_ram,
	                                unsigned need_caps,
	                                unsigned avail_caps);
};

/*
 * The decision. Outcome plus exactly the payload the existing report
 * helpers need:
 *
 *   exclusive_conflict      -> peer + group (for _report_conflict)
 *   insufficient_resources  -> the four echoed figures
 *                              (for _report_insufficient)
 */
struct Decision
{
	Outcome      outcome { Outcome::ok };

	/* exclusive_conflict payload; "" otherwise. Points into the
	 * Launch_input the decision was made over. */
	char const  *peer  { "" };
	char const  *group { "" };

	/* insufficient_resources payload; zeros otherwise. */
	Genode::size_t need_ram   { 0 };
	Genode::size_t avail_ram  { 0 };
	unsigned       need_caps  { 0 };
	unsigned       avail_caps { 0 };
};

/*
 * The complete launch decision: gates 1-5 in the documented order,
 * over the given inputs. Gate 4 (binary) precedes gate 5 (resource)
 * — when both refuse, the outcome is binary_missing and the resource
 * figures stay zero.
 */
Decision decide(Launch_input const &in, Resource_verdict const &resource);

/*
 * Gates 1-4 only. Returns outcome ok when every gate BEFORE the
 * resource gate passed — ok here means "the resource gate is now
 * decisive", NOT a launch approval (decide() has the last word).
 * decide(in, res) is exactly this prefix followed by the resource
 * gate; whenever this prefix does not return ok, decide(in, res)
 * returns the same decision for EVERY resource verdict.
 */
Decision decide_pre_resource(Launch_input const &in);

/*
 * Gate 4 in isolation: true when 'binary' is staged under
 * 'manifest.binary_prefix', or when the manifest is not loaded
 * (fail-open inertness).
 */
bool binary_available(Manifest_view const &manifest, char const *binary);

/*
 * The RUNNING peer that holds 'group', excluding 'self', or "" when
 * the group is free (docs/12-package-format.md §4.6). Scans the
 * running set only — the constraint is about simultaneous hardware
 * claims, not about what is staged: a staged-but-stopped peer never
 * conflicts. The first match in installed order wins. An empty
 * 'group' claims nothing and always answers "".
 */
char const *exclusive_peer(Installed_view const *installed,
                           unsigned num_installed,
                           char const *const *running, unsigned num_running,
                           char const *self, char const *group);

/*
 * Name-set membership over a caller-owned name array (name identity,
 * strcmp). This is the running-set's (and installed-set's) only
 * identity rule — see the START-MODEL IDENTITY invariant.
 */
bool name_in_set(char const *const *names, unsigned count, char const *name);

/*
 * The running-set sync rule (docs/12-package-format.md §9.2.1),
 * pure: reads the installed and running sets and writes ONLY the
 * caller's 'out' buffer. The derived set is:
 *
 *   - every running entry whose package is still installed
 *     (survivors keep their relative order), then
 *   - every installed <autostart/> package not already present
 *     (installed order),
 *
 * truncated at 'out_cap'. The output pointers alias the INPUTS'
 * strings (running[i] / installed[i].name) — the caller materializes
 * them before mutating those arrays.
 *
 * Invariants (tested): after a sync, running ⊆ installed and every
 * installed autostart package is running; a re-sync is idempotent.
 * The mutation of the caller's live state stays with the caller
 * (sponge_pkgd's registry.cc), as does the bounded add-once
 * (_add_running) rule.
 */
unsigned sync_running(Installed_view const *installed, unsigned num_installed,
                      char const *const *running, unsigned num_running,
                      char const **out, unsigned out_cap);

}  /* namespace Sponge::Pkg_launch */
