# settings_thirdparty_probe — run/sponge-settings-thirdparty.run driver
# (Criterion 18 third-party acceptance: two independent settings_demo
# entries, provider first paint, validate + package-private persist,
# close/restart retention, removal/reinstall, never-aliased stores).
#
# Plain Genode component (no libc, no Qt — AGENTS.md §3.1). It drives
# the whole scenario itself:
#   - sponge_pkgd install/remove/settings-open over the single-writer
#     report channels (settings_thirdparty_probe -> request /
#     settings_request), result XML validated exactly;
#   - input into the provider windows through nitpicker's Event
#     service (the sponge_de_probe inject path);
#   - first-paint witnesses through a Capture session (the provider
#     header band, #27273a, vs the #1e1e2e background);
#   - provider window close through the production wm close channel
#     (hover-gated decorator-closer click -> window_layouter close ->
#     wm resize_request 0x0 -> Gui-info "closed" -> provider commit);
#   - exact store-XML readback of the package-private store
#     (/greeting.xml, /about.xml) plus a write/read/delete sentinel
#     proving the package-private File_system path is writable.
#
# The capture_session/connection.h header pulls in <blit/painter.h>,
# so the blit library must be linked (same as test/sponge_de_probe).

TARGET   := settings_thirdparty_probe
SRC_CC   := main.cc
LIBS     := base blit vfs
INC_DIR  := $(PRG_DIR)
