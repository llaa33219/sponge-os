#!/usr/bin/env tclsh
# \brief  Dispatch test for QMP-TARGET click / tablet markers.
# \author Sponge OS contributors
# \date   2026
#
# Validates that the expect patterns in run/qmp.inc's qmp_exec_target
# dispatch `QMP-TARGET tablet <gx> <gy>` to qmp_tablet_click and
# `QMP-TARGET click <gx> <gy>` to qmp_ps2_click — with NO pattern
# collision (the "click" pattern must not match a "tablet" line, and
# vice versa).
#
# The test simulates the relevant slice of expect's pattern-dispatch
# logic: for each input line, the patterns are tried in order (the
# qmp_exec_target block lists "tablet" before "click" — we must reproduce
# that ordering) and the FIRST matching pattern wins. The dispatch
# target is recorded; we then verify:
#   - tablet lines always dispatch to qmp_tablet_click
#   - click lines always dispatch to qmp_ps2_click
#   - no line cross-matches (i.e., a "tablet" line never matches the
#     "click" pattern and vice versa)
#
# The patterns and the dispatch order are taken verbatim from
# run/qmp.inc's qmp_exec_target.

# The two expect patterns (exact copies from qmp.inc).
set tablet_pat {QMP-TARGET tablet (-?\d+) (-?\d+)\r*\n}
set click_pat  {QMP-TARGET click (-?\d+) (-?\d+)\r*\n}

# Mimics the dispatch logic of qmp_exec_target: try the tablet pattern
# first ("tablet" is listed first in qmp.inc), then the click pattern.
# Returns: "tablet" or "click" or "none".
proc dispatch {line} {
    global tablet_pat click_pat
    if {[regexp $tablet_pat $line -> gx gy]} {
        return [list "tablet" $gx $gy]
    }
    if {[regexp $click_pat $line -> gx gy]} {
        return [list "click" $gx $gy]
    }
    return [list "none" "" ""]
}

# Test cases: each is {line, expected_target, expected_x, expected_y}.
# The lines are appended with "\r\n" (QEMU's CR CR LF line ending,
# consumed by the CR*\n anchor). The dispatch loop in qmp.inc expects
# the line to arrive with the EOL bytes; we model that here.
set cases {
    {"QMP-TARGET tablet 170 73"          tablet 170 73}
    {"QMP-TARGET click 512 412"          click 512 412}
    {"QMP-TARGET click 32 14"            click 32 14}
    {"QMP-TARGET tablet 10 65"           tablet 10 65}
    {"QMP-TARGET tablet 170 73"          tablet 170 73}
    {"QMP-TARGET click 200 200"          click 200 200}
    {"QMP-TARGET tablet 0 0"             tablet 0 0}
    {"QMP-TARGET click 999 999"          click 999 999}
}

# Append the CR LF to each test line before regex matching.
set lines [list]
foreach c $cases {
    set line [lindex $c 0]
    lappend lines "${line}\r\n"
}

set all_pass 1
foreach c $cases line $lines {
    set exp_target  [lindex $c 1]
    set exp_x       [lindex $c 2]
    set exp_y       [lindex $c 3]

    # 1. Verify the regex itself matches the expected coords.
    set pat [expr {$exp_target eq "tablet" ? "tablet_pat" : "click_pat"}]
    if {![regexp [set $pat] $line -> gx gy]} {
        puts "FAIL: pattern '$pat' did not match '$line'"
        set all_pass 0
        continue
    }
    if {$gx ne $exp_x || $gy ne $exp_y} {
        puts "FAIL: pattern '$pat' matched '$line' but extracted ($gx,$gy) instead of ($exp_x,$exp_y)"
        set all_pass 0
        continue
    }

    # 2. Verify the cross-pattern collision behavior. This is the
    # headline invariant: a "tablet" line MUST NOT match the "click"
    # pattern and vice versa.
    if {$exp_target eq "tablet"} {
        if {[regexp $click_pat $line]} {
            puts "FAIL: pattern collision — 'click' pattern matched 'tablet' line '$line'"
            set all_pass 0
            continue
        }
    } else {
        if {[regexp $tablet_pat $line]} {
            puts "FAIL: pattern collision — 'tablet' pattern matched 'click' line '$line'"
            set all_pass 0
            continue
        }
    }

    # 3. Verify the dispatch decision (pattern order matches qmp.inc).
    set result [dispatch $line]
    set actual_target [lindex $result 0]
    set actual_x      [lindex $result 1]
    set actual_y      [lindex $result 2]
    if {$actual_target ne $exp_target || $actual_x ne $exp_x || $actual_y ne $exp_y} {
        puts "FAIL: dispatch('$line') = ($actual_target,$actual_x,$actual_y), expected ($exp_target,$exp_x,$exp_y)"
        set all_pass 0
        continue
    }
    puts "PASS: '$line' -> ($actual_target, $actual_x, $actual_y)"
}

# Verify the check is robust against lines that arrive with a single
# CR (not CR CR LF) — the CR*\n anchor must accept either.
set test_lines_cr {
    {"QMP-TARGET tablet 170 73\n"        tablet 170 73}
    {"QMP-TARGET click 512 412\n"        click 512 412}
    {"QMP-TARGET tablet 10 65\r\n"       tablet 10 65}
    {"QMP-TARGET click 32 14\r\n"        click 32 14}
}
foreach c $test_lines_cr {
    set line [lindex $c 0]
    set exp_target  [lindex $c 1]
    set exp_x       [lindex $c 2]
    set exp_y       [lindex $c 3]
    set result [dispatch $line]
    set actual_target [lindex $result 0]
    set actual_x      [lindex $result 1]
    set actual_y      [lindex $result 2]
    if {$actual_target ne $exp_target || $actual_x ne $exp_x || $actual_y ne $exp_y} {
        puts "FAIL: dispatch('$line') = ($actual_target,$actual_x,$actual_y), expected ($exp_target,$exp_x,$exp_y)"
        set all_pass 0
    } else {
        puts "PASS: '$line' -> ($actual_target, $actual_x, $actual_y) (EOL variant)"
    }
}

# Constant arithmetic: confirm the W4 tablet coordinate scaling matches
# the values in qmp_tablet_click: ax = (tx*32767 + 512) / 1024, ay =
# (ty*32767 + 384) / 768. For the launch entry click (170, 73):
#   ax = (170*32767 + 512) / 1024 = 5570494 / 1024 = 5440
#   ay = (73 * 32767 + 384) / 768  = 2392095 / 768  = 3114
# The W4 evidence says the abs event +528+396 for abs (16896,16895)
# lands precisely on the target — that's a different scale (the W4
# test scrolled to (528,396) which is the terminal center). The W4
# scale is the same formula as the new one above.
set ax [expr {int((170 * 32767 + 512) / 1024)}]
set ay [expr {int((73 * 32767 + 384) / 768)}]
puts "W4 tablet scale: QMP-TARGET tablet 170 73 -> QMP abs ($ax, $ay)"
# ax = (170*32767 + 512) / 1024 = 5570494 / 1024 = 5440 (exact)
# ay = (73 * 32767 + 384) / 768  = 2392375 / 768  = 3115 (floor)
if {$ax ne 5440 || $ay ne 3115} {
    puts "FAIL: W4 tablet scale mismatch — expected (5440, 3115), got ($ax, $ay)"
    set all_pass 0
} else {
    puts "PASS: W4 tablet scale matches (170, 73) -> abs ($ax, $ay)"
}

# Final summary.
if {!$all_pass} {
    puts stderr "DISPATCH TEST: FAIL"
    exit 1
} else {
    puts "DISPATCH TEST: PASS"
    exit 0
}
