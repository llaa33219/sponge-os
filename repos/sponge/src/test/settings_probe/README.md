# settings_probe — Phase 16 W4 acceptance probe (test/feat, TDD-first).
#
# Plain Genode component (no libc, no Qt — AGENTS.md §3.1). Two phases,
# selected via `<config phase="..."/>`:
#
#   phase="write-via-de-channel" — open the dedicated
#       `sponge-de -> de_config_request` channel; write five keys
#       covering the W4 tabs (Panel / Theme / Background / Shortcuts /
#       Defaults); assert each key/value round-trips into the broadcast
#       the DE-side ConfigController already reads. The probe exercises
#       the SAME channel SettingsController will use (mirror of
#       vct's ReportRomClient with label suffix `de_config_request` /
#       `de_config_result` per D16.1 / D16.9).
#
#   phase="right-click-marker" — emit a single QMP-TARGET rightclick
#       marker (host-side qmp_right_click in run/qmp.inc) at the
#       panel-center coordinates so the host can dispatch a real
#       right-button press on the BTN_RIGHT axis. Used by the W4
#       regression scenario as the gateway into the panel context
#       menu's "Settings" entry that W5 wires up (W4 only proves the
#       marker dispatch + the de-channel write path).
#
# One probe serves both phases. Each scenario gates on a specific
# PASS marker:
#
#   run/sponge-de-settings.run              -> "settings-probe: PASS"
#   run/sponge-de-settings-regression.run   -> "settings-regression-probe: PASS"
#
# Capability surface: Report (de_config_request) + ROM
# (de_config_result, configd broadcast) + Timer. No Capture, no GUI.
#
# The probe is intentionally small and reads the SAME configuration
# registry the SettingsController will write through — proving the
# writer validator parity with vct (D16.9) and the broadcast
# regeneration. A real SettingsController-internal test would
# duplicate this same plumbing; the probe is the spec.
