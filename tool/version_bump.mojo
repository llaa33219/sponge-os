# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Bump the Sponge OS version in repos/sponge/include/sponge/version.h.
#
# Updates VERSION_MAJOR / VERSION_MINOR / VERSION_PATCH and rewrites
# VERSION_STRING accordingly. The suffix and codename are preserved.
#
# Usage:
#   mojo tool/version_bump.mojo --patch
#   mojo tool/version_bump.mojo --minor
#   mojo tool/version_bump.mojo --major
#   mojo tool/version_bump.mojo --show

from std.sys import argv
from std.python import Python


def version_header_path() raises -> String:
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here)) + "/repos/sponge/include/sponge/version.h"


def read_file(path: String) raises -> String:
    var io = Python.import_module("io")
    var f = io.open(path, "r")
    var content = f.read()
    f.close()
    return String(content)


def write_file(path: String, content: String) raises:
    var io = Python.import_module("io")
    var f = io.open(path, "w")
    f.write(content)
    f.close()


def parse_field(content: String, field: String) raises -> Int:
    var prefix = "constexpr unsigned " + field + " = "
    var lines = content.split("\n")
    for line in lines:
        var s = String(line).strip()
        if s.startswith(prefix):
            var after = s.removeprefix(prefix)
            var digits = String(after.split(";")[0])
            return Int(digits)
    raise Error("could not find field: " + field)


def parse_suffix(content: String) raises -> String:
    var marker = "constexpr char const *VERSION_SUFFIX = \""
    var lines = content.split("\n")
    for line in lines:
        var s = String(line).strip()
        if s.startswith(marker):
            var after = s.removeprefix(marker)
            return String(after.split("\"")[0])
    return String("")


def join_strings(parts: List[String], separator: String) -> String:
    var result = String("")
    var first = True
    for part in parts:
        if not first:
            result += separator
        result += part
        first = False
    return result


def replace_const_line(line: String, field: String, value: String) -> String:
    var s = line.strip()
    var prefix = "constexpr unsigned " + field + " = "
    if s.startswith(prefix):
        var indent = line.split("constexpr")[0]
        return String(indent) + "constexpr unsigned " + field + " = " + value + ";"
    return line


def replace_string_const_line(line: String, field: String, value: String) -> String:
    var s = line.strip()
    var prefix = "constexpr char const *" + field + " = \""
    if s.startswith(prefix):
        var indent = line.split("constexpr")[0]
        return String(indent) + "constexpr char const *" + field + " = \"" + value + "\";"
    return line


def rewrite_version(content: String, major: Int, minor: Int, patch: Int) raises -> String:
    var version_str = String(major) + "." + String(minor) + "." + String(patch)
    var suffix = parse_suffix(content)
    var full_str = version_str + suffix

    var out: List[String] = []
    var lines = content.split("\n")
    for line in lines:
        var s = String(line)
        s = replace_const_line(s, "VERSION_MAJOR", String(major))
        s = replace_const_line(s, "VERSION_MINOR", String(minor))
        s = replace_const_line(s, "VERSION_PATCH", String(patch))
        s = replace_string_const_line(s, "VERSION_STRING", full_str)
        out.append(s)
    return join_strings(out, "\n")


def show(path: String, content: String) raises:
    var major = parse_field(content, "VERSION_MAJOR")
    var minor = parse_field(content, "VERSION_MINOR")
    var patch = parse_field(content, "VERSION_PATCH")
    var suffix = parse_suffix(content)
    print("Sponge OS version (" + path + ")")
    print("  major: " + String(major))
    print("  minor: " + String(minor))
    print("  patch: " + String(patch))
    print("  full:  " + String(major) + "." + String(minor) + "." + String(patch) + suffix)


def main() raises:
    var os_path = Python.import_module("os.path")
    var path = version_header_path()
    if not os_path.isfile(path):
        print("error: version header not found: " + path)
        return

    var content = read_file(path)
    var args = argv()

    if len(args) < 2:
        show(path, content)
        print()
        print("usage: mojo tool/version_bump.mojo [--major | --minor | --patch | --show]")
        return

    var mode = String(args[1])

    if mode == "--show":
        show(path, content)
        return

    var major = parse_field(content, "VERSION_MAJOR")
    var minor = parse_field(content, "VERSION_MINOR")
    var patch = parse_field(content, "VERSION_PATCH")

    if mode == "--major":
        major = major + 1
        minor = 0
        patch = 0
    elif mode == "--minor":
        minor = minor + 1
        patch = 0
    elif mode == "--patch":
        patch = patch + 1
    else:
        print("error: unknown mode '" + mode + "'")
        print("use one of: --major, --minor, --patch, --show")
        return

    var updated = rewrite_version(content, major, minor, patch)
    write_file(path, updated)

    print("bumped to " + String(major) + "." + String(minor) + "." + String(patch))
    print("updated: " + path)
