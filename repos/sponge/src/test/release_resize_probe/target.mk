# release_resize_probe — criterion-9 (mouse window resize) verification.
#
# Phase 16 W9 (docs/plans/phase16-daily-desktop-defaults.md §"W9, Mouse window
# resize (criterion 9) + vendored themed_decorator sizer + minimizer patch
# (ledger row #17)"). Headless window-management probe modeled after
# wm_probe's observe mode (Phase 10 W3). It boots in release-mode
# (sponge_pkgd launches pkg_gui_demo for the headline test), then issues
# 8 QMP-driven edge/corner drags against the live decorator's sizer
# hover zones — 4 edges + 4 corners — and verifies, after every drag:
#
#   (a) window_layout ROM's <window title="...pkg_gui_demo..."/> reports a
#       new width/height (i.e. the layouter actually republished the
#       geometry — the structural "does it move" proof);
#   (b) the layouter's resize_request ROM carries the same <window id=...
#       width=... height=.../> entry for pkg_gui_demo (the resize_request
#       ROM is the wire that reaches wm and ultimately the Gui session;
#       observing it proves the complete layouter→wm path is alive);
#   (c) Capture-sampled pixels in pkg_gui_demo's content area still
#       read as green (#00ff00) — a Capture sample at the OLD content
#       center would now be background, and a sample at the NEW content
#       center is green (the "content resized" structural proof);
#   (d) a final title-bar drag still moves the window — a regression
#       check that resize does not break the existing move-via-title-bar
#       path (criterion 5).
#
# The 8 hit zones are the canonical motif decorator's resize affordances
# (genode/repos/gems/src/app/decorator/window.cc:322-325 publishes
# left_sizer/right_sizer/top_sizer/bottom_sizer; window_layouter/
# main.cc:364-406 maps the absolute hover to a 9-cell Window::Element
# grid via the three percent bands >75 / >25 / else on each axis).
#
# For themed_decorator (sibling scenario
# run/sponge-de-themed-chrome-resize.run), W9 also closes Phase 14's
# deferred D14.8(d) `<minimizer/>` button (the same patch adds sizer +
# minimizer; see docs/plans/phase16-daily-desktop-defaults.md D16.6).
#
# Marker contract (run-script side):
#   QMP-TARGET zone-N  <press_x> <press_y> <release_x> <release_y>
#       -- the 8 hit zones (N=1..8; see the table below) dispatching
#          to the qmp_drag primitive in run/qmp.inc. After every zone,
#          the probe polls the layouter's window_layout + resize_request
#          + Capture for the new size, then logs `[observe N+1]`
#          and emits the next QMP-TARGET.
#   QMP-TARGET post-resize-title-drag <x1> <y1> <x2> <y2>
#       -- the criterion-5 regression: a final title-bar drag that must
#          still move the window after 8 resizes.
#   release-resize-probe: PASS
#       -- the final gate. Exit 0 on success, exit 1 with a
#          release-resize-probe: FAIL <reason> log on any miss.
#
# Qt6 QPA observation (Phase 16 W9 open question, resolved upstream):
# the Qt6 QPA in the vendored qt6_base port emits a
# QWindowSystemInterface::handleGeometryChange(window, geo) inside its
# `_info_changed()` slot whenever the Gui session's info ROM fires
# (the QPA registers `gui_connection.info_sigh(_handler)` for every
# QPlatformWindow). The synthesized QResizeEvent reaches the QWidget,
# so the app's content tracks the new geometry. NO app-side
# ResizeSubscriber shim is needed for Qt6 apps; the metadata
# <resizeable="yes"/> is a forward-compatible opt-out marker for
# future non-Qt apps (Phase 17+) where signal registration depends
# on Component::construct.

TARGET   := release_resize_probe
SRC_CC   := main.cc
LIBS     := base blit
