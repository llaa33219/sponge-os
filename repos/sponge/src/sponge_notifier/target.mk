# sponge_notifier — Sponge OS notification backend daemon (Phase 14 W4).
#
# Plain Genode component (no libc, no Qt, no exceptions). It watches a
# "notif_request" ROM (relayed by report_rom from every client that posts
# a notification: sponge-de, vct, the W4 probe), validates each incoming
# <notification> (id/kind/ttl_ms), assigns a monotonic id, stores the
# active list (cap = max_live, default 8), and emits a single
# "notifications" ROM that the panel notifier_widget reads. A periodic
# Timer-driven expiry sweep removes expired entries and re-emits.
#
# Sessions (capability minimal, AGENTS.md §1.2):
#   - Report (publishes "notifications" — the read-side of the bus)
#   - ROM (reads "notif_request" — the write-side of the bus)
#   - ROM (reads "config" — optional; defaults if absent)
#   - Timer (drives the periodic expiry sweep)
# No PD, no RM, no GUI. The daemon is purely signal-driven (ROM sigh
# + Timer One_shot_timeout).
#
# The XML contract:
#   Inbound (<notif_request>):
#     <notif_request>
#       <notification source="..." kind="info|warn|error" ttl_ms="...">
#         <title>...</title>
#         <body>...</body>
#       </notification>
#       ... (a single <notification> per request — the daemon keeps
#       one request = one notification; callers posting more than one
#       send one request per notification)
#     </notif_request>
#   Outbound (<notifications>):
#     <notifications>
#       <notification id="..." ts="..." source="..." kind="..." ttl_ms="...">
#         <title>...</title>
#         <body>...</body>
#       </notification>
#       ... (up to max_live entries, FIFO by id)
#     </notifications>
#
# Validation (fail-soft, never crash):
#   - ttl_ms:     1..30000 (caps at 30000 per D14.1); 0/missing defaults
#                 to default_ttl_ms (default 5000)
#   - kind:       "info" | "warn" | "error" (default "info")
#   - source:     non-empty printable string (default "unknown")
#   - title:      non-empty, <= 96 chars; empty POSTS are silently dropped
#                 with a warning (the notifier facade is the dumb side,
#                 callers are responsible for meaningful titles)
#   - body:       optional, <= 256 chars
#   - id:         assigned by the daemon, monotonic per-process
#   - ts:         assigned by the daemon (uptime ms)
#   The broadcast <notifications> is regenerated on every state change
#   (insert or expiry) and on the initial publish from the constructor.

TARGET   := sponge_notifier
SRC_CC   := main.cc
LIBS     := base
INC_DIR  := $(PRG_DIR)/include \
            $(REP_DIR)/include
