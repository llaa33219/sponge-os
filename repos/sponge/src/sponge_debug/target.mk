# sponge_debug — the SPONGE DEBUG diagnostic package component.
#
# Pattern B bootstrap (same as sponge_files / sponge-de): the component
# provides its own Libc::Component::construct, so the qt6_component
# auto-bootstrap lib is disabled via QT6_COMPONENT_LIB_SO=.
#
# The app is a read-only system monitor: it subscribes to the report_rom
# relays (pkgd's installed set + launcher results, the wm's window list)
# and renders every update in a scrolling log view, with Mark/Clear
# buttons so an on-site tester can correlate launcher clicks with the
# session activity that follows.

QMAKE_PROJECT_FILE = $(PRG_DIR)/sponge_debug.pro

QMAKE_TARGET_BINARIES = sponge_debug

QT6_PORT_LIBS = libQt6Core libQt6Gui libQt6Widgets

LIBS = qt6_qmake base libc libm mesa stdcxx qt6_component

QT6_COMPONENT_LIB_SO =

QT6_GENODE_LIBS_APP += ld.lib.so
qmake_prepared.tag: $(addprefix build_dependencies/lib/,$(QT6_GENODE_LIBS_APP))
