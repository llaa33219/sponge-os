/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkg_launch — pure launch model for Sponge OS package
 * sandboxes (Phase 16 P1.2 extraction; see
 * <sponge/pkg_launch.h> for the contract and the documented decision
 * order). Pre-extraction source: sponge_pkgd's ops.cc _do_launch gate
 * sequence and registry.cc's _is_running/_add_running/
 * _sync_running_state/_exclusive_peer. Pure functions only: no Env,
 * no sessions, no I/O, no exceptions, no mutable globals — the caller
 * owns every buffer and all state.
 */

#include <sponge/pkg_launch.h>

namespace {

using namespace Sponge::Pkg_launch;

/* First installed entry answering to 'name' (NULL when absent).
 * First match wins, exactly like the pre-extraction lookup loops in
 * _do_launch. */
Installed_view const *find_installed(Launch_input const &in, char const *name)
{
	for (unsigned i = 0; i < in.num_installed && i < MAX_PACKAGES; ++i)
		if (Genode::strcmp(in.installed[i].name, name) == 0)
			return &in.installed[i];
	return nullptr;
}


/*
 * Gate 5 alone: the resource verdict decides. A quota the caller's
 * admission code could not parse refuses with the zero figures the
 * pre-extraction code reported (_report_insufficient(..., 0, 0, 0, 0));
 * a refusal echoes the figures the admission code read.
 */
Decision resource_gate(Resource_verdict const &resource)
{
	Decision d { };

	switch (resource.kind) {
	case Resource_verdict::Kind::admitted:
		d.outcome = Outcome::ok;
		break;
	case Resource_verdict::Kind::quota_unparsable:
		d.outcome = Outcome::insufficient_resources;
		break;
	case Resource_verdict::Kind::refused:
		d.outcome     = Outcome::insufficient_resources;
		d.need_ram    = resource.need_ram;
		d.avail_ram   = resource.avail_ram;
		d.need_caps   = resource.need_caps;
		d.avail_caps  = resource.avail_caps;
		break;
	}
	return d;
}

}  /* anonymous namespace */


Sponge::Pkg_launch::Resource_verdict
Sponge::Pkg_launch::Resource_verdict::admitted()
{
	return Resource_verdict { };
}


Sponge::Pkg_launch::Resource_verdict
Sponge::Pkg_launch::Resource_verdict::quota_unparsable()
{
	Resource_verdict v { };
	v.kind = Kind::quota_unparsable;
	return v;
}


Sponge::Pkg_launch::Resource_verdict
Sponge::Pkg_launch::Resource_verdict::refused(Genode::size_t need_ram,
                                              Genode::size_t avail_ram,
                                              unsigned need_caps,
                                              unsigned avail_caps)
{
	Resource_verdict v { };
	v.kind      = Kind::refused;
	v.need_ram  = need_ram;
	v.avail_ram = avail_ram;
	v.need_caps = need_caps;
	v.avail_caps = avail_caps;
	return v;
}


bool Sponge::Pkg_launch::name_in_set(char const *const *names,
                                     unsigned count, char const *name)
{
	if (!names || !name)
		return false;

	for (unsigned i = 0; i < count && i < MAX_PACKAGES; ++i)
		if (Genode::strcmp(names[i], name) == 0)
			return true;
	return false;
}


bool Sponge::Pkg_launch::binary_available(Manifest_view const &manifest,
                                          char const *binary)
{
	/* Fail-open inertness: no manifest opted in => the gate passes
	 * (AGENTS.md §1.1 escape hatch). */
	if (!manifest.loaded)
		return true;

	if (!binary)
		return false;

	/* The effective path — exactly the composition the staged
	 * manifest records and the config generator emits. */
	Genode::String<96> const path(manifest.binary_prefix, binary);

	for (unsigned i = 0; i < manifest.num_staged && i < MAX_PACKAGES; ++i)
		if (Genode::strcmp(manifest.staged[i], path.string()) == 0)
			return true;
	return false;
}


char const *Sponge::Pkg_launch::exclusive_peer(Installed_view const *installed,
                                               unsigned num_installed,
                                               char const *const *running,
                                               unsigned num_running,
                                               char const *self,
                                               char const *group)
{
	/* An empty group claims nothing (pre-extraction: `if
	 * (!p.exclusive()) return empty;`). */
	if (!group || group[0] == '\0')
		return "";

	for (unsigned i = 0; i < num_installed && i < MAX_PACKAGES; ++i) {
		Installed_view const &o = installed[i];
		if (Genode::strcmp(o.exclusive_group, "") == 0)
			continue;
		if (!name_in_set(running, num_running, o.name))
			continue;
		if (Genode::strcmp(o.exclusive_group, group) != 0)
			continue;
		if (self && Genode::strcmp(o.name, self) == 0)
			continue;
		return o.name;
	}
	return "";
}


Sponge::Pkg_launch::Decision
Sponge::Pkg_launch::decide_pre_resource(Launch_input const &in)
{
	Decision d { };

	/* Gate 1: not-installed. */
	Installed_view const *const found = find_installed(in, in.name);
	if (!found) {
		d.outcome = Outcome::not_installed;
		return d;
	}

	/* Gate 2: already-running (idempotent launch is a no-op). */
	if (name_in_set(in.running, in.num_running, in.name)) {
		d.outcome = Outcome::already_running;
		return d;
	}

	/* Gate 3: exclusive-conflict. The group comes from the FIRST
	 * installed entry answering to the name (first-match semantics of
	 * the pre-extraction scan, which broke after the first match). */
	{
		char const *const peer = exclusive_peer(
			in.installed, in.num_installed, in.running, in.num_running,
			in.name, found->exclusive_group);

		if (peer[0] != '\0') {
			d.outcome = Outcome::exclusive_conflict;
			d.peer    = peer;
			d.group   = found->exclusive_group;
			return d;
		}
	}

	/* Gate 4: binary-missing — precedes the resource gate. */
	if (!binary_available(in.manifest, found->binary)) {
		d.outcome = Outcome::binary_missing;
		return d;
	}

	/* Every gate before the resource gate passed. */
	d.outcome = Outcome::ok;
	return d;
}


Sponge::Pkg_launch::Decision
Sponge::Pkg_launch::decide(Launch_input const &in,
                           Resource_verdict const &resource)
{
	Decision const pre = decide_pre_resource(in);
	if (pre.outcome != Outcome::ok)
		return pre;

	/* Gate 5: the resource/admission verdict, supplied by the
	 * caller's admission code. */
	return resource_gate(resource);
}


unsigned Sponge::Pkg_launch::sync_running(Installed_view const *installed,
                                          unsigned num_installed,
                                          char const *const *running,
                                          unsigned num_running,
                                          char const **out, unsigned out_cap)
{
	if (!out)
		return 0;

	unsigned n { 0 };

	/* Survivors: running entries whose package is still installed,
	 * in their original relative order. */
	for (unsigned i = 0; i < num_running && n < out_cap; ++i) {
		bool installed_still { false };
		for (unsigned j = 0; j < num_installed && j < MAX_PACKAGES; ++j)
			if (Genode::strcmp(installed[j].name, running[i]) == 0) {
				installed_still = true;
				break;
			}
		if (installed_still)
			out[n++] = running[i];
	}

	/* Every installed <autostart/> package not already present, in
	 * installed order (idempotent — membership is re-checked against
	 * what is already derived). */
	for (unsigned i = 0; i < num_installed && i < MAX_PACKAGES && n < out_cap; ++i) {
		if (!installed[i].has_autostart)
			continue;
		if (name_in_set(out, n, installed[i].name))
			continue;
		out[n++] = installed[i].name;
	}

	return n;
}
