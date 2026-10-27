/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd admission module - resource admission math.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): verbatim
 * move of Main::_quota_bytes/_read_budget/_admission_state/_admits and
 * Main::_init_admission/_init_driver_admission from main.cc: the quota
 * parser, the sandbox-budget readers, the admission decision with its
 * ADMISSION_MARGIN_* safety margins (declared in pkgd.h), and the
 * config-gated ROM arming. _report_insufficient/_report_conflict stay
 * in main.cc with the rest of the _report_* replay cluster.
 */

#include "pkgd.h"

#include <base/log.h>

/* ===================== resource admission control ===================== */

/*
 * Parse a package metadata quota string ("48M", "1024M", "1G", "512K",
 * or a plain byte count) into bytes. Package metadata is authored by
 * hand (docs/16-package-authoring.md §2), so the suffix set is closed
 * and small; anything else is a malformed quota and yields "no answer",
 * which callers treat as "cannot prove it fits" -> the launch is
 * refused rather than silently clamped.
 */
bool Sponge::Pkgd::Main::_quota_bytes(Genode::String<16> const &q,
                                      Genode::size_t &out)
{
	char const *p = q.string();
	Genode::size_t v { 0 };
	unsigned digits { 0 };
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + Genode::size_t(*p - '0');
		++p; ++digits;
	}
	if (!digits) return false;

	switch (*p) {
	case 'K': case 'k': v *= 1024; break;
	case 'M': case 'm': v *= 1024 * 1024; break;
	case 'G': case 'g': v *= 1024 * 1024 * 1024; break;
	case '\0':            break;
	default:               return false;
	}
	out = v;
	return true;
}


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
 * out-of-budget package.
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

	bool ram_ok { false }, caps_ok { false };
	Genode::size_t ram_bytes { 0 }, caps_value { 0 };

	try {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(
			state.local_addr<char const>(), state.size()));

		Genode::String<16> ram_avail  { }, caps_avail { };
		root.for_each_sub_node([&] (Genode::Hid_node const &n) {
			if (n.has_type("ram") && !ram_ok) {
				ram_avail = n.attribute_value("avail", Genode::String<16>());
				ram_ok = _quota_bytes(ram_avail, ram_bytes);
			}
			if (n.has_type("caps") && !caps_ok) {
				caps_avail = n.attribute_value("avail", Genode::String<16>());
				caps_ok = _quota_bytes(caps_avail, caps_value) &&
				          caps_value > 0 && caps_value <= 0xffffffu;
			}
		});
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		/* Hid_node does not throw; defensive only. */
		return false;
	}

	if (!ram_ok || !caps_ok)
		return false;

	avail_ram  = ram_bytes;
	avail_caps = (unsigned)caps_value;
	return true;
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

	if (free_ram < need_ram + ADMISSION_MARGIN_RAM)
		return false;
	if (free_caps < need_caps + ADMISSION_MARGIN_CAPS)
		return false;
	return true;
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
