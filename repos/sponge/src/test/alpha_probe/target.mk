# alpha_probe — Phase 7 todo 4 composite Alpha-desktop verifier.
#
# Plain Genode component (no Qt, no libc). Asserts all four Alpha criteria
# in bounded iterations, then logs exactly "alpha-probe: PASS". Any
# failure logs "alpha-probe: FAIL <reason>" and exits non-zero so the
# run scenario fails by bounded run_genode_until timeout (fail-loud,
# docs/09-roadmap.md §11.1 — never a silent hang).
#
# Criteria:
#   (a) Themed sponge-de panel/window is composited (Capture pixel check
#       on the panel band — the default theme's panel_bg is non-zero).
#   (b) sponge-de's "launcher" report carries the pre-staged `hello`
#       package with category="Utilities" — proves the pkgd feed works
#       end to end (install -> pkgd broadcast -> sponge-de launcher).
#   (c) configd's broadcast "config" ROM is live (readable, non-empty,
#       parses as XML with at least one <key> child).
#   (d) lz_viewer's Leitzentrale window is visible on the outer
#       nitpicker (the marker patch #bf5fbf appears at the known offset),
#       which only happens after the probe flips leitzentrale.enabled=true
#       via configd and the lz subsystem fader fades in.
#
# The probe owns both the pkgd request channel (installs hello) and the
# configd config_request channel (enables leitzentrale) — report_rom is
# single-writer per label and there is no vct in this scenario.

TARGET   := alpha_probe
SRC_CC   := main.cc
LIBS     := base blit
