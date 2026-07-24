# theme_probe — vct<->Sponge-DE theme consistency probe (Phase 5b).
#
# Plain Genode component (no Qt, no libc). Drives the full one-way theme
# pipeline end to end and asserts a THREE-WAY value match between:
#   (1) sponge_configd's broadcast "config" ROM (theme.active),
#   (2) sponge_themed's "theme" ROM (resolved name + content),
#   (3) sponge-de's "applied_theme" report.
# Optionally reads nitpicker's composited pixels (Capture) to confirm the
# demo window background actually changed. See main.cc.

TARGET   := theme_probe
SRC_CC   := main.cc
LIBS     := base blit
