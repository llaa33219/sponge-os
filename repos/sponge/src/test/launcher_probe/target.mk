# launcher_probe — Sponge DE launcher integration verifier (Phase 5c).
#
# Drives sponge_pkgd's Report/ROM channel to install `hello`, then polls
# sponge-de's "launcher" report (relayed by report_rom) until it contains
# the freshly-installed app with its declared category. This proves the
# end-to-end path pkgd -> report_rom -> sponge-de -> launcher_report,
# which is the Phase 5c criterion "Sponge DE panel and launcher
# implemented". The probe also performs an OPTIONAL Capture pixel check
# on the panel band — informational, never gating (matches the
# theme_probe pattern).

TARGET   := launcher_probe
SRC_CC   := main.cc
LIBS     := base blit
