# settings_failure_probe — Criterion 18 settings failure-contract probe.
#
# Companion of run/sponge-settings-failure.run; marker grammar
# "settings-failure-probe: ...". A FOCUSED harness (AGENTS.md §3.1,
# plain Genode component, no Qt, no libc) with exactly four jobs:
#
#   1. Gui service for the settings providers launched into
#      pkg_runtime: a self-contained Gui::Session server (per-session
#      framebuffer + input + info ROM) that delivers the v1.1
#      app-visible close channel (docs/05 §4.6) deterministically -
#      the info ROM flips to <capture closed="yes"/> so the provider's
#      production _handle_info path commits and exits(0). No window is
#      ever "hidden" to fake a crash: the close is the real channel and
#      the exit is the provider's own.
#   2. State-report evidence relay: watches pkg_runtime's `state`
#      report (the same HID report sponge_pkgd's liveness gate reads)
#      and logs every settings_demo.greeting transition verbatim -
#      explicit `exited` attribute observed, post-release absence
#      (absence alone is never an exit), fresh instance.
#   3. probe_request caller: `remove settings_demo` plus the stale
#      open of the removed identity (expect not-installed).
#   4. system config relay: publishes the nested `system` init config
#      (run/sponge-de-pkgd-failure.run pattern) so the scenario can
#      STOP sponge_pkgd deterministically and arm the late vct caller
#      for the package-manager-unavailable fail-fast leg.
#
TARGET   := settings_failure_probe
SRC_CC   := main.cc
LIBS     := base
