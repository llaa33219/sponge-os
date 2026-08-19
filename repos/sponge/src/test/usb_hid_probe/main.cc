/*
 * SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * usb_hid_probe — USB HID hotplug probe (Phase 12 W4 + Phase 15 W5;
 * docs/plans/phase12-hardware.md §"W4: USB boot and USB keyboard
 * scenarios" Risk 20 mitigation, docs/plans/phase15-real-hardware-
 * boot.md §W5 USB-mouse envelope).
 *
 * Opens ROM "report" — the pc_usb_host (drivers/usb_host/pc) device
 * enumeration report relayed through the drivers sub-init's
 * report_rom (policy "label: usb_hid -> report | report: usb ->
 * devices" in run/sponge-de-sel4-interactive.run:271 and friends).
 *
 * Watches the ROM for the lifecycle of two USB HID device classes:
 *
 *   1. USB keyboard (Phase 12 W4 contract):
 *
 *        - "Keyboard" substring first appears (QEMU usb-kbd model
 *          hot-plugged) → logs "usb_hid: KEYBOARD detected" once.
 *        - "Keyboard" substring disappears on a subsequent update
 *          (QEMU device_del succeeded) → logs "usb_hid: KEYBOARD
 *          removed" once.
 *
 *   2. USB mouse (Phase 15 W5 contract, docs/15-hardware-
 *      compatibility.md §"Phase 15 cells"):
 *
 *        - "Mouse" substring first appears (QEMU usb-mouse model
 *          hot-plugged, after the existing usb-tablet baseline is
 *          already bound) → logs "usb_hid: MOUSE detected" once.
 *        - "Mouse" substring disappears on a subsequent update
 *          (QEMU device_del succeeded) → logs "usb_hid: MOUSE
 *          removed" once.
 *
 * Both substring searches are case-sensitive byte scans across the
 * whole ROM. The pc_usb_host devices report emits device names as
 * `<device name="...">` attributes (see
 * genode/repos/pc/src/driver/usb_host/pc/README for the schema).
 * The QEMU usb-kbd model surfaces as "QEMU USB Keyboard" (the
 * "Keyboard" substring catches it); the QEMU usb-mouse model
 * surfaces as "QEMU USB Mouse" (the "Mouse" substring catches it).
 *
 * The two lifecycles are observed independently — one and the same
 * ROM update can advance both if both devices are present (the
 * Phase 15 W5 scenario boots WITHOUT a mouse, then QMP hot-plugs
 * one, so by construction only the MOUSE transitions fire). A
 * future scenario that hot-plugs BOTH could see both lifecycles
 * interleaved; the probe treats them as two parallel state
 * machines with the same ROM-update source.
 *
 * The probe does NOT exit on any marker — both events are
 * observed transitions on a single continuous probe. The probe
 * keeps observing until the parent (init) tears it down via
 * env.parent().exit() at the end of the run script.
 *
 * === Probe semantics ===
 *
 *   - The ROM arrives asynchronously (report_rom creates an empty
 *     module on the first reader lookup, before the writer has
 *     published). The probe subscribes to ROM update signals and
 *     scans on every update; per-device "is_present" booleans
 *     track the transitions.
 *
 *   - "Keyboard" / "Mouse" are substring searches of the whole
 *     report; the pc_usb_host devices report emits the device
 *     name as the `<device name="...">` attribute (e.g.
 *     `<device name="usb-1-1" ...>`). The QEMU usb-kbd device
 *     model surfaces under the bus-id assigned by QEMU at hotplug
 *     time. The substring "Keyboard" inside the device name is
 *     what identifies it as a keyboard (the QEMU device model's
 *     internal product string is exposed in the `<config>` /
 *     `<interface>` block too, but the device-name attribute is
 *     the cleanest discriminator). The same substring rule
 *     applies to the usb-mouse model.
 *
 *   - The probe does NOT distinguish "keyboard" from "keypad" or
 *     other HID keyboard-class devices. Both carry "Keyboard" or
 *     close, so the substring is sufficient for the W4 audit
 *     chain. A more precise class-only check would require
 *     parsing the `<interface class="0x3 subclass="0x1">` block;
 *     the substring approach is intentionally simple.
 *
 *   - The Mouse substring is also fuzzy: any USB device whose
 *     surfaced name contains "Mouse" matches. The QEMU usb-mouse
 *     device model name is "QEMU USB Mouse" so this catches the
 *     intended case. A wired/wireless OEM mouse with a different
 *     product string ("Logitech M525" etc.) would NOT match —
 *     but the Phase 15 W5 scenario targets QEMU, where the
 *     QEMU-model-name substring holds.
 *
 *   - On ROM signal: scan the new content. If Keyboard/Mouse was
 *     absent and is now present → log "detected". If present and
 *     is now absent → log "removed". Otherwise no log line. Each
 *     lifecyele is independent.
 *
 * === Failure modes ===
 *
 *   - Empty/invalid ROM (initial state): no log line, no transition,
 *     wait for the writer's first non-empty publish. Bounded by
 *     MAX_UPDATE_RETRIES so a broken chain eventually surfaces
 *     FAIL rather than spinning forever (the run script's bounded
 *     run_genode_until timeout is the ultimate backstop).
 *
 *   - Keyboard / Mouse never appears (QMP device_add was rejected
 *     by QEMU, or the device model lacks the surface): no
 *     "detected" log line, the run script's gate waits until
 *     timeout and fails loudly.
 *
 *   - Keyboard / Mouse stays after device_del (QEMU ignored
 *     device_del): no "removed" log line, the run script's gate
 *     waits until timeout and fails loudly.
 *
 *   - A non-keyboard/non-mouse USB device (a tablet, a hub) appears
 *     in the report: substrings absent → no transition logged.
 *     Test still passes (the marker is a function of the
 *     substrings only).
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


struct Main
{
	Env &_env;

	Attached_rom_dataspace _report_rom { _env, "report" };
	Signal_handler<Main>   _update_handler { _env.ep(), *this, &Main::_check };

	unsigned _retries { 0 };

	bool    _keyboard_seen { false };
	bool    _keyboard_present { false };
	bool    _done_detect { false };
	bool    _done_remove { false };

	bool    _mouse_seen { false };
	bool    _mouse_present { false };
	bool    _mouse_done_detect { false };
	bool    _mouse_done_remove { false };

	Main(Env &env) : _env(env)
	{
		_report_rom.sigh(_update_handler);
		_check();
	}

	void _check()
	{
		_report_rom.update();

		if (!_report_rom.valid()) {
			if (_retries++ < MAX_UPDATE_RETRIES) return;
			log("usb_hid_probe: FAIL: rom_invalid (after ",
			    _retries, " update retries)");
			return;
		}

		char const *base  = _report_rom.local_addr<char>();
		size_t const avail = _report_rom.size() < MAX_ROM_BYTES
			? _report_rom.size() : MAX_ROM_BYTES;

		/*
		 * Substring scans: locate "Keyboard" / "Mouse" anywhere in
		 * the report. The pc_usb_host devices report uses
		 * '<device name="...">' attributes that contain the QEMU
		 * device model's product string; the usb-kbd model names
		 * itself "Keyboard" in that string; the usb-mouse model
		 * names itself "Mouse". A simple byte-search is robust
		 * against future schema changes as long as the surface
		 * tokens stay the same.
		 */
		bool const now_keyboard_present = _substring(base, avail, "Keyboard");
		bool const now_mouse_present    = _substring(base, avail, "Mouse");

		if (now_keyboard_present && !_keyboard_seen) {
			_keyboard_seen = true;
			log("usb_hid: KEYBOARD detected");
			_done_detect = true;
		}

		if (!now_keyboard_present && _keyboard_seen) {
			log("usb_hid: KEYBOARD removed");
			_done_remove = true;
		}

		if (now_mouse_present && !_mouse_seen) {
			_mouse_seen = true;
			log("usb_hid: MOUSE detected");
			_mouse_done_detect = true;
		}

		if (!now_mouse_present && _mouse_seen) {
			log("usb_hid: MOUSE removed");
			_mouse_done_remove = true;
		}

		_keyboard_present = now_keyboard_present;
		_mouse_present    = now_mouse_present;
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


void Component::construct(Genode::Env &env) { static Main main(env); }

Genode::size_t Component::stack_size() { return 16 * 1024; }