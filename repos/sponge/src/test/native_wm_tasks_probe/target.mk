# native_wm_tasks_probe — native-WM tasklist/protocol acceptance probe.
#
# A plain Genode component (Component::construct; no Qt, no libc). It
# drives the DELIVERED panel tasklist against the native trio
# (sponge_wm + sponge_layouter + sponge_decorator) and verifies the
# frozen protocol v1 surface (docs/05-sponge-de.md §4.6) on every
# transition: window_list identity, window_layout parking/restore
# geometry, rules shape + target-relative assigns, and the
# focus_request verbatim-label contract.
#
# Output: "native-wm-tasks-probe: PASS" on success. Failures are
# explicit "native-wm-tasks-probe: FAIL <reason>" lines plus exit 1.
#
# Wire topology (per run/sponge-wm-native-tasks.run):
#
#   sponge_pkgd -> request     <-- native_wm_tasks_probe -> request
#   sponge_pkgd -> result     --> native_wm_tasks_probe -> result
#   sponge_wm -> window_list  --> native_wm_tasks_probe -> window_list
#   sponge_layouter -> window_layout -> ... -> window_layout
#   TasklistController -> focus_request/rules --> ... (read-only)
#
# Shared assertions live in ../native_wm_common (header-only).
#
# See main.cc for the state-machine sequence and marker contract.

TARGET   := native_wm_tasks_probe
SRC_CC   := main.cc
LIBS     := base blit
INC_DIR  := $(PRG_DIR)/../native_wm_common
