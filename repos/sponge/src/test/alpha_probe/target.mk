# alpha_probe — Phase 7 todo 4 composite Alpha-desktop verifier +
# Phase 16 W3 first-boot acceptance checks.
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
#   (b) sponge-de's "launcher" report carries the 7 desktop packages the
#       `desktop` bake profile pre-stages (hello/terminal/textedit/files/
#       calculator/pdf_view/falkon — Phase 16 W3 / D16.4). Per-pair
#       timeout is 30 s; total ceiling 210 s.
#   (c) configd's broadcast "config" ROM carries the 8 baked keys the
#       `desktop` profile seeds on first boot (bake.profile=desktop,
#       bake.version=1, bake.applied=yes, theme.active=default,
#       panel.height=28, panel.visible_widgets=clock,launcher,
#       clock.format=HH:mm, launcher.sort_by=alpha — Phase 16 W3 /
#       D15.9/D16.4). On any missing key the probe emits the named
#       sentry marker `alpha-probe: defaults-firstboot-stub: FAIL
#       (missing baked key ...)` the run scenario's stub gate matches.
#   (d) lz_viewer's Leitzentrale window is visible on the outer
#       nitpicker (the marker patch #bf5fbf appears at the known offset),
#       which only happens after the probe flips leitzentrale.enabled=true
#       via configd and the lz subsystem fader fades in. Skipped when
#       <config skip_lz="yes"/> is set (Phase 8 P2 disk-desktop mode).
#
# The probe owns both the pkgd request channel (installs hello) and the
# configd config_request channel (enables leitzentrale) — report_rom is
# single-writer per label and there is no vct in this scenario.

TARGET   := alpha_probe
SRC_CC   := main.cc
LIBS     := base blit
