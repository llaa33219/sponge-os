/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd operations module - install/remove/launch operations.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): verbatim
 * move of Main::_do_explain/_do_list/_do_install/_do_remove/_do_launch
 * from main.cc, plus the launch pre-flight binary-manifest pair
 * (_init_binary_manifest/_binary_available). The pair lands here
 * because _binary_available's only caller is _do_launch's pre-flight
 * check and _init_binary_manifest is its initializer - the same
 * init+query cohesion that puts _init_admission with the admission
 * math. _bootstrap still calls _init_binary_manifest (cross-TU, same
 * class).
 *
 * _report_* and the duplicate-request replay recipe stay in main.cc
 * (channel); this module calls them like any other Main method.
 */

#include "pkgd.h"

#include <base/log.h>

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


void Sponge::Pkgd::Main::_do_install(Genode::String<128> const &pkg)
{
	char const *const name = pkg.string();

	_resolve(name);
	if (!_ok) {
		_report_error("install", pkg, _error.string());
		return;
	}

	Package const &root = _plan[_num_plan > 0 ? _num_plan - 1 : 0];

	/*
	 * Admission pre-flight for the packages this install would newly put
	 * into the running set. An <autostart/> package gets its <start>
	 * node the moment the install lands, so it is a spawn exactly like a
	 * launch and must clear the same budget gate. Checked BEFORE any
	 * state mutation so a refusal leaves nothing half-applied: no root,
	 * no installed entry, no config regeneration.
	 *
	 * _plan is install-ordered (deps before dependents) and the budget is
	 * decremented per admitted package, so a chain of autostart deps is
	 * checked against what each leaves the next one.
	 */
	if (_admission_armed || _driver_admission_armed) {
		/* One budget per sandbox. A plan may legitimately mix kinds (a
		 * driver with an app dependency, or the reverse), and each kind
		 * is admitted against the grant of the sandbox it will actually
		 * be spawned in — not against a merged pool that neither sandbox
		 * would ever refuse. */
		Genode::size_t free_ram  [2] { 0, 0 };
		unsigned       free_caps[2] { 0, 0 };
		bool           known    [2] { false, false };
		Genode::size_t avail_ram  { 0 };
		unsigned       avail_caps { 0 };

		for (unsigned i = 0; i < _num_plan; ++i) {
			Package const &c = _plan[i];
			if (!c.has_autostart || _is_running(c.name.string()))
				continue;

			Genode::size_t need_ram { 0 };
			if (!_quota_bytes(c.ram, need_ram)) {
				Genode::warning("sponge_pkgd: package '", c.name,
				                "' has an unparsable <quota ram=\"", c.ram,
				                "\"> — refusing the install");
				_report_insufficient("install", pkg, 0, 0, 0, 0);
				return;
			}

			unsigned const k = c.is_driver() ? 1u : 0u;
			if (!known[k]) {
				bool ok = (k == 1)
					? (_driver_admission_armed &&
					   _driver_state_rom.constructed() &&
					   _read_budget(*_driver_state_rom,
					                free_ram[k], free_caps[k]))
					: (_admission_armed && _state_rom.constructed() &&
					   _read_budget(*_state_rom,
					                free_ram[k], free_caps[k]));
				if (!ok) {
					Genode::log("sponge_pkgd: no ",
					            k ? "driver_runtime" : "pkg_runtime",
					            " state report yet — admitting ", c.name);
					break;
				}
				known[k] = true;
			}

			avail_ram  = free_ram[k];
			avail_caps = free_caps[k];

			if (!_admits(c, need_ram, avail_ram, c.caps, avail_caps)) {
				_report_insufficient("install", c.name, need_ram,
				                     avail_ram, c.caps, avail_caps);
				return;
			}

			free_ram [k] -= need_ram;
			free_caps[k] -= (c.caps > free_caps[k] ? 0u : c.caps);
		}
	}

	/* Dirty-scoped emission: mark the targets this install's RESOLVED
	 * plan touches — plan kinds, not the request string. A plan can
	 * legitimately mix kinds (a driver with an app dependency, or the
	 * reverse), and each kind dirties only its own runtime config.
	 * Marked before _sync_installed_from_roots() because that call
	 * re-resolves every root and clobbers _plan. */
	for (unsigned i = 0; i < _num_plan; ++i) {
		if (_plan[i].is_driver())
			_driver_config_dirty = true;
		else
			_app_config_dirty = true;
	}

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

	/* Snapshot before, to report what removal dropped — names and
	 * kinds: the kind snapshot feeds the dirty marks below, which must
	 * be derived before _sync_installed_from_roots() erases the
	 * dropped entries. */
	Genode::String<64> before[MAX_PACKAGES] { };
	bool before_driver[MAX_PACKAGES] { };
	unsigned num_before { 0 };
	for (unsigned i = 0; i < _num_installed && i < MAX_PACKAGES; ++i) {
		before[num_before]        = _installed[i].name;
		before_driver[num_before] = _installed[i].is_driver();
		++num_before;
	}

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

	/* Dirty-scoped emission: the removed closure — the root plus any
	 * dependency the GC drops — can span both kinds, so the targets
	 * come from the kinds of the packages actually dropped, not from
	 * the request string. */
	for (unsigned b = 0; b < num_before; ++b) {
		if (_installed_contains(before[b].string()))
			continue;
		if (before_driver[b])
			_driver_config_dirty = true;
		else
			_app_config_dirty = true;
	}

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
 * starts the new <start> node. Five bounded outcomes:
 *   - not-installed: the package is not in the installed set
 *     (no implicit install — caller must install first);
 *   - already-running: the package already has a <start> node
 *     (idempotent launch is a no-op);
 *   - binary-missing: the package's binary is not staged in this image.
 *     Reported BEFORE the package enters _running and BEFORE the config
 *     is regenerated, so pkg_runtime is never even asked to start a
 *     child it would silently drop;
 *   - insufficient-resources: the package's declared quota does not fit
 *     into pkg_runtime's remaining RAM/caps budget. Same no-op shape as
 *     binary-missing, plus the four figures the caller needs to explain
 *     the refusal to the user (see _report_insufficient);
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

	/* Exclusive-group gate, same rule as the install path
	 * (docs/12 §4.6). Defense in depth for the shipped framebuffer
	 * drivers: today the install pre-flight already refuses the second
	 * one, so this branch is unreachable through vct — but it is the
	 * guard that keeps the invariant true for any future peer in the
	 * group, and for a deployment that marks a package NOT autostart. */
	for (unsigned i = 0; i < _num_installed; ++i) {
		if (Genode::strcmp(_installed[i].name.string(), name) != 0)
			continue;
		Genode::String<64> const peer = _exclusive_peer(_installed[i], name);
		if (peer != Genode::String<64>()) {
			_report_conflict("launch", pkg, peer,
			                 _installed[i].exclusive_group);
			return;
		}
		break;
	}

	/*
	 * Pre-flight: a package can be fully installed (metadata staged,
	 * resolvable, in the index) while its BINARY is absent from the
	 * image. Reporting "ok" in that case is a lie the user never sees
	 * corrected: pkg_runtime's init catches Rom_connection_failed,
	 * Out_of_ram and Out_of_caps around child creation and merely
	 * WARNING (sandbox/library.cc), so the launch silently evaporates.
	 * Gate on the staged-binary manifest BEFORE touching _running.
	 */
	Package const *found { nullptr };
	for (unsigned i = 0; i < _num_installed; ++i)
		if (Genode::strcmp(_installed[i].name.string(), name) == 0) {
			found = &_installed[i];
			break;
		}
	{
		if (found && !_binary_available(*found)) {
			Genode::log("sponge_pkgd: launch ", pkg,
			            " -> binary-missing (", _binary_prefix,
			            found->binary, " is not staged in this image)");
			_report_launch_ok(pkg, "binary-missing");
			return;
		}

		/*
		 * Admission control: the same pre-flight shape, one resource
		 * dimension over. A package that does not fit into pkg_runtime's
		 * remaining budget would be clamped by the sandbox and die
		 * silently, so refuse BEFORE _running and BEFORE the config is
		 * regenerated.
		 */
		if (found) {
			Genode::size_t need_ram { 0 };
			if (!_quota_bytes(found->ram, need_ram)) {
				Genode::warning("sponge_pkgd: package '", name,
				                "' has an unparsable <quota ram=\"",
				                found->ram, "\"> — refusing the launch");
				_report_insufficient("launch", pkg, 0, 0, 0, 0);
				return;
			}

			Genode::size_t avail_ram { 0 };
			unsigned avail_caps { 0 };
			if (!_admits(*found, need_ram, avail_ram,
			             found->caps, avail_caps)) {
				_report_insufficient("launch", pkg, need_ram, avail_ram,
				                     found->caps, avail_caps);
				return;
			}
		}
	}

	_add_running(name);

	/* Dirty-scoped emission: a launch dirties only the runtime that
	 * hosts the package (its resolved <kind>), never both. Marked here
	 * — after every refusal path has returned — so a refused launch
	 * never leaves a stale mark behind. */
	if (found) {
		if (found->is_driver())
			_driver_config_dirty = true;
		else
			_app_config_dirty = true;
	}

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


/*
 * Load the staged-binary manifest — the same opt-in pattern as <bake/>
 * above: the "pkg_binaries" ROM route must be wired by the scenario, and
 * Genode's denial path sleeps forever rather than throwing, so an eager
 * open in an unrouted scenario would hang the daemon.
 *
 * Format (mirrors pkg_index.xml, which is a derived staging artifact
 * generated at boot-image assembly from the actual pkg/ contents):
 *
 *   <binaries>
 *     <binary name="hello"/>
 *     <binary name="pkg_gui_demo"/>
 *     <binary name="bin/terminal"/>
 *   </binaries>
 *
 * The names are the EFFECTIVE binary paths, i.e. exactly what
 * _generate_runtime_config composes from binary_prefix + <binary>.
 */
void Sponge::Pkgd::Main::_init_binary_manifest()
{
	try {
		_config_rom.node().with_optional_sub_node("binaries",
			[&] (Genode::Node const &) {
				_binary_manifest_rom.construct(_env, "pkg_binaries");
				_binary_manifest_rom->update();
				if (!_binary_manifest_rom->valid())
					return;

				Genode::Xml_node const root(_binary_manifest_rom->local_addr<char>(),
				                            _binary_manifest_rom->size());
				if (!root.has_type("binaries"))
					return;

				root.for_each_sub_node("binary", [&](Genode::Xml_node const &b) {
					if (_num_staged_binaries < MAX_PACKAGES)
						_staged_binaries[_num_staged_binaries++] =
						    b.attribute_value("name", Genode::String<96>());
				});
				_binary_manifest_loaded = true;
				Genode::log("sponge_pkgd: ", _num_staged_binaries,
				            " staged binary/binar(ies) — launch pre-flight active");
			});
	} catch (Genode::Xml_node::Invalid_syntax) { }
}


/*
 * True when the package's effective binary is staged in this image.
 * Inert (always true) unless <binaries/> opted a manifest in, so no
 * deployment changes behavior without opting in (AGENTS.md §1.1).
 */
bool Sponge::Pkgd::Main::_binary_available(Package const &p)
{
	if (!_binary_manifest_loaded)
		return true;

	Genode::String<96> const path(_binary_prefix, p.binary);
	for (unsigned i = 0; i < _num_staged_binaries; ++i)
		if (Genode::strcmp(_staged_binaries[i].string(), path.string()) == 0)
			return true;
	return false;
}
