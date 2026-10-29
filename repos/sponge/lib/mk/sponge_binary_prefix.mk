# sponge_binary_prefix — pure parse-time binary_prefix rewriting for
# Sponge OS package metadata <config> blocks.
#
# Extracted verbatim from src/sponge_pkgd's _parse_package rewrite
# block (Phase 16 P1.2, docs/plans/phase16-runtime-overhaul.md).
# Pure scanning only: no Env, no sessions, no File_system, no hardware
# access. The caller owns every input and output buffer
# (sponge_pkgd feeds its static cfg_buf/out_buf scratch).
#
# The public header is at repos/sponge/include/sponge/binary_prefix.h.
# Byte-identity against the pre-extraction rewrite block is proven by
# the host-side self-test in lib/src/sponge_binary_prefix/self_test.cc
# (NOT built here; see its header comment for the build/run command).

SRC_CC := binary_prefix.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_binary_prefix
