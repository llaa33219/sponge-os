# configd_corrupt_seed — pre-stages a torn store.xml before sponge_configd
# starts (Phase 14 W6 corrupt-store variant).
#
# Plain Genode component (no libc, no Qt, no exceptions). On construct it
# writes a deliberately torn <sponge-config> payload to /store.xml on
# the shared RAM vfs, then enters sleep_forever() so init's child list
# stays stable while sponge_configd starts and reads the torn store.
# Capability surface: File_system only.
#
# Started BEFORE sponge_configd in the corrupt scenario's init config so
# the torn store is in place by the time sponge_configd's constructor
# runs _load_store().

TARGET   := configd_corrupt_seed
SRC_CC   := main.cc
LIBS     := base vfs
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
