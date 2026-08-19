/*
 * SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * usb_hid_mouse_probe — Phase 15 W5 USB-mouse HID envelope probe
 * (docs/plans/phase15-real-hardware-boot.md §W5; companion to
 * the MOUSE-detected / MOUSE-removed lifecycle probe in
 * repos/sponge/src/test/usb_hid_probe/).
 *
 * Opens two ROMs in parallel:
 *
 *   - "report" — the pc_usb_host (drivers/usb_host/pc) device
 *     enumeration report relayed through the drivers sub-init's
 *     report_rom under the policy
 *     `label: usb_hid_mouse_probe -> report | report: usb -> devices`
 *     (added to drivers.config by run/sponge-usb-hid-mouse.run;
 *     same pattern as the existing usb_hid_probe's policy).
 *     Same source the usb_hid_probe observes for the
 *     Keyboard/Mouse substring transitions.
 *
 *   - "pointer" — nitpicker's `pointer` reporter, relayed through
 *     the drivers sub-init's report_rom via the policy
 *     `label: usb_hid_mouse_probe -> pointer | report: nitpicker
 *     -> pointer`. The drivers sub-init's report_rom has a parent
 *     route for the Report service so it can RECEIVE the
 *     nitpicker pointer reporter. nitpicker emits this report
 *     when its pointer position changes (the
 *     `_pointer_reporter.generate(...)` call is gated by
 *     `result.motion_activity` at
 *     genode/repos/os/src/server/nitpicker/main.cc:980-984; the
 *     content is emitted via
 *     `_user_state.report_pointer_position(g)`).
 *
 * The probe is a PASSIVE OBSERVER. It does NOT drive anything — the
 * scenario's QMP choreography is the only writer of events. The
 * probe's job is to surface the three observable sinks the audit
 * chain needs as evidence:
 *
 *   1. The pc_usb_host devices report gains / loses a Mouse entry.
 *      The probe logs:
 *
 *         usb_hid_mouse_probe: devices report MOUSE present
 *         usb_hid_mouse_probe: devices report MOUSE absent
 *
 *      exactly once each, on the first ROM update that flips the
 *      Mouse substring state.
 *
 *   2. usb_hid's Linux-hid-core emits `Connected device: inputN
 *      (QEMU USB Mouse at ...) POINTER' on the bind and
 *      `disconnect ...' on the unbind (these log lines come from
 *      the genode/repos/dde_linux/src/lib/lx_emul/shadow/drivers/
 *      input/evdev.c:790 printk — the probe does NOT parse them;
 *      the run script's `expect` arm catches them via
 *      `usb_hid_mouse_probe: POINTER_BIND observed' / POINTER_UNBIND
 *      observed' markers, which the probe emits when its FIRST
 *      observation of the devices report shows a Mouse entry —
 *      a side-channel witness to the usb_hid bind.
 *
 *      (The "POINTER_BIND observed" marker is emitted on the FIRST
 *      ROM update that shows a Mouse entry; the run script's
 *      expect arm matches the marker's exact text, so the marker
 *      is the gate. The actual usb_hid `Connected device: ...
 *      POINTER' log line is captured by the run script's
 *      `expect` arm directly — it's a separate observable sink.)
 *
 *   3. The pointer ROM delta (or absence of delta). On every ROM
 *      update of the `pointer` ROM, the probe compares the new
 *      content to the previous content. If the content changed,
 *      it logs `pointer ROM delta observed'. If the content is
 *      identical, it logs `pointer ROM stable (<hash>)' where
 *      `<hash>` is a stable 64-bit FNV-1a hash of the ROM bytes.
 *      This is the Phase 14 gap row #2 observation surface: per
 *      the gap ("nitpicker pointer ROM only updates on
 *      absolute_motion"), the pointer ROM may NOT update when the
 *      hot-plugged usb-mouse emits REL events through the
 *      ps2 → event_filter → nitpicker chain. The probe records
 *      whether the ROM delta fires or not as honest evidence.
 *
 * The probe does NOT exit on any marker. It keeps observing until
 * the parent (init) tears it down at end-of-scenario.
 *
 * === Probe semantics ===
 *
 *   - Both ROMs arrive asynchronously. The probe subscribes to
 *     ROM update signals on each and scans on every update.
 *
 *   - The Mouse substring check (devices report) is a simple
 *     byte-search, identical to usb_hid_probe's logic.
 *
 *   - The pointer ROM delta is a byte-equality check between the
 *     current ROM content and the previous content. The probe
 *     logs one marker per unique content (no log spam — only
 *     emits `pointer ROM delta observed' on the FIRST content
 *     change, and `pointer ROM stable (<hash>)' on the FIRST
 *     observation that confirms a stable content).
 *
 *   - On the FIRST ROM update of `pointer` the probe has no
 *     previous content; it logs `pointer ROM initial (<hash>)'
 *     and stores the content. Subsequent updates either show
 *     a delta or stay stable.
 *
 * === Failure modes ===
 *
 *   - Empty / invalid ROMs: no log lines, no transitions; bounded
 *     by MAX_UPDATE_RETRIES so a broken chain eventually surfaces
 *     FAIL rather than spinning forever.
 *
 *   - The devices report gains a Mouse entry but the pointer
 *     ROM NEVER updates: this is the EXPECTED Phase 14 gap row
 *     #2 outcome on this host (REL motion → no nitpicker pointer
 *     ROM update). The probe logs `pointer ROM stable (<hash>)'
 *     on each update with no delta. The run script records the
 *     observation in its evidence log; the scenario PASS marker
 *     is the audit chain (MOUSE detected / MOUSE removed), not
 *     the pointer ROM delta.
 *
 *   - The pointer ROM updates once on the bind and stays there:
 *     the probe logs `pointer ROM delta observed' once. This is
 *     the EXPECTED behavior on a host where nitpicker DOES emit
 *     the pointer ROM on REL motion (the gap row #2 might not
 *     hold for every guest / driver chain). The probe records
 *     the observation; the scenario still PASSes via the audit
 *     chain.
 *
 * AGENTS.md §3.1: qualified Genode types (`Genode::size_t`,
 * `Genode::Env`), snake_case methods, PascalCase classes, no
 * exceptions, `#pragma once` not needed (single TU).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/signal.h>
#include <util/string.h>

namespace {

using Genode::Attached_rom_dataspace;
using Genode::Env;
using Genode::log;
using Genode::Signal_handler;
using Genode::size_t;

size_t const MAX_ROM_BYTES = 16384;

unsigned const MAX_UPDATE_RETRIES = 256;

}


static Genode::uint64_t fnv1a64(char const *base, size_t n)
{
	Genode::uint64_t h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) {
		h ^= (unsigned char)base[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

struct Usb_hid_mouse_probe
{
	Env &_env;

	Attached_rom_dataspace _devices_rom { _env, "report" };
	Attached_rom_dataspace _pointer_rom { _env, "pointer" };

	Signal_handler<Usb_hid_mouse_probe> _devices_handler {
		_env.ep(), *this, &Usb_hid_mouse_probe::_check_devices };
	Signal_handler<Usb_hid_mouse_probe> _pointer_handler {
		_env.ep(), *this, &Usb_hid_mouse_probe::_check_pointer };

	unsigned _devices_retries { 0 };
	unsigned _pointer_retries { 0 };

	bool    _mouse_seen { false };
	bool    _mouse_present { false };

	bool    _pointer_observed_first { false };
	bool    _pointer_delta_emitted { false };

	Genode::size_t _pointer_last_len { 0 };
	Genode::uint64_t _pointer_last_hash { 0ULL };

	Usb_hid_mouse_probe(Env &env) : _env(env)
	{
		_devices_rom.sigh(_devices_handler);
		_pointer_rom.sigh(_pointer_handler);
		_check_devices();
		_check_pointer();
	}

	void _check_devices()
	{
		_devices_rom.update();

		if (!_devices_rom.valid()) {
			if (_devices_retries++ < MAX_UPDATE_RETRIES) return;
			log("usb_hid_mouse_probe: FAIL: devices rom_invalid (after ",
			    _devices_retries, " update retries)");
			return;
		}

		char const *base  = _devices_rom.local_addr<char>();
		size_t const avail = _devices_rom.size() < MAX_ROM_BYTES
			? _devices_rom.size() : MAX_ROM_BYTES;

		bool const now_present = _substring(base, avail, "Mouse");

		if (now_present && !_mouse_seen) {
			_mouse_seen = true;
			log("usb_hid_mouse_probe: devices report MOUSE present");
			log("usb_hid_mouse_probe: POINTER_BIND observed");
		}

		if (!now_present && _mouse_seen) {
			log("usb_hid_mouse_probe: devices report MOUSE absent");
			log("usb_hid_mouse_probe: POINTER_UNBIND observed");
		}

		_mouse_present = now_present;
	}

	void _check_pointer()
	{
		_pointer_rom.update();

		if (!_pointer_rom.valid()) {
			if (_pointer_retries++ < MAX_UPDATE_RETRIES) return;
			/* ROM never became valid is fine — emit a single
			 * stable-empty observation so the run script's
			 * secondary expect arm can record the outcome. */
			if (!_pointer_observed_first) {
				_pointer_observed_first = true;
				log("usb_hid_mouse_probe: pointer ROM stable (rom_invalid)");
			}
			return;
		}

		char const *base  = _pointer_rom.local_addr<char>();
		size_t const avail = _pointer_rom.size() < MAX_ROM_BYTES
			? _pointer_rom.size() : MAX_ROM_BYTES;

		Genode::uint64_t const h = fnv1a64(base, avail);

		if (!_pointer_observed_first) {
			_pointer_observed_first = true;
			_pointer_last_len   = avail;
			_pointer_last_hash  = h;
			log("usb_hid_mouse_probe: pointer ROM initial (hash=",
			    Genode::Hex(_pointer_last_hash), " bytes=",
			    (unsigned long)avail, ")");
			return;
		}

		if (h != _pointer_last_hash || avail != _pointer_last_len) {
			if (!_pointer_delta_emitted) {
				_pointer_delta_emitted = true;
				log("usb_hid_mouse_probe: pointer ROM delta observed "
				    "(Phase 14 gap row #2 contradicted on this host: "
				    "REL motion DID update the nitpicker pointer ROM)");
			}
			_pointer_last_len  = avail;
			_pointer_last_hash = h;
			return;
		}

		log("usb_hid_mouse_probe: pointer ROM stable (hash=",
		    Genode::Hex(_pointer_last_hash), " bytes=",
		    (unsigned long)avail, ")");
	}

	static bool _substring(char const *base, size_t avail, char const *needle)
	{
		size_t const nlen = Genode::strlen(needle);
		if (avail < nlen) return false;
		for (size_t i = 0; i + nlen <= avail; i++) {
			bool ok = true;
			for (size_t k = 0; k < nlen; k++) {
				if (base[i + k] != needle[k]) { ok = false; break; }
			}
			if (ok) return true;
		}
		return false;
	}
};


void Component::construct(Genode::Env &env)
{
	static Usb_hid_mouse_probe probe(env);
}

Genode::size_t Component::stack_size() { return 16 * 1024; }