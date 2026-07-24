# sponge_themed — theme resolver daemon (Phase 5b).
#
# A plain Genode component (LIBS = base, no Qt, no libc). It watches
# sponge_configd's broadcast "config" ROM, reads the "theme.active" key,
# resolves the named theme from a staged "<name>.theme" ROM module, and
# republishes the resolved theme CONTENT as a "theme" report that
# sponge-de reads (live theme reload). See main.cc.
#
# The data flow is one-way: vct -> sponge_configd -> sponge_themed ->
# sponge-de (docs/04-components.md). sponge_themed never interprets the
# theme (it is Qt-free) — it only resolves a name to file content and
# relays it, concentrating all theme interpretation in the renderer
# (sponge-de, which already owns the Qt-free ThemeLoader).

TARGET   := sponge_themed
SRC_CC   := main.cc
LIBS     := base
