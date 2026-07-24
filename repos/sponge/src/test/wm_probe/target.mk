# wm_probe — headless window-manager verification probe.
#
# Plain Genode component (no Qt, no libc). It proves that the upstream
# wm + window_layouter + decorator stack actually decorates and moves a
# Sponge DE window, by reading the layouter's "window_layout" ROM and the
# decorator's "hover" ROM (both relayed by report_rom) and by injecting a
# synthetic title-bar drag through nitpicker's Event session.
#
# It also opens a Capture session solely to define nitpicker's panorama
# (there is no framebuffer driver in this headless scenario), exactly as
# sponge_de_probe does in run/sponge-de-test.run. The
# capture_session/connection.h header pulls in <blit/painter.h>, so the
# blit library must be linked.
#
# See main.cc for the three-stage verification logic.

TARGET   := wm_probe
SRC_CC   := main.cc
LIBS     := base blit
