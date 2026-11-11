# sponge_wm — native window-manager core (Criterion 16, M1)
#
# Public Genode interfaces only (D16W.2): provides 'Gui', wraps one
# real nitpicker Gui connection per session, implements the frozen
# protocol v1 'window_list' plus the v1.1 internal contract
# (docs/plans/phase16-sponge-wm.md §11).
#
# The pure registry/protocol-state model (wm_registry.cc) is
# host-tested via self_test.cc (not part of this build; see the header
# comment there for the build/run command).

TARGET   := sponge_wm
SRC_CC   := main.cc wm_registry.cc wm_session.cc wm_root.cc
LIBS     := base sponge_wm_protocol
