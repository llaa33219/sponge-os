# sponge_files — minimal Qt6 Widgets file manager (Phase 7 todo 15).
#
# Pattern B bootstrap (same as sponge-de / pkg_gui_demo): the component
# provides its own Libc::Component::construct, so the qt6_component
# auto-bootstrap lib is disabled via QT6_COMPONENT_LIB_SO=.
#
# Built by qmake (not the normal SRC_CC mechanism). The qmake glue is
# pulled in via the qt6_qmake dummy library; see sponge-de/target.mk
# for the canonical pattern. File-system access is via the libc POSIX
# API (opendir/readdir/open/read/unlink) against the <vfs> node the
# metadata <config> mounts — no Genode Vfs library direct use.

QMAKE_PROJECT_FILE = $(PRG_DIR)/sponge_files.pro

QMAKE_TARGET_BINARIES = sponge_files

QT6_PORT_LIBS = libQt6Core libQt6Gui libQt6Widgets

LIBS = qt6_qmake base libc libm mesa stdcxx qt6_component

QT6_COMPONENT_LIB_SO =

QT6_GENODE_LIBS_APP += ld.lib.so
qmake_prepared.tag: $(addprefix build_dependencies/lib/,$(QT6_GENODE_LIBS_APP))
