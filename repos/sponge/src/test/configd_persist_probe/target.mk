# configd_persist_probe — sponge_configd persistence acceptance probe (Phase 14 W6).
#
# Plain Genode component (no libc, no Qt, no exceptions). It exercises
# sponge_configd's <vfs>-backed persistent store end-to-end:
#   (1) sends three set requests through the established
#       config_request/config_result channel,
#   (2) verifies the broadcast carries the latest value after each set,
#   (3) reads back the on-disk store.xml via a second vfs mount (the same
#       vfs sponge_configd wrote to) and asserts it carries the most-recent
#       panel.height AND the intermediate panel.visible_widgets (no clobber),
#   (4) logs exactly "configd-persist-probe: PASS".
#
# Used by run/sponge-configd-persist.run and the corrupt-store variant.
# Capability surface: Report + ROM + File_system.

TARGET   := configd_persist_probe
SRC_CC   := main.cc
LIBS     := base vfs timeout
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
