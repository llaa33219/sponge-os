# grouped_ui_probe — P2.4 D16R.11 grouped-runtime UI acceptance probe.
#
# Plain Genode component (no Qt, no libc). Observes the product grouped
# topology (window_list / window_layout / focus / class state reports +
# Capture pixels) and emits the run/qmp.inc QMP-TARGET marker
# choreography; the host drives real QMP mouse/keyboard through the
# Sponge DE launcher. Asserts per runtime class: launcher-entry click ->
# first paint + typed-input delta, with the window label bound to
# pkg_runtime_simple / pkg_runtime_terminal. Logs exactly
# "grouped-ui-probe: PASS" after both classes pass; any failure logs
# "grouped-ui-probe: FAIL <reason>" and exits non-zero (fail-loud).
#
# sponge_backend_client: the shared Report/ROM client for the ONE
# sanctioned pkgd call this probe makes (P5.1 falkon teardown `remove`
# on the probe_request channel — see _remove_falkon).

TARGET   := grouped_ui_probe
SRC_CC   := main.cc
LIBS     := base sponge_backend_client
INC_DIR += $(REP_DIR)/include
