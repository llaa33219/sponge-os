# Sponge OS bake profile: minimal
# Phase 15 (D15.3/D15.4). Consumed by run/bake.inc (W2) and tool/bake.mojo.
# The smallest usable system: desktop environment + terminal survival kit.

config_version = 1
name = minimal
description = Smallest usable Sponge OS media: Sponge DE plus the terminal package (core shell, no extra CLI toolset tars).

[packages]
# probe/smoke compatibility — always staged
hello = enabled
terminal = enabled
# terminal_toolset is intentionally NOT enabled here: pkg/terminal/
# already bundles the full bash + CLI toolset (per its metadata.xml
# description: "Terminal with bash, vim, and a UNIX CLI toolset").
# A separate pkg/terminal_toolset/metadata.xml does not exist.
# (W2 design call — bake.inc R15.3 verifier would otherwise error.)

[config]
panel.height = 28
panel.visible_widgets = clock,launcher
clock.format = HH:mm
launcher.sort_by = alpha

[theme]
active = default
