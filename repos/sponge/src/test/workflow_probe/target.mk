# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# workflow_probe — Phase 14 W8 everyday-workflow acceptance probe.
#
# Plain Genode component (Component::construct, no libc, no Qt —
# AGENTS.md §3.1). Drives the seven-step sequence in
# docs/plans/phase14-daily-desktop.md §W8 against the live Alpha
# desktop stack (sponge-de + tasklist + clipboard + qtsettext harness).
# Emits the QMP-TARGET markers that the host run script catches to
# dispatch host-driven input (clicks, keystrokes), and verifies each
# step via structural reports (window_list, window_layout, focus_request,
# rules, installed) and Capture-based pixel checks (terminal glyph
# growth, textedit content delta, calculator render).
#
# Step 7 publishes <system state="poweroff"/> via a Report session so
# the acpica child (inside the drivers sub-init, consuming the "system"
# ROM via a route chain through the outer init's report_rom) acts on
# AcpiEnterSleepState(5) -> QEMU exits. The audit line "vct: shutdown:
# requesting poweroff" matches the sponge-power.run convention so the
# run script can gate on it via run_genode_until (fail-loud).
#
# Capability surface (capability-minimal per AGENTS.md §1.2):
#   - Report at "request"                           (write to sponge_pkgd)
#   - Report at "system"                            (write to acpica's "system" ROM)
#   - ROM    at "result"                            (read sponge_pkgd result)
#   - ROM    at "installed"                         (read sponge_pkgd installed)
#   - ROM    at "window_list"                       (read wm's window list)
#   - ROM    at "window_layout"                     (read layouter's window layout)
#   - ROM    at "focus_request"                     (read sponge-de's focus request)
#   - ROM    at "rules"                             (read sponge-de's layouter rules)
#   - ROM    at "clipboard"                         (read upstream clipboard bus)
#   - Capture                                        (read composited nitpicker pixels)
#   - Timer                                          (poll cadence)

TARGET   := workflow_probe
SRC_CC   := main.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)/include
