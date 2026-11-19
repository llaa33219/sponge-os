# SPDX-License-Identifier: Apache-2.0
#
# Default-native Tier-0 compositor selection for run scenarios. An unset
# SPONGE_COMPOSITOR or 'native' selects sponge_compositor; 'upstream'
# selects the nitpicker fallback. Any other value fails loudly. This
# include is the only Tier-0 selection seam and is reused by later
# G3/G4 product integration (docs/plans/phase16-sponge-compositor.md,
# D16C.8/D16C.14).
#
# Public procs:
#   compositor::enabled        -> 1 for native, 0 for upstream
#                                (error on any invalid env value)
#   compositor::tier0_start_node ?kind?  -> start-node text for the
#                                Tier-0; kind is 'selected' (default,
#                                follow SPONGE_COMPOSITOR), 'native',
#                                or 'upstream'. Explicit 'native' kind
#                                requires native selection (the default
#                                or an explicit native override).
#
# Node vocabulary (stable, used by scenario routes):
#   upstream -> child 'nitpicker', native -> child 'sponge_compositor'
#
# Native is the default; upstream remains an explicit checked fallback.
#

namespace eval compositor {
}


proc compositor::enabled { } {
	if {![info exists ::env(SPONGE_COMPOSITOR)]} {
		return 1
	}
	switch -- $::env(SPONGE_COMPOSITOR) {
		upstream { return 0 }
		native   { return 1 }
		default  { error "SPONGE_COMPOSITOR must be 'upstream' or 'native'" }
	}
}


#
# The upstream Tier-0 node: the full nitpicker with capture + event,
# one full-screen client-content domain with 'label: no', and the
# shared #1e1e2e background (shape per run/sponge-de-test.run).
#
proc compositor::_upstream_start_node { } {
	return "+ start nitpicker | ram: 8M
  + provides
    + service Gui
    + service Capture
    + service Event
  + route
    + service Timer | + child timer
    + any-service
      + parent
      + any-child
  + config
    + capture
    + event
    + domain default | layer: 1 | content: client | label: no | hover: always | focus: click
    + default-policy | domain: default
    + background | color: #1e1e2e
"
}


#
# The native Tier-0 node: sponge_compositor providing the same
# Gui/Capture/Event surface, headless screen configured to 1024x768,
# and the same #1e1e2e background.
#
proc compositor::_native_start_node { } {
	return "+ start sponge_compositor | ram: 16M
  + provides
    + service Gui
    + service Capture
    + service Event
  + route
    + service Timer | + child timer
    + any-service
      + parent
      + any-child
  + config | width: 1024 | height: 768
    + background | color: #1e1e2e
"
}


#
# Tier-0 start-node helper (see the header for the contract). The env
# value is validated on every call so an invalid SPONGE_COMPOSITOR
# fails loudly regardless of the requested kind.
#
proc compositor::tier0_start_node { {kind selected} } {
	set native [compositor::enabled]
	switch -- $kind {
		selected {
			if {$native} {
				return [compositor::_native_start_node]
			}
			return [compositor::_upstream_start_node]
		}
		native {
			if {!$native} {
				error "the native Tier-0 requires native selection (default or SPONGE_COMPOSITOR=native)"
			}
			return [compositor::_native_start_node]
		}
		upstream {
			return [compositor::_upstream_start_node]
		}
		default {
			error "compositor::tier0_start_node: kind must be 'selected', 'native', or 'upstream'"
		}
	}
}
