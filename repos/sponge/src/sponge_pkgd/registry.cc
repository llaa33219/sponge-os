/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd registry module - installed/running state.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): verbatim
 * move of Main::_installed_contains/_add_root/_sync_installed_from_
 * roots/_is_running/_add_running/_sync_running_state/_exclusive_peer
 * from main.cc. Pure state maintenance over _roots/_installed and the
 * Phase 7 _running set - no reports, no emission, no I/O.
 *
 * The RUNNING-SET DECISIONS now live in the pure sponge_pkg_launch
 * library (Phase 16 P1.2 extraction): membership (name identity), the
 * exclusive-peer lookup (running-only semantics) and the sync rule.
 * This module stays the STATE shell - it snapshots the caller's
 * Genode::String arrays into the library's non-owning views and
 * applies the results back to Main's arrays. The _add_running
 * mutation and the sync materialization below are deliberately still
 * here: Main owns its state; the library only decides (it writes
 * nothing but caller-owned output buffers).
 *
 * Stack discipline is preserved exactly as it was: _sync_installed_
 * from_roots keeps its `static Package fresh[MAX_PACKAGES]` scratch
 * (the requesting thread's stack is small - see the comment there).
 * The running-set helpers below only take small view snapshots on the
 * stack (pointer arrays), same as before.
 */

#include "pkgd.h"

#include <sponge/pkg_launch.h>

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
	/*
	 * Static: 32 Packages × the 16K config_xml member is ~532K — as a
	 * stack local on the (small-stack) ep thread this overflowed the
	 * stack (illegal-WRITE pager fault, fixed address). Single ep
	 * thread runs this serially — no reentrancy.
	 */
	static Package fresh[MAX_PACKAGES] { };
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
	/* Name identity (Start_model name+version invariant: emitted
	 * <start> nodes carry no version, so name is the whole identity —
	 * see <sponge/pkg_launch.h>). */
	char const *names[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_running; ++i)
		names[i] = _running[i].string();
	return Sponge::Pkg_launch::name_in_set(names, _num_running, name);
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
 * package that is currently installed (idempotent — dedup by name).
 * Explicit (non-autostart) launches of still-installed packages are
 * preserved.
 *
 * The sync rule and its invariants are decided by sponge_pkg_launch's
 * sync_running (host-tested); this method only applies the derived
 * set to _running.
 */
void Sponge::Pkgd::Main::_sync_running_state()
{
	Pkg_launch::Installed_view installed[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_installed; ++i) {
		installed[i].name          = _installed[i].name.string();
		installed[i].has_autostart = _installed[i].has_autostart;
	}

	char const *running[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_running; ++i)
		running[i] = _running[i].string();

	char const *derived[MAX_PACKAGES] { };
	unsigned const n = Pkg_launch::sync_running(
		installed, _num_installed, running, _num_running,
		derived, MAX_PACKAGES);

	/*
	 * Materialize in increasing order: sync_running preserves the
	 * survivors' relative order, so a survivor's pointer aliases
	 * _running[j] with j >= i and is read before that slot is
	 * overwritten (the same argument as the pre-extraction in-place
	 * compaction `_running[w++] = _running[i]`); appended autostart
	 * names alias _installed, which this loop never touches.
	 */
	for (unsigned i = 0; i < n; ++i)
		_running[i] = Genode::String<64>(derived[i]);
	_num_running = n;
}


/*
 * The running peer that holds `name`'s exclusive group, or an empty
 * string when the group is free (docs/12-package-format.md §4.6).
 *
 * Scans the RUNNING set only: the constraint is about simultaneous
 * hardware claims, not about what is staged on the media. Both frame-
 * buffer packages are staged on every product medium so the user can
 * switch; only the active one holds the group.
 */
Genode::String<64> Sponge::Pkgd::Main::_exclusive_peer(
	Package const &p, Genode::String<64> const &self)
{
	Pkg_launch::Installed_view installed[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_installed; ++i) {
		installed[i].name            = _installed[i].name.string();
		installed[i].exclusive_group = _installed[i].exclusive_group.string();
	}

	char const *running[MAX_PACKAGES] { };
	for (unsigned i = 0; i < _num_running; ++i)
		running[i] = _running[i].string();

	char const *const peer = Pkg_launch::exclusive_peer(
		installed, _num_installed, running, _num_running,
		self.string(), p.exclusive_group.string());

	return Genode::String<64>(peer);
}
