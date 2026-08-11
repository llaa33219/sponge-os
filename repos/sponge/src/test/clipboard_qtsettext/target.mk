# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# clipboard_qtsettext — Phase 14 W5 follow-on Qt-side write probe.
#
# Decoupled Qt-side harness: drives
# `QGuiApplication::clipboard()->setText(...)` from a synthetic
# Qt main loop (QTimer::singleShot, no widgets, no shortcuts, no
# focus), bypassing every keyboard-chain / shortcut-register /
# QTextEdit-priority variable that the §0 evidence log already
# ruled out for textedit's failing write.
#
# The harness's Report + ROM session routes match the
# sponge-de start-node style verbatim (see run/sponge-clipboard.run
# start "sponge-de" report/rom label_last="clipboard" -> child
# clipboard). The harness is a direct child of init, same domain
# ("default") as textedit in the failing qtwrite scenario, so the
# upstream server's write_permitted check accepts it.
#
# Built by qmake (same glue as sponge-de); Pattern B bootstrap
# (own Libc::Component::construct, qt6_component auto-bootstrap lib
# disabled via QT6_COMPONENT_LIB_SO=).

QMAKE_PROJECT_FILE = $(PRG_DIR)/clipboard_qtsettext.pro

QMAKE_TARGET_BINARIES = clipboard_qtsettext

QT6_PORT_LIBS = libQt6Core libQt6Gui

LIBS = qt6_qmake base libc libm mesa stdcxx qt6_component

QT6_COMPONENT_LIB_SO =

QT6_GENODE_LIBS_APP += ld.lib.so
qmake_prepared.tag: $(addprefix build_dependencies/lib/,$(QT6_GENODE_LIBS_APP))
