# sponge_files qmake project file.
#
# Links only the Qt modules actually used (AGENTS.md §3.4): Core, Gui,
# Widgets. No Network/PrintSupport/Sql/etc — Alpha scope is a minimal
# file manager with no rename-undo, no search, no drag-drop.

QT       += core gui widgets
TEMPLATE  = app
TARGET    = sponge_files
CONFIG   += c++2a

# Same cproc/qt6_api port workaround as sponge-de.pro / pkg_gui_demo.pro:
# the 'permissions' feature is enabled by the port but its .prf is not
# shipped, so qmake would abort. Dropping it is harmless for a Genode
# component.
QT_CONFIG -= permissions

INCLUDEPATH += $$PWD

SOURCES  += main.cc \
            files_window.cc \
            theme/theme_loader.cc

HEADERS  += files_window.h \
            theme/theme_loader.h
