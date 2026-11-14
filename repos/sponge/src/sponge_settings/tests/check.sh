#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check.sh — focused host checks for sponge_settings (Criterion 18).
#
#   1. capability-surface structural check: the sources must open only
#      the contract sessions (installed ROM + settings_request /
#      settings_result + Gui/Timer) and no forbidden channel
#      (docs/05-sponge-de.md §5.4, docs/12-package-format.md §14.6).
#   2. model self-test: builds and runs tests/self_test.cc on the host
#      against the shipped model.cc (see self_test.cc for rationale).
#
# Run from anywhere; expects the repository checkout layout.
#   repos/sponge/src/sponge_settings/tests/check.sh
#
# Exit code 0 = all checks passed.

set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../../.." && pwd)"
SRC="$DIR/.."

fail=0

note()  { printf '%s\n' "$*"; }
pass()  { note "PASS: $*"; }
fail_() { note "FAIL: $*"; fail=1; }

# Strip C/C++ comments so contract prose in the sources cannot satisfy
# or trip a structural grep — only real code is scanned.
code_of() {
	awk '
	BEGIN { in_block = 0 }
	{
		line = $0
		out = ""
		while (1) {
			if (in_block) {
				if (match(line, /\*\//)) {
					line = substr(line, RSTART + RLENGTH)
					in_block = 0
				} else { break }
			} else if (match(line, /\/\*/)) {
				out = out substr(line, 1, RSTART - 1)
				line = substr(line, RSTART + RLENGTH)
				in_block = 1
			} else {
				out = out line
				break
			}
		}
		sub(/\/\/.*/, "", out)
		print out
	}' "$1"
}

require_absent() {
	local pattern="$1" file="$2" why="$3"
	if code_of "$file" | grep -Eq "$pattern"; then
		fail_ "$file code contains forbidden '$pattern' ($why)"
	else
		pass "$file code free of '$pattern'"
	fi
}

require_present() {
	local pattern="$1" file="$2" why="$3"
	if code_of "$file" | grep -Eq "$pattern"; then
		pass "$file carries '$pattern'"
	else
		fail_ "$file missing '$pattern' ($why)"
	fi
}

note "== sponge_settings host checks =="

for f in main.cc model.cc model.h view.cc view.h target.mk; do
	if [ ! -f "$SRC/$f" ]; then
		fail_ "missing expected file $SRC/$f"
		exit 1
	fi
done

note "-- structural: capability surface --"

for f in main.cc model.cc model.h view.cc view.h; do
	require_absent 'file_system|File_system' "$SRC/$f" \
		"no package File_system session (docs/12 §14.5)"
	require_absent 'config_request|de_config|configd' "$SRC/$f" \
		"no configd write channel (docs/12 §14.6)"
	require_absent 'gems/|QtCore|qt6|dlopen|libc/' "$SRC/$f" \
		"public Genode APIs only, no GL-heavy dependency"
done

for f in model.cc model.h; do
	require_absent 'gui_session|timer_session|attached_rom|attached_dataspace|reporter.h' \
		"$SRC/$f" "purity contract: the model holds no sessions"
done

require_present '"installed"' "$SRC/main.cc" \
	"discovery input is the installed metadata ROM (§14.4)"
require_present '"settings_request"' "$SRC/main.cc" \
	"own single-writer settings launch request label"
require_present '"settings_result"' "$SRC/main.cc" \
	"own settings launch result label"
require_absent 'trigger_periodic' "$SRC/main.cc" \
	"event-driven refresh only: no polling (§14.4)"
require_absent 'system\(|popen\(|exec[lv]?p?\(' "$SRC/main.cc" \
	"no command execution on the launch path (§14.3: target is data)"

if grep -Eq '^LIBS[[:space:]]*:=[[:space:]]*base[[:space:]]*$' "$SRC/target.mk"; then
	pass "target.mk links only 'base'"
else
	fail_ "target.mk must link only 'base' (no Qt/GL modules)"
fi

note "-- structural: wire-shape seams --"

require_present 'gen_settings_request' "$SRC/model.cc" \
	"request wire shape lives in one adapter seam"
require_present 'parse_settings_result' "$SRC/model.cc" \
	"result wire shape lives in one adapter seam"
require_present 'settings-id' "$SRC/model.cc" \
	"identity pair uses the §14.5 settings-id discriminator"

note "-- model self-test (host g++) --"

BIN=/tmp/sponge_settings_self_test

if g++ -std=c++20 -O1 \
	-I "$ROOT/genode/repos/base/include" \
	-I "$ROOT/genode/repos/base/include/spec/64bit" \
	-I "$ROOT/genode/repos/base/include/spec/x86_64" \
	-I "$ROOT/genode/repos/base/include/spec/x86" \
	-I "$ROOT/genode/repos/base/src/include" \
	-I "$ROOT/genode/repos/os/include" \
	-I "$ROOT/repos/sponge/include" \
	"$DIR/self_test.cc" \
	"$DIR/test_cases.cc" \
	"$SRC/model.cc" \
	"$ROOT/genode/repos/base/src/lib/base/node.cc" \
	"$ROOT/genode/repos/base/src/lib/base/hid.cc" \
	"$ROOT/genode/repos/base/src/lib/base/xml_generator.cc" \
	"$ROOT/genode/repos/base/src/lib/base/output.cc" \
	-o "$BIN"
then
	pass "self-test builds on the host"
else
	fail_ "self-test build failed"
fi

if [ -x "$BIN" ]; then
	if "$BIN"; then
		pass "self-test executed"
	else
		fail_ "self-test reported failures"
	fi
fi

if [ "$fail" -eq 0 ]; then
	note "== sponge_settings host checks: ALL PASS =="
else
	note "== sponge_settings host checks: FAILURES =="
fi

exit "$fail"
