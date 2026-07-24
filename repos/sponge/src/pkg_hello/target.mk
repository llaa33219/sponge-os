# pkg_hello — minimal package payload component (Phase 4b).
#
# The installable counterpart of pkg/hello. It is deliberately the
# smallest possible long-lived Genode component: it logs a marker and
# then sleeps forever. It needs only the parent-provided services
# (LOG/ROM/PD/CPU), which is why pkg/hello/metadata.xml declares no
# <sessions> (docs/12-package-format.md §4.1).
#
# The boot marker lets run/sponge-pkg-install.run verify that
# `vct install hello` actually started the component under the nested
# pkg_runtime init.

TARGET   := hello
SRC_CC   := main.cc
LIBS     := base
