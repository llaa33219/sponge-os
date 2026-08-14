# leak_audit_probe — Phase 14 W11 #47-50 QTimer leak audit probe.
#
# Plain Genode component (no Qt, no libc). Drives 200 cycles of
# theme + config reload against sponge-de via the configd
# config_request channel, snapshots init RAM before and after,
# and emits a bounded-growth assertion. The 1 MiB ΔRAM threshold
# catches any per-cycle QTimer leak; the destructors added in
# W11 #47-50 close the destruction-boundary race that would
# otherwise leak a queued timeout per cycle.
#
# See main.cc for the wire contract + the parsing logic.

TARGET   := leak_audit_probe
SRC_CC   := main.cc
LIBS     := base