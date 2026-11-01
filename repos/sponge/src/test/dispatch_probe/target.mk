# dispatch_probe — pkgd dispatch-liveness driver.
#
# Stamps sent/got markers around each request so
# run/sponge-pkg-dispatch-liveness.run can assert the metadata ROM and
# store-flush windows, and checks the FIFO no-overtaking contract for a
# query pipelined into a pending flush.

TARGET   := dispatch_probe
SRC_CC   := main.cc
LIBS     := base
