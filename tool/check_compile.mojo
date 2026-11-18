# SPDX-License-Identifier: Apache-2.0
#
# Structural sanity check for a Sponge OS component.
#
# Verifies that a component directory has the files Genode's build system
# expects (target.mk, main.cc), and prints its contents for quick inspection.
# Bare paths keep resolving under repos/sponge/; repository-qualified paths
# (repos/sponge-de/src/sponge-de, for example) resolve from the repo root.
#
# Mojo-first layouts (AGENTS.md §3.2/§3.5) are recognized too: a directory
# with `.mojo` sources must wire them into the Genode link set through the
# `SRC_O` seam of target.mk (objects produced by tool/mojo_objects). The
# C++ checks are unchanged — target.mk and main.cc stay required either
# way (the thin C++ adapter is part of the Mojo-first layout).
#
# The authoritative build is the Genode build (see docs/08-development.md).
# This script does NOT compile anything; it only checks file presence and
# the SRC_O seam wiring (literal entries only — make expressions with
# $(...) are reported but not checked).
#
# Usage:
#   mojo tool/check_compile.mojo src/vct
#   mojo tool/check_compile.mojo repos/sponge-de/src/sponge-de

from std.sys import argv
from std.python import Python


def repo_root() raises -> String:
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def contains_substring(haystack: String, needle: String) raises -> Bool:
    """Case-sensitive substring test (Mojo String has no .contains)."""
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(
        builtins.str(haystack).find(needle) >= 0))


def parse_src_o(target_mk: String) raises -> List[String]:
    """Object entries from the target.mk SRC_O assignment(s). Handles
    line continuations and inline comments; `:=`, `+=`, and `?=` all
    count as assignments."""
    var builtins = Python.import_module("builtins")
    var re = Python.import_module("re")
    var entries = List[String]()
    var f = builtins.open(target_mk, "r")
    var content_obj = f.read()
    f.close()
    var lines = String(builtins.str(content_obj)).splitlines()
    var pending = String("")
    for line_py in lines:
        var line = String(line_py)
        var trimmed = String(builtins.str(line).strip())
        if trimmed.endswith("\\"):
            pending += String(builtins.str(trimmed)[
                0:trimmed.byte_length() - 1]) + " "
            continue
        var full = pending + trimmed
        pending = String("")
        if contains_substring(full, "#"):
            var hash_idx = Int(py=builtins.str(full).find("#"))
            full = String(builtins.str(
                String(builtins.str(full)[0:hash_idx])).strip())
        if full.byte_length() == 0:
            continue
        var m = re.match(r"^SRC_O\s*[:+?]?=\s*(.*)$", full)
        if Bool(py=m):
            for tok_pys in builtins.str(String(m.group(1))).split():
                entries.append(String(tok_pys))
    return entries.copy()


def obj_stem(entry: String) raises -> String:
    """`build/scene.o` -> `scene`. "" for non-plain entries."""
    var os_path = Python.import_module("os.path")
    var base = String(os_path.basename(entry))
    if not base.endswith(".o"):
        return String("")
    return String(os_path.splitext(base)[0])


def check(component_rel: String) raises:
    var os_path = Python.import_module("os.path")
    var os_py = Python.import_module("os")

    var cdir = repo_root() + "/" + component_rel
    if not component_rel.startswith("repos/"):
        cdir = repo_root() + "/repos/sponge/" + component_rel
    if not os_path.isdir(cdir):
        print("error: component directory not found: " + cdir)
        return

    print("[sponge-check] " + component_rel)
    print()

    var required: List[String] = ["target.mk", "main.cc"]
    var problems: List[String] = []

    for name in required:
        var full = cdir + "/" + name
        if os_path.isfile(full):
            print("  ok:        " + name)
        else:
            print("  MISSING:   " + name)
            problems.append(name)

    print()
    print("Files in " + cdir + ":")
    var mojo_units: List[String] = []
    for entry in os_py.listdir(cdir):
        print("  " + String(entry))
        if String(entry).endswith(".mojo"):
            mojo_units.append(String(entry))

    # Mojo-first layout: .mojo units enter the link set through the
    # target.mk SRC_O seam (AGENTS.md §3.2 Mojo-first layout). The C++
    # checks above are unaffected.
    if len(mojo_units) > 0:
        print()
        print("Mojo-first layout: " + String(len(mojo_units))
              + " .mojo unit(s)")
        for unit in mojo_units:
            print("  mojo unit:  " + unit)

        var src_o = parse_src_o(cdir + "/target.mk")
        var stems: List[String] = []
        if len(src_o) == 0:
            print("  MISSING:   SRC_O seam (target.mk declares no SRC_O"
                  + " objects)")
            problems.append("SRC_O")
        else:
            for obj in src_o:
                if contains_substring(obj, "$"):
                    print("  src_o:     " + obj
                          + "  (make expression — not statically checked)")
                    continue
                if not obj.endswith(".o"):
                    print("  MISSING:   SRC_O entry is not a .o object: "
                          + obj)
                    problems.append(obj)
                    continue
                print("  src_o:     " + obj)
                stems.append(obj_stem(obj))

            # Every .mojo unit must enter the link set through SRC_O.
            for unit in mojo_units:
                var unit_stem = String(os_path.splitext(unit)[0])
                var matched = False
                for stem in stems:
                    if stem == unit_stem:
                        matched = True
                if not matched:
                    print("  MISSING:   " + unit
                          + " has no SRC_O entry (expected '"
                          + unit_stem + ".o')")
                    problems.append(unit)

            # Literal entries with no local .mojo source: the target.mk
            # rule may reference a source outside this directory, so
            # this is a warning, not a failure.
            for stem in stems:
                var found = False
                for unit in mojo_units:
                    if String(os_path.splitext(unit)[0]) == stem:
                        found = True
                if not found:
                    print("  warning:   SRC_O object '" + stem
                          + ".o' has no matching .mojo source in this"
                          + " directory")

    if len(problems) > 0:
        print()
        print("FAIL: " + String(len(problems))
              + " problem(s) (missing files or SRC_O seam gaps).")
        return

    print()
    print("OK: structure looks complete. Real Genode build is the source of truth.")


def main() raises:
    var args = argv()
    if len(args) != 2:
        print("usage: mojo tool/check_compile.mojo <component-path>   (e.g. src/vct or repos/sponge-de/src/sponge-de)")
        return

    check(String(args[1]))
