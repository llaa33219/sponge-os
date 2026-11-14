#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# check.sh — focused host checks for pkg/settings_demo and its
# provider sources (Criterion 18 third-party demo).
#
#   1. structural: the providers open only Gui and the package-private
#      File_system store — no configd label, no Report channel, no
#      command execution, no Qt/GL/plugins (docs/12-package-format.md
#      §14.5/§14.6, D16.10 tier (b)).
#   2. metadata schema: pkg/settings_demo/metadata.xml against the
#      docs/12 §14.2/§14.3 rules (two entries, distinct ids, bare
#      payload targets, closed categories, payload icons present,
#      writable package-private store session).
#   3. adapter seam: the real in-flight sponge_pkgd settings rules
#      accept both entries (tests/seam_check.cc — see its header).
#   4. store self-test: tests/self_test.cc against the shipped
#      common/store.h validation and store-XML logic.
#
# Run from anywhere; expects the repository checkout layout.
#   repos/sponge/src/settings_demo/tests/check.sh
#
# Exit code 0 = all checks passed.

set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../../../.." && pwd)"
SRC="$DIR/.."
PKG="$ROOT/pkg/settings_demo"

fail=0

note()  { printf '%s\n' "$*"; }
pass()  { note "PASS: $*"; }
fail_() { note "FAIL: $*"; fail=1; }

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

note "== settings_demo host checks =="

for f in common/store.h common/store_io.h common/font5x7.h common/paint.h \
         common/provider.h greeting/main.cc greeting/target.mk \
         about/main.cc about/target.mk; do
	if [ ! -f "$SRC/$f" ]; then
		fail_ "missing expected file $SRC/$f"
		exit 1
	fi
done

note "-- structural: capability surface --"

for f in common/store.h common/store_io.h common/paint.h common/provider.h \
         greeting/main.cc about/main.cc; do
	require_absent 'config_request|de_config|configd' "$SRC/$f" \
		"third-party providers never touch shared DE config (docs/12 §14.6)"
	require_absent 'report_session|os/reporter|Expanding_reporter' "$SRC/$f" \
		"no Report channel: errors surface in the provider's own window"
	require_absent 'system\(|popen\(|exec[lv]?p?\(' "$SRC/$f" \
		"no command execution (docs/12 §14.3: target is data)"
	require_absent 'gems/|QtCore|qt6|dlopen|libc/' "$SRC/$f" \
		"public Genode APIs only, no plugin or interpreter path"
	require_absent 'File_system::Connection|Connection< *File_system' "$SRC/$f" \
		"the store goes through Vfs::Root, not a raw File_system connection"
done

require_present 'Vfs::Root' "$SRC/common/store_io.h" \
	"package-private store via the established Vfs::Root pattern"
require_present 'validate_value' "$SRC/common/provider.h" \
	"input is validated before anything is persisted"
require_present '_commit' "$SRC/common/provider.h" \
	"explicit save path exists"

for d in greeting about; do
	if grep -Eq '^LIBS[[:space:]]*:=[[:space:]]*base vfs[[:space:]]*$' \
		"$SRC/$d/target.mk"; then
		pass "$d/target.mk links only 'base vfs'"
	else
		fail_ "$d/target.mk must link only 'base vfs'"
	fi
done

note "-- metadata schema (docs/12 §14.2/§14.3) --"

if python3 - "$PKG/metadata.xml" "$SRC" "$PKG" <<'PYEOF'
import re, sys
import xml.etree.ElementTree as ET

meta_path, src_dir, pkg_dir = sys.argv[1], sys.argv[2], sys.argv[3]
failures = []

def check(ok, what):
    if not ok:
        failures.append(what)

try:
    root = ET.parse(meta_path).getroot()
except ET.ParseError as e:
    print(f"FAIL: metadata.xml is not well-formed XML: {e}")
    sys.exit(1)

check(root.tag == "package", "root element is <package>")
check((root.findtext("name") or "") == "settings_demo", "<name> is settings_demo")
check(len(root.findtext("version") or "") > 0, "<version> present")
check(len(root.findtext("description") or "") > 0, "<description> present")

check(root.find("binary") is None, "settings-only package carries no <binary>")
check(root.find("launcher") is None, "settings-only package carries no <launcher>")
check(root.find("autostart") is None, "no <autostart>: entries open on demand")
check(root.find("exclusive") is None, "settings targets are never exclusive")

settings = root.findall("settings")
check(len(settings) == 2, f"exactly two <settings> entries (got {len(settings)})")

ID_RE = re.compile(r"^[a-z0-9_-]{1,32}$")
TARGET_RE = re.compile(r"^[a-z0-9_]+$")
CATS = {"desktop", "hardware", "system", "applications", "other"}
ICON_FORBIDDEN = "/\\ \t;|&$`'\""

ids, targets = [], []
for s in settings:
    sid = s.get("id", "")
    ids.append(sid)
    check(bool(ID_RE.match(sid)), f"id '{sid}' matches [a-z0-9_-]{{1,32}}")

    check(len(s.get("title", "")) > 0, f"entry '{sid}' has a non-empty title")

    cat = s.get("category")
    check(cat is None or cat in CATS,
          f"entry '{sid}' category '{cat}' is in the closed enum")

    icon = s.get("icon")
    check(icon is None or
          (".." not in icon and
           not any(c in ICON_FORBIDDEN for c in icon)),
          f"entry '{sid}' icon is a bare payload file name")
    import os
    check(icon is None or os.path.isfile(os.path.join(pkg_dir, icon)),
          f"entry '{sid}' icon '{icon}' exists in the package payload")

    target = s.get("target", "")
    targets.append(target)
    check(bool(target), f"entry '{sid}' has a target")
    check(bool(TARGET_RE.match(target)),
          f"entry '{sid}' target '{target}' is one bare payload name "
          f"(no path/escape/shell forms)")
    check(len(target) <= 64, f"entry '{sid}' target is bounded")

    kws = [k.text for k in s.findall("keyword")]
    check(len(kws) <= 8, f"entry '{sid}' carries at most 8 keywords")
    check(all(k and len(k) <= 32 for k in kws),
          f"entry '{sid}' keywords are short non-empty terms")

check(len(set(ids)) == len(ids), "ids are distinct within the package")
check(len(set(targets)) == len(targets), "targets are distinct binaries")

for attr in settings[0].attrib:
    check(attr not in ("exclusive", "group"),
          "no exclusive claim on a <settings> entry")

sessions = root.find("sessions")
check(sessions is not None, "<sessions> declared")
if sessions is not None:
    fs = [s for s in sessions.findall("session") if s.get("name") == "File_system"]
    check(len(fs) == 1, "exactly one File_system session")
    if fs:
        check(fs[0].get("subpath") == "/app/settings_demo",
              "File_system subpath scopes the package-private store")
        check(fs[0].get("readonly") in (None, "no"),
              "the store session is writable")
        check(fs[0].get("default-route") == "vfs",
              "File_system routes to vfs")
    gui = [s for s in sessions.findall("session") if s.get("name") == "Gui"]
    check(len(gui) == 1, "exactly one Gui session (one window per provider)")

config = root.find("config")
check(config is not None, "<config> carries the store mount")
if config is not None:
    vfs = config.find("vfs")
    check(vfs is not None and vfs.find("fs") is not None,
          "<config><vfs> mounts the store with <fs/>")

for f in failures:
    print(f"FAIL: {f}")
print(f"metadata-schema: {'PASS' if not failures else 'FAIL'} "
      f"({len(settings)} entries)")
sys.exit(1 if failures else 0)
PYEOF
then
	pass "metadata.xml satisfies the §14.2/§14.3 schema"
else
	fail_ "metadata.xml schema checks failed"
fi

note "-- build-target cross-check --"

for t in settings_demo_greeting settings_demo_about; do
	if grep -Eq "^TARGET[[:space:]]*:=[[:space:]]*$t[[:space:]]*$" \
		"$SRC"/*/target.mk 2>/dev/null; then
		pass "target $t is built by a target.mk under src/settings_demo/"
	else
		fail_ "no target.mk builds payload binary $t"
	fi
done

note "-- adapter seam: in-flight pkgd settings rules --"

SEAM=/tmp/settings_demo_seam_check

if g++ -std=c++20 -O1 \
	-I "$ROOT/genode/repos/base/include" \
	-I "$ROOT/genode/repos/base/include/spec/64bit" \
	-I "$ROOT/genode/repos/base/include/spec/x86_64" \
	-I "$ROOT/genode/repos/base/include/spec/x86" \
	-I "$ROOT/genode/repos/base/src/include" \
	-I "$ROOT/repos/sponge/include" \
	-I "$ROOT/repos/sponge/src/sponge_pkgd" \
	"$DIR/seam_check.cc" \
	"$ROOT/repos/sponge/src/sponge_pkgd/settings_rules.cc" \
	"$ROOT/repos/sponge/lib/src/sponge_pkg_launch/pkg_launch.cc" \
	"$ROOT/genode/repos/base/src/lib/base/hid.cc" \
	"$ROOT/genode/repos/base/src/lib/base/output.cc" \
	-o "$SEAM"
then
	pass "seam-check builds on the host"
else
	fail_ "seam-check build failed (adapter seam moved?)"
fi

if [ -x "$SEAM" ]; then
	if "$SEAM" "$PKG/metadata.xml"; then
		pass "seam-check executed"
	else
		fail_ "seam-check reported failures (pkgd rules reject the metadata)"
	fi
fi

note "-- store self-test (host g++) --"

BIN=/tmp/settings_demo_self_test

if g++ -std=c++20 -O1 \
	-I "$ROOT/genode/repos/base/include" \
	-I "$ROOT/genode/repos/base/include/spec/64bit" \
	-I "$ROOT/genode/repos/base/include/spec/x86_64" \
	-I "$ROOT/genode/repos/base/include/spec/x86" \
	-I "$ROOT/genode/repos/base/src/include" \
	-I "$ROOT/repos/sponge/include" \
	"$DIR/self_test.cc" \
	"$DIR/test_cases.cc" \
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
	note "== settings_demo host checks: ALL PASS =="
else
	note "== settings_demo host checks: FAILURES =="
fi

exit "$fail"
