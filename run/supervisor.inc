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
# P3.4 per-name markers: the runtime SUBTREES - every `pkg_runtime*`
# sandbox and `driver_runtime`, all direct children of `system` - are
# stamped with a PER-NAME sentinel (`SPONGE_KILLVER_<name>`) that
# becomes `@KILL:<name>@` in the templates. The supervisor keeps a
# fixed name->version table for those markers, so a containment action
# can bump ONE runtime child's version while every sibling node renders
# byte-identical bytes and is never rebuilt. Every other node keeps the
# plain sentinel (global counter). The synthetic fault child
# `pkg_runtime_fault` (the SPONGE_SUPERVISOR_TEST=contain arm) is named
# to match the runtime pattern on purpose: that is what makes it
# containable-by-name.
#
# === Per-child heartbeat gate values =====================================
#
# Each contained child's `<heartbeat>` node carries the gate triplet
# (restart_after_skipped / restart_budget / startup_grace_ms). The five
# desktop-disk scenarios declare their per-child values with
# `supervisor::heartbeat_values` and mark the nodes in their staged HID
# with `@HB:<name>@` placeholders, which prepare() renders. The default
# triplet is the product deployment (3 / 3 / 120000) and renders
# byte-identical to the previous inline literals - a scenario that does
# not declare a name still gets the defaults, and a name that cannot be
# resolved aborts staging loudly.
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
	# value on XML conversion). The runtime subtrees get
	# `<killver>_<name>` instead - see the stamping note above.
	#
	variable killver "SPONGE_KILLVER"

	#
	# Names stamped with the PER-NAME sentinel (the runtime subtrees).
	# Filled by stamp(), consumed by prepare() for the @KILL:<name>@
	# template markers and the boot-version mapping.
	#
	variable stamped_names {}

	#
	# Per-child heartbeat gate values (P3.4), set by the scenarios via
	# supervisor::heartbeat_values. The product defaults are the triplet
	# (restart_after_skipped 3, restart_budget 3, startup_grace_ms
	# 120000) and render byte-identical to the previous inline literals.
	#
	variable hb_values {}

	#
	# Per-subtree supervisor gate overrides (P3.4), a dict
	# `name -> {grace_ms settle_ms}` written into the supervisor policy
	# as `+ gate | key: grace_ms.<name>=<ms>` rows. Empty by default:
	# every subtree uses the global grace_ms/settle_ms until a scenario
	# opts a name in.
	#
	variable per_name_gates {}

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
#   SPONGE_SUPERVISOR_TEST contain           a persistent wedge in one
#                                            runtime subtree that the
#                                            supervisor must contain BY
#                                            NAME - only that child's
#                                            node is rebuilt
#
proc supervisor::test_mode {} {
	if {![info exists ::env(SPONGE_SUPERVISOR_TEST)]} { return "off" }

	set v $::env(SPONGE_SUPERVISOR_TEST)

	if {$v eq "" || $v eq "0" || $v eq "off"} { return "off" }
	if {$v eq "1" || $v eq "on"}               { return "takeover" }
	if {$v eq "takeover" || $v eq "control" ||
	    $v eq "contain"}                       { return $v }

	error "SPONGE_SUPERVISOR_TEST='$v' is not a valid arm (expected: off, \
on/takeover, control, contain)"
}

#
# Per-child heartbeat gate values (P3.4). 'values' is a dict
# `name -> {restart_after_skipped restart_budget startup_grace_ms}`;
# each name must own a `@HB:<name>@` placeholder in the staged HID and
# vice versa - both directions are staging anchors, the same fail-loud
# rule as every other anchor in this file.
#
proc supervisor::heartbeat_values { values } {
	variable hb_values
	set hb_values $values
}

proc supervisor::_heartbeat_line { name } {
	variable hb_values

	set vals {}
	foreach {n v} $hb_values {
		if {$n eq $name} { set vals $v }
	}

	if {[llength $vals] != 3} {
		error "supervisor: heartbeat values for '$name' must be \
{restart_after_skipped restart_budget startup_grace_ms} - got '$vals'"
	}

	lassign $vals ras rb sg

	# no leading indent: the @HB:<name>@ placeholder line carries it
	return "+ heartbeat | restart_after_skipped: $ras | restart_budget: $rb | startup_grace_ms: $sg"
}

#
# Render every `@HB:<name>@` placeholder in the staged HID from the
# per-child values. The rendered line is byte-identical to the previous
# inline literal when the values are the product defaults
# (3 / 3 / 120000).
#
proc supervisor::substitute_heartbeats { text } {
	variable hb_values

	set used {}

	foreach {n v} $hb_values {
		set ph "@HB:${n}@"
		if {[string first $ph $text] < 0} {
			error "supervisor: heartbeat values declared for '$n' but no \
@HB:${n}@ placeholder exists in the staged config - the per-child gate \
exposure drifted"
		}
		set text [string map [list $ph [supervisor::_heartbeat_line $n]] $text]
		lappend used $n
	}

	if {[regexp {@HB:([A-Za-z0-9_-]+)@} $text -> leftover]} {
		error "supervisor: heartbeat placeholder for '$leftover' has no \
supervisor::heartbeat_values entry - the per-child gate exposure drifted"
	}

	return $text
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
# The runtime SUBTREES (`pkg_runtime*`, `driver_runtime`) get the
# PER-NAME sentinel `<killver>_<name>` so the supervisor can bump one
# runtime child's version without touching its siblings (P3.4); every
# other node keeps the plain sentinel (global counter). The stamped
# per-name set is recorded in `stamped_names`.
#
proc supervisor::stamp { hid_path } {
	variable killver
	variable stamped_names

	set stamped_names {}

	set text [supervisor::_read $hid_path]
	set nl [expr {[string index $text end] eq "\n"}]

	set lines [split $text \n]
	set out   {}
	set stamped 0

	# NOTE: the loop variable is never rebound - expect's `foreach` does
	# not write back to it, so a modified copy would be silently lost.
	foreach line $lines {
		if {[regexp {^\+ start ([A-Za-z0-9_-]+)(\s|$)} $line -> n] &&
		    ![regexp {\|\s*version:} $line]} {
			if {[string match "pkg_runtime*" $n] || $n eq "driver_runtime"} {
				lappend out "$line | version: ${killver}_${n}"
				lappend stamped_names $n
			} else {
				lappend out "$line | version: $killver"
			}
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
# The wedge happens AFTER the child has answered heartbeats
# (`wedge_after_ms`), not inside its constructor: a child that has never
# answered a heartbeat is legitimately classified as STARTING by the
# sandbox (the startup grace, docs/evidence/startup-grace.md), and the
# supervisor exempts starting children by policy. A fault injected
# inside a constructor would therefore test the exemption, not the
# escalation.
#
# wedge_after_ms per arm - how long a RESPAWNED child stays responsive
# before it wedges again. That number has to be read against the
# supervisor's settle window (20000 here), not only against the grace:
# a fault that recurs later than the settle window is declared
# "contained" on every round and the console phase is structurally
# unreachable - the same race the settle_ms row exists for.
#
#   takeover  6000 - the fault recurs well inside the 20 s settle
#                     window, so "contained" is never reached and the
#                     console is; the child's own containment is off
#                     (bare <heartbeat/>), so nothing else restarts it
#   control   9000 - the fault lands inside the sandbox's own kill
#                     budget (10 s) instead of past it: the sandbox
#                     abandons and respawns the child itself, every
#                     ~10 s, and the fault clears with it, so the
#                     supervisor's 4 s escalation window never fills
#                     and nothing may be killed. The supervisor does
#                     observe the fault - skipped_heartbeats reaches 3
#                     at 8 s - which is the arm's point.
#   contain   3000 - the fault RECURS (persistent) but must be
#                     contained BY NAME on every round: only
#                     pkg_runtime_fault's node is rebuilt. 3000 ms
#                     exceeds the victim's own settle (1000 ms) plus
#                     the detection margin (the state report's 1000 ms
#                     delay + the 250 ms poll ≈ 1250 ms), so the
#                     "contained" verdict always lands on genuine
#                     health, and the recurrence is long enough for the
#                     DE's functional PASS to be a POST-wedge receipt.
#
# The child goes in BEFORE the package runtimes (`pkg_runtime` in the
# legacy topology, `pkg_runtime_simple` first in the P2.4 grouped one),
# because their clamp holder — `pkg_runtime`, or `pkg_runtime_browser`
# after the P2.4 port — must stay the last <start> of this config (RAM
# remainder clamp). The anchor is the `pkg_runtime` name PREFIX so both
# topologies resolve to the same insertion point: directly above the
# runtime block.
#
proc supervisor::_test_child_hid { mode } {
	#
	# ep_wedge in `wedge_after_ms` mode returns from its constructor
	# (so it becomes responsive and answers heartbeats) and parks its
	# entrypoint later: a persistent fault, which is the only kind the
	# supervisor can escalate to the console (a fault that heals inside a
	# kill window is contained, by design - that is the control arm).
	#
	# The arms differ in the child's own containment and in its NAME:
	#
	#   takeover  bare <heartbeat/> = monitoring on, restart_after_skipped
	#             0, so nothing below the supervisor restarts it and the
	#             supervisor is the single restart authority
	#   control   <heartbeat restart_after_skipped="3"/> = exactly the
	#             product media's own containment deployment, so the
	#             sandbox abandons and respawns the wedged child itself,
	#             forever, and the supervisor must NOT kill anything
	#   contain   bare <heartbeat/> again (NOT restart_budget-driven:
	#             restart_budget would stop the sandbox restarting it
	#             after three tries and leave a dead node behind), named
	#             `pkg_runtime_fault` so the per-name stamp marks it as
	#             a containable runtime subtree
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
	set name "wedge"
	set wedge_after 6000
	if {$mode eq "control"} {
		set hb "  + heartbeat | restart_after_skipped: 3"
		set wedge_after 9000
	}
	if {$mode eq "contain"} {
		set name "pkg_runtime_fault"
		set wedge_after 3000
	}

	set h "+ start $name | caps: 200 | ram: 4M\n"
	append h "  + binary | name: bin/ep_wedge\n"
	append h "$hb\n"
	append h "  + config | wedge_after_ms: $wedge_after\n"
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
		#
		# Anchor on the pkg_runtime name PREFIX: it matches the
		# legacy `+ start pkg_runtime | ...` node AND the grouped
		# `+ start pkg_runtime_simple | ...` (the first runtime of
		# the P2.4 block), so both topologies place the test child
		# directly above the runtime block and keep the clamp
		# holder last.
		#
		if {[regexp {^\+ start pkg_runtime} [lindex $lines $i]]} {
			set at $i
			break
		}
	}

	if {$at < 0} {
		error "supervisor::prepare: no top-level '+ start pkg_runtime' \
(family) in '$hid_path' - the test child cannot be placed without \
breaking the runtime-last RAM clamp"
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
	variable stamped_names
	variable per_name_gates

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

	if {$mode eq "contain"} {
		#
		# The victim's own supervisor gates (P3.4 per-subtree
		# lookups). The recurrence (wedge_after_ms 3000) exceeds
		# settle (1000) + the detection margin (the state report's
		# 1000 ms delay + the 250 ms poll), so "contained" is always
		# declared on genuine health and the console phase is never
		# reached - the arm's whole point.
		#
		set per_name_gates [dict create pkg_runtime_fault { 1000 1000 }]
	}

	# per-child heartbeat gate values (P3.4) before anything reads the HID
	set hb_text [supervisor::substitute_heartbeats \
	             [supervisor::_read $hid_path]]
	supervisor::_write $hid_path $hb_text

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

	#
	# Per-name sentinels map to `@KILL:<name>@` BEFORE the plain one:
	# string map matches in list order at each position, and the bare
	# sentinel is a prefix of every per-name one.
	#
	set marker_map {}
	foreach n $stamped_names {
		lappend marker_map "${killver}_${n}" "@KILL:${n}@"
	}
	lappend marker_map $killver {@KILL@}

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
		append templates [string map $marker_map $xml]
		append templates "</$tag>\n\n"
	}

	supervisor::_write "bin/supervisor.templates" $templates

	#
	# The supervisor's own policy. Written as HID and converted, because a
	# boot module named `.config` is validated as HID by boot_dir (the
	# same dance the acceptance scenario performs). Both files land in
	# bin/, which is where build_boot_image expects a boot module to be.
	#
	# The per-subtree gate overrides (P3.4) ride as `key: <lookup>=<ms>`
	# rows: a dotted ATTRIBUTE name does not survive the HID parser, but
	# the dotted key inside a value does, and the supervisor's text scan
	# reads it back verbatim (grace_ms.<name> / settle_ms.<name>).
	#
	set pol "[run_dir]/supervisor.config.hid"
	set pol_lines [list "config" \
		"+ policy | poll_ms: $poll_ms | fault_skipped: $fault_skipped" \
		"         | grace_ms: $grace_ms | console_hold_ms: $console_hold_ms" \
		"         | settle_ms: $settle_ms | boot_grace_ms: $boot_grace_ms" \
		"         | ignore_exited: $ignore_exited"]

	foreach {name vals} $per_name_gates {
		lassign $vals g s
		lappend pol_lines "  + gate | key: grace_ms.${name}=${g}"
		lappend pol_lines "  + gate | key: settle_ms.${name}=${s}"
	}
	lappend pol_lines "-"

	supervisor::_write $pol [supervisor::_rejoin $pol_lines 0]

	exec $hid_tool format --output-xml $pol > "bin/supervisor.config"
	file delete $pol

	#
	# Hand the staged HID back with the boot version in place of every
	# sentinel, so the scenario's own conversion writes the config init
	# boots with - and that config matches the supervisor's first publish
	# ("1" on every node, named or plain).
	#
	set boot_map {}
	foreach n $stamped_names {
		lappend boot_map "${killver}_${n}" $boot_version
	}
	lappend boot_map $killver $boot_version

	supervisor::_write $hid_path [string map $boot_map $full_text]
}