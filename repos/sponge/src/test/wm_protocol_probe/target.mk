# wm_protocol_probe — deterministic WM protocol v1 contract tests for
# sponge_wm_protocol (docs/05-sponge-de.md §4.6, frozen 2026-11-12;
# Criterion 16, D16W.6/M0).
#
# Plain Genode component (no libc, no Qt — AGENTS.md §3.1) with an
# EMPTY capability surface: every case is a pure call into
# sponge_wm_protocol against in-memory buffers (Xml_generator/
# Hid_generator generation, Genode::Node parsing). No ROM, Report, or
# Timer sessions, so the probe is scenario-independent and
# deterministic.
#
# Output marker: "wm-protocol-probe: PASS" (or
# "wm-protocol-probe: FAIL (<n> failed checks)" with exit code 1).
#
# The identical case list runs host-side without a Genode runtime via
# self_test.cc in this directory (see its header comment for the
# build/run command).

TARGET   := wm_protocol_probe
SRC_CC   := main.cc contract_tests.cc
LIBS     := base sponge_wm_protocol
