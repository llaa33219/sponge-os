# sponge_runtime_config_validate — pure validation of the runtime
# <config> documents sponge_pkgd publishes (Phase 16 P1.5 publication
# seam).
#
# Validates the pkgd-emitted subset of the init config schema (render
# -> validate -> publish gate) with structured results; see
# repos/sponge/include/sponge/runtime_config_validate.h for the subset
# contract and the documented exceptions. Pure: no Env, no sessions,
# no I/O, no exceptions, no mutable globals; the caller owns every
# buffer. Self-contained XML reader (no util/xml_node.h: that one
# requires C++ exceptions for invalid-syntax handling).
#
# Host-side self-test: lib/src/sponge_runtime_config_validate/
# self_test.cc (NOT built here; see its header comment for the
# build/run command).

SRC_CC := runtime_config_validate.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_runtime_config_validate
