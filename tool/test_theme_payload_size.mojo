# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS theme payload-size host gate (Phase 11 W5, risk-register #1).
#
# Asserts that every shipped `*.theme` file under
# `repos/sponge/src/sponge-de/themes/` fits within the theme transport
# cap raised in Phase 11 W1 (`Genode::String<8192>` at
# `repos/sponge/src/sponge-de/theme/theme_controller.cc:155`). The
# `_assert_transport_cap` check inside the daemon emits a
# `Genode::warning("theme payload truncated at 8192 bytes")` when a
# ROM sink is exactly at the cap (i.e. a real shipped theme would
# overflow without a cap raise). This host-side gate closes the same
# trap BEFORE any Genode build: every theme file's byte length must be
# `< 8192` (strictly below the cap; the `length() == 8191` truncation
# warning fires at the daemon side if the file exactly fills the
# buffer).
#
# === Why a host tool exists for this (and not just a probe) ===
#
# The Genode probe path only exercises ONE theme at a time (the active
# `theme.active`); it cannot gate every shipped theme in one boot. A
# host-side assertion catches the same regression in milliseconds and
# survives before any build runs. The Mojo tool follows the same
# pattern as `tool/decor_assets.mojo` (mirror of
# `tool/decor_assets_data/*`): a single-file script + thin bash
# launcher.
#
# === Manual escape hatch (AGENTS.md §3.5 control door) ===
#
# To shrink a theme that exceeds the cap:
#   1. Edit `repos/sponge/src/sponge-de/themes/<name>.theme` to remove
#      or shorten values (e.g. drop unused `[fonts]` rows, collapse
#      multi-line `[colors]` to one row each).
#   2. Re-run `./tool/test_theme_payload_size` to confirm.
#   3. If the cap itself must move: change BOTH the daemon's
#      `Genode::String<8192>` (theme_controller.cc) AND this tool's
#      `comptime MAX_PAYLOAD_BYTES` in lockstep — the two together are
#      the §3 #1 trap closure.
#
# === Usage ===
#
#   mojo tool/test_theme_payload_size.mojo [<themes-dir>]
#   ./tool/test_theme_payload_size              (default:
#                                               repos/sponge/src/sponge-de/themes)
#   ./tool/test_theme_payload_size help
#
# Exit codes:
#   0 — every shipped theme is strictly below MAX_PAYLOAD_BYTES
#   1 — at least one theme is missing / over the cap / not a regular
#       file (the run script uses non-zero to fail loud)
#
# Output: per-file size line + a final SUMMARY line + a non-zero
# exit if any file exceeds the cap. The script NEVER silently truncates
# or downgrades the assertion.

from std.python import Python, PythonObject
from std.sys import argv, exit


comptime MAX_PAYLOAD_BYTES: Int = 8192


comptime DEFAULT_THEMES_DIR = "repos/sponge/src/sponge-de/themes"


def themes_dir_from_argv() raises -> String:
    var args = argv()
    if len(args) == 1:
        return String(DEFAULT_THEMES_DIR)
    var first = String(args[1])
    if first == "help" or first == "--help" or first == "-h":
        print_help()
        exit(0)
    return first


def print_help() raises:
    print("[sponge-theme-payload] Sponge OS theme payload-size host gate")
    print()
    print("Reads every *.theme file in <themes-dir> and asserts each is")
    print("strictly below the transport cap (default " + String(MAX_PAYLOAD_BYTES)
          + " bytes; raise both this constant AND the daemon's")
    print("Genode::String<8192> at theme_controller.cc in lockstep).")
    print()
    print("Usage:")
    print("  mojo tool/test_theme_payload_size.mojo [<themes-dir>]")
    print("  ./tool/test_theme_payload_size                (default: "
          + DEFAULT_THEMES_DIR + ")")
    print("  ./tool/test_theme_payload_size help")
    print()
    print("Exit codes:")
    print("  0  every shipped theme is below the cap")
    print("  1  at least one theme is missing, not a regular file, or")
    print("     exceeds the cap")
    print()
    print("Manual escape hatch (AGENTS.md §3.5):")
    print("  - To shrink a theme: edit repos/sponge/src/sponge-de/themes/")
    print("    <name>.theme and re-run.")
    print("  - To raise the cap: edit BOTH this constant AND")
    print("    theme_controller.cc:155 in lockstep.")


def list_theme_files(themes_dir: String) raises -> List[String]:
    var glob_py = Python.import_module("glob")
    var patterns = String(themes_dir) + "/*.theme"
    # glob_py.glob returns a Python list; iterate it directly without
    # wrapping in Python.list() (which would call repr() and yield the
    # whole list as a single string on String conversion).
    var py_results = glob_py.glob(patterns)
    var n = Int(py=py_results.__len__())
    var out = List[String]()
    for i in range(n):
        var item = String(py_results.__getitem__(i))
        out.append(item^)
    return out^


def file_size(path: String) raises -> Int:
    var os_path = Python.import_module("os.path")
    var s = os_path.getsize(path)
    return Int(py=s)


def main() raises:
    var themes_dir = themes_dir_from_argv()

    print("[sponge-theme-payload] Sponge OS theme payload-size host gate")
    print("  themes_dir:        " + themes_dir)
    print("  max_payload_bytes: " + String(MAX_PAYLOAD_BYTES))
    print()

    var files = list_theme_files(themes_dir)
    if len(files) == 0:
        print("[sponge-theme-payload] FAIL: no *.theme files found in "
              + themes_dir)
        print("  - is the path correct? (default: "
              + DEFAULT_THEMES_DIR + ")")
        print("  - did the W1+W3 themes land in the right directory?")
        exit(1)

    print("[sponge-theme-payload] shipped themes:")
    print()

    var largest: Int = 0
    var largest_name = String("")
    var max_fname_width: Int = 0
    for path in files:
        var name = String(path)
        var slash = name.rfind("/")
        var basename: String
        if slash >= 0:
            basename = String(name[byte=slash + 1:])
        else:
            basename = name
        if basename.byte_length() > max_fname_width:
            max_fname_width = basename.byte_length()

    var failures: Int = 0
    for path in files:
        var name = String(path)
        var slash = name.rfind("/")
        var basename: String
        if slash >= 0:
            basename = String(name[byte=slash + 1:])
        else:
            basename = name
        var size = file_size(path)
        var padded = basename
        while padded.byte_length() < max_fname_width:
            padded = padded + " "
        # Right-pad the size field to width 6 (manually, since String has
        # no rjust yet).
        var size_str = String(size)
        while size_str.byte_length() < 6:
            size_str = " " + size_str
        var status: String
        if size >= MAX_PAYLOAD_BYTES:
            status = "OVER CAP"
            failures += 1
        else:
            status = "ok"
        print("  " + padded + "  " + size_str + " bytes  " + status)
        if size > largest:
            largest = size
            largest_name = basename

    print()
    var headroom = MAX_PAYLOAD_BYTES - largest - 1
    if failures == 0:
        print("[sponge-theme-payload] SUMMARY: "
              + String(len(files)) + " theme(s) verified, largest "
              + largest_name + " = " + String(largest)
              + " bytes, headroom under cap = " + String(headroom)
              + " bytes")
        print("[sponge-theme-payload] PASS")
        exit(0)
    else:
        print("[sponge-theme-payload] SUMMARY: " + String(failures)
              + " of " + String(len(files)) + " theme(s) exceeded the cap")
        print("[sponge-theme-payload] FAIL: shrink the offending theme(s) "
              + "OR raise the cap in lockstep with theme_controller.cc:155")
        exit(1)
