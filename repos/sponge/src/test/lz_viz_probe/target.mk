# lz_viz_probe — Leitzentrale on-screen verification (Phase 6b, criterion 3).
#
# Opens a Capture session to the OUTER nitpicker (the Sponge desktop
# surface), defining the 1024x768 panorama, and polls the composited
# pixels until lz_viewer's Leitzentrale window is genuinely displayed:
# enough pixels in the window's region match the Leitzentrale's inner
# background color (#272f45, distinct from the outer desktop background
# #1e1e2e). This is the headless proof that the Leitzentrale is shown as
# a real window, not merely that its components are alive.
#
# Logs "lz-viz-probe: PASS" once the window is detected on screen.

TARGET   := lz_viz_probe
SRC_CC   := main.cc
LIBS     := base blit
