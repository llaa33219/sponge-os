# lz_edit_probe — Phase 6c end-to-end driver: edit model fs → lz_watch
# detects → configd broadcast reflects diverged → revert → diverged clears.
#
# Sits inside the leitzentrale subsystem as a sibling of model_fs and
# lz_watch. It appends a bogus node to /deploy (a REAL fs change), then
# polls configd's broadcast (lz_config, which mirrors leitzentrale.diverged)
# until divergence is observed, sends a revert request, and confirms the
# divergence clears. This exercises the genuine detection path — lz_watch
# checksums the file, configd watches lz_watch's report — nothing is faked.
#
# Logs "lz-edit-probe: PASS" on the full cycle.

TARGET   := lz_edit_probe
SRC_CC   := main.cc
LIBS     := base vfs
