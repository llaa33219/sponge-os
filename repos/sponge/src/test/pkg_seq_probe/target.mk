# pkg_seq_probe — config-driven sponge_pkgd sequence verifier.
#
# Executes a sequence of package operations declared in its config ROM
# (install/remove/list with optional assertions) against sponge_pkgd over
# the Report/ROM channel, since vct is short-lived and takes one command
# per boot. Used by run/sponge-pkg-remove.run and run/sponge-pkg-list.run.

TARGET   := pkg_seq_probe
SRC_CC   := main.cc
LIBS     := base
