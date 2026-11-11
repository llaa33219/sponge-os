# native_wm_tiling_probe — native-WM Master-Stack / mode / reflow probe.
#
# A plain Genode component (no Qt, no libc). Drives the D16W.5 tiling
# acceptance (docs/plans/phase16-sponge-wm.md) against the native trio:
# Master-Stack placement, every reflow trigger of the state-table
# native rows, wm.layout.mode switching via the `vct config` backend
# (D16W.11), mode-switch geometry retention, and the >=20-cycle
# rules-drift soak (§7.2) with EXACT arrangement assertions.
#
# Output: "native-wm-tiling-probe: PASS". Failures are explicit
# "native-wm-tiling-probe: FAIL <reason>" lines plus exit 1.
#
# Wire topology (per run/sponge-wm-native-tiling.run): plus the
# sponge_configd config_request/config_result channel and the configd
# broadcast ROM (wm.layout.mode source for sponge_layouter).
#
# Shared assertions live in ../native_wm_common (header-only).

TARGET   := native_wm_tiling_probe
SRC_CC   := main.cc
LIBS     := base blit
INC_DIR  := $(PRG_DIR)/../native_wm_common
