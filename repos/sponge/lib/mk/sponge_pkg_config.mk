# sponge_pkg_config — pure runtime-config generation for Sponge OS
# package sandboxes.
#
# Extracted verbatim from src/sponge_pkgd's _generate_runtime_config/
# _emit_runtime_config (Phase 16 P1.2,
# docs/plans/phase16-runtime-overhaul.md). Pure templating only: no
# Env, no Report/ROM sessions, no File_system, no hardware access.
# The caller owns the output buffers (sponge_pkgd feeds its
# pre-sized Expanding_reporter buffers through Xml_generator).
#
# The public header is at repos/sponge/include/sponge/pkg_config.h.
# Byte-identity against the pre-extraction generator is proven by the
# host-side self-test in lib/src/sponge_pkg_config/self_test.cc (NOT
# built here; see its header comment for the build/run command).

SRC_CC := pkg_config.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_pkg_config
