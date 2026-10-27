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
 * Stack discipline is preserved exactly as it was: _sync_installed_
 * from_roots keeps its `static Package fresh[MAX_PACKAGES]` scratch
 * (the requesting thread's stack is small - see the comment there).
 */

#include "pkgd.h"

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
	if (!p.exclusive()) return Genode::String<64>();

	for (unsigned i = 0; i < _num_installed; ++i) {
		Package const &o = _installed[i];
		if (!o.exclusive() || !_is_running(o.name.string()))
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
