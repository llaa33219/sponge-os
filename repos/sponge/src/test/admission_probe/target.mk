# \brief  Admission-control probe for sponge_pkgd resource pre-flight
# \author Sponge OS contributors
# \date   2026
#
# Purpose
# -------
# Verifies that sponge_pkgd REFUSES to spawn a package whose declared
# quota does not fit in pkg_runtime's remaining RAM/caps budget
# (launch result token "insufficient-resources"), and that the refusal
# is a clean no-op rather than a collateral event:
#
#   1. the refused package does NOT enter the running set (the
#      "list" broadcast still reports running="no"),
#   2. an already-running witness package KEEPS running and keeps
#      counting (its monotonic beat counter must not reset),
#   3. freeing the budget (removing the hog) makes the very same
#      launch succeed — the gate is a live budget check, not a latch.
#
# The numbers sponge_pkgd reports (need_ram / avail_ram / need_caps /
# avail_caps) are cross-checked against the pkg_runtime state report the
# probe observes itself, so a refusal that cites the wrong budget fails.
#
# Driven by run/sponge-pkg-admission.run. Gates on
# "admission-probe: PASS" / "admission-probe: FAIL <reason>".

TARGET   := admission_probe
SRC_CC   := main.cc
LIBS     := base