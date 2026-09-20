# SPDX-License-Identifier: Apache-2.0
#
# configd_registry_probe — sponge_configd registry-extension acceptance
# probe (Phase 16 W2).
#
# Plain Genode component (no libc, no Qt — AGENTS.md §3.1). It exercises
# sponge_configd's Phase-16 W2 registry extensions end-to-end:
#
#   - Pattern keys (U16.5 / D16.5): the probe writes `panel.ids` then
#     `panel.<id>.height` with a valid id, asserts the broadcast carries
#     the per-id key (proves the pattern registry instantiated the slot
#     on first write), then writes a `panel.<id>.height` whose id
#     violates the `[a-z0-9_-]{1,16}` charset and asserts the error
#     reply mentions the charset rule.
#
#   - Structured shortcuts key (U16.4 / D16.5/D16.10): the probe writes
#     a well-formed multi-line binding list (action<TAB>sequence per
#     line), asserts the broadcast carries the value, then writes a
#     line with an unknown action token and asserts the error.
#
#   - Validator parity (F15): the probe writes a typo'd
#     `panel.visble_widgets=clock` and asserts the structured error
#     reply suggests `panel.visible_widgets` (the established
#     validator parity path).
#
# One probe serves all three scenarios (selected via
# `<config phase="..."/>`). Each scenario gates on its specific PASS
# marker:
#
#   run/sponge-configd-pattern-keys.run  -> "pattern-keys-probe: PASS"
#   run/sponge-configd-shortcuts.run    -> "shortcuts-probe: PASS"
#   run/sponge-configd-badkey.run        -> "badkey-probe: PASS"
#
# Capability surface: Report (config_request) + ROM (config_result,
# broadcast) + Timer. No Capture, no GUI.
#

TARGET   := configd_registry_probe
SRC_CC   := main.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include