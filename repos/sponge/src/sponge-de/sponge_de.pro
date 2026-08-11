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
# $$PWD/../../include — Sponge shared headers (<sponge/version.h>,
#                       <sponge/backend_client.h>)
# $$PWD/../../lib/src/sponge_backend_client — shared Report/ROM client
#     source compiled directly into sponge-de (qmake simplicity; the
#     same sources are also packaged as the sponge_backend_client.lib.a
#     used by vct and other plain Genode components).
INCLUDEPATH += $$PWD \
               $$PWD/../../include \
               $$PWD/../../lib/src/sponge_backend_client

SOURCES  += main.cc \
            sponge_de_main.cc \
            panel/panel_widget.cc \
            panel/notifier_widget.cc \
            panel/notifier_controller.cc \
            panel/notify_poster.cc \
            launcher/launcher_controller.cc \
            launcher/launcher_menu_view.cc \
            theme/theme_loader.cc \
            theme/theme_controller.cc \
            config/config_controller.cc \
            ../../lib/src/sponge_backend_client/backend_client.cc

HEADERS  += sponge_de_main.h \
            panel/panel_widget.h \
            panel/notifier_widget.h \
            panel/notifier_controller.h \
            panel/notify_poster.h \
            launcher/launcher_controller.h \
            launcher/launcher_menu_view.h \
            theme/theme_loader.h \
            theme/theme_qt.h \
            theme/theme_controller.h \
            config/config_controller.h
