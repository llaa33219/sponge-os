# \brief  Child resource-request probe for the dynamic quota top-up verdict
# \author Sponge OS contributors
# \date   2026
#
# Purpose
# -------
# Settles, by observation, one question about Genode 26.08's sandbox:
# when a child issues a resource_request() that its own init cannot
# satisfy from its remaining slack, does ANYTHING above that init ever
# grant it?
#
# The probe registers a resource_avail signal, issues one explicit
# resource_request against its parent, and then watches for the grant:
#
#   - a signal within the timeout  -> the parent answered on its own
#     ("resq-<name>: GRANT");
#   - no signal within the timeout -> the request is parked, and the
#     probe verifies that its own PD quota really did NOT grow
#     ("resq-<name>: DEFER quota=<n>"), which distinguishes "parked"
#     from "granted but the signal was lost".
#
# Three instances run in run/sponge-resource-request.run, differing only
# in their config:
#
#   a  inside a SLACK-RICH init        -> expect grant
#   b  inside a TIGHT init            -> expect defer
#   c  inside a TIGHT init whose own config is later rewritten by
#      config_bump                    -> expect defer, then grant
#
# Instance c is the only path on which a deferred request is ever
# resolved, and it is resolved by a CONFIG CHANGE, never by a parent
# above the sandbox.
#
# Driven by run/sponge-resource-request.run. Each instance emits
# "resq-<name>: PASS" or "resq-<name>: FAIL <reason>".

TARGET   := resq_probe
SRC_CC   := main.cc
LIBS     := base