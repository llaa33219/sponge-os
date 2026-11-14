# quickmenu_probe — Criterion 18 click-through quick-menu acceptance
# probe (run/sponge-settings-quickmenu.run). Plain Genode component
# (no libc, no Qt): emits the QMP-TARGET markers and validates the
# real QuickMenuController -> provider chains from ROM/Capture
# evidence. Its sole writer session is the package-install `request`
# channel (see main.cc's write-attribution note).

TARGET  := quickmenu_probe
SRC_CC  := main.cc
LIBS    := base
INC_DIR := $(PRG_DIR)
