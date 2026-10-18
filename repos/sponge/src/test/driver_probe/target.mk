# driver_probe — driver-package install acceptance probe.
#
# Drives sponge_pkgd's install channel and then asserts the driver shows
# up in driver_runtime's sandbox state report. Used by
# run/sponge-pkg-driver.run.

TARGET   := driver_probe
SRC_CC   := main.cc
LIBS     := base
