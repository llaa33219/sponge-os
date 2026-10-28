# provider_isolation_probe — P3.1 provider-stall isolation probe
# (docs/plans/phase16-runtime-overhaul.md P3.1, FD4/D16R.8).
#
# Two bounded per-path checks for run/sponge-provider-stall.run:
#   path=sys-rom    ROM byte round-trip through the rom_sys chain
#   path=pkgd-store File_system write/read round-trip on the P4
#                   package-store region
# Both run on worker threads so a parked session create can never park
# the probe's entrypoint. Markers: 'provider-isolation-probe: PASS/RED
# path=...' + a final 'provider-isolation-probe: DONE'.
#
# Plain Genode component (no libc, no Qt). Capability surface: ROM,
# File_system, Timer.

TARGET   := provider_isolation_probe
SRC_CC   := main.cc
LIBS     := base vfs
INC_DIR  := $(PRG_DIR)/include
