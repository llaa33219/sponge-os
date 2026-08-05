#
# \brief  run/qmp.inc — shared Tcl helper for driving QEMU guests via QMP
# \author Sponge OS contributors
# \date   2026
#
# Sourced by run scripts that need host-driven guest input:
#
#     source [file join [file dirname [file normalize [info script]]] qmp.inc]
#     set qmp_port [expr {20000 + ([pid] % 20000)}]
#     append qemu_args " -qmp tcp:127.0.0.1:${qmp_port},server=on,wait=off "
#     set qmp_chan [qmp_connect $qmp_port]
#     qmp_exec_target $qmp_chan 300        ;# bounded expect for QMP-TARGET marker
#     run_genode_until {.*some-probe: PASS.*} 120
#     qmp_disconnect $qmp_chan
#
# QMP-TARGET marker contract (component → host):
#
#     QMP-TARGET click <gx> <gy>
#     QMP-TARGET drag <x1> <y1> <x2> <y2>
#     QMP-TARGET type <string>
#     QMP-TARGET move <dx> <dy>             (closed-loop PS/2 relative move)
#
# Probes in observe mode log these lines on the serial console; the run
# script's bounded expect on global `qemu_spawn_id` (set by the Genode
# run tool's power_on/qemu:163, see run/sponge-power.run:228-244 for the
# in-repo precedent) catches the marker and dispatches to the matching
# QMP action. Every wait is bounded — fail loud, never hang.
#
# Requires: Tcl 8.x built-in `socket` (TCP only; matches `-qmp tcp:`),
#           `expect`, no other external dependencies.
#
# Pointer delivery:
#
#   Two pointer models are supported, both proven on this host (QEMU
#   system-x86_64 11.0.2, -nographic):
#
#   1. Absolute (qmp_click via qmp_pointer_move) — emits QMP
#      input-send-event ABS_MOTION events on the usb-tablet axis
#      (0..32767 per axis, scaled from guest pixels by 32767/1024,
#      32767/768). Under -nographic the usb-tablet is NOT bound to a
#      QemuConsole and nitpicker treats Input::Absolute_motion values
#      as raw pixels — so every abs event in 0..32767 lands off-screen
#      on a 1024x768 display, and nitpicker's pointer sanitizer
#      (`main.cc:780-810`) clamps all of them to the screen center
#      (~512, 384). (Documented in
#      docs/evidence/task-3-phase10-interactive.md §"W1's ~29px y-drift
#      root cause — NOT a QEMU translation bug".) This is why W1's
#      "click worked" — the demo domain (192,172,640,480) is large
#      enough that the screen center lands inside it. The first-phase
#      criterion-1 proof STILL holds (the click traverses the real
#      usb-tablet → pc_usb_host → usb_hid → event_filter → nitpicker →
#      sponge-de chain) but the click is not precisely targeted.
#
#   2. Relative (qmp_ps2_click / qmp_move_rel) — emits QMP
#      input-send-event REL events on the PS/2 mouse axis. The PS/2
#      mouse is the QEMU-default "current" pointer device
#      (`query-mice` shows current:true on the PS/2 mouse under
#      -nographic). event_filter's `accelerate` chain
#      (sensitivity_percent=1000, max=50, curve=127, the recipe's
#      standard config at genode/repos/os/recipes/raw/drivers_interactive-
#      pc/event_filter.config) gives a non-linear LUT:
#        rel-1   →  1px per event  (precision steps)
#        rel-50  →  100px per event (full acceleration)
#        rel-100 → 150px per event (clamped)
#      This is W3's proven recipe (see
#      docs/evidence/task-3-phase10-interactive.md) and lands the
#      pointer within ±1px of any absolute target after a clamp-to-
#      (0,0) + coarse (rel-50) + fine (rel-1) sequence. The same
#      hardware input chain as abs (QMP → emulated PS/2 controller
#      → ps2 driver → event_filter → nitpicker → sponge-de) — just
#      the PS/2 branch.
#
#   The closed-loop move protocol (QMP-TARGET move) is also supported
#   for scenarios that want pixel-exact landing on an absolute target
#   by reading the nitpicker pointer position from a report ROM
#   (W5's approach in docs/evidence/task-5-phase10-interactive.md).
#   It is not used by the default recipes in this file.
#
# Keyboard:
#
#   `send-key` requires the qcode/KeyValue object form on QEMU 11
#   (`{"type":"qcode","data":"h"}` per key, NOT the string form
#   `["h"]` rejected with `Invalid parameter type for 'keys[0]',
#   expected: object`). The string form is a latent W1 bug fixed
#   here. The proc qmp_send_key emits the correct object form.
#
# Serial line endings:
#
#   QEMU's `-nographic` serial console emits lines terminated with
#   `CR CR LF` (verified by `od -c` on a QEMU serial capture), not
#   just `CR LF`. The expect patterns in qmp_exec_target anchor on
#   `\r*\n` (W4's fix for the click marker that never matched with
#   `\r?\n`).
#

##
# Read one line from chan with a hard deadline. A stuck QMP socket must
# fail loud — never hang the run script forever.
#
proc qmp_read_line { chan {deadline_ms 5000} } {
	set start [clock milliseconds]
	while {1} {
		if {![eof $chan]} {
			if {[gets $chan line] >= 0} {
				return $line
			}
		} else {
			puts stderr "\nqmp: EOF on QMP socket (QEMU died?)"
			exit 1
		}
		if {[expr {[clock milliseconds] - $start}] > $deadline_ms} {
			puts stderr "\nqmp: timeout reading from QMP socket (${deadline_ms}ms)"
			exit 1
		}
		after 10
	}
}

##
# Pick a free TCP port on the loopback for QEMU's QMP server. Tcl's
# `socket -server` on port 0 lets the kernel pick a free port, then we
# close the listener and return the number. QEMU binds it next; the
# window between our close and QEMU's bind is short enough that the
# kernel won't reuse the port for someone else on a quiet loopback.
#
proc qmp_pick_port {} {
	set s ""
	set port 0
	if {![catch {socket -server apply {args {}} 0} c]} {
		set s $c
		set port [lindex [fconfigure $s -sockname] 2]
		close $s
	}
	if {$port == 0} {
		# Fallback: PID-derived port (collision-prone on retries).
		set port [expr {20000 + ([pid] % 20000)}]
	}
	return $port
}

##
# Open TCP socket to QEMU's QMP server, complete the capability handshake.
# Returns the channel handle. Fails loud (exit 1) on any error.
#
proc qmp_connect { port } {
	# Retry the connect briefly — `-qmp ...,server=on,wait=off' means QEMU
	# does not block on us, but in the moments after spawn the listener
	# may not yet be ready.
	set chan ""
	set tries 0
	while {$tries < 100} {
		if {![catch {socket 127.0.0.1 $port} c]} {
			set chan $c
			break
		}
		after 50
		incr tries
	}
	if {$chan eq ""} {
		puts stderr "\nqmp: failed to connect to 127.0.0.1:$port after 100 tries"
		exit 1
	}
	fconfigure $chan -buffering line -translation crlf -blocking 0

	# Read QMP greeting — QEMU sends one line beginning with the literal
	# text "QMP" wrapped in JSON. We just check for that marker; full JSON
	# parsing is overkill here.
	set greeting [qmp_read_line $chan 5000]
	if {![regexp {^\{"QMP"} $greeting]} {
		puts stderr "\nqmp: malformed greeting: $greeting"
		exit 1
	}

	# Enter capabilities negotiation mode → command mode.
	puts $chan {{"execute":"qmp_capabilities"}}
	flush $chan
	set ack [qmp_read_line $chan 5000]
	if {![regexp {^\{"return":\s*\{?\}} $ack]} {
		puts stderr "\nqmp: qmp_capabilities handshake failed: $ack"
		exit 1
	}

	return $chan
}

##
# Send one QMP command (JSON string, no trailing newline needed) and read
# the response. Async {"event": ...} lines are skipped. Dies loud on
# {"error": ...} or timeout.
#
proc qmp_cmd { chan json } {
	puts $chan $json
	flush $chan

	set start [clock milliseconds]
	while {1} {
		set line [qmp_read_line $chan 10000]
		# Skip async event notifications.
		if {[regexp {^\{"event":} $line]} { continue }
		if {[regexp {^\{"error":\s*\{(.*)\}\}} $line dummy detail]} {
			puts stderr "\nqmp: command failed: $line"
			exit 1
		}
		if {[regexp {^\{"return":} $line]} {
			return $line
		}
		# Unknown line — log and continue (some QEMU versions send banners).
		if {[clock milliseconds] - $start > 10000} {
			puts stderr "\nqmp: no return for command: $json (last line: $line)"
			exit 1
		}
	}
}

##
# Scale guest (1024x768) coordinates to QEMU usb-tablet absolute axes
# (0..32767). Returns a Tcl list {ax ay}.
#
# IMPORTANT NOTE (Phase 10 W1 / W3, QEMU system-x86_64 11.0.2 with
# `-nographic'): the usb-tablet is not bound to a QemuConsole under
# -nographic. nitpicker treats Input::Absolute_motion coordinates as
# raw pixels, so every abs value in 0..32767 lands off-screen on a
# 1024x768 display and nitpicker's pointer sanitizer clamps all of
# them to the screen center (~512, 384). The W1-era "~29px y-drift"
# diagnosis was really screen-center-y (384) - intended-y (412) = -28,
# not a per-event QEMU translation offset (proven by W3: NO divisor
# choice in this proc changes the landing position — see
# docs/evidence/task-3-phase10-interactive.md §"W1's ~29px y-drift
# root cause — NOT a QEMU translation bug"). The click DOES reach
# sponge-de (criterion 1's usb-tablet chain is exercised end-to-end)
# but is not precisely targeted. For precise targeting use
# qmp_ps2_click or qmp_move_rel below (PS/2 relative, ±1px).
#
proc qmp_abs { x y } {
	set ax [expr {($x * 32767 + 512) / 1024}]
	set ay [expr {($y * 32767 + 384) / 768}]
	return [list $ax $ay]
}

##
# Send an absolute-axis pointer motion event via input-send-event.
# The usb-tablet accepts abs axes; QEMU routes the event to the bound
# usb-tablet device automatically. QMP's abs event format is per-axis
# (one event per axis), not a combined x/y event — see the QEMU QMP
# docs for `InputMoveEvent`. We send both axes in one input-send-event
# call so they reach the guest atomically.
#
# See qmp_abs for the abs-clamp caveat under -nographic. For precise
# targeting use the PS/2 relative procs below.
#
proc qmp_pointer_move { chan x y } {
	lassign [qmp_abs $x $y] ax ay
	qmp_cmd $chan [format {{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":%d}},{"type":"abs","data":{"axis":"y","value":%d}}]}}} $ax $ay]
}

##
# Send a button event. `state` is the literal string "down" or "up" used
# in the JSON; `btn` is the QEMU button name ("left" / "right" / "middle"
# — QMP's InputBtnEvent accepts these symbolic names). The JSON key is
# `button` (not `btn`) per the QMP schema.
#
proc qmp_button { chan btn state } {
	qmp_cmd $chan [format {{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":%s,"button":"%s"}}]}}} [expr {$state eq "down" ? "true" : "false"}] $btn]
}

##
# Click (press + release BTN_LEFT) at the given guest coordinates via
# the USB tablet's absolute axis.
#
# Important caveat: under QEMU -nographic the usb-tablet is not bound
# to a QemuConsole, so abs events get clamped to screen center by
# nitpicker's pointer sanitizer. This click is sufficient for
# criterion 1 (the click DOES reach sponge-de, proving the real
# usb-tablet chain) but it is not precisely targeted. Use
# qmp_ps2_click below for precise targeting.
#
proc qmp_click { chan x y } {
	qmp_pointer_move $chan $x $y
	after 15
	qmp_button $chan left down
	after 15
	qmp_button $chan left up
}

##
# Send a single PS/2 mouse relative motion event. axis is "x" or "y";
# value is the signed rel value. Used by the closed-loop move
# protocol (QMP-TARGET move <dx> <dy>).
#
proc qmp_move_rel { chan axis value } {
	qmp_cmd $chan [format {{"execute":"input-send-event","arguments":{"events":[{"type":"rel","data":{"axis":"%s","value":%d}}]}}} $axis $value]
}

##
# PS/2 mouse press/release. value is the literal "true" (down) or
# "false" (up).
#
proc qmp_ps2_button { chan down } {
	set val [expr {$down ? "true" : "false"}]
	qmp_cmd $chan [format {{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"down":%s,"button":"left"}}]}}} $val]
}

##
# Click (press + release BTN_LEFT) at the given guest coordinates via
# the PS/2 mouse's relative axis. W3's proven recipe (see
# docs/evidence/task-3-phase10-interactive.md):
#
#   Step 1: clamp pointer to (0,0). 10 × rel-100 per axis = ~1500px
#           of negative motion, more than any screen dimension.
#   Step 2: navigate to (x,y) — coarse (rel-50 ≈ 100px per event
#           through event_filter's accelerate) then fine (rel-1 ≈ 1px
#           per event).
#   Step 3: jiggle ±1 to advance decorator hover (harmless for plain
#           sponge-de widgets but proven necessary for wm/decorator
#           hover state in W3).
#   Step 4: press BTN_LEFT (held 200ms — W3's proven hold time, long
#           enough for Qt's mousePressEvent to register).
#   Step 5: release BTN_LEFT.
#
# Precision: ±1px after the coarse-fine sequence (event_filter's
# accelerate LUT makes rel-1 → 1px, rel-50 → 100px). Sufficient
# for the 48x20 S toggle and ~30px launcher entries.
#
proc qmp_ps2_click { chan x y } {
	puts "\nqmp: PS/2 click -> ($x,$y) — clamp to (0,0) + nav + press/release"

	# W3's exact recipe (proven on this host per
	# docs/evidence/task-3-phase10-interactive.md). No delays between
	# individual fine rel-1 events — W3 shows this is correct.

	# Step 1: clamp to (0,0).
	for {set i 0} {$i < 10} {incr i} { qmp_move_rel $chan x -100 }
	for {set i 0} {$i < 10} {incr i} { qmp_move_rel $chan y -100 }
	after 200

	# Step 2: coarse (rel-50 ≈ 100px).
	set cx [expr {$x / 100}]
	set cy [expr {$y / 100}]
	puts "qmp:   coarse rel-50: cx=$cx cy=$cy"
	for {set i 0} {$i < $cx} {incr i} { qmp_move_rel $chan x 50; after 20 }
	for {set i 0} {$i < $cy} {incr i} { qmp_move_rel $chan y 50; after 20 }

	# Step 3: fine (rel-1 ≈ 1px) — no delays between events.
	set fx [expr {$x % 100}]
	set fy [expr {$y % 100}]
	puts "qmp:   fine rel-1: fx=$fx fy=$fy"
	for {set i 0} {$i < $fx} {incr i} { qmp_move_rel $chan x 1 }
	for {set i 0} {$i < $fy} {incr i} { qmp_move_rel $chan y 1 }
	after 400

	# Step 4: hover jiggle.
	qmp_move_rel $chan x 1
	after 200
	qmp_move_rel $chan x -1
	after 200

	# Step 5: press.
	puts "qmp:   press"
	qmp_ps2_button $chan true
	after 200

	# Step 6: release.
	puts "qmp:   release"
	qmp_ps2_button $chan false
	after 100
}

##
# Drag from (x1,y1) to (x2,y2) with `steps` interpolated absolute-axis
# motions. Begins with a small hover jiggle on the start point so the
# decorator's hover state advances (mirrors the synthetic wm_probe drag
# pattern), then presses BTN_LEFT and walks the pointer to the endpoint
# in `steps` equal increments before releasing.
#
# Uses the absolute-axis variant (see qmp_pointer_move) — under
# -nographic the precision is poor due to the abs-clamp root cause
# documented at qmp_abs. The W3 scenario replaces this proc with its
# own PS/2-based version (see run/sponge-wm-qmp.run).
#
proc qmp_drag { chan x1 y1 x2 y2 steps } {
	# Hover jiggle — nudge ±1px a few times to trigger decorator hover.
	qmp_pointer_move $chan $x1 $y1
	after 30
	qmp_pointer_move $chan [expr {$x1 + 1}] $y1
	after 30
	qmp_pointer_move $chan $x1 $y1
	after 30
	# Press on the start point.
	qmp_button $chan left down
	after 30
	# Interpolated motion.
	for {set i 1} {$i <= $steps} {incr i} {
		set xi [expr {$x1 + (($x2 - $x1) * $i) / $steps}]
		set yi [expr {$y1 + (($y2 - $y1) * $i) / $steps}]
		qmp_pointer_move $chan $xi $yi
		after 20
	}
	after 30
	qmp_button $chan left up
}

##
# Map a single ASCII character to its QEMU keyname. Rejects unmapped
# characters loud (the caller controls the input — see qmp_type).
#
proc qmp_char_to_keyname { ch } {
	scan $ch %c code
	# a-z
	if {$code >= 0x61 && $code <= 0x7a} { return [format %c $code] }
	# 0-9
	if {$code >= 0x30 && $code <= 0x39} { return [format %c $code] }
	# A-Z (shifted) — uppercase letter; qmp send-key takes the keyname as-is
	if {$code >= 0x41 && $code <= 0x5a} { return [format %c $code] }
	switch -- $ch {
		" "  { return "spc" }
		"\n" { return "ret" }
		"\t" { return "tab" }
		"-"  { return "minus" }
		"."  { return "dot" }
		","  { return "comma" }
		"/"  { return "slash" }
		"_"  { return "minus" } ;# underscore is shift+minus — caller should avoid
		":"  { return "shift" } ;# caller should handle shift sequences explicitly
	}
	puts stderr "\nqmp: unmapped character (code=$code, char='$ch') — extend qmp_char_to_keyname"
	exit 1
}

##
# Type a string one character at a time via send-key (atomic press+release
# per key). Keeps the input within the mapped ASCII subset above; the
# component's QMP-TARGET type marker must only emit chars this helper
# accepts (a-z, 0-9, space, newline, minus, dot, etc.).
#
proc qmp_type { chan string } {
	foreach ch [split $string ""] {
		set kn [qmp_char_to_keyname $ch]
		qmp_send_key $chan $kn
		after 15
	}
}

##
# Send a single QEMU keyname (e.g. "ret", "spc", "a") via send-key.
# Uses the qcode/KeyValue object form ({"type":"qcode","data":"h"}),
# the only form QEMU 11 accepts — the string form ["h"] is rejected
# with `Invalid parameter type for 'keys[0]', expected: object`
# (latent W1 bug, fixed here; per W4's evidence at
# docs/evidence/task-4-phase10-interactive.md §"Root cause #3").
#
proc qmp_send_key { chan keyname } {
	qmp_cmd $chan [format {{"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":"%s"}]}}} $keyname]
}

##
# Bounded expect on global `qemu_spawn_id` for the next QMP-TARGET marker,
# then dispatch to the matching qmp_* proc. The marker formats are:
#
#     QMP-TARGET click <gx> <gy>
#     QMP-TARGET drag <x1> <y1> <x2> <y2>
#     QMP-TARGET type <string>
#     QMP-TARGET move <dx> <dy>            (closed-loop PS/2 rel move)
#
# `<string>` extends to end of line and must contain only mapped chars.
# On timeout prints FAIL and exits 1 — never a silent hang.
#
# Each numeric pattern ends with a \r*\n anchor to handle QEMU's
# CR CR LF serial line ending (W4's fix — the click capture did not
# match with the unanchored \r?).
#
proc qmp_exec_target { chan timeout_s } {
	global qemu_spawn_id
	if {![info exists qemu_spawn_id]} {
		puts stderr "\nqmp: qemu_spawn_id not set — qmp_exec_target must run after run_genode_until spawned QEMU"
		exit 1
	}
	set timeout [expr {$timeout_s}]
	set spawn_id $qemu_spawn_id
	puts "\nqmp: qmp_exec_target waiting for marker (timeout=${timeout_s}s)"
	expect {
		-i $qemu_spawn_id
		-re {QMP-TARGET tabclick (-?\d+) (-?\d+)\r*\n} {
			set gx $expect_out(1,string)
			set gy $expect_out(2,string)
			puts "\nqmp: dispatching tabclick at guest ($gx,$gy) via usb-tablet abs + HMP mouse_button"
			qmp_tablet_click $chan $gx $gy
		}
		-re {QMP-TARGET click (-?\d+) (-?\d+)\r*\n} {
			set gx $expect_out(1,string)
			set gy $expect_out(2,string)
			puts "\nqmp: dispatching click at guest ($gx,$gy) via PS/2 relative"
			qmp_ps2_click $chan $gx $gy
		}
		-re {QMP-TARGET drag (-?\d+) (-?\d+) (-?\d+) (-?\d+)\r*\n} {
			set x1 $expect_out(1,string)
			set y1 $expect_out(2,string)
			set x2 $expect_out(3,string)
			set y2 $expect_out(4,string)
			puts "\nqmp: dispatching drag ($x1,$y1) -> ($x2,$y2) via PS/2 relative"
			# Navigate to start, press, then walk deltas to end, then release.
			set dx [expr {$x2 - $x1}]
			set dy [expr {$y2 - $y1}]
			set sx [expr {$dx > 0 ? 1 : -1}]
			set sy [expr {$dy > 0 ? 1 : -1}]
			qmp_ps2_click $chan $x1 $y1
			after 100
			# Press is already released by qmp_ps2_click; re-press and
			# walk to the end point for a real drag (button held).
			qmp_ps2_button $chan true
			after 50
			set ax [expr {abs($dx) / 50}]
			set ay [expr {abs($dy) / 50}]
			for {set i 0} {$i < $ax} {incr i} { qmp_move_rel $chan x [expr {$sx * 50}]; after 20 }
			for {set i 0} {$i < $ay} {incr i} { qmp_move_rel $chan y [expr {$sy * 50}]; after 20 }
			set fx [expr {abs($dx) % 50}]
			set fy [expr {abs($dy) % 50}]
			for {set i 0} {$i < $fx} {incr i} { qmp_move_rel $chan x $sx }
			for {set i 0} {$i < $fy} {incr i} { qmp_move_rel $chan y $sy }
			after 100
			qmp_ps2_button $chan false
		}
		-re {QMP-TARGET type (\S.*)\r*\n} {
			set s $expect_out(1,string)
			puts "\nqmp: dispatching type '$s'"
			qmp_type $chan $s
		}
		-re {QMP-TARGET move (-?\d+) (-?\d+)\r*\n} {
			set dx $expect_out(1,string)
			set dy $expect_out(2,string)
			puts "\nqmp: dispatching PS/2 rel move dx=$dx dy=$dy"
			# Per-axis step cap ±100 to avoid swamping the input chain.
			while {$dx != 0 || $dy != 0} {
				set sx 0
				set sy 0
				if {$dx > 0}      { set sx [expr {$dx > 100 ? 100 : $dx}] }
				elseif {$dx < 0}  { set sx [expr {$dx < -100 ? -100 : $dx}] }
				if {$dy > 0}      { set sy [expr {$dy > 100 ? 100 : $dy}] }
				elseif {$dy < 0}  { set sy [expr {$dy < -100 ? -100 : $dy}] }
				if {$sx != 0} { qmp_move_rel $chan x $sx }
				if {$sy != 0} { qmp_move_rel $chan y $sy }
				incr dx [expr {-$sx}]
				incr dy [expr {-$sy}]
				after 10
			}
		}
		timeout {
			puts stderr "\nqmp: FAIL - no QMP-TARGET marker within ${timeout_s}s (probe did not emit one — boot/render race or probe died?)"
			exit 1
		}
	}
}


##
# W4 proven-exact landing recipe (docs/evidence/task-4-phase10-
# interactive.md). On QEMU 11.0.2 under -nographic:
#   1. input-send-event btn events reach NO guest device (PS/2 mouse
#      doesn't fire; tablet doesn't receive). FIX: HMP `mouse_button 1/0`
#      delivers BTN_LEFT through the current (tablet) device.
#   2. Untargeted abs events broadcast to both mice; the PS/2 mouse
#      wins and translates abs to accelerated rel deltas that slam the
#      pointer into a corner. FIX: HMP `mouse_set <tablet-index>` makes
#      the tablet current so abs events take the tablet path.
#   3. The usb-tablet reports absolute positions in device units
#      (0..32767 per axis) and nitpicker treats Absolute_motion
#      values as raw pixels, so unscaled tablet events clamp to the
#      screen corner. FIX: event_filter <transform> scale on the usb
#      input (see run/sponge-de-sel4-interactive.run staging) maps
#      device units back to pixels.
# Together these three fixes make the QMP-TARGET tabclick marker
# land exactly on the target pixel (±0-1 px verified in W4).
#

##
# Send a QEMU Human Monitor Protocol (HMP) command via QMP. The HMP
# accepts the same command-line syntax as the QEMU monitor (e.g.
# `mouse_set 3`, `mouse_button 1`, `info mice`). Wraps the string in
# the QMP `human-monitor-command` envelope.
#
proc qmp_hmp { chan cmdline } {
	qmp_cmd $chan [format {{"execute":"human-monitor-command","arguments":{"command-line":"%s"}}} $cmdline]
}


##
# Find the usb-tablet's `query-mice` index. Caches the result in a
# global so repeated tabclick calls don't re-query. Returns the empty
# string if no absolute device is present (caller should fail loud).
#
proc qmp_tablet_index { chan } {
	global _qmp_tablet_idx
	if {![info exists _qmp_tablet_idx]} {
		set _qmp_tablet_idx ""
		set result [qmp_cmd $chan {{"execute":"query-mice"}}]
		if {[regexp {"index":\s*([0-9]+)[^\}]*"absolute":\s*true} $result dummy idx]} {
			set _qmp_tablet_idx $idx
		}
		puts "\nqmp: absolute tablet mouse index: '$_qmp_tablet_idx'"
		flush stdout
	}
	return $_qmp_tablet_idx
}


##
# Send a QMP-TARGET tabclick via the W4 tablet-abs recipe:
#   1. HMP `mouse_set <tablet-index>' — make the usb-tablet current
#   2. QMP input-send-event abs (scaled by qmp_abs) — position cursor
#   3. HMP `mouse_button 1' — press
#   4. HMP `mouse_button 0' — release
# The click is deterministic on this host (verified in W4: ABS_MOTION
# +528+396 for abs (16896,16895) lands precisely on the target pixel).
#
proc qmp_tablet_click { chan tx ty } {
	set idx [qmp_tablet_index $chan]
	if {$idx eq ""} {
		puts stderr "\nFAIL: query-mice shows no absolute mouse device"
		exit 1
	}
	qmp_hmp $chan "mouse_set $idx"
	set ax [expr {($tx * 32767 + 512) / 1024}]
	set ay [expr {($ty * 32767 + 384) / 768}]
	qmp_cmd $chan [format {{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":%d}},{"type":"abs","data":{"axis":"y","value":%d}}]}}} $ax $ay]
	after 15
	qmp_hmp $chan "mouse_button 1"
	after 15
	qmp_hmp $chan "mouse_button 0"
}

##
# Disconnect cleanly. Best-effort — closes the channel; errors are
# ignored since QEMU may already be tearing down.
#
proc qmp_disconnect { chan } {
	catch {close $chan}
}
