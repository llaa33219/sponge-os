# usb_hid_probe — Phase 12 W4 USB HID hotplug probe

> `docs/plans/phase12-hardware.md` §"W4: USB boot and USB keyboard
> scenarios", Risk 20 mitigation.

A tiny Genode component (no libc, no Qt) that watches the
`pc_usb_host` devices report relayed through the drivers sub-init's
report_rom (policy `label: usb_hid -> report | report: usb ->
devices`) and emits two literal markers on the lifecycle of a USB
keyboard device entry:

| Transition | Marker (literal)              | When                                |
|------------|------------------------------|--------------------------------------|
| add        | `usb_hid: KEYBOARD detected` | a device entry containing "Keyboard" first appears in the report |
| remove     | `usb_hid: KEYBOARD removed`  | the previously-present Keyboard entry disappears on a subsequent report update |

The probe does NOT exit on either marker; both events are observed
transitions on a single continuous probe. It is torn down by
init at end-of-scenario.

## What it does

1. Opens ROM module `"report"` (served by the drivers sub-init's
   report_rom after the standard `usb_hid -> report` policy wires
   the pc_usb_host devices reporter).
2. Subscribes to ROM update signals.
3. On every update: scans the new content for the ASCII substring
   `"Keyboard"`. The pc_usb_host devices report emits the device
   name in `<device name="...">` attributes
   (`genode/repos/pc/src/driver/usb_host/pc/README` documents the
   format). The QEMU usb-kbd device model surfaces under the
   bus-id assigned at hotplug time, with "Keyboard" as part of the
   surfaced name.
4. Logs `usb_hid: KEYBOARD detected` exactly once when "Keyboard"
   transitions from absent to present.
5. Logs `usb_hid: KEYBOARD removed` exactly once when "Keyboard"
   transitions from present to absent.
6. Never logs the same marker twice — keeps observing on every
   subsequent update without re-emitting.

## Why the substring search

A future-proof search across the whole ROM is robust against
schema evolution. As long as the QEMU usb-kbd device model's name
or product string still contains "Keyboard", the substring catches
it. The pc_usb_host report schema (genode/repos/pc/src/driver/usb_
host/pc/README:18-66) has `<device name="..." speed="..." vendor_id=
"..." product_id="..." class="...">` plus nested `<config>` /
`<interface>` blocks; the substring scan covers all of them.

## Run-script contract

```tcl
# drivers sub-init config (vendored recipes/raw/drivers_interactive-pc/):
+ start drivers ...
    + start report_rom
      + config
        + policy | label: usb_hid -> report | report: usb -> devices
        ...

+ start usb_hid ...
+ start usb_hid_probe | caps: 100 | ram: 4M
  + route
    + service ROM | label: report | + child drivers
    + service ROM | label_last: ld.lib.so | + parent
    + any-service
      + parent
      + any-child
```

The probe's `report` ROM session must be satisfied by the drivers
sub-init's `report_rom` (the same ROM the `usb_hid` child consumes
— `pc_usb_host` is the writer, `report_rom` is the relay).

## Bounded run-script gates

The run script gates the hotplug choreography on these literal
markers; every wait is bounded (Phase 12 plan: fail-loud on
timeout, never a silent hang):

```tcl
# after QMP device_add named usb-kbd:
run_genode_until {.*usb_hid: KEYBOARD detected.*} 30

# after QMP device_del:
run_genode_until {.*usb_hid: KEYBOARD removed.*} 30

# after QMP send-key (post-removal, the existing PS/2 keyboard
# chain carries the keys):
run_genode_until {.*sponge-usb-kbd-via-qmp: PASS.*} 600
```

A PS/2-only run (no QMP device_add) cannot satisfy the
`usb_hid: KEYBOARD detected` gate — the probe only emits that
marker when a Keyboard entry actually appears in the report. The
plan's "A PS/2-only pass must not satisfy the KEYBOARD detection
gate" requirement is enforced by this gate being a hard
prerequisite for the subsequent send-key + glyph-delta steps.

## Failure modes

| Symptom | Probe behavior | Run script behavior |
|---|---|---|
| ROM never becomes valid (drivers sub-init broken) | Logs `usb_hid_probe: FAIL: rom_invalid` after MAX_UPDATE_RETRIES retries; never emits the lifecycle markers | Bounded `run_genode_until` timeout on `usb_hid: KEYBOARD detected` |
| QMP device_add rejected by QEMU (no Keyboard entry appears) | Probe never emits `KEYBOARD detected` | Bounded `run_genode_until` timeout on `usb_hid: KEYBOARD detected` — fail-loud |
| QMP device_del rejected by QEMU (Keyboard entry persists) | Probe never emits `KEYBOARD removed` | Bounded `run_genode_until` timeout on `usb_hid: KEYBOARD removed` — fail-loud |
| A non-keyboard USB device (mouse, tablet) appears in the report | "Keyboard" substring absent — no transition logged | Test still passes (the marker is a function of "Keyboard" only) |

AGENTS.md §3.1: qualified Genode types (`Genode::size_t`), snake_case
methods, PascalCase classes, no exceptions, `#pragma once` not
needed (single TU).