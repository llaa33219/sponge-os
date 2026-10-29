/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test for sponge_pkg_launch (Phase 16 P1.2).
 *
 * NOT part of the Genode build (deliberately absent from
 * lib/mk/sponge_pkg_launch.mk), so it lives outside the shared build
 * run. It proves the extraction claim two ways:
 *
 *   1. Equivalence: the library's decide() is compared against
 *      `ref_decide` below — a verbatim transcription of the
 *      pre-extraction launch decision (sponge_pkgd ops.cc:_do_launch
 *      gate sequence plus registry.cc's _is_running/_exclusive_peer/
 *      _binary_available, P1.3-split tree), with the resource gate
 *      arriving as caller-computed input exactly like the
 *      _quota_bytes/_admits pair did. Every fixture runs under every
 *      Resource_verdict kind.
 *
 *   2. Targeted assertions: every outcome, the exact branch ordering
 *      (an earlier gate beats every later one — in particular
 *      binary-missing beats a resource refusal when both apply),
 *      exclusive-peer running-only semantics (a staged-but-stopped
 *      peer never conflicts), manifest inertness (not-loaded is
 *      fail-open), the running-set sync invariants (running ⊆
 *      installed, installed autostart ⊆ running, idempotent,
 *      survivors-then-additions order — byte-equal to the reference
 *      sync), and the Start_model name-identity invariant (emitted
 *      <start> nodes have no version, so name-stable regeneration
 *      preserves children).
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
 *       repos/sponge/lib/src/sponge_pkg_launch/self_test.cc \
 *       repos/sponge/lib/src/sponge_pkg_launch/pkg_launch.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_pkg_launch_self_test \
 *   && /tmp/sponge_pkg_launch_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every check
 * matched. "self-test: FAIL" prints every divergence.
 */

#include <sponge/pkg_launch.h>

#include <stdio.h>
#include <string.h>


/* ===================== reference implementation ===================== */
/*
 * Verbatim transcription of the pre-extraction decision, in the
 * original control-flow shape. The only adaptation is the one the
 * extraction itself defines: the resource gate (_quota_bytes +
 * _admits) arrives as pure input — `quota_parsable` mirrors
 * _quota_bytes's answer and `admitted` _admits's — because that math
 * stays in sponge_pkgd's admission.cc and is NOT re-extracted here.
 * Field types stay Genode::String<NN> exactly like the parse-side
 * Package type, so comparison semantics are identical to the daemon's.
 */

namespace {

using Sponge::Pkg_launch::MAX_PACKAGES;

struct Ref_package
{
	Genode::String<64> name;
	Genode::String<64> binary;
	Genode::String<16> exclusive_group;
	bool               has_autostart { false };
};

enum Ref_outcome {
	REF_OK = 0,
	REF_NOT_INSTALLED,
	REF_ALREADY_RUNNING,
	REF_EXCLUSIVE_CONFLICT,
	REF_BINARY_MISSING,
	REF_INSUFFICIENT_RESOURCES,
};

struct Ref_result
{
	Ref_outcome      outcome { REF_OK };
	Genode::String<64> peer;
	Genode::String<16> group;
	Genode::size_t     need_ram  { 0 };
	Genode::size_t     avail_ram { 0 };
	unsigned           need_caps  { 0 };
	unsigned           avail_caps { 0 };
};

/* Pre-extraction Main::_installed_contains (registry.cc, verbatim). */
bool ref_installed_contains(Ref_package const *installed,
                            unsigned num_installed, char const *name)
{
	for (unsigned i = 0; i < num_installed; ++i)
		if (Genode::strcmp(installed[i].name.string(), name) == 0)
			return true;
	return false;
}

/* Pre-extraction Main::_is_running (registry.cc, verbatim). */
bool ref_is_running(Genode::String<64> const *running,
                    unsigned num_running, char const *name)
{
	for (unsigned i = 0; i < num_running; ++i)
		if (Genode::strcmp(running[i].string(), name) == 0)
			return true;
	return false;
}

/* Pre-extraction Main::_add_running (registry.cc, verbatim). */
bool ref_add_running(Genode::String<64> *running, unsigned &num_running,
                     char const *name)
{
	if (ref_is_running(running, num_running, name))
		return false;
	if (num_running < MAX_PACKAGES) {
		running[num_running++] = Genode::String<64>(name);
		return true;
	}
	return false;
}

/*
 * Pre-extraction Main::_exclusive_peer (registry.cc, verbatim): scans
 * the RUNNING set only — a staged-but-stopped peer never conflicts.
 */
Genode::String<64> ref_exclusive_peer(
	Ref_package const &p, Genode::String<64> const &self,
	Ref_package const *installed, unsigned num_installed,
	Genode::String<64> const *running, unsigned num_running)
{
	if (Genode::strcmp(p.exclusive_group.string(), "") == 0)
		return Genode::String<64>();

	for (unsigned i = 0; i < num_installed; ++i) {
		Ref_package const &o = installed[i];
		if (Genode::strcmp(o.exclusive_group.string(), "") == 0 ||
		    !ref_is_running(running, num_running, o.name.string()))
			continue;
		if (Genode::strcmp(o.exclusive_group.string(),
		                   p.exclusive_group.string()) != 0)
			continue;
		if (Genode::strcmp(o.name.string(), self.string()) == 0)
			continue;
		return o.name;
	}
	return Genode::String<64>();
}

/*
 * Pre-extraction Main::_binary_available (ops.cc, verbatim): inert
 * (always true) unless the manifest was loaded.
 */
bool ref_binary_available(bool manifest_loaded,
                          Genode::String<32> binary_prefix,
                          Genode::String<96> const *staged,
                          unsigned num_staged,
                          Ref_package const &p)
{
	if (!manifest_loaded)
		return true;

	Genode::String<96> const path(binary_prefix, p.binary);
	for (unsigned i = 0; i < num_staged; ++i)
		if (Genode::strcmp(staged[i].string(), path.string()) == 0)
			return true;
	return false;
}

/*
 * Pre-extraction Main::_do_launch gate sequence (ops.cc, verbatim
 * order): not-installed -> already-running -> exclusive-conflict ->
 * binary-missing -> insufficient-resources -> ok. The two resource
 * branches are the _quota_bytes and _admits call sites with the
 * verdict supplied as input.
 */
Ref_result ref_decide(
	Ref_package const *installed, unsigned num_installed,
	Genode::String<64> const *running, unsigned num_running,
	bool manifest_loaded, Genode::String<32> binary_prefix,
	Genode::String<96> const *staged, unsigned num_staged,
	char const *name,
	bool quota_parsable, bool admitted,
	Genode::size_t need_ram, Genode::size_t avail_ram,
	unsigned need_caps, unsigned avail_caps)
{
	Ref_result r { };

	if (!ref_installed_contains(installed, num_installed, name)) {
		r.outcome = REF_NOT_INSTALLED;
		return r;
	}

	if (ref_is_running(running, num_running, name)) {
		r.outcome = REF_ALREADY_RUNNING;
		return r;
	}

	/* Exclusive-group gate: first installed match decides, then
	 * break (verbatim loop shape). */
	for (unsigned i = 0; i < num_installed; ++i) {
		if (Genode::strcmp(installed[i].name.string(), name) != 0)
			continue;
		Genode::String<64> const peer = ref_exclusive_peer(
			installed[i], Genode::String<64>(name),
			installed, num_installed, running, num_running);
		if (peer != Genode::String<64>()) {
			r.outcome = REF_EXCLUSIVE_CONFLICT;
			r.peer    = peer;
			r.group   = installed[i].exclusive_group;
			return r;
		}
		break;
	}

	/* First-match lookup (verbatim). */
	Ref_package const *found { nullptr };
	for (unsigned i = 0; i < num_installed; ++i)
		if (Genode::strcmp(installed[i].name.string(), name) == 0) {
			found = &installed[i];
			break;
		}

	{
		if (found && !ref_binary_available(manifest_loaded, binary_prefix,
		                                   staged, num_staged, *found)) {
			r.outcome = REF_BINARY_MISSING;
			return r;
		}

		if (found) {
			if (!quota_parsable) {
				r.outcome = REF_INSUFFICIENT_RESOURCES;
				return r;
			}
			if (!admitted) {
				r.outcome   = REF_INSUFFICIENT_RESOURCES;
				r.need_ram  = need_ram;
				r.avail_ram = avail_ram;
				r.need_caps = need_caps;
				r.avail_caps = avail_caps;
				return r;
			}
		}
	}

	r.outcome = REF_OK;
	return r;
}

/*
 * Pre-extraction Main::_sync_running_state (registry.cc, verbatim):
 * compact the survivors, then add every installed autostart package
 * via _add_running. Mutates the running array in place, exactly like
 * the daemon.
 */
unsigned ref_sync_running(Ref_package const *installed, unsigned num_installed,
                          Genode::String<64> *running, unsigned &num_running)
{
	unsigned w { 0 };
	for (unsigned i = 0; i < num_running; ++i) {
		if (ref_installed_contains(installed, num_installed,
		                           running[i].string()))
			running[w++] = running[i];
	}
	num_running = w;

	for (unsigned i = 0; i < num_installed; ++i)
		if (installed[i].has_autostart)
			ref_add_running(running, num_running,
			                installed[i].name.string());
	return num_running;
}

/*
 * Sandbox::Start_model::matches() — transcribed from
 * genode/repos/os/src/lib/sandbox/config_model.h:178-182. The sandbox
 * keeps a running child across a config regeneration iff BOTH the
 * name and the version match. sponge_pkgd's emission writes no
 * version attribute (sponge_pkg_config's <start> nodes carry `name`
 * only), so the version is "" on both sides and identity reduces to
 * the name alone — the documented sponge_pkg_launch invariant.
 */
bool ref_start_model_matches(char const *name_a, char const *version_a,
                             char const *name_b, char const *version_b)
{
	return strcmp(name_a, name_b) == 0
	    && strcmp(version_a, version_b) == 0;
}


/* ===================== fixtures ===================== */

struct Fx_pkg
{
	char const *name;
	char const *binary;
	char const *group;
	bool        autostart;
};

struct Fixture
{
	char const *name;

	Fx_pkg      installed[4];
	unsigned    num_installed;

	char const *running[4];
	unsigned    num_running;

	bool        manifest_loaded;
	char const *binary_prefix;
	char const *staged[4];
	unsigned    num_staged;

	char const *launch;

	/* Expected Ref_outcome per Resource_verdict kind
	 * (0 = admitted, 1 = quota_unparsable, 2 = refused). */
	int expect[3];
};

/* The refused-verdict figures every fixture runs under. */
Genode::size_t const FX_NEED_RAM  = 56u * 1024 * 1024;
Genode::size_t const FX_AVAIL_RAM = 8u * 1024 * 1024;
unsigned       const FX_NEED_CAPS = 2000;
unsigned       const FX_AVAIL_CAPS = 1034;

Fixture const FIXTURES[] = {

	/* Every outcome, plus the branch-ordering pairs. */
	{ "plain-ok",
	  { { "hello", "hello", "", false } }, 1,
	  { }, 0,
	  false, "", { }, 0,
	  "hello",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },

	{ "not-installed-beats-everything",
	  { { "hello", "hello", "", false } }, 1,
	  { "ghost" }, 1,          /* running-but-not-installed: NOT
	                            * already-running — gate order */
	  true, "bin/", { "bin/nothing" }, 1,
	  "ghost",
	  { REF_NOT_INSTALLED, REF_NOT_INSTALLED, REF_NOT_INSTALLED } },

	{ "already-running-beats-binary-and-resource",
	  { { "hello", "hello", "", false } }, 1,
	  { "hello" }, 1,
	  true, "", { }, 0,        /* binary ALSO missing */
	  "hello",
	  { REF_ALREADY_RUNNING, REF_ALREADY_RUNNING, REF_ALREADY_RUNNING } },

	{ "exclusive-beats-binary-and-resource",
	  { { "fb_a", "fb_a", "fb", false },
	    { "fb_b", "fb_b", "fb", false } }, 2,
	  { "fb_a" }, 1,
	  true, "", { }, 0,        /* binary ALSO missing */
	  "fb_b",
	  { REF_EXCLUSIVE_CONFLICT, REF_EXCLUSIVE_CONFLICT,
	    REF_EXCLUSIVE_CONFLICT } },

	{ "stopped-peer-no-conflict",
	  { { "fb_a", "fb_a", "fb", false },
	    { "fb_b", "fb_b", "fb", false } }, 2,
	  { }, 0,                  /* peer staged but NOT running */
	  false, "", { }, 0,
	  "fb_b",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },

	{ "other-group-peer-no-conflict",
	  { { "fb_a", "fb_a", "fb", false },
	    { "gfx",  "gfx",  "gpu", false } }, 2,
	  { "fb_a" }, 1,
	  false, "", { }, 0,
	  "gfx",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },

	{ "binary-missing-beats-resource",
	  { { "hello", "hello", "", false } }, 1,
	  { }, 0,
	  true, "", { }, 0,
	  "hello",
	  { REF_BINARY_MISSING, REF_BINARY_MISSING, REF_BINARY_MISSING } },

	{ "manifest-inert-fail-open",
	  { { "hello", "hello", "", false } }, 1,
	  { }, 0,
	  false, "", { }, 0,       /* SAME data as binary-missing, but the
	                            * manifest was never loaded */
	  "hello",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },

	{ "prefix-composition-hit",
	  { { "term", "terminal", "", false } }, 1,
	  { }, 0,
	  true, "bin/", { "bin/terminal" }, 1,
	  "term",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },

	{ "prefix-composition-miss",
	  { { "term", "terminal", "", false } }, 1,
	  { }, 0,
	  true, "bin/", { "terminal" }, 1,   /* bare name does NOT match */
	  "term",
	  { REF_BINARY_MISSING, REF_BINARY_MISSING, REF_BINARY_MISSING } },

	{ "self-claim-alone-no-conflict",
	  { { "fb_a", "fb_a", "fb", false } }, 1,
	  { }, 0,
	  false, "", { }, 0,
	  "fb_a",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },

	{ "first-match-wins",
	  { { "dup", "one", "", false },     /* first match: group "", bin one */
	    { "dup", "two", "g", false },    /* second: would conflict        */
	    { "peer", "peer", "g", false } }, 3,
	  { "peer" }, 1,
	  true, "", { "one" }, 1,
	  "dup",
	  { REF_OK, REF_INSUFFICIENT_RESOURCES, REF_INSUFFICIENT_RESOURCES } },
};

unsigned const NUM_FIXTURES = sizeof(FIXTURES) / sizeof(FIXTURES[0]);

/* Resource-verdict kinds, in Resource_verdict::Kind order. */
int const VERDICT_KINDS[3] = { 0, 1, 2 };


/* ===================== fixture conversion ===================== */

void to_ref(Fixture const &fx,
            Ref_package (&installed)[MAX_PACKAGES], unsigned &num_installed,
            Genode::String<64> (&running)[MAX_PACKAGES], unsigned &num_running,
            Genode::String<96> (&staged)[MAX_PACKAGES], unsigned &num_staged)
{
	num_installed = 0;
	for (unsigned i = 0; i < fx.num_installed; ++i) {
		Ref_package &p = installed[num_installed++];
		p.name            = fx.installed[i].name;
		p.binary          = fx.installed[i].binary;
		p.exclusive_group = fx.installed[i].group;
		p.has_autostart   = fx.installed[i].autostart;
	}
	num_running = 0;
	for (unsigned i = 0; i < fx.num_running; ++i)
		running[num_running++] = Genode::String<64>(fx.running[i]);
	num_staged = 0;
	for (unsigned i = 0; i < fx.num_staged; ++i)
		staged[num_staged++] = Genode::String<96>(fx.staged[i]);
}

void to_input(Fixture const &fx, Sponge::Pkg_launch::Launch_input &in)
{
	using namespace Sponge::Pkg_launch;

	in = Launch_input { };
	in.name = fx.launch;
	for (unsigned i = 0; i < fx.num_installed; ++i) {
		in.installed[i].name            = fx.installed[i].name;
		in.installed[i].binary          = fx.installed[i].binary;
		in.installed[i].exclusive_group = fx.installed[i].group;
		in.installed[i].has_autostart   = fx.installed[i].autostart;
	}
	in.num_installed = fx.num_installed;
	for (unsigned i = 0; i < fx.num_running; ++i)
		in.running[i] = fx.running[i];
	in.num_running = fx.num_running;
	in.manifest.loaded        = fx.manifest_loaded;
	in.manifest.binary_prefix = fx.binary_prefix;
	for (unsigned i = 0; i < fx.num_staged; ++i)
		in.manifest.staged[i] = fx.staged[i];
	in.manifest.num_staged = fx.num_staged;
}

Sponge::Pkg_launch::Resource_verdict to_verdict(int kind)
{
	using Sponge::Pkg_launch::Resource_verdict;
	switch (kind) {
	case 0:  return Resource_verdict::admitted();
	case 1:  return Resource_verdict::quota_unparsable();
	default: return Resource_verdict::refused(FX_NEED_RAM, FX_AVAIL_RAM,
	                                          FX_NEED_CAPS, FX_AVAIL_CAPS);
	}
}

/* The (quota_parsable, admitted) pair the pre-extraction call sites
 * derived from _quota_bytes/_admits, for the same verdict kind. */
void verdict_to_flags(int kind, bool &quota_parsable, bool &admitted)
{
	quota_parsable = (kind != 1);
	admitted       = (kind == 0);
}


/* ===================== comparison harness ===================== */

unsigned failures = 0;

void expect_true(char const *what, bool cond)
{
	if (cond)
		return;
	++failures;
	printf("FAIL: %s\n", what);
}

void expect_str(char const *what, char const *a, char const *b)
{
	if (strcmp(a, b) == 0)
		return;
	++failures;
	printf("FAIL: %s — expected \"%s\", got \"%s\"\n", what, a, b);
}

void expect_u64(char const *what, unsigned long long a, unsigned long long b)
{
	if (a == b)
		return;
	++failures;
	printf("FAIL: %s — expected %llu, got %llu\n", what, a, b);
}

char const *outcome_name(int ref_outcome)
{
	switch (ref_outcome) {
	case REF_OK:                     return "ok";
	case REF_NOT_INSTALLED:          return "not-installed";
	case REF_ALREADY_RUNNING:        return "already-running";
	case REF_EXCLUSIVE_CONFLICT:     return "exclusive-conflict";
	case REF_BINARY_MISSING:         return "binary-missing";
	case REF_INSUFFICIENT_RESOURCES: return "insufficient-resources";
	}
	return "?";
}

int lib_to_ref(Sponge::Pkg_launch::Outcome o)
{
	using Sponge::Pkg_launch::Outcome;
	switch (o) {
	case Outcome::ok:                     return REF_OK;
	case Outcome::not_installed:          return REF_NOT_INSTALLED;
	case Outcome::already_running:        return REF_ALREADY_RUNNING;
	case Outcome::exclusive_conflict:     return REF_EXCLUSIVE_CONFLICT;
	case Outcome::binary_missing:         return REF_BINARY_MISSING;
	case Outcome::insufficient_resources: return REF_INSUFFICIENT_RESOURCES;
	}
	return -1;
}

void expect_same(char const *what, Ref_result const &ref,
                 Sponge::Pkg_launch::Decision const &lib)
{
	expect_u64(what, (unsigned long long)ref.outcome,
	           (unsigned long long)lib_to_ref(lib.outcome));
	expect_str(what, ref.peer.string(),  lib.peer);
	expect_str(what, ref.group.string(), lib.group);
	expect_u64(what, (unsigned long long)ref.need_ram,
	           (unsigned long long)lib.need_ram);
	expect_u64(what, (unsigned long long)ref.avail_ram,
	           (unsigned long long)lib.avail_ram);
	expect_u64(what, ref.need_caps,  lib.need_caps);
	expect_u64(what, ref.avail_caps, lib.avail_caps);
}

void expect_same(char const *what,
                 Sponge::Pkg_launch::Decision const &a,
                 Sponge::Pkg_launch::Decision const &b)
{
	expect_u64(what, (unsigned long long)lib_to_ref(a.outcome),
	           (unsigned long long)lib_to_ref(b.outcome));
	expect_str(what, a.peer,  b.peer);
	expect_str(what, a.group, b.group);
	expect_u64(what, (unsigned long long)a.need_ram,
	           (unsigned long long)b.need_ram);
	expect_u64(what, (unsigned long long)a.avail_ram,
	           (unsigned long long)b.avail_ram);
	expect_u64(what, a.need_caps,  b.need_caps);
	expect_u64(what, a.avail_caps, b.avail_caps);
}

}  /* anonymous namespace */


int main()
{
	using namespace Sponge::Pkg_launch;

	/* ===================== 1. equivalence battery =====================
	 * Every fixture under every resource verdict: the library must
	 * answer exactly what the pre-extraction decision answered. */
	for (unsigned f = 0; f < NUM_FIXTURES; ++f) {
		Fixture const &fx = FIXTURES[f];

		Ref_package      ref_installed[MAX_PACKAGES] { };
		Genode::String<64> ref_running[MAX_PACKAGES] { };
		Genode::String<96> ref_staged[MAX_PACKAGES] { };
		unsigned num_installed { 0 }, num_running { 0 }, num_staged { 0 };
		to_ref(fx, ref_installed, num_installed,
		       ref_running, num_running, ref_staged, num_staged);

		Launch_input in { };
		to_input(fx, in);

		for (unsigned v = 0; v < 3; ++v) {
			int const kind = VERDICT_KINDS[v];
			bool quota_parsable { false }, admitted { false };
			verdict_to_flags(kind, quota_parsable, admitted);

			Ref_result const ref = ref_decide(
				ref_installed, num_installed,
				ref_running, num_running,
				fx.manifest_loaded, Genode::String<32>(fx.binary_prefix),
				ref_staged, num_staged,
				fx.launch,
				quota_parsable, admitted,
				FX_NEED_RAM, FX_AVAIL_RAM, FX_NEED_CAPS, FX_AVAIL_CAPS);

			Decision const lib = decide(in, to_verdict(kind));

			char what[128];
			snprintf(what, sizeof(what),
			         "equivalence: %s / verdict %d", fx.name, kind);
			expect_same(what, ref, lib);

			/* Explicit expectation (pins the reference too). */
			snprintf(what, sizeof(what),
			         "expectation: %s / verdict %d -> %s",
			         fx.name, kind, outcome_name(fx.expect[kind]));
			expect_u64(what, (unsigned long long)fx.expect[kind],
			           (unsigned long long)lib_to_ref(lib.outcome));

			/* Laziness property — the shell computes the admission
			 * input only when no earlier gate fired: whenever
			 * decide_pre_resource answers non-ok, decide() returns
			 * the SAME decision for EVERY resource verdict. */
			Decision const pre = decide_pre_resource(in);
			if (pre.outcome != Outcome::ok) {
				snprintf(what, sizeof(what),
				         "laziness: %s / verdict %d", fx.name, kind);
				expect_same(what, pre, lib);
			}
		}
	}

	/* ===================== 2. outcome payloads ===================== */

	{
		/* exclusive-conflict carries the RUNNING peer and the group. */
		Launch_input in { };
		Fixture const &fx = FIXTURES[3];   /* exclusive-beats-... */
		to_input(fx, in);
		Decision const d = decide(in, to_verdict(2));
		expect_u64("payload: exclusive outcome",
		           REF_EXCLUSIVE_CONFLICT, lib_to_ref(d.outcome));
		expect_str("payload: exclusive peer",   "fb_a", d.peer);
		expect_str("payload: exclusive group",  "fb",   d.group);
	}
	{
		/* refused echoes the admission code's four figures. */
		Launch_input in { };
		to_input(FIXTURES[0], in);
		Decision const d = decide(in, to_verdict(2));
		expect_u64("payload: refused outcome",
		           REF_INSUFFICIENT_RESOURCES, lib_to_ref(d.outcome));
		expect_u64("payload: refused need_ram",
		           (unsigned long long)FX_NEED_RAM,
		           (unsigned long long)d.need_ram);
		expect_u64("payload: refused avail_ram",
		           (unsigned long long)FX_AVAIL_RAM,
		           (unsigned long long)d.avail_ram);
		expect_u64("payload: refused need_caps",  FX_NEED_CAPS,  d.need_caps);
		expect_u64("payload: refused avail_caps", FX_AVAIL_CAPS, d.avail_caps);
	}
	{
		/* quota_unparsable refuses with the zero figures the
		 * pre-extraction code reported. */
		Launch_input in { };
		to_input(FIXTURES[0], in);
		Decision const d = decide(in, to_verdict(1));
		expect_u64("payload: quota outcome",
		           REF_INSUFFICIENT_RESOURCES, lib_to_ref(d.outcome));
		expect_u64("payload: quota need_ram",  0, (unsigned long long)d.need_ram);
		expect_u64("payload: quota avail_ram", 0, (unsigned long long)d.avail_ram);
		expect_u64("payload: quota need_caps", 0, d.need_caps);
		expect_u64("payload: quota avail_caps", 0, d.avail_caps);
	}

	/* ===================== 3. branch ordering ===================== */
	/* The equivalence battery encodes order via the fixtures, but the
	 * claim is worth restating as direct pairs: for EVERY fixture
	 * whose expected outcome is an early gate, the later gates are
	 * also all refusing under the same input and must lose. */
	for (unsigned f = 0; f < NUM_FIXTURES; ++f) {
		Fixture const &fx = FIXTURES[f];
		Launch_input in { };
		to_input(fx, in);

		/* Under the refused verdict every fixture except plain-ok and
		 * the fail-open ones is refused by SOME gate; the outcome must
		 * always be the EARLIEST refusing gate. The earliest gate is
		 * pinned by expect[] (checked above); here we only restate
		 * that binary-missing precedes insufficient-resources when
		 * both apply (fixtures 6 and 9 run refused with a missing
		 * binary). */
		if (fx.expect[2] == REF_BINARY_MISSING) {
			Decision const d = decide(in, to_verdict(2));
			char what[128];
			snprintf(what, sizeof(what),
			         "ordering: %s binary-missing beats resource refusal",
			         fx.name);
			expect_u64(what, REF_BINARY_MISSING, lib_to_ref(d.outcome));
			expect_u64(what, 0, (unsigned long long)d.need_ram);
		}
	}

	/* ===================== 4. exclusive-peer running-only =========== */
	{
		Installed_view installed[2] { };
		installed[0].name = "fb_a";
		installed[0].exclusive_group = "fb";
		installed[1].name = "fb_b";
		installed[1].exclusive_group = "fb";

		char const *running_none[1] { "fb_b" };   /* self, not a peer */
		expect_str("peer: self running only", "",
		           exclusive_peer(installed, 2, running_none, 1,
		                          "fb_b", "fb"));

		char const *running_peer[1] { "fb_a" };
		expect_str("peer: running peer", "fb_a",
		           exclusive_peer(installed, 2, running_peer, 1,
		                          "fb_b", "fb"));

		/* Staged-but-stopped: the peer is installed but NOT in the
		 * running set — the group is free. */
		char const *running_stopped[1] { "other" };
		expect_str("peer: stopped peer does not conflict", "",
		           exclusive_peer(installed, 2, running_stopped, 1,
		                          "fb_b", "fb"));

		/* Empty group claims nothing. */
		expect_str("peer: empty group", "",
		           exclusive_peer(installed, 2, running_peer, 1,
		                          "fb_b", ""));

		/* First match in installed order wins. */
		Installed_view three[3] { };
		three[0].name = "p1"; three[0].exclusive_group = "fb";
		three[1].name = "p2"; three[1].exclusive_group = "fb";
		three[2].name = "p3"; three[2].exclusive_group = "fb";
		char const *running_all[3] { "p1", "p2", "p3" };
		expect_str("peer: first match wins", "p1",
		           exclusive_peer(three, 3, running_all, 3, "self", "fb"));
	}

	/* ===================== 5. manifest inertness ==================== */
	{
		Manifest_view m { };
		m.loaded = false;
		char const *staged[1] { "bin/hello" };
		m.staged[0] = staged[0];
		m.num_staged = 1;
		m.binary_prefix = "bin/";
		expect_true("manifest: not loaded is fail-open (unstaged)",
		            binary_available(m, "absent"));

		m.loaded = true;
		expect_true("manifest: loaded + staged",
		            binary_available(m, "hello"));
		expect_true("manifest: loaded + unstaged refuses",
		            !binary_available(m, "absent"));

		/* The composition rule: staged entries are EFFECTIVE paths. */
		m.binary_prefix = "bin/";
		expect_true("manifest: prefix composition hit",
		            binary_available(m, "hello"));
		m.binary_prefix = "";
		expect_true("manifest: bare entry misses under prefix",
		            !binary_available(m, "hello"));
	}

	/* ===================== 6. running-set sync invariants =========== */
	{
		struct Sync_fx {
			char const *name;
			Fx_pkg      installed[4];
			unsigned    num_installed;
			char const *running[4];
			unsigned    num_running;
			char const *expect[4];
			unsigned    num_expect;
		};

		Sync_fx const S[] = {
			{ "autostart-picked-up",
			  { { "a", "a", "", true }, { "b", "b", "", false },
			    { "c", "c", "", true } }, 3,
			  { "b" }, 1,
			  { "b", "a", "c" }, 3 },
			{ "stale-dropped",
			  { { "a", "a", "", true } }, 1,
			  { "zz", "a" }, 2,
			  { "a" }, 1 },
			{ "explicit-preserved",
			  { { "x", "x", "", false } }, 1,
			  { "x" }, 1,
			  { "x" }, 1 },
			{ "explicit-plus-autostart",
			  { { "x", "x", "", false }, { "y", "y", "", true } }, 2,
			  { "x" }, 1,
			  { "x", "y" }, 2 },
			{ "empty-boot",
			  { { "a", "a", "", true }, { "b", "b", "", false } }, 2,
			  { }, 0,
			  { "a" }, 1 },
		};

		for (unsigned f = 0; f < sizeof(S) / sizeof(S[0]); ++f) {
			Sync_fx const &s = S[f];

			/* Reference sync (mutating, pre-extraction shape). */
			Ref_package ref_installed[MAX_PACKAGES] { };
			for (unsigned i = 0; i < s.num_installed; ++i) {
				ref_installed[i].name            = s.installed[i].name;
				ref_installed[i].binary          = s.installed[i].binary;
				ref_installed[i].exclusive_group = s.installed[i].group;
				ref_installed[i].has_autostart   = s.installed[i].autostart;
			}
			Genode::String<64> ref_running[MAX_PACKAGES] { };
			unsigned num_running { 0 };
			for (unsigned i = 0; i < s.num_running; ++i)
				ref_running[num_running++] = Genode::String<64>(s.running[i]);

			unsigned const ref_n = ref_sync_running(
				ref_installed, s.num_installed, ref_running, num_running);

			/* Library sync (pure derivation into caller buffer). */
			Installed_view installed[MAX_PACKAGES] { };
			for (unsigned i = 0; i < s.num_installed; ++i) {
				installed[i].name          = s.installed[i].name;
				installed[i].has_autostart = s.installed[i].autostart;
			}
			char const *running[MAX_PACKAGES] { };
			for (unsigned i = 0; i < s.num_running; ++i)
				running[i] = s.running[i];

			char const *derived[MAX_PACKAGES] { };
			unsigned const n = sync_running(installed, s.num_installed,
			                                running, s.num_running,
			                                derived, MAX_PACKAGES);

			char what[128];
			snprintf(what, sizeof(what), "sync: %s count", s.name);
			expect_u64(what, ref_n, n);
			expect_u64(what, s.num_expect, n);

			for (unsigned i = 0; i < n && i < s.num_expect; ++i) {
				snprintf(what, sizeof(what), "sync: %s [%u]", s.name, i);
				expect_str(what, ref_running[i].string(), derived[i]);
				expect_str(what, s.expect[i], derived[i]);
			}

			/* Invariant: running ⊆ installed. */
			for (unsigned i = 0; i < n; ++i) {
				bool found { false };
				for (unsigned j = 0; j < s.num_installed; ++j)
					if (strcmp(derived[i], s.installed[j].name) == 0)
						found = true;
				snprintf(what, sizeof(what),
				         "sync invariant: %s running '%s' is installed",
				         s.name, derived[i]);
				expect_true(what, found);
			}

			/* Invariant: installed autostart ⊆ running. */
			for (unsigned j = 0; j < s.num_installed; ++j) {
				if (!s.installed[j].autostart)
					continue;
				bool found { false };
				for (unsigned i = 0; i < n; ++i)
					if (strcmp(derived[i], s.installed[j].name) == 0)
						found = true;
				snprintf(what, sizeof(what),
				         "sync invariant: %s autostart '%s' is running",
				         s.name, s.installed[j].name);
				expect_true(what, found);
			}

			/* Idempotence: syncing the derived set changes nothing. */
			char const *again[MAX_PACKAGES] { };
			for (unsigned i = 0; i < n; ++i)
				again[i] = derived[i];
			unsigned const n2 = sync_running(installed, s.num_installed,
			                                 again, n,
			                                 derived, MAX_PACKAGES);
			snprintf(what, sizeof(what), "sync: %s idempotent", s.name);
			expect_u64(what, n, n2);
		}

		/* Cap truncation: extras are ignored (documented bound). */
		{
			Installed_view installed[3] { };
			installed[0].name = "a"; installed[0].has_autostart = true;
			installed[1].name = "b"; installed[1].has_autostart = true;
			installed[2].name = "c"; installed[2].has_autostart = true;
			char const *derived[3] { };
			unsigned const n = sync_running(installed, 3, nullptr, 0,
			                                derived, 2);
			expect_u64("sync: cap truncation count", 2, n);
			expect_str("sync: cap truncation [0]", "a", derived[0]);
			expect_str("sync: cap truncation [1]", "b", derived[1]);
		}
	}

	/* ===================== 7. name-identity / Start_model =========== */
	{
		/* Emitted <start> nodes carry NO version attribute
		 * (sponge_pkg_config), so both sides of every regeneration
		 * compare as name+"" — and the name-stable regeneration
		 * preserves every running child. */
		expect_true("identity: version-less regeneration preserves",
		            ref_start_model_matches("pkg", "", "pkg", ""));

		/* The supervisor's kill lever: changing ONLY the version
		 * breaks the match and replaces the child. */
		expect_true("identity: version change replaces the child",
		            !ref_start_model_matches("pkg", "1", "pkg", "2"));

		/* Why emission must stay version-less: a versioned node would
		 * NOT match a version-less child. */
		expect_true("identity: versioned node misses version-less child",
		            !ref_start_model_matches("pkg", "", "pkg", "1"));

		/* The model keys its sets by NAME alone — the safe identity
		 * exactly because emission never emits versions. */
		char const *names[1] { "foo" };
		expect_true("identity: set membership is name-keyed",
		            name_in_set(names, 1, "foo"));

		/* already-running is decided by name identity. */
		Launch_input in { };
		in.name = "foo";
		in.installed[0].name = "foo";
		in.num_installed = 1;
		in.running[0] = "foo";
		in.num_running = 1;
		Decision const d = decide(in, Resource_verdict::admitted());
		expect_u64("identity: already-running is name-keyed",
		           REF_ALREADY_RUNNING, lib_to_ref(d.outcome));

		/* Sync deduplicates by name (autostart foo + running foo is
		 * one entry). */
		Installed_view installed[1] { };
		installed[0].name = "foo";
		installed[0].has_autostart = true;
		char const *running[1] { "foo" };
		char const *derived[2] { };
		unsigned const n = sync_running(installed, 1, running, 1,
		                                derived, 2);
		expect_u64("identity: sync dedups by name", 1, n);
		expect_str("identity: sync dedup keeps name", "foo", derived[0]);
	}

	if (failures) {
		printf("self-test: FAIL (%u divergence(s))\n", failures);
		return 1;
	}
	printf("self-test: PASS (equivalence + outcomes + ordering + "
	       "exclusive/manifest/sync/identity invariants)\n");
	return 0;
}
