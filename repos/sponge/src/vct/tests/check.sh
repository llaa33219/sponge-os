#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check.sh — focused host checks for vct's settings commands
# (Criterion 18 direct-launch surface, docs/06-vct.md §4.8).
#
#   1. capability-surface structural check: the settings wire adapter
#      must never write configuration and must hold no package
#      File_system capability (docs/12-package-format.md §14.6: `vct
#      settings` is not a config writer; `vct config` stays the only
#      manual door).
#   2. settings-wire self-test: builds and runs tests/self_test.cc on
#      the host against the shipped settings_wire.cc (entry collection,
#      category grouping, open request/result wire shapes incl. the
#      id / settings-id identity aliases).
#
# Run from anywhere; expects the repository checkout layout.
#   repos/sponge/src/vct/tests/check.sh
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

note "== vct settings host checks =="

for f in settings_wire.cc settings_wire.h commands.cc command_router.cc tests/self_test.cc; do
	if [ ! -f "$SRC/$f" ]; then
		fail_ "missing expected file $SRC/$f"
		exit 1
	fi
done

note "-- structural: capability surface --"

require_absent 'config_request|config_set|configd|de_config' "$SRC/settings_wire.cc" \
	"no config write channel (docs/12 §14.6: vct settings never writes configuration)"
require_absent 'File_system|file_system' "$SRC/settings_wire.cc" \
	"no package File_system capability (docs/12 §14.5: identity only)"

note "-- structural: wire-shape seams --"

require_present 'gen_settings_request' "$SRC/settings_wire.cc" \
	"request wire shape lives in one adapter seam"
require_present 'parse_settings_result' "$SRC/settings_wire.cc" \
	"result wire shape lives in one adapter seam"
require_present 'settings-id' "$SRC/settings_wire.cc" \
	"identity pair honors the §14.5 settings-id alias"

note "-- settings-wire self-test (host g++) --"

BIN=/tmp/vct_settings_self_test

if g++ -std=c++20 -O1 \
	-I "$ROOT/genode/repos/base/include" \
	-I "$ROOT/genode/repos/base/include/spec/64bit" \
	-I "$ROOT/genode/repos/base/include/spec/x86_64" \
	-I "$ROOT/genode/repos/base/include/spec/x86" \
	-I "$ROOT/genode/repos/base/src/include" \
	-I "$ROOT/genode/repos/os/include" \
	-I "$ROOT/repos/sponge/include" \
	"$DIR/self_test.cc" \
	"$SRC/settings_wire.cc" \
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
	note "== vct settings host checks: ALL PASS =="
else
	note "== vct settings host checks: FAILURES =="
fi

exit "$fail"
