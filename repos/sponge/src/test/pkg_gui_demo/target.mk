# pkg_gui_demo — minimal Qt6 Widgets package payload (Phase 7 todo 8).
#
# The GUI counterpart of pkg_hello: the smallest possible Qt6 component
# that paints a distinctive solid-color window, so the runtime-config
# generator's <binary>/<config>/<parent/>-route/caps changes can be
# pixel-verified end to end. Built by qmake (same glue as sponge-de);
# see docs/12-package-format.md §4.1 and run/sponge-pkg-gui.run.
#
# Pattern B bootstrap (same as sponge-de): the component provides its
# own Libc::Component::construct, so the qt6_component auto-bootstrap
# lib is disabled via QT6_COMPONENT_LIB_SO=.

QMAKE_PROJECT_FILE = $(PRG_DIR)/pkg_gui_demo.pro

QMAKE_TARGET_BINARIES = pkg_gui_demo

QT6_PORT_LIBS = libQt6Core libQt6Gui libQt6Widgets

LIBS = qt6_qmake base libc libm mesa stdcxx qt6_component

QT6_COMPONENT_LIB_SO =

QT6_GENODE_LIBS_APP += ld.lib.so
qmake_prepared.tag: $(addprefix build_dependencies/lib/,$(QT6_GENODE_LIBS_APP))
