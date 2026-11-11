# sponge_wm_protocol — Sponge-owned WM protocol v1 report/ROM shapes
# (docs/05-sponge-de.md §4.6, frozen 2026-11-12; Criterion 16, D16W.6).
#
# Shared protocol-side types, parsing, and generation helpers for the
# native WM components (sponge_wm emits window_list, sponge_layouter
# emits window_layout and consumes focus_request/rules/window_list).
# Pure shape handling only: no Env, no Report/ROM sessions, no
# File_system, no hardware access, no exceptions, no mutable global
# state. The caller owns every buffer.
#
# The public header is at repos/sponge/include/sponge/wm_protocol.h.
# The deterministic contract tests live in
# src/test/wm_protocol_probe/ (Genode test target 'wm_protocol_probe'
# plus the host-side self-test in the same directory, see its header
# comment for the build/run command); they are deliberately not built
# here.

SRC_CC := wm_protocol.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_wm_protocol
