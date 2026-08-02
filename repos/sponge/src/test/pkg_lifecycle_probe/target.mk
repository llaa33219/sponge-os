# pkg_lifecycle_probe — Phase 7 todo 9 installed-vs-running lifecycle verifier.
#
# Drives sponge_pkgd through the install/launch/remove lifecycle over the
# Report/ROM channel AND observes the "installed" broadcast ROM to assert
# per-package running="yes"|"no" state. Used by run/sponge-pkg-lifecycle.run.

TARGET   := pkg_lifecycle_probe
SRC_CC   := main.cc
LIBS     := base
