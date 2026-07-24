# pkg_seq_probe — install/remove lifecycle test driver (Phase 4b).
#
# vct is short-lived (one command per boot), so it cannot exercise
# install-then-remove in a single run. This headless probe drives the
# Report/ROM channel directly: it installs hello, confirms the result,
# removes hello, confirms the result, then logs PASS. It plays the same
# role for the package backend that sponge_de_probe plays for the DE.

TARGET   := pkg_seq_probe
SRC_CC   := main.cc
LIBS     := base
