# sponge_de_probe — headless GUI verification probe (Phase 3).
#
# Plain Genode component (no Qt, no libc). It acts as a client of
# nitpicker's Capture session (reads composited pixels), the Event
# session (injects a synthetic pointer click), and a ROM session fed by
# report_rom (watches sponge-de's "input" report). See main.cc.
#
# The capture_session/connection.h header pulls in <blit/painter.h>, so
# the blit library must be linked. All session connection headers under
# repos/os/include are globally available to every target because the
# Genode build framework adds <repo>/include for each repository
# (repos/base/mk/global.mk).

TARGET   := sponge_de_probe
SRC_CC   := main.cc
LIBS     := base blit
