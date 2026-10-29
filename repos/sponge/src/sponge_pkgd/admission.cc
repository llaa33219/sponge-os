/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd admission module - resource admission I/O shell.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): verbatim
 * move of Main::_quota_bytes/_read_budget/_admission_state/_admits and
 * Main::_init_admission/_init_driver_admission from main.cc.
 *
 * Phase 16 P1.2 admission-math extraction (zero behavior change): the
 * quota parser, the HID budget decode and the margin comparison moved
 * into the pure sponge_pkg_admission library
 * (<sponge/pkg_admission.h>); this module keeps only the I/O and state
 * around them — the config-gated ROM arming, the state-report ROM
 * reads, and the fail-open logging — and delegates every number to the
 * library. _report_insufficient/_report_conflict stay in main.cc with
 * the rest of the _report_* replay cluster.
 */

#include "pkgd.h"

#include <base/log.h>
#include <sponge/pkg_admission.h>

/* ===================== resource admission control ===================== */

/*
 * Read pkg_runtime's remaining budget from its sandbox state report.
 *
 * HID, not XML: the 26.08 sandbox Generator serializes reports in HID
 * (the same lesson the Sponge DE recorded when an Xml_node read of
 * runtime_state silently yielded zero children). The top-level <ram>
 * and <caps> nodes are pkg_runtime's OWN PD stats — quota minus used —
 * i.e. the pool a new <start> node still has to come out of. The
 * per-child <ram>/<caps> nodes sit one level deeper inside <child> and
 * are not visited here.
 *
 * The numbers are read as strings and parsed by hand because the
 * Generator prints every round value through Number_of_bytes::print():
 * a plain numeric conversion would read "64M" as 64 BYTES and admit an
 * out-of-budget package. The HID walk and the parsing are the pure
 * sponge_pkg_admission::decode_budget (see <sponge/pkg_admission.h>);
 * this wrapper only does the ROM I/O.
 *
 * Returns false when no usable report is available yet; the caller then
 * admits (see _admits).
 */
bool Sponge::Pkgd::Main::_read_budget(Genode::Attached_rom_dataspace &state,
                                      Genode::size_t &avail_ram,
                                      unsigned &avail_caps)
{
	state.update();
	if (!state.valid() || state.size() == 0)
		return false;

	return Sponge::Pkg_admission::decode_budget(
		state.local_addr<char const>(), state.size(), avail_ram, avail_caps);
}


bool Sponge::Pkgd::Main::_admission_state(Genode::size_t &avail_ram,
                                          unsigned &avail_caps)
{
	if (!_state_rom.constructed())
		return false;
	return _read_budget(*_state_rom, avail_ram, avail_caps);
}


/*
 * Admission decision for one package, against the budget `avail_ram` /
 * `avail_caps` (which the caller may have already decremented for
 * siblings admitted earlier in the same request).
 *
 * Fail-safe: when admission is not armed (no <admission/> node, or the
 * periodic state report has not arrived yet) the package is ADMITTED.
 * The report appears ~2 s into boot and is refreshed by init on every
 * child transition, so the unarmed window is the empty-system boot
 * where the budget is at its largest; gating on it would refuse
 * legitimate first launches for no benefit. The opposite choice —
 * fail-closed — would also silently disable the feature in every
 * deployment that has not wired the relay, which is exactly the kind of
 * invisible control removal AGENTS.md §1.1 rejects. Every unarmed
 * decision is logged so a run can tell the two apart.
 */
bool Sponge::Pkgd::Main::_admits(Package const &p,
                                 Genode::size_t need_ram,
                                 Genode::size_t &avail_ram,
                                 unsigned need_caps, unsigned &avail_caps)
{
	/*
	 * Budget selection is by emission slot (P2.4 commit 2): a package
	 * is admitted against the sandbox it will actually be spawned in —
	 * driver_runtime for a driver, its OWN class runtime's report in
	 * grouped mode, pkg_runtime's single-app report in legacy mode —
	 * never against another class's pool, which would compare the
	 * quota against a number it is never subtracted from.
	 */
	unsigned const slot   = _emission_slot(p);
	bool   const driver  = (slot == SLOT_DRIVER);

	if (driver) {
		if (!_driver_admission_armed || !_driver_state_rom.constructed()) {
			Genode::log("sponge_pkgd: driver admission not armed — admitting ",
			            p.name);
			return true;
		}
	} else if (_grouped_runtimes) {
		/* Per-class escape hatch: an unarmed class admits (never by
		 * borrowing another class's budget). */
		if (!_class_admission_armed[slot]) {
			Genode::log("sponge_pkgd: ", _slot_name(slot),
			            " admission not armed — admitting ", p.name);
			return true;
		}
	} else if (!_admission_armed || !_state_rom.constructed()) {
		Genode::log("sponge_pkgd: admission not armed — admitting ", p.name);
		return true;
	}

	Genode::size_t free_ram  { 0 };
	unsigned       free_caps { 0 };
	bool have_budget { false };
	if (driver) {
		if (_driver_admission_armed && _driver_state_rom.constructed())
			have_budget = _read_budget(*_driver_state_rom,
			                          free_ram, free_caps);
	} else if (_grouped_runtimes) {
		if (_class_state_rom[slot].constructed())
			have_budget = _read_budget(*_class_state_rom[slot],
			                          free_ram, free_caps);
	} else {
		have_budget = _read_budget(*_state_rom, free_ram, free_caps);
	}

	/* Delayed first report: a not-yet-published report admits, per
	 * class (see _init_class_admission). */
	if (!have_budget) {
		Genode::log("sponge_pkgd: no ", _slot_state_label(slot),
		            " state report yet — admitting ", p.name);
		return true;
	}

	avail_ram  = free_ram;
	avail_caps = free_caps;

	/*
	 * The margin comparison is the pure sponge_pkg_admission::admits
	 * (values and arithmetic verbatim from the pre-extraction inline
	 * comparison; see <sponge/pkg_admission.h>).
	 */
	return Sponge::Pkg_admission::admits(free_ram, free_caps,
	                                     need_ram, need_caps);
}


char const *Sponge::Pkgd::Main::_slot_state_label(unsigned slot) const
{
	if (slot == SLOT_DRIVER)
		return "driver_runtime";
	if (!_grouped_runtimes)
		return "pkg_runtime";
	switch (slot) {
	case SLOT_APP:      return "runtime_simple";
	case SLOT_TERMINAL: return "runtime_terminal";
	default:            return "runtime_browser";
	}
}


/*
 * Arm admission control for the legacy single-app gate — or, in grouped
 * mode, the simple-class alias. Opt-in via an <admission/> node in this
 * component's <config>, the same gate <binaries/> and
 * <launcher_request/> use: the state-report ROM route must be wired by
 * the scenario, and a denied ROM session is fatal under Genode's base
 * Env policy (sleep_forever, not a catchable throw).
 *
 * In grouped mode (<grouped_runtimes/>) <admission/> is the SIMPLE-class
 * alias: it arms exactly what <simple_admission/> arms (the
 * runtime_simple_state report) with one diagnostic, so a legacy
 * deployment can flip the marker on without rewriting its admission
 * node. The legacy runtime_state report is pkg_runtime's single-app
 * pool and is deliberately never opened in grouped mode. Legacy mode
 * keeps this gate unchanged. Driver admission is unaffected either way
 * (driver_runtime is the same sandbox in both modes).
 */
void Sponge::Pkgd::Main::_init_admission()
{
	try {
		_config_rom.node().with_optional_sub_node("admission",
			[&] (Genode::Node const &) {
				if (_grouped_runtimes) {
					Genode::log("sponge_pkgd: grouped runtimes: "
					            "<admission/> is the simple-class alias "
					            "(runtime_simple_state) — prefer "
					            "<simple_admission/>");
					_arm_class_admission(SLOT_APP);
					return;
				}
				_state_rom.construct(_env, "runtime_state");
				_admission_armed = true;
				Genode::log("sponge_pkgd: resource admission armed "
				            "(pkg_runtime state report)");
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — "
		                "resource admission disabled");
	}
}


/*
 * Per-class admission arming (grouped mode, P2.4 commit 2).
 * <simple_admission/>, <terminal_admission/> and <browser_admission/>
 * each open that class's sandbox state report (runtime_simple_state /
 * runtime_terminal_state / runtime_browser_state); the grouped
 * <admission/> alias arms the same simple gate from _init_admission.
 *
 * Without <grouped_runtimes/> the per-class nodes are inert with a
 * warning — there are no class sandboxes to read, and silently opening
 * their relays would be an unwanted capability. Without a node, the
 * class keeps the explicit fail-open escape hatch and the delayed
 * first-report behavior (AGENTS.md §1.1): each class fails open on its
 * own, never by borrowing another class's budget. A denied ROM session
 * is fatal under the base Env policy, which is exactly why each class's
 * ROM is opened only by its own node.
 */
void Sponge::Pkgd::Main::_init_class_admission()
{
	struct Node { char const *name; unsigned slot; };
	static Node const nodes[] {
		{ "simple_admission",   SLOT_APP },
		{ "terminal_admission", SLOT_TERMINAL },
		{ "browser_admission",  SLOT_BROWSER },
	};

	try {
		for (Node const &n : nodes) {
			_config_rom.node().with_optional_sub_node(n.name,
				[&] (Genode::Node const &) {
					if (!_grouped_runtimes) {
						Genode::warning("sponge_pkgd: <", n.name,
						                "> requires <grouped_runtimes/> "
						                "— ignored");
						return;
					}
					_arm_class_admission(n.slot);
				});
		}
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — per-class "
		                "resource admission disabled");
	}
}


bool Sponge::Pkgd::Main::_arm_class_admission(unsigned slot)
{
	if (slot >= NUM_APP_CLASSES)
		return false;

	/* Idempotent: <admission/> (the grouped simple alias) and
	 * <simple_admission/> may both name the simple gate. */
	if (_class_state_rom[slot].constructed())
		return true;

	Genode::String<64> const rom_label(_slot_state_label(slot), "_state");
	_class_state_rom[slot].construct(_env, rom_label.string());
	_class_admission_armed[slot] = true;
	Genode::log("sponge_pkgd: ", _slot_name(slot),
	            " class admission armed (", rom_label, " report)");
	return true;
}


/*
 * Arm admission control for the driver sandbox. Opt-in via a separate
 * <driver_admission/> node so a deployment that has no driver_runtime at
 * all (every scenario predating it) does not open a ROM session that
 * would be denied — and a denied ROM session is fatal here, not a
 * catchable throw.
 */
void Sponge::Pkgd::Main::_init_driver_admission()
{
	try {
		_config_rom.node().with_optional_sub_node("driver_admission",
			[&] (Genode::Node const &) {
				_driver_state_rom.construct(_env, "driver_runtime_state");
				_driver_admission_armed = true;
				Genode::log("sponge_pkgd: driver resource admission armed "
				            "(driver_runtime state report)");
			});
	} catch (Genode::Xml_node::Invalid_syntax) {
		Genode::warning("sponge_pkgd: malformed <config> — "
		                "driver resource admission disabled");
	}
}
