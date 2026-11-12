# SPDX-License-Identifier: Apache-2.0
#
# Opt-in native WM topology for product scenarios. The default path returns
# the generated configuration byte-for-byte; SPONGE_WM=native performs an
# explicit, checked transformation after all other topology expansion.
#

namespace eval wm_trio {
}


proc wm_trio::enabled { } {
	if {![info exists ::env(SPONGE_WM)]} {
		return 0
	}
	switch -- $::env(SPONGE_WM) {
		upstream { return 0 }
		native   { return 1 }
		default  { error "SPONGE_WM must be 'upstream' or 'native'" }
	}
}


proc wm_trio::_replace_once { text needle replacement } {
	set at [string first $needle $text]
	if {$at < 0} {
		error "native WM transform: missing required anchor: $needle"
	}
	return [string replace $text $at \
	        [expr {$at + [string length $needle] - 1}] $replacement]
}


proc wm_trio::_block { text start end } {
	set first [string first $start $text]
	set last  [string first $end $text]
	if {$first < 0 || $last < 0 || $last <= $first} {
		error "native WM transform: invalid block '$start' .. '$end'"
	}
	return [string range $text $first [expr {$last - 1}]]
}


proc wm_trio::_native_nodes { product wm_block layouter_block decorator_block } {
	set wm_first [lindex [split $wm_block "\n"] 0]
	set wm_first [_replace_once $wm_first "+ start wm" "+ start sponge_wm"]
	set layouter_first [lindex [split $layouter_block "\n"] 0]
	set layouter_first [_replace_once $layouter_first "+ start layouter" \
	                    "+ start sponge_layouter"]
	set decorator_first [lindex [split $decorator_block "\n"] 0]
	set decorator_first [_replace_once $decorator_first "+ start decorator" \
	                     "+ start sponge_decorator"]

	set config_at [string first "  + config | rules: rom\n" $layouter_block]
	set route_at  [string first "  + route\n" $layouter_block]
	if {$config_at < 0 || $route_at < 0 || $route_at <= $config_at} {
		error "native WM transform: cannot recover layouter rules"
	}
	set rules [string range $layouter_block $config_at [expr {$route_at - 1}]]
	set rules [string map [list "    + report | rules: yes\n" ""] $rules]
	set rules [_replace_once $rules "  + config | rules: rom\n" \
	       "  + config | rules: rom\n    + work_area | x: 0 | y: 28 | width: 1024 | height: 740\n"]

	set desktop [expr {$product eq "desktop"}]
	set gui_route [expr {$desktop ? "+ parent" : "+ child nitpicker"}]
	set ld_route [expr {$desktop ?
	       "    + service ROM | label_last: ld.lib.so | + parent\n" : ""}]
	set wm_binary [expr {$desktop ?
	       "  + binary | name: bin/sponge_wm\n" : ""}]
	set layouter_binary [expr {$desktop ?
	       "  + binary | name: bin/sponge_layouter\n" : ""}]
	set decorator_binary [expr {$desktop ?
	       "  + binary | name: bin/sponge_decorator\n" : ""}]
	set wm_hb [expr {$desktop ? "  @HB:wm@\n" : ""}]
	set layouter_hb [expr {$desktop ? "  @HB:layouter@\n" : ""}]
	set decorator_hb [expr {$desktop ? "  @HB:decorator@\n" : ""}]

	return "${wm_first}\n${wm_binary}${wm_hb}  + provides\n    + service Gui\n  + config\n    + policy | label_prefix: sponge_decorator | role: decorator\n    + policy | label_prefix: sponge_layouter  | role: layouter\n    + default-policy\n  + route\n${ld_route}    + service ROM | label: focus            | + child report_rom\n    + service ROM | label: window_request   | + child report_rom\n    + service ROM | label: decorator_margins | + child report_rom\n    + service Report | + child report_rom\n    + service Gui | ${gui_route}\n    + any-service\n      + parent\n      + any-child\n\n${layouter_first}\n${layouter_binary}${layouter_hb}${rules}  + route\n${ld_route}    + service ROM | label: window_list       | + child report_rom\n    + service ROM | label: focus_request     | + child report_rom\n    + service ROM | label: rules             | + child report_rom\n    + service ROM | label: focus_state       | + child report_rom\n    + service ROM | label: decorator_margins | + child report_rom\n    + service Report | + child report_rom\n    + any-service\n      + child sponge_wm\n      + parent\n      + any-child\n\n${decorator_first}\n${decorator_binary}${decorator_hb}  + config\n    + theme | source: default\n  + route\n${ld_route}    + service ROM | label: theme         | + parent\n    + service ROM | label: default.theme | + parent\n    + service ROM | label: window_chrome | + child report_rom\n    + service Report | + child report_rom\n    + any-service\n      + child sponge_wm\n      + parent\n      + any-child\n\n"
}


proc wm_trio::apply { text product } {
	if {![enabled]} {
		return $text
	}
	if {$product ni {alpha desktop}} {
		error "native WM transform: unknown product '$product'"
	}

	set wm_start     [string first "+ start wm |" $text]
	set lay_start    [string first "+ start layouter |" $text]
	set decor_start  [string first "+ start decorator |" $text]
	set config_start [string first "+ start sponge_configd |" $text]
	if {$wm_start < 0 || $lay_start < 0 || $decor_start < 0 \
	 || $config_start < 0 || !($wm_start < $lay_start \
	 && $lay_start < $decor_start && $decor_start < $config_start)} {
		error "native WM transform: upstream trio anchors missing or misordered"
	}

	set wm_block       [_block $text "+ start wm |" "+ start layouter |"]
	set layouter_block [_block $text "+ start layouter |" "+ start decorator |"]
	set decorator_block [_block $text "+ start decorator |" \
	                       "+ start sponge_configd |"]

	set text [string map [list \
		"+ child wm"             "+ child sponge_wm" \
		"label: layouter ->"     "label: sponge_layouter ->" \
		"label: decorator ->"    "label: sponge_decorator ->" \
		"label: wm ->"           "label: sponge_wm ->" \
		"report: layouter ->"    "report: sponge_layouter ->" \
		"report: decorator ->"   "report: sponge_decorator ->" \
		"report: wm ->"          "report: sponge_wm ->"] $text]

	set anchor "    + policy | label: sponge_layouter -> focus_request      | report: sponge-de -> focus_request"
	set extra "$anchor\n    + policy | label: sponge_layouter -> focus_state       | report: sponge_wm -> focus_state\n    + policy | label: sponge_wm -> window_request          | report: sponge_layouter -> window_request\n    + policy | label: sponge_wm -> decorator_margins       | report: sponge_decorator -> decorator_margins\n    + policy | label: sponge_decorator -> window_chrome    | report: sponge_wm -> window_chrome"
	set text [_replace_once $text $anchor $extra]

	# The route/policy rewrites above change byte offsets; slice only after
	# recomputing them against the transformed text.
	set wm_start     [string first "+ start wm |" $text]
	set config_start [string first "+ start sponge_configd |" $text]
	if {$wm_start < 0 || $config_start < 0 || $config_start <= $wm_start} {
		error "native WM transform: post-rewrite start anchors missing"
	}
	set prefix [string range $text 0 [expr {$wm_start - 1}]]
	set suffix [string range $text $config_start end]
	set nodes [_native_nodes $product $wm_block $layouter_block $decorator_block]
	set result "${prefix}${nodes}${suffix}"

	foreach forbidden {"+ start wm |" "+ start layouter |" "+ start decorator |"} {
		if {[string first $forbidden $result] >= 0} {
			error "native WM transform: upstream start node survived: $forbidden"
		}
	}
	return $result
}
