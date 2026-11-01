# counting_rom — ROM proxy that logs every session create.
#
# P3.3 instrumentation: proves package dispatch opens no metadata ROM
# sessions (the bootstrap cache is the only reader). Used by
# run/sponge-pkg-dispatch-liveness.run.

TARGET   := counting_rom
SRC_CC   := main.cc
LIBS     := base
