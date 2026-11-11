# native_wm_qmp_probe — native-WM QMP acceptance battery probe.
#
# A plain Genode component (no Qt, no libc). It drives the §7.1
# acceptance battery (docs/plans/phase16-sponge-wm.md) against the
# native trio: move, full 8-zone resize in one run, focus flip,
# chrome minimize/restore+focus, chrome maximize/restore (with the
# frozen rules maximized-inference assertions), and chrome close.
#
# Output: "native-wm-qmp-probe: PASS". Failures are explicit
# "native-wm-qmp-probe: FAIL <reason>" lines plus exit 1.
#
# Marker contract: QMP-TARGET click/drag lines, dispatched by the
# SHARED run/qmp.inc qmp_exec_target (no local Tcl QMP procs in the
# run scenario). See main.cc for the serialized step sequence.
#
# Shared assertions live in ../native_wm_common (header-only).
# Capture (blit) is used for the per-step green-pixel corroboration
# (misleading_success_output defense).

TARGET   := native_wm_qmp_probe
SRC_CC   := main.cc
LIBS     := base blit
INC_DIR  := $(PRG_DIR)/../native_wm_common
