# clipboard_qtsettext qmake project file.
#
# Links only the Qt modules actually used (AGENTS.md §3.4): Core, Gui.
# No Widgets — the harness never instantiates widgets. The setText call
# flows through QGenodeClipboard::setMimeData (the same Qt-internal path
# textedit's QTextEdit uses), reaching the Genode clipboard bus via the
# QGenodeIntegration::clipboard() plugin service.

QT       += core gui
TEMPLATE  = app
TARGET    = clipboard_qtsettext
CONFIG   += c++2a

# Same cproc/qt6_api port workaround as sponge-de.pro: drop the
# 'permissions' feature (the upstream port enables it but does not
# ship the .prf; qmake aborts otherwise). Harmless on Genode.
QT_CONFIG -= permissions

INCLUDEPATH += $$PWD

SOURCES  += main.cc
