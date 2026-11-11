# sponge_decorator — lightweight native theme-driven window chrome
# (protocol v1.1; docs/plans/phase16-sponge-wm.md §11, D16W.1/M3).
#
# Public Genode interfaces only (D16W.2): Gui session + framebuffer,
# nitpicker_gfx painters (Surface/Box_painter/Text_painter), Tff_font
# over the component's own data/font.tff asset, ROM/Report sessions.
# No Qt, no OpenGL, no libc, no gems internals.
#
# The frozen chrome geometry comes from the shared sponge_wm_protocol
# library; the deterministic tests live in tests/ (host-side self-test,
# see its header comment) and are deliberately not built here.

TARGET   := sponge_decorator
SRC_CC   := main.cc theme.cc chrome_geometry.cc renderer.cc chrome_view.cc
SRC_BIN  := font.tff
LIBS     := base sponge_wm_protocol

vpath %.tff $(PRG_DIR)/data
