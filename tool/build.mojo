# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS top-level build wrapper.
#
# Wraps the standard Genode build flow so contributors don't have to remember
# the create_builddir + make run/<scenario> dance.
#
# Usage:
#   mojo tool/build.mojo prepare
#   mojo tool/build.mojo list
#   mojo tool/build.mojo run <scenario>
#   mojo tool/build.mojo help
#
# Status: Phase 0/1 scaffold. `list` and `help` are fully implemented;
# `prepare` and `run` print the next steps the user must take manually
# until the Genode source-tree integration is wired up (see
# docs/09-roadmap.md, Phase 1 milestone).

from std.sys import argv
from std.python import Python, PythonObject


def repo_root() raises -> String:
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def print_help() raises:
    var commands: Dict[String, String] = {
        "prepare": "Set up a Genode build directory for Sponge OS",
        "list": "List available run scenarios in run/",
        "run": "Build and run a scenario (mojo tool/build.mojo run <scenario>)",
        "help": "Show this help",
    }

    print("Sponge OS build wrapper")
    print()
    print("Usage:")
    print("  mojo tool/build.mojo <command> [args]")
    print()
    print("Commands:")
    for entry in commands.items():
        print("  " + entry.key + "  " + entry.value)
    print()
    print("See docs/08-development.md for the manual Genode build flow.")


def list_scenarios() raises:
    var os_py = Python.import_module("os")
    var root = repo_root()
    var run_dir = root + "/run"

    var entries: PythonObject
    try:
        entries = os_py.listdir(run_dir)
    except e:
        print("No run/ directory found at " + run_dir)
        return

    var found = False
    for entry in entries:
        var name = String(entry)
        if name.endswith(".run"):
            found = True
            print("  " + name.removesuffix(".run"))

    if not found:
        print("No .run scenarios found in " + run_dir)


def cmd_prepare() raises:
    print("Phase 1 work item: prepare is not yet automated.")
    print()
    print("Manual steps (see docs/08-development.md):")
    print("  1. Check out the Genode source tree.")
    print("  2. Symlink this repo: ln -s <sponge-os>/repos/sponge <genode>/repos/sponge")
    print("  3. Create a build dir: cd <genode> && ./tool/create_builddir x86_64")
    print("  4. Build kernel:       cd build/x86_64 && make kernel")


def cmd_run(scenario: String) raises:
    print("Phase 1 work item: run is not yet automated.")
    print()
    print("To run scenario '" + scenario + "' manually:")
    print("  cd <genode>/build/<target>")
    print("  make run/" + scenario)


def main() raises:
    var args = argv()

    if len(args) < 2:
        print_help()
        return

    var subcommand = String(args[1])

    if subcommand == "help" or subcommand == "--help" or subcommand == "-h":
        print_help()
        return

    if subcommand == "list":
        list_scenarios()
        return

    if subcommand == "prepare":
        cmd_prepare()
        return

    if subcommand == "run":
        if len(args) < 3:
            print("error: 'run' requires a scenario name")
            print("Try: mojo tool/build.mojo list")
            return
        cmd_run(String(args[2]))
        return

    print("error: unknown command '" + subcommand + "'")
    print()
    print_help()
