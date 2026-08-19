# Sponge OS bake profile: desktop
# Phase 15 (D15.3/D15.4). Consumed by run/bake.inc (W2) and tool/bake.mojo.
# Sensible everyday defaults: every pre-staged Phase 13/14 package,
# including the Falkon browser (D14.10 opt-in overridden by D15.3 — the
# boot-module ceiling that motivated the opt-in was closed in Phase 9).

config_version = 1
name = desktop
description = Everyday-default Sponge OS media: terminal with full CLI toolset, text editor, file manager, calculator, PDF viewer, and the Falkon web browser.

[packages]
# probe/smoke compatibility — always staged
hello = enabled
terminal = enabled
# terminal_toolset is intentionally NOT enabled here: pkg/terminal/
# already bundles the full bash + CLI toolset. See pkg/bake/minimal.profile.
textedit = enabled
files = enabled
calculator = enabled
pdf_view = enabled
falkon = enabled

[config]
panel.height = 28
panel.visible_widgets = clock,launcher
clock.format = HH:mm
launcher.sort_by = alpha

[theme]
active = default
