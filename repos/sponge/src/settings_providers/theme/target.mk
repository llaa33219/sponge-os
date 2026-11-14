# theme_settings — first-party settings provider (Criterion 18).
# Plain Genode component (no libc, no Qt). Shared scaffolding is
# header-only under ../common (sp_provider.h and friends); see
# ../README.md for the channel/quick-pair label contract.

TARGET   := theme_settings
SRC_CC   := main.cc
LIBS     := base
