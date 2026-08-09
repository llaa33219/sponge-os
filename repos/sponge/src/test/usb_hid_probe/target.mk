# usb_hid_probe — Phase 12 W4 USB HID hotplug probe
#
# Tiny Genode component (no libc, no Qt) that watches the pc_usb_host
# devices report (relayed via the drivers sub-init's report_rom under
# the policy "label: usb_hid -> report | report: usb -> devices")
# and emits two lifecycle markers for a USB keyboard device entry:
#
#   usb_hid: KEYBOARD detected   when a device entry containing
#                                 "Keyboard" first appears in the
#                                 report (QMP device_add succeeded)
#   usb_hid: KEYBOARD removed    when the previously-present
#                                 "Keyboard" entry disappears on a
#                                 subsequent report update (QMP
#                                 device_del succeeded)
#
# Both markers are emitted exactly once per lifecycle; the probe
# keeps observing on every subsequent update without re-emitting.
# The probe does NOT exit — init tears it down at end-of-scenario.
#
# See README.md for the run-script contract and the substring-search
# rationale.
#
# AGENTS.md §3.1: qualified Genode types (Genode::size_t), snake_case
# methods, PascalCase classes, no exceptions, #pragma once not needed
# (single TU).

TARGET   := usb_hid_probe
SRC_CC   := main.cc
LIBS     := base