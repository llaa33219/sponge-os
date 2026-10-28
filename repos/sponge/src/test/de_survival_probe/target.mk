# de_survival_probe — P2.1 DE package-control failure survival probe.
#
# Companion of run/sponge-de-pkgd-failure.run; marker grammar
# "de-survival-probe: ..." (contract: docs/evidence/phase16-runtime-
# de-failure-modes.md §4-§5, plan: docs/plans/phase16-runtime-overhaul.md
# P2.1). Plain Genode component (no Qt, no libc — AGENTS.md §3.1).
#
TARGET   := de_survival_probe
SRC_CC   := main.cc
LIBS     := base blit
