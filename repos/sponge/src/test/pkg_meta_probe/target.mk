# pkg_meta_probe — Phase 7 todo 18 search/update assertion-matrix probe.
#
# vct is short-lived (one command per boot), so the multi-step search +
# update assertion matrix is exercised here against the SAME ROMs vct's
# SearchCommand/UpdateCommand read (pkg_index.xml + pkg_<name>.xml +
# the `installed` broadcast), using the SAME comparison logic (exact
# string inequality for versions, case-insensitive substring for the
# search term). Passing this probe == passing vct's real code path.
#
# Used by run/sponge-pkg-meta.run.

TARGET   := pkg_meta_probe
SRC_CC   := main.cc
LIBS     := base
