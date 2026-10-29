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
	if (p.is_driver()) {
		if (!_driver_admission_armed || !_driver_state_rom.constructed()) {
			Genode::log("sponge_pkgd: driver admission not armed — admitting ",
			            p.name);
			return true;
		}
	} else if (!_admission_armed || !_state_rom.constructed()) {
		Genode::log("sponge_pkgd: admission not armed — admitting ", p.name);
		return true;
	}

	/*
	 * A driver package is checked against driver_runtime's budget, not
	 * pkg_runtime's — the two sandboxes have separate grants, so reading
	 * the app report would compare a driver's quota against a number it
	 * is never subtracted from.
	 */
	Genode::size_t free_ram  { 0 };
	unsigned       free_caps { 0 };
	bool have_budget { false };
	if (p.is_driver()) {
		if (_driver_admission_armed && _driver_state_rom.constructed())
			have_budget = _read_budget(*_driver_state_rom,
			                          free_ram, free_caps);
	} else {
		have_budget = _read_budget(*_state_rom, free_ram, free_caps);
	}

	if (!have_budget) {
		Genode::log("sponge_pkgd: no ",
		            p.is_driver() ? "driver_runtime" : "pkg_runtime",
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


/*
 * Arm admission control. Opt-in via an <admission/> node in this
 * component's <config>, the same gate <binaries/> and
 * <launcher_request/> use: the "runtime_state" ROM route must be wired
 * by the scenario, and a denied ROM session is fatal under Genode's base
 * Env policy (sleep_forever, not a catchable throw).
 */
void Sponge::Pkgd::Main::_init_admission()
{
	try {
		_config_rom.node().with_optional_sub_node("admission",
			[&] (Genode::Node const &) {
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
