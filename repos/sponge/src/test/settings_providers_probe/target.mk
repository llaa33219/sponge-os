# settings_providers_probe — Criterion 18 settings-provider acceptance
# probe (run/sponge-settings-providers.run). Plain Genode component
# (no libc, no Qt): drives the exact QuickMenuController /
# LauncherController::request_settings_open wire lifecycle and the
# theme_settings out-of-domain adversarial write. See main.cc.

TARGET   := settings_providers_probe
SRC_CC   := main.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)
