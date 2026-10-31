# vfs_qtwebengine_shm — process-local shared-memory VFS plugin for
# qtwebengine (falkon's <qtwebengine_shm/> mount).
#
# Source port of the cproc/src/vfs_qtwebengine_shm recipe 2026-10-23
# (https://depot.genode.org/cproc/src/vfs_qtwebengine_shm/2026-10-23.tar.xz,
# SHA-256 795045d08967dd26cfca313c2869fc531f391d5cc68182d07c33b05e299e04dd)
# adapted to the vendored Genode 26.08 VFS API. See vfs.cc header for
# the exact interface adaptations and provenance.

SRC_CC = vfs.cc

VFS_DIR = $(REP_DIR)/lib/src/vfs_qtwebengine_shm
INC_DIR += $(VFS_DIR)
vpath %.cc $(VFS_DIR)

LD_OPT  += --version-script=$(VFS_DIR)/symbol.map

SHARED_LIB = yes
