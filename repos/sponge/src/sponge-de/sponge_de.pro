# Sponge DE qmake project file.
#
# Links only the Qt modules actually used (AGENTS.md §3.4: minimize Qt
# dependencies). Phase 3 needs Core, Gui, and Widgets only.

QT       += core gui widgets
TEMPLATE  = app
TARGET    = sponge-de
CONFIG   += c++2a

INCLUDEPATH += $$PWD/../../include

SOURCES  += main.cc \
            sponge_de_main.cc
HEADERS  += sponge_de_main.h
