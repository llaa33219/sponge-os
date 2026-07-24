# sponge_backend_client — shared Report/ROM client for Sponge OS backends.
#
# Lifted from src/vct/pkg_client.{h,cc} in Phase 5c so that sponge-de's
# launcher can reuse the exact same request/poll plumbing as vct. The
# sources live under lib/src/ per lib/README.md; the public header is
# at repos/sponge/include/sponge/backend_client.h.

SRC_CC := backend_client.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_backend_client
