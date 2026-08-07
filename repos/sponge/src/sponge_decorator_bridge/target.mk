# sponge_decorator_bridge — Phase 11 W4 themed_decorator color bridge
# (see main.cc for the full design).
#
# Plain Genode component (LIBS = base, no Qt, no libc, no exceptions, no
# Timer). It is purely signal-driven: opens the sponge_themed `theme` ROM
# (the same channel sponge-de's ThemeController reads), parses the
# `[colors] panel_bg` key, and publishes a `decorator_config` report
# carrying the `<policy ... color="..."/>` block themed_decorator
# consumes.
#
# Minimum privilege: one ROM session (`theme`) + one Report session
# (`decorator_config`). No Timer, no libc, no File_system, no NIC
# (AGENTS.md §1.2, §5.5 risk-register row 11).

TARGET   := sponge_decorator_bridge
SRC_CC   := main.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
