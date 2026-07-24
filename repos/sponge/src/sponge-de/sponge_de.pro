# Sponge DE qmake project file.
#
# Links only the Qt modules actually used (AGENTS.md §3.4: minimize Qt
# dependencies). Phase 3 needs Core, Gui, and Widgets only.

QT       += core gui widgets
TEMPLATE  = app
TARGET    = sponge-de
CONFIG   += c++2a

# The cproc/qt6_api port (issue5854_2) enables the 'permissions' feature in
# mkspecs/modules/qt_lib_core.pri but does not ship mkspecs/features/
# permissions.prf, so qmake aborts at qt.prf with "Cannot find feature
# permissions". The feature is only relevant to Apple/Android packaging;
# dropping it is harmless for a Genode component.
QT_CONFIG -= permissions

# $$PWD            — component-root includes ("theme/theme_loader.h", ...)
# $$PWD/../../include — Sponge shared headers (<sponge/version.h>)
INCLUDEPATH += $$PWD \
               $$PWD/../../include

SOURCES  += main.cc \
            sponge_de_main.cc \
            panel/panel_widget.cc \
            theme/theme_loader.cc \
            theme/theme_controller.cc

HEADERS  += sponge_de_main.h \
            panel/panel_widget.h \
            theme/theme_loader.h \
            theme/theme_qt.h \
            theme/theme_controller.h
