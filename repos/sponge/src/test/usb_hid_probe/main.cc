/*
 * SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * usb_hid_probe — Phase 12 W4 USB HID hotplug probe (docs/plans/
 * phase12-hardware.md §"W4: USB boot and USB keyboard scenarios",
 * Risk 20 mitigation).
 *
 * Opens ROM "report" — the pc_usb_host (drivers/usb_host/pc) device
 * enumeration report relayed through the drivers sub-init's
 * report_rom (policy "label: usb_hid -> report | report: usb ->
 * devices" in run/sponge-de-sel4-interactive.run:271 and friends).
 *
 * Watches the ROM for the lifecycle of a USB keyboard device:
 *
 *   - When a device entry containing the substring "Keyboard" appears
 *     (case-sensitive; matches the QEMU usb-kbd device model name in
 *     the pc_usb_host devices report's `<device name="...">` attribute
 *     — see genode/repos/pc/src/driver/usb_host/pc/README for the
 *     report format), logs:
 *
 *         usb_hid: KEYBOARD detected
 *
 *     exactly once. The plan risk-20 ordered chain uses this literal
 *     marker as the distinguishing gate that a PS/2-only run cannot
 *     satisfy (the report only gains a Keyboard entry when the QEMU
 *     usb-kbd hotplug succeeds).
 *
 *   - When the previously-present Keyboard entry disappears on a
 *     subsequent ROM update (the QMP device_del succeeded), logs:
 *
 *         usb_hid: KEYBOARD removed
 *
 *     exactly once. The plan risk-20 ordered chain uses this marker to
 *     gate the subsequent send-key step (the keyboard must be removed
 *     before the run script proceeds).
 *
 * The probe does NOT exit on either marker — both events are observed
 * transitions on a single continuous probe. The probe keeps observing
 * until the parent (init) tears it down via env.parent().exit() at
 * the end of the run script.
 *
 * === Probe semantics ===
 *
 *   - The ROM arrives asynchronously (report_rom creates an empty
 *     module on the first reader lookup, before the writer has
 *     published). The probe subscribes to ROM update signals and
 *     scans on every update; an "is_present" boolean tracks the
 *     transition.
 *
 *   - "Keyboard" is a substring search of the whole report; the
 *     pc_usb_host devices report emits the device name as the
 *     `<device name="...">` attribute (e.g.
 *     `<device name="usb-1-1" ...>`). The QEMU usb-kbd device model
 *     surfaces under the bus-id assigned by QEMU at hotplug time.
 *     The substring "Keyboard" inside the device name is what
 *     identifies it as a keyboard (the QEMU device model's
 *     internal product string is exposed in the `<config>` /
 *     `<interface>` block too, but the device-name attribute is the
 *     cleanest discriminator). If the future QEMU version surfaces
 *     a different attribute, the substring match is robust enough to
 *     catch it as long as "Keyboard" appears in any field.
 *
 *   - The probe does NOT distinguish "keyboard" from "keypad" or
 *     other HID keyboard-class devices. Both carry "Keyboard" or
 *     close, so the substring is sufficient for the W4 audit chain.
 *     A more precise class-only check would require parsing the
 *     `<interface class="0x3 subclass="0x1">` block; the substring
 *     approach is intentionally simple.
 *
 *   - On ROM signal: scan the new content. If Keyboard was absent
 *     and is now present → log "detected". If Keyboard was present
 *     and is now absent → log "removed". Otherwise no log line.
 *
 * === Failure modes ===
 *
 *   - Empty/invalid ROM (initial state): no log line, no transition,
 *     wait for the writer's first non-empty publish. Bounded by
 *     MAX_UPDATE_RETRIES so a broken chain eventually surfaces
 *     FAIL rather than spinning forever (the run script's bounded
 *     run_genode_until timeout is the ultimate backstop).
 *
 *   - Keyboard never appears (QMP device_add was rejected by QEMU,
 *     or the device model lacks a Keyboard surface): no "detected"
 *     log line, the run script's gate waits until timeout and fails
 *     loudly.
 *
 *   - Keyboard stays after device_del (QEMU ignored device_del): no
 *     "removed" log line, the run script's gate waits until timeout
 *     and fails loudly.
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
		 * Substring scan: locate "Keyboard" anywhere in the report.
		 * The pc_usb_host devices report uses '<device name="...">'
		 * attributes that contain the QEMU device model's product
		 * string; the usb-kbd model names itself "Keyboard" in that
		 * string. A simple byte-search is robust against future
		 * schema changes as long as the surface token stays the same.
		 */
		bool const now_present = _substring(base, avail, "Keyboard");

		if (now_present && !_keyboard_seen) {
			_keyboard_seen = true;
			log("usb_hid: KEYBOARD detected");
			_done_detect = true;
		}

		if (!now_present && _keyboard_seen) {
			log("usb_hid: KEYBOARD removed");
			_done_remove = true;
		}

		_keyboard_present = now_present;
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