# usb_hid_mouse_probe — Phase 15 W5 USB-mouse HID envelope probe
#
# See README.md for the rationale. This probe observes the
# pc_usb_host devices report and nitpicker's pointer reporter
# in parallel and emits witness markers the run script's
# `expect` arms can match.
#
# AGENTS.md §3.1: qualified Genode types (Genode::size_t),
# snake_case methods, PascalCase classes, no exceptions, #pragma once
# not needed (single TU).

TARGET   := usb_hid_mouse_probe
SRC_CC   := main.cc
LIBS     := base