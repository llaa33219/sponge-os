# usb_hid_mouse_probe — Phase 15 W5 USB-mouse HID envelope probe

> `docs/plans/phase15-real-hardware-boot.md` §W5 USB-mouse envelope.
> Companion to `repos/sponge/src/test/usb_hid_probe/` (which fires
> the `usb_hid: MOUSE detected/removed` lifecycle markers on the
> pc_usb_host devices report).

A tiny Genode component (no libc, no Qt) that observes three
observable sinks on the USB-mouse HID chain in parallel and emits
literal markers the run script's bounded `expect` arms can match:

| Sink                       | Marker (literal)                                         | When                                                                                                              |
|----------------------------|----------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------|
| "report" ROM (devices)     | `usb_hid_mouse_probe: devices report MOUSE present`      | first ROM update where the devices report contains "Mouse" (QMP device_add succeeded; pc_usb_host enumerated the device) |
| "report" ROM (devices)     | `usb_hid_mouse_probe: devices report MOUSE absent`       | the previously-present Mouse entry disappears on a subsequent report update (QMP device_del succeeded)            |
| "report" ROM (devices)     | `usb_hid_mouse_probe: POINTER_BIND observed`            | the same transition as MOUSE-present — the witness marker for usb_hid's Linux-hid-core `Connected device: ... POINTER' log line; emitted alongside MOUSE-present |
| "report" ROM (devices)     | `usb_hid_mouse_probe: POINTER_UNBIND observed`           | the same transition as MOUSE-absent — the witness marker for usb_hid's disconnect log line                       |
| "pointer" ROM (nitpicker)  | `usb_hid_mouse_probe: pointer ROM initial (hash=...)`    | the FIRST observation of nitpicker's pointer reporter — establishes the baseline content                          |
| "pointer" ROM (nitpicker)  | `usb_hid_mouse_probe: pointer ROM delta observed ...`    | a subsequent pointer ROM update changed the bytes — Phase 14 gap row #2 contradicted on this host                |
| "pointer" ROM (nitpicker)  | `usb_hid_mouse_probe: pointer ROM stable (hash=...)`     | a subsequent pointer ROM update was identical to the previous — Phase 14 gap row #2 confirmed (REL motion did NOT update the ROM) |

The probe does NOT exit on any marker; it keeps observing until
init tears it down at end-of-scenario.

## Why the pointer-ROM observation matters

Per Phase 14 row #2 / #12, nitpicker's pointer reporter only emits
on `result.motion_activity` triggered by an **absolute** Motion
event (genode/repos/os/src/server/nitpicker/main.cc:980-984 — the
reporter is gated by `_user_state`'s motion activity). A USB mouse
emits **relative** Motion events; on most host / driver chains the
relative → nitpicker path does NOT trip the activity threshold the
absolute path does, so the pointer ROM stays empty. On a host
where it DOES update, the gap row #2 does not hold. The probe
records the observation either way; the run script's secondary
`expect` arm surfaces it in the evidence log.

This is NOT a scenario-failure path — the load-bearing cell marker
is the `usb_hid: MOUSE detected` / `usb_hid: MOUSE removed` audit
chain (fired by the sibling `usb_hid_probe`), and the scenario PASS
line is emitted after that chain completes. The pointer-ROM
observation is documented evidence for the gap row, not a gating
condition.

## ROM source

Both ROMs come from the drivers sub-init's report_rom (see
`run/sponge-usb-hid-mouse.run:install_config`):

* `devices` — policy
  `label: usb_hid_mouse_probe -> devices | report: usb -> devices`
  (relays the pc_usb_host's device enumeration through the same
  report_rom the `usb_hid` child itself consumes).

* `pointer` — top-level report_rom policy
  `label: usb_hid_mouse_probe -> pointer | report: nitpicker -> pointer`
  (relays nitpicker's pointer reporter).

## Build / use

```
make -j1 -C genode/build/x86_64 run/sponge-usb-hid-mouse.run
```

AGENTS.md §3.1: qualified Genode types (`Genode::size_t`), snake_case
methods, PascalCase classes, no exceptions, `#pragma once` not
needed (single TU).