#
# \brief  Shared wiring for the top-level supervisor in the product media
# \author Sponge OS contributors
# \date   2026
#
# Every product scenario (the five desktop-disk variants) has to do the
# SAME four things for `sponge_supervisor` + `sponge_console` to be live:
#
#   1. derive the supervisor's two config templates from the desktop
#      config the scenario already generates, so staging stays the single
#      source of truth for the desktop composition;
#   2. hand the top-level `report_rom` the two new policies and move the
#      `system` sandbox's config ROM from `rom_sys` to the relay;
#   3. add a nitpicker domain for the console, above the domains the
#      shipped desktop already uses;
#   4. start the supervisor and the console BEFORE the `system` sandbox.
#
# Per-scenario copies of that would be five hand-maintained duplicates
# of the same wiring, so it lives here and each scenario calls:
#
#   source .../supervisor.inc
#   supervisor::prepare $sys_dir $sysconfig_hid
#   install_config [supervisor::tier0 { <the scenario's own config> }]
#   foreach m [supervisor::boot_modules] { lappend boot_modules $m }
#
# Every transform below is ASSERTED: a missing or drifted anchor aborts
# the scenario with a loud error instead of quietly booting a desktop
# without supervision. That is the reason this file is allowed to edit
# text at all.
#
# === Why the templates are derived, not authored =========================
#
# The desktop composition is DATA on disk (docs/14 §4.4: "init config is
# data, not image"): each scenario writes /system/init/system.config from
# a HID literal at staging time. The supervisor needs that exact text as
# its template, because it re-emits it verbatim with one substitution
# (@KILL@ -> a rising counter), and that substitution is the only restart
# lever a config owner has (genode/repos/os/src/lib/sandbox/config_model.h:
# `Start_model::matches()` compares only `name` and `version`).
#
# So the supervisor does NOT carry its own copy of the desktop config:
# the staged config IS the template, and staging stays the single source
# of truth. An authored second copy would drift on the first change to
# the desktop composition, and the drift would stay invisible until a
# fault made the supervisor rebuild a subtree that no longer matched the
# media.
#
# The templates reach the supervisor as a boot module rather than through
# `rom_sys`, because the supervisor lives in Tier 0 and rom_sys only
# exists behind the storage chain. That keeps its dependency set exactly
# what the acceptance scenario proved: Timer, Report, and its own two
# ROMs - no session into the subtree it supervises.
#
# === Version stamping ====================================================
#
# Every top-level `<start>` of the staged config is stamped with a
# sentinel version, which becomes @KILL@ in the templates and "1" in the
# config init actually boots with. "1" is exactly what the supervisor's
# FIRST publish substitutes (its counter starts at 0 and is pre-
# incremented), so the initial publish matches every running child and
# the desktop is never rebuilt at boot - a boot-time rebuild would be a
# spurious containment event, and the product media already spends
# minutes on the desktop's first boot.
#
# === The console variant =================================================
#
# `<system_console>` is the same config with the desktop CLIENTS removed
# (`sponge-de` and `alpha_probe`), so that while the console is armed the
# supervised subtree holds no Gui client at all. That is what makes "the
# console never coexists with the desktop" structural rather than a hope:
# the supervisor destroys the desktop subtree before it arms the console
# and rebuilds it only after the console reported DISARMED. The services
# (wm, layouter, decorator, configd, themed, pkgd, pkg_runtime) are
# deliberately KEPT - they are what the subtree needs to come back
# healthy without fetching anything again, and a GUI package running
# inside pkg_runtime is below the console's own nitpicker domain anyway.
#
# SPDX-License-Identifier: Apache-2.0
#

namespace eval supervisor {

	#
	# Sentinel written into the staged config's <start> version. A bare
	# HID token (never quoted: the `hid` tool double-quotes a quoted
	# value on XML conversion).
	#
	variable killver "SPONGE_KILLVER"

	#
	# The version the media boots with. Must equal the supervisor's first
	# substitution; asserted in prepare().
	#
	variable boot_version "1"

	#
	# Children whose `exited` is a CLEAN exit (a boot probe doing its
	# job) and must therefore never be read as a fault: Genode keeps an
	# exited child in the state report until it is abandoned, so an
	# unconditional "any exit is a fault" rule tears down a healthy
	# desktop at every boot. Mirrors the exclusion in sponge-de's health
	# watcher (repos/sponge/src/sponge-de/main.cc).
	#
	#
	# A scenario that adds another clean-exiting boot probe appends it
	# here (supervisor::ignore_exited) — a probe that did its job must
	# not be read as a fault, and a probe that FAILED should be allowed
	# to stay loud rather than being silenced by this list.
	#
	variable ignore_exited "alpha_probe,cfg_probe"

	#
	# Supervisor timing. poll_ms, fault_skipped, grace_ms, console_hold_ms
	# and boot_grace_ms are the acceptance scenario's proven defaults.
	#
	# settle_ms is NOT, and the deviation is MEASURED, not argued
	# (docs/evidence/product-supervision.md). The acceptance scenario runs
	# the sandbox heartbeat at 500 ms; the product media runs it at
	# 2000 ms, and the state report shows a RESPAWNED child climbing
	# skipped_heartbeats back over the threshold in 10-12 s rather than
	# 1.75 s. With the proven 3 s window the supervisor therefore declared
	# the fault "contained" while the respawned child had not yet reached
	# the detection threshold, and the console phase was structurally
	# unreachable - two kills in three ended in "contained" instead.
	#
	# The window must exceed the respawn-to-refault time or it races it,
	# so 20000 ms over the measured 12 s. It is also the wait for the
	# console's Gui session to close before the desktop is rebuilt, which
	# is why a longer window costs a longer dark period and nothing else.
	#
	variable poll_ms         250
	variable fault_skipped   3
	variable grace_ms        4000
	variable console_hold_ms 6000
	variable settle_ms       20000
	variable boot_grace_ms   30000

	# desktop clients dropped from the console variant
	variable console_drop { sponge-de alpha_probe }

	#
	# Wiring can be switched off as a whole, and then tier0() is a
	# pass-through and boot_modules() is empty. The one scenario that
	# uses this is the SPONGE_DISK_FAIL failure channel, whose entire
	# subject is "the nested init rejects a corrupted system.config" -
	# a diagnostic that predates the supervisor. Leaving it exactly as it
	# was keeps the two failures from being conflated.
	#
	variable enabled 1

	#
	# A top-level `<start>` node. run/sponge-desktop-disk-uefi-usb.run
	# prefixes its last node with a Tcl variable (`$logdisk_child+ start
	# system`), so that prefix is part of the pattern.
	#
	variable top_start_re {^(\$?[A-Za-z_][A-Za-z0-9_]*)?\+ start ([A-Za-z0-9_-]+)(\s|$)}

	# the supervised sandbox's config ROM route, replaced by the relay
	variable sys_config_route {+ service ROM | label: config | + child rom_sys | label: init/system.config}
	variable sys_config_relay {+ service ROM | label: config | + child report_rom}
}

#
# Fault-injection gate. OFF by default; shipped media must never carry a
# fault child.
#
#   SPONGE_SUPERVISOR_TEST unset / off / 0   (default)
#   SPONGE_SUPERVISOR_TEST on / 1 / takeover a persistent wedge
#   SPONGE_SUPERVISOR_TEST control           a bounded wedge the
#                                            supervised sandbox must
#                                            survive without any kill
#
proc supervisor::test_mode {} {
	if {![info exists ::env(SPONGE_SUPERVISOR_TEST)]} { return "off" }

	set v $::env(SPONGE_SUPERVISOR_TEST)

	if {$v eq "" || $v eq "0" || $v eq "off"} { return "off" }
	if {$v eq "1" || $v eq "on"}               { return "takeover" }
	if {$v eq "takeover" || $v eq "control"}  { return $v }

	error "SPONGE_SUPERVISOR_TEST='$v' is not a valid arm (expected: off, \
on/takeover, control)"
}

proc supervisor::_read { path } {
	if {![file exists $path]} { error "supervisor: '$path' does not exist" }
	set fh [open $path r]
	set text [read $fh]
	close $fh
	return $text
}

proc supervisor::_write { path text } {
	set fh [open $path w]
	puts -nonewline $fh $text
	close $fh
}

#
# Split text into lines, dropping a single trailing empty line so a
# block written with a trailing newline does not insert a blank line.
#
proc supervisor::_lines { text } {
	set l [split $text \n]

	if {[llength $l] && [lindex $l end] eq ""} { set l [lrange $l 0 end-1] }

	return $l
}

#
# Count non-overlapping occurrences of a LITERAL substring.
#
# Literal, not regexp: the anchors below are HID attribute lists, and a
# bare `|` is regexp alternation - a pattern built from one of them fails
# to compile at all.
#
proc supervisor::_count { text needle } {
	set n   0
	set idx [string first $needle $text]

	while {$idx >= 0} {
		incr n
		set idx [string first $needle $text [expr {$idx + 1}]]
	}

	return $n
}

#
# Rejoin lines the way the file had them.
#
# The staged HID ends with the end-of-data mark `-` and NO trailing
# newline (the run scripts close their `puts ... {` brace with `-}`, so
# the dash is the last byte written). `hid format` rejects a newline
# after the mark, so the trailing-newline-ness has to survive every
# transform here.
#
proc supervisor::_rejoin { lines had_newline } {
	set text [join $lines "\n"]

	if {$had_newline} { return "$text\n" }
	return $text
}

#
# Stamp the sentinel version onto every top-level `<start>` of a HID
# config. Only column-0 lines are stamped: a `<start>` nested inside
# some child's own config would be a different (and wrong) restart
# target, and the product configs have none.
#
proc supervisor::stamp { hid_path } {
	variable killver

	set text [supervisor::_read $hid_path]
	set nl [expr {[string index $text end] eq "\n"}]

	set lines [split $text \n]
	set out   {}
	set stamped 0

	# NOTE: the loop variable is never rebound - expect's `foreach` does
	# not write back to it, so a modified copy would be silently lost.
	foreach line $lines {
		if {[regexp {^\+ start [A-Za-z0-9_-]+(\s|$)} $line] &&
		    ![regexp {\|\s*version:} $line]} {
			lappend out "$line | version: $killver"
			incr stamped
		} else {
			lappend out $line
		}
	}

	if {$stamped == 0} {
		error "supervisor: no top-level <start> found in '$hid_path' - the \
desktop config layout drifted and nothing would be supervised"
	}

	supervisor::_write $hid_path [supervisor::_rejoin $out $nl]
	return $stamped
}

#
# Remove whole `<start>` blocks by name from a HID config. Errors if a
# requested block is absent, which is what stops a renamed child from
# silently producing a console variant that still starts a desktop.
#
proc supervisor::drop_children { text names } {
	set nl [expr {[string index $text end] eq "\n"}]
	set out {}
	set dropping 0
	set depth 0
	set name ""

	foreach line [split $text \n] {

		if {$dropping} {
			# a non-blank line indented no deeper than the <start> line
			# itself ends the block
			if {![regexp {^(\s*)(.*)$} $line -> ind rest] || $rest eq ""} {
				continue
			}
			if {[string length $ind] > $depth} { continue }
			set dropping 0
		}

		if {!$dropping &&
		    [regexp {^( *)\+ start ([A-Za-z0-9_-]+)(\s|$)} $line -> ind n]} {
			if {[lsearch -exact $names $n] >= 0} {
				set dropping 1
				set depth [string length $ind]
				set name  $n
				continue
			}
		}

		lappend out $line
	}

	if {$dropping} {
		error "supervisor: unterminated <start> block for '$name' in the \
desktop config"
	}

	foreach child $names {
		if {[regexp [format {^\+ start %s(\s|$)} $child] $out]} {
			error "supervisor: <start> '$child' survived the console-variant \
derivation - the console would still start a desktop"
		}
	}

	return [supervisor::_rejoin $out $nl]
}

#
# Insert HID text as the first children of a `<start>` node's own
# `<config>`: 'start_re' matches the node's first line, 'inner_re' the
# line the text goes after.
#
proc supervisor::_insert_into_block { text start_re inner_re insert } {
	set lines [split $text \n]
	set block -1
	set at    -1

	for {set i 0} {$i < [llength $lines]} {incr i} {
		set line [lindex $lines $i]

		if {$block < 0} {
			if {[regexp $start_re $line]} { set block $i }
			continue
		}

		# a non-blank line at column 0 ends the <start> block
		if {[string trim $line] ne "" && ![regexp {^\s} $line]} { break }

		if {[regexp $inner_re $line]} { set at $i; break }
	}

	if {$block < 0} {
		error "supervisor: no <start> node matching '$start_re' in the \
top-level config"
	}
	if {$at < 0} {
		error "supervisor: the node matching '$start_re' has no '$inner_re' \
child to extend"
	}

	set head [lrange $lines 0 $at]
	set tail [lrange $lines [expr {$at + 1}] end]

	return [join [concat $head [supervisor::_lines $insert] $tail] \n]
}

#
# Insert HID text before the last top-level `<start>` node (the supervised
# `system` sandbox), asserting that node really is last: it absorbs the
# RAM remainder, and a child created after it eats out of its pool
# (docs/17 §6.1).
#
proc supervisor::_insert_before_last_start { text insert } {
	variable top_start_re

	set lines [split $text \n]
	set last -1
	set last_name ""

	for {set i 0} {$i < [llength $lines]} {incr i} {
		if {[regexp -line $top_start_re [lindex $lines $i] -> pre n]} {
			set last $i
			set last_name $n
		}
	}

	if {$last < 0} {
		error "supervisor: the top-level config has no <start> node"
	}

	if {$last_name ne "system"} {
		error "supervisor: the last top-level <start> node is '$last_name', \
not 'system' - the RAM remainder clamp would be broken"
	}

	set head [lrange $lines 0 [expr {$last - 1}]]
	set tail [lrange $lines $last end]

	return [join [concat $head [supervisor::_lines $insert] $tail] \n]
}

#
# The Tier-0 nodes, plus the three rewrites the product topology needs.
#
#   report_rom  two new policies: the supervised config and the console's
#               arming report are written by the supervisor. The state
#               report it READS is already relayed by the product's
#               existing `label_suffix: system_state` policy.
#   system      its config ROM moves from rom_sys to the relay; the
#               binary, ld.lib.so and the .lib.so closure still come
#               from rom_sys/rom_lib.
#   nitpicker   a `console` domain ABOVE panel (1), launcher (4) and
#               default (3), so the console is the topmost client without
#               moving the domains the shipped desktop already uses.
#               Capture mirrors the whole panorama, so a pixel oracle
#               sees the console at any layer.
#   ordering    the supervisor and the console are created BEFORE the
#               system sandbox, which must stay the last top-level
#               <start> node.
#
proc supervisor::tier0 { config_text } {
	variable sys_config_route
	variable sys_config_relay
	variable enabled

	if {!$enabled} { return $config_text }

	if {[string first "+ start sponge_supervisor" $config_text] >= 0} {
		error "supervisor: the top-level config already starts \
sponge_supervisor - refusing to wire it twice"
	}

	set nodes   "+ start sponge_supervisor | caps: 400 | ram: 24M\n"
	append nodes "  + route\n"
	append nodes "    + service ROM | label_last: ld.lib.so | + parent\n"
	append nodes "    + service ROM | label: config    | + parent | label: supervisor.config\n"
	append nodes "    + service ROM | label: templates | + parent | label: supervisor.templates\n"
	append nodes "    + service ROM | label: supervisor -> system_state | + child report_rom\n"
	append nodes "    + service Report | + child report_rom\n"
	append nodes "    + service Timer | + child timer\n"
	append nodes "    + any-service\n"
	append nodes "      + parent\n"
	append nodes "      + any-child\n"
	append nodes "\n"
	append nodes "+ start console | caps: 400 | ram: 16M\n"
	append nodes "  + binary | name: sponge_console\n"
	append nodes "  + config | label: console | role: console\n"
	append nodes "  + route\n"
	append nodes "    + service ROM | label_last: ld.lib.so | + parent\n"
	append nodes "    + service ROM | label: console_state | + child report_rom\n"
	append nodes "    + service Gui | + child nitpicker\n"
	append nodes "    + any-service\n"
	append nodes "      + parent\n"
	append nodes "      + any-child\n"

	set policies  "    + policy | label: system -> config       | report: sponge_supervisor -> system_config\n"
	append policies "    + policy | label_suffix: console_state | report: sponge_supervisor -> console_state\n"

	set domain  "    + domain console | layer: 8 | content: client | label: no\n"
	append domain "    + policy | label_prefix: console | domain: console\n"

	set n [supervisor::_count $config_text $sys_config_route]
	if {$n != 1} {
		error "supervisor: expected exactly one '$sys_config_route' route, \
found $n - the supervised sandbox's config source drifted"
	}
	set out [string map [list $sys_config_route $sys_config_relay] $config_text]

	set out [supervisor::_insert_into_block $out {^\+ start report_rom(\s|$)} \
		{^  \+ config \| verbose: no\s*$} $policies]

	set out [supervisor::_insert_into_block $out {^\+ start nitpicker(\s|$)} \
		{^  \+ config(\s|$)} $domain]

	if {[supervisor::test_mode] ne "off"} {
		append nodes "\n"
		append nodes [supervisor::_test_probe_hid]
	}

	return [supervisor::_insert_before_last_start $out $nodes]
}

#
# Boot modules the product media must carry.
#
proc supervisor::ignore_exited { names } {
	variable ignore_exited
	foreach n $names {
		if {[string first $n $ignore_exited] < 0} {
			append ignore_exited ",$n"
		}
	}
}

proc supervisor::boot_modules {} {
	variable enabled

	if {!$enabled} { return {} }

	set m { sponge_supervisor sponge_console supervisor.templates supervisor.config }

	if {[supervisor::test_mode] ne "off"} { lappend m takeover_probe }

	return $m
}

#
# The fault-injection child, staged only when the gate is on.
#
# `restart_after_skipped` is deliberately ABSENT (the bare `<heartbeat/>`
# form): monitoring ON, local auto-restart OFF, so the supervisor is the
# single restart authority for this child. With the sandbox restarting it
# every 6 s forever, the fault would be contained indefinitely and the
# console would never be reached - which is the correct product
# behaviour for a fault the sandbox CAN fix, and the reason the control
# arm below exists.
#
# wedge_ms per arm:
#   takeover  60000 - outlives every window in the escalation chain
#   control      8000 - shorter than the escalation window
#                     (fault_skipped*heartbeat + poll_ms + grace_ms), so
#                     the fault heals INSIDE the supervisor's grace and
#                     no kill may happen at all.
#
# The child goes in BEFORE pkg_runtime, which must stay the last <start>
# of this config (RAM remainder clamp).
#
proc supervisor::_test_child_hid { mode } {
	#
	# ep_wedge with no attributes blocks its entrypoint FOREVER, on every
	# incarnation: a persistent fault, which is the only kind the
	# supervisor can escalate to the console (a fault that heals inside a
	# kill window is contained, by design - that is the control arm).
	#
	# The two arms differ ONLY in the child's own containment:
	#
	#   takeover  bare <heartbeat/> = monitoring on, restart_after_skipped
	#             0, so nothing below the supervisor restarts it and the
	#             supervisor is the single restart authority
	#   control   <heartbeat restart_after_skipped="3"/> = exactly the
	#             product media's own containment deployment, so the
	#             sandbox abandons and respawns the wedged child itself,
	#             forever, and the supervisor must NOT kill anything
	#
	# The control arm is therefore not a smaller proof: it is the proof of
	# the escalation rule, and its negative assertion (no kill, no
	# console) is the half of the property that matters most - a
	# supervisor that tears down a healthy desktop on a timer is worse
	# than one that does nothing. It also proves the clean-exit exclusion:
	# alpha_probe exits 0 after its PASS marker, and an unexcluded exit
	# would make the supervisor kill the desktop minutes later.
	#
	set hb "  + heartbeat"
	if {$mode eq "control"} { set hb "  + heartbeat | restart_after_skipped: 3" }

	set h "+ start wedge | caps: 200 | ram: 4M\n"
	append h "  + binary | name: bin/ep_wedge\n"
	append h "$hb\n"
	append h "  + config\n"
	append h "  + route\n"
	append h "    + service ROM | label_last: ld.lib.so | + parent\n"
	append h "    + service Timer | + parent\n"
	append h "    + any-service\n"
	append h "      + parent\n"
	append h "      + any-child\n"
	return $h
}

#
# The pixel oracle, Tier-0, test gate only: a Capture client that reports
# which of the configured colours the composited screen centre carries.
#
# The "desktop" colour is the product nitpicker's background
# (#1e1e2e): with no window in the `default` domain the composited
# screen IS that colour, so the pixel claim distinguishes "the desktop
# owns the display" from "the fault console owns the display" without
# depending on any particular theme.
#
# The sampled point is deliberately OFF-CENTRE (768x576, bottom-right
# quadrant). The product media keeps a visible mouse cursor, nitpicker
# parks it at the screen centre at boot, and the upstream `pointer`
# component paints its glyph exactly there - a centre sample on this
# media reads the CURSOR (measured: 0x909490 where the desktop
# background is 0x1e1e2e). The acceptance topology has no cursor and
# still samples the centre, which is its default.
#
proc supervisor::_test_probe_hid {} {
	set h "+ start takeover_probe | caps: 400 | ram: 16M\n"
	append h "  + binary | name: takeover_probe\n"
	append h "  + route\n"
	append h "    + service ROM | label_last: ld.lib.so | + parent\n"
	append h "    + service Capture | + child nitpicker\n"
	append h "    + service Timer | + child timer\n"
	append h "    + any-service\n"
	append h "      + parent\n"
	append h "      + any-child\n"
	append h "  + config | poll_ms: 250 | sample_x: 768 | sample_y: 576\n"
	append h "    + state | name: desktop | color: #1e1e2e\n"
	append h "    + state | name: console | color: #601010\n"
	return $h
}

#
# Binaries the fault-injection arms need on the DISK, not in image.elf:
# both are plain base-lib components with no libc and no Qt, so they
# ride the existing rom_sys closure. A missing one is a hard error here
# rather than a silent "the child cannot be created" at boot.
#
proc supervisor::stage_test_binaries { sys_dir } {
	foreach b { ep_wedge } {
		if {![file exists "bin/$b"]} {
			error "supervisor: bin/$b missing - the scenario must build test/$b when SPONGE_SUPERVISOR_TEST is set"
		}
		exec cp -fL "bin/$b" "$sys_dir/bin/$b"
	}
}

proc supervisor::_insert_test_child { hid_path mode } {
	set lines [split [supervisor::_read $hid_path] \n]
	set at    -1

	for {set i 0} {$i < [llength $lines]} {incr i} {
		if {[regexp {^\+ start pkg_runtime(\s|$)} [lindex $lines $i]]} {
			set at $i
			break
		}
	}

	if {$at < 0} {
		error "supervisor::prepare: no top-level '+ start pkg_runtime' in \
'$hid_path' - the test child cannot be placed without breaking the \
pkg_runtime-last RAM clamp"
	}

	set head [lrange $lines 0 [expr {$at - 1}]]
	set tail [lrange $lines $at end]

	supervisor::_write $hid_path [join [concat $head \
		[supervisor::_lines [supervisor::_test_child_hid $mode]] $tail] \n]
}

#
# Build both templates and the supervisor's policy, and leave the staged
# HID carrying the boot version so the scenario's own HID -> XML
# conversion produces the config init actually boots with.
#
proc supervisor::prepare { sys_dir hid_path } {
	variable killver
	variable boot_version
	variable ignore_exited
	variable console_drop
	variable poll_ms
	variable fault_skipped
	variable grace_ms
	variable console_hold_ms
	variable settle_ms
	variable boot_grace_ms

	if {$boot_version ne "1"} {
		error "supervisor::prepare: boot_version must be 1 - it is what the \
supervisor's first @KILL@ substitution yields, and any other value would \
rebuild the whole subtree at boot"
	}

	set mode [supervisor::test_mode]

	if {$mode ne "off"} {
		supervisor::stage_test_binaries $sys_dir
		supervisor::_insert_test_child $hid_path $mode
		puts "sponge-supervisor: FAULT-INJECTION ARM '$mode' \
(SPONGE_SUPERVISOR_TEST) - a test gate, never shipped media"
	}

	set stamped [supervisor::stamp $hid_path]
	set full_text [supervisor::_read $hid_path]

	set console_text [supervisor::drop_children $full_text $console_drop]

	if {$console_text eq $full_text} {
		error "supervisor: the console variant is identical to the desktop \
config - the desktop clients were not found"
	}

	set hid_tool [file join [genode_dir] tool hid]
	set templates "<!-- derived from the staged /system/init/system.config by \
run/supervisor.inc; do not hand-edit -->\n"

	foreach {tag text} [list "system" $full_text \
	                     "system_console" $console_text] {

		set tmp "[run_dir]/supervisor.$tag.hid"
		supervisor::_write $tmp $text

		set xml [exec $hid_tool format --output-xml $tmp]
		file delete $tmp

		set found [regexp -all $killver $xml]
		set want [supervisor::_count $text "\n+ start "]

		if {$want == 0} {
			error "supervisor: template <$tag> has no <start> node"
		}
		if {$found != $want} {
			error "supervisor: template <$tag> carries $found restart \
versions for $want children - the console variant would leave a child \
unrestartable"
		}

		append templates "<$tag>\n"
		append templates [string map [list $killver {@KILL@}] $xml]
		append templates "</$tag>\n\n"
	}

	supervisor::_write "bin/supervisor.templates" $templates

	#
	# The supervisor's own policy. Written as HID and converted, because a
	# boot module named `.config` is validated as HID by boot_dir (the
	# same dance the acceptance scenario performs). Both files land in
	# bin/, which is where build_boot_image expects a boot module to be.
	#
	set pol "[run_dir]/supervisor.config.hid"
	set pol_lines [list "config" \
		"+ policy | poll_ms: $poll_ms | fault_skipped: $fault_skipped" \
		"         | grace_ms: $grace_ms | console_hold_ms: $console_hold_ms" \
		"         | settle_ms: $settle_ms | boot_grace_ms: $boot_grace_ms" \
		"         | ignore_exited: $ignore_exited" \
		"-"]

	supervisor::_write $pol [supervisor::_rejoin $pol_lines 0]

	exec $hid_tool format --output-xml $pol > "bin/supervisor.config"
	file delete $pol

	#
	# Hand the staged HID back with the boot version in place of the
	# sentinel, so the scenario's own conversion writes the config init
	# boots with - and that config matches the supervisor's first publish.
	#
	supervisor::_write $hid_path [string map [list $killver $boot_version] \
		$full_text]
}