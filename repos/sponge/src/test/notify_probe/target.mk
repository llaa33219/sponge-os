# notify_probe — Sponge notification daemon verifier (Phase 14 W4).
#
# Plain Genode component (no Qt, no libc). It exercises the
# <notif_request> / <notifications> Report/ROM bus end-to-end:
#
#   (1) the probe opens a Report session labeled "notif_request" and posts
#       a sentinel notification (the canonical W4 example: kind="info",
#       ttl_ms="3000", title "Sponge Phase 14 notification sentinel",
#       body "W4 probe"). report_rom (under the sponge-notify.run
#       run-config) routes the probe's "notif_request" report to
#       sponge_notifier's "notif_request" ROM.
#   (2) the probe opens a Capture session on the outer nitpicker and
#       samples the popover rect (a fixed rect the notifier widget
#       occupies while a notification is on screen). It polls the
#       non-background fraction: rising means the popover opened,
#       dropping back to the baseline means the popover closed.
#   (3) on success, the probe logs "notify-probe: PASS" and exits 0. The
#       run scenario gates on that marker.
#
# The probe is the canonical Phase-14 notification acceptance probe.
# It is the same bus as the in-sponge-de notifier_widget, so a clean
# end-to-end pass is the cross-component proof (D14.1: the daemon is one
# component, the widget is in sponge-de, the probe is a third).
#
# AGENTS.md §3.1: plain Genode component, qualified Genode types,
# Component::construct/stack_size exactly as the framework expects.

TARGET   := notify_probe
SRC_CC   := main.cc
LIBS     := base blit
