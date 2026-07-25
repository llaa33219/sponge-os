# lz_watch — Leitzentrale model-fs change detector + snapshot/revert
# (Phase 6c).
#
# Sits inside the leitzentrale subsystem as a sibling of model_fs. It
# periodically reads the relevant model-fs files (the deploy config, which
# is what sculpt_manager rewrites when the user adds/removes components),
# keeps a baseline snapshot in RAM, and reports divergence:
#
#   <lz_model diverged="true|false" checksum="...">
#     <file name="deploy" changed="true|false"/>
#   </lz_model>
#
# It also handles a request ROM (lz_watch_request):
#   <request op="snapshot"/>  re-baseline (adopt current model as blessed)
#   <request op="revert"/>    write the baseline back to the model fs
# and answers via an lz_watch_result report.
#
# Reports flow up (subsystem provides Report) to the top-level report_rom;
# configd watches lz_model to mirror leitzentrale.diverged in its broadcast,
# and vct's `leitzentrale diff/keep/revert` use the same channels. The
# request ROM flows down from the top-level report_rom.
#
# Plain Genode component (no libc): uses the Vfs library to access the
# model fs, like sculpt_manager's own Vfs wrapper.

TARGET   := lz_watch
SRC_CC   := main.cc
LIBS     := base vfs
