# vfs_store_probe — instrumented single-file VFS plugin for the pkgd
# store-flush path (Phase 16 P3.3). Logs the calling thread of every
# write/sync and delays writes by a configurable `delay_ms`, so
# run/sponge-pkg-dispatch-liveness.run can attribute the store flush to
# the Bootstrap worker and widen the flush window deterministically.
#
# Loaded on demand from the vfs_store_probe.lib.so boot module via the
# VFS external-factory probe (root.cc `_probe_external_factory`); never
# mounted by product media.

SRC_CC = vfs.cc

VFS_DIR = $(REP_DIR)/lib/src/vfs_store_probe
INC_DIR += $(VFS_DIR)
vpath %.cc $(VFS_DIR)

LD_OPT  += --version-script=$(VFS_DIR)/symbol.map

SHARED_LIB = yes
