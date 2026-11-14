# settings_vct_probe — real-vct settings acceptance harness.
#
# Companion of run/sponge-settings-vct.run; marker grammar
# "settings-vct-probe: ...". A FOCUSED harness (AGENTS.md 3.1, plain
# Genode component, no Qt, no libc) with exactly three jobs:
#
#   1. Gui service for the settings providers sponge_pkgd launches
#      into pkg_runtime (self-contained Gui::Session server: per-
#      session framebuffer + input + info ROM). The providers open
#      real windows with real buffers; nothing is closed or hidden -
#      this scenario's claims are lifecycle claims (started / focused
#      / not-installed), and the app-visible close channel is proven
#      elsewhere (run/sponge-settings-failure.run).
#   2. Nested `system` init config relay (the
#      run/sponge-settings-failure.run publication pattern): the
#      probe owns the `system` sandbox's config and serializes REAL
#      vct children on the primary request label - one `vct_cmd`
#      incarnation at a time (install, settings list, settings list
#      --json, settings open xN), each config version arming the next
#      command only after the previous vct child completed. Never two
#      concurrent writers on the primary request label.
#   3. Per-step wire validation + one terminal PASS marker: after
#      each vct_cmd exit the probe reads sponge_pkgd's `result` (and
#      the `installed` broadcast) and fails loudly unless the wire
#      answer matches the step's contract (install ok / ok started /
#      ok focused / not-installed).
#
TARGET   := settings_vct_probe
SRC_CC   := main.cc
LIBS     := base
