# sponge_configd — Sponge OS configuration backend daemon.
#
# Plain Genode component (no libc, no Qt, no exceptions). It watches a
# "config_request" ROM (relayed by report_rom from vct's request report),
# validates and applies the requested key/value change, and writes a
# structured "config_result" report (relayed back to vct as a ROM). The
# Report/ROM channel is the settled vct<->backend design
# (docs/04-components.md §5) — there is no RPC stub or IDL.
#
# A second Expanding_reporter ("config") broadcasts the entire store as a
# ROM so future watchers (sponge_themed, sponge-de) can react to config
# changes without issuing requests. It is regenerated on every successful
# set and emitted once at startup with the defaults.
#
# Needs only Report + ROM sessions (minimum privilege). Purely
# signal-driven — no Timer, no libc, no Qt, no exceptions.

TARGET   := sponge_configd
SRC_CC   := main.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
