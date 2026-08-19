# Sponge OS bake profile: test
# Phase 15 (D15.3/D15.4, user-requested test-profile pattern). Consumed
# by run/bake.inc (W2) and tool/bake.mojo.
#
# Identical package/config/theme content to `desktop`, but the boot
# media uses the DIAGNOSTIC GRUB config (text-mode console, visible
# 10 s menu, SPONGE-DIAG stage markers before every multiboot2 load)
# instead of the production hidden gfxterm menuentry. Use this profile
# whenever a boot must be observed stage-by-stage on real hardware
# (the 15-3 LG gram bring-up is the reference case); the template is
# run/fixtures/grub-diag.cfg.

config_version = 1
name = test
description = Diagnostic variant of the desktop profile: same packages and defaults, but the bootloader runs the text-mode SPONGE-DIAG grub.cfg so every boot stage is visible on real hardware.

[packages]
hello = enabled          # probe/smoke compatibility — always staged
terminal = enabled
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

[boot]
grub_mode = diagnostic
