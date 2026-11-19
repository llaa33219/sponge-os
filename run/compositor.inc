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
# WM-shaped seam (G3, docs/plans/phase16-sponge-compositor.md §7): the
# Criterion-16 WM scenarios select their Tier-0 through a scenario-local
# 'tier0_kind' that defaults to 'upstream' (the reference runs stay on
# nitpicker); 'native' selects sponge_compositor. These helpers are
# independent of SPONGE_COMPOSITOR: G1/G2 keep defaulting native through
# compositor::enabled above, while the WM seam is driven only by
# tier0_kind.
#
# Public procs (WM seam):
#   compositor::wm_tier0_build kind  -> build target for the Tier-0:
#                                'upstream' -> 'server/nitpicker',
#                                'native'   -> 'sponge_compositor'
#   compositor::wm_tier0_child kind  -> init child name used by routes
#                                and report labels: 'nitpicker' or
#                                'sponge_compositor'
#   compositor::wm_start_node kind report_flags -> the WM-shaped Tier-0
#                                start node (Gui/Capture/Event provides,
#                                Report -> report_rom and Timer routes,
#                                capture/event children, panel domain
#                                layer 1 ypos 0 height 28, default domain
#                                layer 2, panel policy, default-policy,
#                                #1e1e2e). 'report_flags' is the exact
#                                '+ report | ...' attribute text and is
#                                scenario-specific. 'native' uses
#                                'start sponge_compositor' with the same
#                                body plus width/height 1024x768 on the
#                                config node. No request_input: there is
#                                no Input provider; Event push is the
#                                proven path.
#
# WM node vocabulary (stable, used by the scenario string maps):
#   upstream -> build 'server/nitpicker', child 'nitpicker'
#   native   -> build 'sponge_compositor', child 'sponge_compositor'
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


#
# WM-seam build target (see the header for the contract).
#
proc compositor::wm_tier0_build { kind } {
	switch -- $kind {
		upstream { return server/nitpicker }
		native   { return sponge_compositor }
		default  {
			error "compositor::wm_tier0_build: kind must be 'upstream' or 'native'"
		}
	}
}


#
# WM-seam init child name for routes and report labels (see the header).
#
proc compositor::wm_tier0_child { kind } {
	switch -- $kind {
		upstream { return nitpicker }
		native   { return sponge_compositor }
		default  {
			error "compositor::wm_tier0_child: kind must be 'upstream' or 'native'"
		}
	}
}


#
# The WM-shaped Tier-0 start node (see the header for the contract).
# Upstream reproduces the existing WM scenario nitpicker node exactly;
# native runs the same body as sponge_compositor with width/height
# 1024x768 on the config node. The report flags are a parameter so each
# scenario keeps its own '+ report | ...' attribute text.
#
proc compositor::wm_start_node { kind report_flags } {
	set child [compositor::wm_tier0_child $kind]
	set config_attrs ""
	if {$kind eq "native"} {
		set config_attrs " | width: 1024 | height: 768"
	}
	return "+ start $child | ram: 16M
  + provides
    + service Gui
    + service Capture
    + service Event
  + route
    + service Report | + child report_rom
    + service Timer | + child timer
    + any-service
      + parent
      + any-child
  + config$config_attrs
    + capture
    + event
    + report | $report_flags
    + domain panel | layer: 1 | content: client | label: no
                   | hover: always | focus: click | ypos: 0 | height: 28
    + domain default | layer: 2 | content: client | label: no
                     | hover: always | focus: click
    + policy | label_prefix: sponge-de -> Sponge Panel | domain: panel
    + default-policy | domain: default
    + background | color: #1e1e2e"
}
