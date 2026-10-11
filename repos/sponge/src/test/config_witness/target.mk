# config_witness — the configuration-regeneration survival witness.
#
# A package payload whose only observable behaviour is a monotonically
# increasing counter published on its Report session ("beat"). The
# counter starts at 1 on every process start, so an observer can tell
# "the child survived" (counter keeps rising) from "the child was killed
# and recreated" (counter jumps back to 1).
#
# Used by run/sponge-config-regen-survival.run, which installs this
# package (autostart) and then forces sponge_pkgd to regenerate the
# pkg_runtime config across the Expanding_reporter buffer boundary,
# asserting via regen_probe that the witness was never restarted.

TARGET   := config_witness
SRC_CC   := main.cc
LIBS     := base