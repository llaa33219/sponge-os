# wm_tasks_probe — Phase 14 W7 window-task acceptance probe.
#
# A plain Genode component (Component::construct; no Qt, no libc). It
# drives the panel tasklist end-to-end through Genode's report_rom bus
# and verifies every state transition in docs/plans/wm-state-table.md.
#
# Output: "wm-tasks-probe: PASS" on success. Failures are explicit
# "wm-tasks-probe: FAIL <reason>" lines (each one exits 1 so the run
# script fails via run_genode_until timeout).
#
# Wire topology (per run/sponge-wm-tasks.run):
#
#   sponge_pkgd -> request     <-- wm_tasks_probe -> request  (install/launch)
#   sponge_pkgd -> result     --> wm_tasks_probe -> result   (status)
#   wm         -> window_list --> wm_tasks_probe -> window_list
#   layouter   -> window_layout -> wm_tasks_probe -> window_layout
#   tasklist_ctrl -> focus_request --> wm_tasks_probe -> focus_request  (read)
#   tasklist_ctrl -> rules        --> wm_tasks_probe -> rules         (read)
#
# The probe reads the rules + focus_request reports the tasklist
# controller writes (the controller is the SOLE writer; the probe
# observes). The probe also receives the window_list/ layout ROMs so
# it can verify the state transitions.
#
# On PASS, logs the exact key transition evidence plus the `wm-tasks-
# probe: PASS` marker. The run script (run/sponge-wm-tasks.run) gates
# on the marker AND the `Run script execution successful.` line.

TARGET   := wm_tasks_probe
SRC_CC   := main.cc
LIBS     := base
