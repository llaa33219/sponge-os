# sponge_pkgd — Sponge OS package backend daemon.
#
# Plain Genode component (no libc, no Qt, no exceptions). It watches a
# "request" ROM (relayed by report_rom from vct's request report),
# resolves package metadata + dependencies, and writes a structured
# "result" report (relayed back to vct as a ROM). The Report/ROM channel
# is the settled vct<->backend design (docs/04-components.md §5).
#
# Sessions: Report + ROM always; an optional File_system session (via the
# Vfs library) is opened only when <config> carries a <vfs> node, to back
# the persistent installed-set store (docs/12-package-format.md §13).
# Purely signal-driven — no Timer, no libc, no Qt, no exceptions.

TARGET   := sponge_pkgd
SRC_CC   := main.cc
LIBS     := base vfs
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
