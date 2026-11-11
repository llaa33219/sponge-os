# sponge_layouter — native layout policy component (Criterion 16,
# D16W.1). Floating + Master-Stack tiling policy and the protocol
# v1/v1.1 state model for the native Sponge WM:
# repos/sponge/src/sponge_layouter/README.md is the component
# contract (sessions, config schema, coordinate frames, work-area
# fallback).
#
# The pure policy model (layout_model.{h,cc}) is exercised by the
# deterministic host self-test in this directory (self_test.cc — see
# its header comment for the build/run command); it is deliberately
# not a separate Genode target.

TARGET   := sponge_layouter
SRC_CC   := main.cc layout_model.cc
LIBS     := base sponge_wm_protocol
