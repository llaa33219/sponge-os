# Sponge OS bake profile: isolation
# Phase 16 P5.1 app-isolation arm (SPONGE_APP_ISOLATION=on on
# run/sponge-desktop-disk.run). A FOCUSED media set: the kill leg needs
# textedit (victim) + calculator (sibling), the boot health witness
# (alpha_probe minimal contract) needs hello, and the wedge leg's
# ep_wedge fixture is injected on top by the run script via
# bake::add_package (pkg/ep_wedge/, probe-only).
#
# Why not the full desktop set: the launcher popup domain is
# (0,28,341,532) and the desktop set already fills it exactly (8 apps =
# 532px, textedit at y=482..532); a ninth entry pushes textedit out of
# the clickable domain, and textedit must stay launcher-reachable for
# the kill + relaunch legs. Fewer Qt apps also keep the arm well below
# the CNode-pressure class the grouped matrix hit (R29/R30).
#
# The [config]/[theme] values are byte-identical to desktop.profile so
# the panel toggle at (32,14) and the probe geometry stay calibrated.

config_version = 1
name = isolation
description = Focused app-isolation media: hello, text edit, calculator, plus the run-script-injected ep_wedge wedge fixture.

[packages]
hello = enabled
textedit = enabled
calculator = enabled

[config]
panel.height = 28
panel.visible_widgets = clock,launcher,tasklist
clock.format = HH:mm
launcher.sort_by = alpha

[theme]
active = default
