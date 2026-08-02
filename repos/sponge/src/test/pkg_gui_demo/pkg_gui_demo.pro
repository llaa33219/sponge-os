# pkg_gui_demo qmake project file.
#
# Links only the Qt modules actually used (AGENTS.md §3.4): Core, Gui,
# Widgets. No theme/launcher/backend sources — this is the smallest Qt
# window that paints a recognizable color.

QT       += core gui widgets
TEMPLATE  = app
TARGET    = pkg_gui_demo
CONFIG   += c++2a

# Same cproc/qt6_api port workaround as sponge-de.pro: the 'permissions'
# feature is enabled by the port but its .prf is not shipped, so qmake
# would abort. Dropping it is harmless for a Genode component.
QT_CONFIG -= permissions

INCLUDEPATH += $$PWD

SOURCES  += main.cc
