# sponge_pkg_launch — pure launch model for Sponge OS package
# sandboxes.
#
# Extracted from src/sponge_pkgd's _do_launch gate sequence and
# registry.cc's running-set functions (Phase 16 P1.2,
# docs/plans/phase16-runtime-overhaul.md). Pure decision + running-set
# rules only: no Env, no sessions, no I/O, no exceptions, no mutable
# globals. The caller owns every buffer and all state; all side effects
# (report generation, _add_running mutation, dirty marks, runtime
# config regeneration, logging) stay in sponge_pkgd's shell. Resource/
# admission outcomes are caller-computed inputs (admission.cc stays the
# admission owner).
#
# The public header is at repos/sponge/include/sponge/pkg_launch.h.
# Behavior preservation against the pre-extraction decision is proven
# by the host-side self-test in lib/src/sponge_pkg_launch/self_test.cc
# (NOT built here; see its header comment for the build/run command).

SRC_CC := pkg_launch.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_pkg_launch
