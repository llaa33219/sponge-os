# lz_probe — Leitzentrale boot verification probe (Phase 6a).
#
# Plain Genode component (no Qt, no libc). It confirms that Sculpt's
# sculpt_manager is actually alive inside the leitzentrale subsystem hosted
# under lz_runtime, by polling the subsystem init's "state" report (relayed
# by the lz_relay report_rom as the "lz_subsys_state" ROM).
#
# Success criterion: the state XML carries a <child name="manager"> entry
# with a non-zero RAM quota and no exit state — i.e. sculpt_manager started
# and is still running. This is the Phase 6a headless boot check; window
# visibility is slice 6b.
#
# On success it logs "lz-probe: PASS"; on timeout the run scenario fails
# via run_genode_until (the correct FAIL path).

TARGET   := lz_probe
SRC_CC   := main.cc
LIBS     := base
