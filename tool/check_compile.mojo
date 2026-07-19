# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Structural sanity check for a Sponge OS component.
#
# Verifies that a component directory under repos/sponge/src/ has the files
# that Genode's build system expects (target.mk, main.cc), and prints its
# contents for quick inspection. This is a developer fast-feedback tool.
#
# The authoritative build is the Genode build (see docs/08-development.md).
# This script does NOT compile anything; it only checks file presence.
#
# Usage:
#   mojo tool/check_compile.mojo src/vct
#   mojo tool/check_compile.mojo src/sponge-de

from std.sys import argv
from std.python import Python


def repo_root() raises -> String:
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def check(component_rel: String) raises:
    var os_path = Python.import_module("os.path")
    var os_py = Python.import_module("os")

    var cdir = repo_root() + "/repos/sponge/" + component_rel
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
    for entry in os_py.listdir(cdir):
        print("  " + String(entry))

    if len(problems) > 0:
        print()
        print("FAIL: missing " + String(len(problems)) + " required file(s).")
        return

    print()
    print("OK: structure looks complete. Real Genode build is the source of truth.")


def main() raises:
    var args = argv()
    if len(args) != 2:
        print("usage: mojo tool/check_compile.mojo <component-path>   (e.g. src/vct)")
        return

    check(String(args[1]))
