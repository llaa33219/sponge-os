# \brief  Config-rewriting writer for the deferred-resource-request probe
# \author Sponge OS contributors
# \date   2026
#
# Purpose
# -------
# The third leg of run/sponge-resource-request.run. A child whose
# resource_request() its sandbox could not answer stays parked until the
# sandbox applies a NEW configuration
# (genode/repos/os/src/lib/sandbox/child.cc, Child::apply_upgrade ->
# _apply_resource_upgrade -> notify_resource_avail). This component is
# that new configuration: after a delay it publishes a config through
# report_rom, raising one child's RAM/caps quota and leaving the rest of
# the topology alone.
#
# Without such a writer the "deferred" observation is unprovable — it
# would be indistinguishable from "the grant simply took longer than the
# probe waited". The delay is what makes the causal order explicit: the
# probe has already logged DEFER by the time this config lands.
#
# Its own config names the children and the raised quota:
#
#   <config | delay_ms: 12000>
#     <child | name: hog | caps: 1600 | ram: 16M/>
#     <child | name: probe | caps: 200 | ram: 12M/>
#   </config>

TARGET   := config_bump
SRC_CC   := main.cc
LIBS     := base