# repostorm_probe — per-child resource-growth measurement for the
# SPONGE DEBUG scroll-storm reproduction.
#
# Plain Genode component (no Qt, no libc). Subscribes to the sandbox
# state report (init's periodic <state>, armed with child_ram/child_caps)
# and samples EVERY child's ram/caps usage once per report_ms. It prints
# one line per changed child per tick, then a final per-child table and
# a single VERDICT line carrying the largest per-child growth and the
# name of the component that produced it.
#
# The point of the component is the NAME in the verdict: growth in
# exactly one child localizes a resource leak; growth nowhere localizes
# shared-substrate saturation (docs/17-runtime-component-topology.md
# section 4). See main.cc for the wire contract.

TARGET   := repostorm_probe
SRC_CC   := main.cc
LIBS     := base