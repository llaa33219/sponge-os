# regen_probe — configuration-regeneration survival verifier.
#
# Drives run/sponge-config-regen-survival.run. Sequence:
#
#   1. install the witness package (autostart) and wait until its beat
#      counter has reached a live value,
#   2. record the counter,
#   3. for each cycle, issue one package operation on a second,
#      never-autostarted package (cycle 0 installs it, later cycles
#      remove it again). Each operation makes sponge_pkgd regenerate the
#      whole pkg_runtime config; with the test-only <repro_pad_grow>
#      knob every such write crosses the Expanding_reporter buffer
#      boundary,
#   4. sample the witness beat counter for a bounded window after each
#      operation and require it to be monotonically non-decreasing and
#      still advancing. A counter that jumps back to 1 means pkg_runtime
#      abandoned and recreated the witness: the regeneration killed a
#      running child.
#
# Logs "regen-probe: PASS" only if the witness survived every cycle;
# otherwise "regen-probe: FAIL <reason>" and exit 1.

TARGET   := regen_probe
SRC_CC   := main.cc
LIBS     := base