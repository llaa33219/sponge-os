# sponge_debug qmake project file.
#
# Links only the Qt modules actually used (AGENTS.md §3.4): Core, Gui,
# Widgets. No Network/Sql — the debug tool is a local monitor.

QT       += core gui widgets
TEMPLATE  = app
TARGET    = sponge_debug
CONFIG   += c++2a

# Same cproc/qt6_api port workaround as sponge_files.pro: the
# 'permissions' feature is enabled by the port but its .prf is not
# shipped, so qmake would abort. Dropping it is harmless here.
QT_CONFIG -= permissions

INCLUDEPATH += $$PWD

SOURCES  += main.cc
