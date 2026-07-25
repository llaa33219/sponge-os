# lz_viewer — displays the Leitzentrale subsystem's composited UI as a
# window on the outer (Sponge desktop) nitpicker (Phase 6b, criterion 3).
#
# Sculpt's nested-GUI proxy chain (sculpt_manager -> gui_fader -> gui_fb)
# carries view operations but no pixel content to the outer nitpicker, so
# the Leitzentrale never became visible that way. This component is the
# explicit "dedicated viewer" the criterion allows: it opens a Capture
# session to the leitzentrale subsystem's PROVIDED Capture service (the
# inner nitpicker's fully-composited output — dialogs + backdrop), and
# memcopies each captured frame into a Gui view on the outer nitpicker.
# The result is the Leitzentrale genuinely shown as a window on the Sponge
# desktop, verifiable by an outer Capture client (lz_viz_probe).
#
# Plain Genode component (no Qt, no libc). Interaction (clicks flowing back
# into the subsystem) is out of scope for 6b.

TARGET   := lz_viewer
SRC_CC   := main.cc
LIBS     := base blit
