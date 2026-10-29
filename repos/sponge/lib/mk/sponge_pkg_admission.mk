# sponge_pkg_admission — pure admission/quota math for Sponge OS
# package sandboxes.
#
# Extracted from src/sponge_pkgd's admission.cc (Phase 16 P1.2,
# docs/plans/phase16-runtime-overhaul.md; target named by
# docs/evidence/phase16-runtime-config-coupling.md §9). Pure math only:
# no Env, no ROM/Report sessions, no File_system, no logging, no heap,
# no mutable global state. The caller owns every input and output.
# All side effects (state-report ROM reads, armed/unarmed arming,
# fail-open logging, mutation, result reports) stay in sponge_pkgd's
# shell.
#
# The public header is at repos/sponge/include/sponge/pkg_admission.h.
# Behavior preservation against the pre-extraction arithmetic is proven
# by the host-side self-test in lib/src/sponge_pkg_admission/self_test.cc
# (NOT built here; see its header comment for the build/run command).

SRC_CC := pkg_admission.cc

INC_DIR += $(REP_DIR)/include

vpath %.cc $(REP_DIR)/lib/src/sponge_pkg_admission
