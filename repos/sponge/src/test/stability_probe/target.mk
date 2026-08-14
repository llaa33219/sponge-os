# stability_probe — Phase 14 W9 stability acceptance probe.
#
# Plain Genode component (no Qt, no libc, AGENTS.md §3.1). Long-running
# workload driver that exercises the sponge-de + sponge_pkgd +
# sponge_configd + sponge_themed + sponge_notifier Report/ROM bus
# end-to-end, repeatedly, and emits one "stability-probe: cycle N PASS"
# marker per cycle.
#
# Topology (per run/sponge-de-stability.run):
#
#   sponge_pkgd     -> request       <-- stability_probe -> request       (install/launch/remove)
#   sponge_pkgd     -> result        --> stability_probe -> result        (status read)
#   sponge_configd  -> config_request <-- stability_probe -> config_request (set theme.active)
#   sponge_configd  -> config_result  --> stability_probe -> config_result (status read)
#   sponge_configd  -> config         --> stability_probe -> config        (broadcast, observes theme.active)
#   sponge_notifier -> notif_request  <-- stability_probe -> notif_request (post notification)
#   sponge_notifier -> notifications  --> stability_probe -> notifications (read back)
#   wm              -> window_list    --> stability_probe -> window_list   (first-paint + teardown gate)
#
# Config (own <config> ROM, parsed at construct):
#   <stability_probe max_cycles="N" fail_at_cycle="M"/>
#     max_cycles   - 0 (default) = run until wall-clock budget; >0 = stop + PASS at this cycle
#     fail_at_cycle - 0 (default) = never fail; >0 = FAIL + exit 1 when cycle reaches this
#
# The fastfail run script sets fail_at_cycle="3" so the probe self-
# terminates with FAIL after cycle 3, proving the crash-detection path.
# The full stability run leaves both at 0 and lets the wall-clock
# budget (1800s) decide.
#
# One cycle:
#   1. install pkg_gui_demo           (idempotent — already-installed returns ok)
#   2. launch pkg_gui_demo            (idempotent)
#   3. wait for pkg_gui_demo in window_list (first-paint gate)
#   4. set theme.active=default       (write configd; observe broadcast)
#   5. post a notification            (write notifier; observe in broadcast)
#   6. remove pkg_gui_demo            (closes the window)
#   7. wait for pkg_gui_demo absent from window_list
#   8. log "stability-probe: cycle N PASS"
#
# Snapshots (log line at fixed wall-clock offsets for run-script audit):
#   t=0      (logged at the first cycle)
#   t=600s   (10 min)
#   t=1200s  (20 min)
#   t=1800s  (30 min — only logged if the run reaches that point)
#
# Output:
#   "stability-probe: cycle N PASS" — one per completed cycle
#   "stability-probe: snapshot t=Xs cycle=N elapsed=Ys" — at the four points
#   "stability-probe: PASS" — final, before exit 0
#   "stability-probe: FAIL <reason>" — any non-recoverable error
#
# Capability surface: Report (request writers for pkgd/configd/notifier),
# ROM (result readers for pkgd/configd; broadcast readers for
# configd/notifier/window_list; own <config>), Timer.

TARGET   := stability_probe
SRC_CC   := main.cc
LIBS     := base
