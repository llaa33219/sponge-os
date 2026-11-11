# native_wm_probe — native-WM boot / window-lifecycle smoke probe.
#
# Plain Genode component (no Qt, no libc). Drives the M1 acceptance
# gate of docs/plans/phase16-sponge-wm.md against the NATIVE trio
# (sponge_wm + sponge_layouter + sponge_decorator): window_list
# protocol-v1 shape, window placement, render, and a synthetic
# title-bar drag (nitpicker Event injection).
#
# Output: "native-wm-probe: PASS" on success. Failures are explicit
# "native-wm-probe: FAIL <reason>" lines plus exit 1 (the run script's
# bounded run_genode_until then fails loud).
#
# Shared assertions live in ../native_wm_common (header-only).
#
# See run/sponge-wm-native.run for the topology and gates.

TARGET   := native_wm_probe
SRC_CC   := main.cc
LIBS     := base blit
INC_DIR  := $(PRG_DIR)/../native_wm_common
