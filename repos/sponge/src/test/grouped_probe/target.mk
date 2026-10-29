# \brief  Grouped-runtime + per-class admission probe for sponge_pkgd
# \author Sponge OS contributors
# \date   2026
#
# Purpose
# -------
# Verifies the P2.4 grouped emission and per-class admission contract
# end to end on one bounded headless boot
# (run/sponge-pkg-grouped.run):
#
#   1. class placement/isolation: each fixture package's <start> node
#      appears in its own class config (runtime_simple / runtime_
#      terminal / runtime_browser / driver_runtime) and in no other,
#      and each class runtime really starts its own child (beat),
#   2. per-class budget refusal figures: a terminal launch that does
#      not fit terminal's budget is refused citing terminal's OWN
#      report while simple still has ample budget,
#   3. malformed app quota: refused before any mutation (zero-figure
#      'insufficient-resources'), with a later valid install/launch
#      still succeeding — no emission wedge,
#   4. mixed-class emission: with the simple render corrupted at that
#      write (repro_malformed_from), the publication seam publishes
#      NOTHING for either dirty class and the running children are
#      undisturbed.
#
# Driven by run/sponge-pkg-grouped.run. Gates on
# "grouped-runtime-probe: PASS" / "grouped-runtime-probe: FAIL <reason>".

TARGET   := grouped_probe
SRC_CC   := main.cc
LIBS     := base
