# churn_probe — containment-churn measurement for a Genode sandbox.
#
# A Tier-0 observer that lives OUTSIDE the sandbox under test and reads
# nothing but that sandbox's periodic <state> report. It answers what
# docs/17-runtime-component-topology.md §5 row "Respawn budget and
# clamp leak" asks: when init abandons and respawns a wedged child over
# and over, what exhausts, and does it stop?
#
# The measurement that matters is the sandbox's OWN remaining RAM, not
# any single child's usage: a leak in the abandon/respawn path debits the
# container without ever appearing as a growing child. See main.cc for
# the wire contract.

TARGET   := churn_probe
SRC_CC   := main.cc
LIBS     := base