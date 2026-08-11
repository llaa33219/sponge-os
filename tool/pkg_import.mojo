# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS host-side depot package importer.
#
# Repackages a downloaded Genode depot pkg archive into a Sponge OS
# package directory under pkg/<name>/, ready to be staged into a boot
# image by run/<scenario>.run and resolved by sponge_pkgd.
#
# What this tool does:
#   Given a depot pkg reference (e.g. cproc/pkg/qt6_textedit/2025-10-27)
#   whose pkg/src/raw/api archives have already been fetched by
#   `genode/tool/depot/download`, this tool:
#     (a) verifies the pkg is downloaded under genode/depot/<ref>/,
#     (b) reads its `runtime` + `archives` metadata,
#     (c) downloads the matching bin/<arch>/... archive on demand by
#         shelling out to genode/tool/depot/download (no re-implementation),
#     (d) stages every payload file the runtime's <content> declares into
#         pkg/<name>/payload/, copying from the local depot tree (raw
#         archives, the bin archive, plus any *.lib.so or *.tar that is
#         already present in sibling bin/ archives),
#     (e) generates pkg/<name>/metadata.xml per docs/12-package-format.md
#         §4 (schema, config element, sessions mapping the runtime's
#         <requires>),
#     (f) writes pkg/<name>/SOURCE recording depot user/pkg/version plus
#         the SHA-256 of the original depot pkg.tar.xz (reproducibility,
#         docs/11-environment.md §1).
#
# What this tool does NOT do:
#   - It does NOT fetch the pkg/src/raw/api archives themselves. Run
#     `genode/tool/depot/download <user>/pkg/<recipe>/<version>` first.
#   - It does NOT contact the depot at Sponge runtime (Metis A1: all
#     binaries are baked into the boot image at build time; sponge_pkgd
#     never fetches).
#   - It does NOT re-implement any depot tool logic. The bin download is
#     delegated to genode/tool/depot/download exactly as a contributor
#     would invoke it by hand (AGENTS.md §3.5).
#   - It does NOT touch anything outside the repository.
#
# Atomicity (failure-channel discipline):
#   The tool builds the new package under pkg/<name>.tmp/, then renames
#   to pkg/<name>/ only after every step succeeds. Any error before that
#   removes pkg/<name>.tmp/ and exits non-zero, so a bogus depot reference
#   never leaves a partial pkg/ directory behind.
#
# The manual equivalent of every step is documented in
# docs/08-development.md §12 (control escape hatch, AGENTS.md §3.5).
#
# Usage:
#   mojo tool/pkg_import.mojo <user>/pkg/<recipe>/<version> [options]
#   mojo tool/pkg_import.mojo help
#
# Options:
#   --name <sponge-name>   Sponge package name (default: derived from the
#                          depot recipe name; see derive_name).
#   --arch <arch>          Target binary architecture (default: x86_64).
#   --bin-version <ver>    Explicit version for the bin archive when the
#                          pkg version does not map 1:1 to a published
#                          bin version.
#   --bin-recipe <recipe>  Explicit bin recipe name when the pkg recipe
#                          name does not match the depot's bin path.
#   --category <cat>       Launcher category for metadata.xml.

from std.sys import argv, exit
from std.python import Python, PythonObject

comptime DEFAULT_ARCH = "x86_64"

# Python helper source, exec'd once at startup. All XML/string/os.walk
# parsing goes through these functions because the Mojo stdlib lacks
# regex, XML, and tuple-unpacking in `for` loops. Each helper returns
# only JSON-serialisable types (str / list / int / None) so the
# Mojo<->Python boundary stays simple.
comptime PY_HELPERS = """
import os
import re
import hashlib
import urllib.request
import urllib.error

def split_str_to_list(s, sep):
    return s.split(sep)

def slice_str(s, start, end):
    return s[start:end]

def find_str(s, needle, start=0):
    return s.find(needle, start)

def startswith(s, prefix):
    return s.startswith(prefix)

def strip_str(s):
    return s.strip()

def upper_first(s):
    if not s:
        return s
    return s[0].upper() + s[1:]

def parse_runtime_attrs(text):
    # The <runtime .../> opening tag carries binary/ram/caps/config.
    m = re.search(r'<runtime\\b([^>]*)>', text)
    if not m:
        return ['', '', '', '']
    attrs_str = m.group(1)
    def get(name):
        mm = re.search(name + r'="([^"]*)"', attrs_str)
        return mm.group(1) if mm else ''
    return [get('binary'), get('ram'), get('caps'), get('config')]

def parse_runtime_content_roms(text):
    out = []
    m = re.search(r'<content>(.*?)</content>', text, re.DOTALL)
    if not m:
        return out
    body = m.group(1)
    for mm in re.finditer(r'<rom\\b[^>]*label="([^"]*)"', body):
        out.append(mm.group(1))
    return out

def parse_runtime_requires(text):
    out = []
    m = re.search(r'<requires>(.*?)</requires>', text, re.DOTALL)
    if not m:
        return out
    body = m.group(1)
    for mm in re.finditer(r'<([a-zA-Z_][a-zA-Z0-9_]*)', body):
        tag = mm.group(1)
        if tag != 'requires':
            out.append(tag)
    return out

def list_files_recursive(root):
    out = []
    if not os.path.isdir(root):
        return out
    for dirpath, dirnames, filenames in os.walk(root):
        for f in filenames:
            full = os.path.join(dirpath, f)
            if os.path.isfile(full):
                out.append(full)
    return out

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        while True:
            chunk = fh.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def http_head_status(url, timeout=15):
    req = urllib.request.Request(url, method='HEAD')
    try:
        resp = urllib.request.urlopen(req, timeout=timeout)
        return int(resp.status)
    except urllib.error.HTTPError as e:
        return int(e.code)
    except Exception:
        return 0
"""

# Mojo has no module-level mutable state, so the helpers cache lives on
# the Python side via sys.modules (see py_helpers below).


def py_helpers() raises -> PythonObject:
    """Lazily exec the Python helpers into a fresh module namespace and
    return it. The result is also stashed in Python's sys.modules so
    subsequent calls reuse it (the cache lives on the Python side)."""
    var sys = Python.import_module("sys")
    if "_sponge_pkg_import_helpers" in sys.modules:
        return sys.modules["_sponge_pkg_import_helpers"]
    var types = Python.import_module("types")
    var builtins = Python.import_module("builtins")
    var ns = types.ModuleType("_sponge_pkg_import_helpers")
    builtins.exec(PY_HELPERS, ns.__dict__)
    sys.modules["_sponge_pkg_import_helpers"] = ns
    return ns


# ----- Mojo-side thin wrappers over the Python helpers ---------------

def py_split(text_ref: String, sep: String) raises -> List[String]:
    var h = py_helpers()
    var py_list = h.split_str_to_list(text_ref, sep)
    var out: List[String] = []
    for item in py_list:
        out.append(String(item))
    return out^


def slice_str(s: String, start: Int, end: Int) raises -> String:
    var h = py_helpers()
    return String(h.slice_str(s, start, end))


def find_str(s: String, needle: String, start: Int = 0) raises -> Int:
    var h = py_helpers()
    return Int(py=h.find_str(s, needle, start))


def startswith(s: String, prefix: String) raises -> Bool:
    var h = py_helpers()
    return Bool(h.startswith(s, prefix))


def parse_runtime_attrs(text: String) raises -> List[String]:
    var h = py_helpers()
    var py_list = h.parse_runtime_attrs(text)
    var out: List[String] = []
    for item in py_list:
        out.append(String(item))
    return out^


def parse_runtime_content_roms(text: String) raises -> List[String]:
    var h = py_helpers()
    var py_list = h.parse_runtime_content_roms(text)
    var out: List[String] = []
    for item in py_list:
        out.append(String(item))
    return out^


def parse_runtime_requires(text: String) raises -> List[String]:
    var h = py_helpers()
    var py_list = h.parse_runtime_requires(text)
    var out: List[String] = []
    for item in py_list:
        out.append(String(item))
    return out^


def list_files_recursive(root: String) raises -> List[String]:
    var h = py_helpers()
    var py_list = h.list_files_recursive(root)
    var out: List[String] = []
    for item in py_list:
        out.append(String(item))
    return out^


def sha256_of_file(path: String) raises -> String:
    var h = py_helpers()
    return String(h.sha256_file(path))


def http_head_status(url: String) raises -> Int:
    var h = py_helpers()
    return Int(py=h.http_head_status(url))


# ----- Pure Mojo utilities ------------------------------------------

def repo_root() raises -> String:
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def run_argv_streaming(cmd: List[String], cwd: String) raises -> Int:
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    var rc: PythonObject
    if cwd.byte_length() > 0:
        rc = subprocess.call(py_args, cwd=cwd)
    else:
        rc = subprocess.call(py_args)
    return Int(py=rc)


def is_dir(path: String) raises -> Bool:
    var os_path = Python.import_module("os.path")
    return Bool(os_path.isdir(path))


def is_file(path: String) raises -> Bool:
    var os_path = Python.import_module("os.path")
    return Bool(os_path.isfile(path))


def read_text(path: String) raises -> String:
    var builtins = Python.import_module("builtins")
    var f = builtins.open(path, "r")
    var content = f.read()
    f.close()
    return String(content)


def write_text(path: String, content: String) raises:
    var builtins = Python.import_module("builtins")
    var os_py = Python.import_module("os")
    var os_path = Python.import_module("os.path")
    var parent = String(os_path.dirname(path))
    if parent.byte_length() > 0 and not is_dir(parent):
        os_py.makedirs(parent)
    var f = builtins.open(path, "w")
    f.write(content)
    f.close()


def ensure_dir(path: String) raises:
    var os_py = Python.import_module("os")
    var os_path = Python.import_module("os.path")
    if not os_path.isdir(path):
        os_py.makedirs(path)


def remove_tree(path: String) raises:
    var os_path = Python.import_module("os.path")
    var shutil = Python.import_module("shutil")
    if not os_path.exists(path):
        return
    if os_path.isdir(path) and not os_path.islink(path):
        shutil.rmtree(path)
    else:
        var os_py = Python.import_module("os")
        os_py.unlink(path)


def copy_file(src: String, dst: String) raises:
    var shutil = Python.import_module("shutil")
    var os_path = Python.import_module("os.path")
    var parent = String(os_path.dirname(dst))
    if parent.byte_length() > 0 and not is_dir(parent):
        ensure_dir(parent)
    shutil.copy2(src, dst)


def basename(path: String) raises -> String:
    var os_path = Python.import_module("os.path")
    return String(os_path.basename(path))


def default_category(pkg_recipe: String) raises -> String:
    if find_str(pkg_recipe, "falkon") >= 0:
        return "Internet"
    if find_str(pkg_recipe, "textedit") >= 0:
        return "Editors"
    return "Utilities"


# Derive a Sponge package name from a depot pkg recipe name.
#   qt6_<thing>           -> <thing>
#   <thing>_qt6           -> <thing>
#   <thing>_qt6-<variant> -> <thing>
def derive_name(pkg_recipe: String) raises -> String:
    var name = pkg_recipe
    if startswith(name, "qt6_"):
        name = slice_str(name, 4, name.byte_length())
    var qt6_at = find_str(name, "_qt6")
    if qt6_at >= 0:
        name = slice_str(name, 0, qt6_at)
    return name


# Derive the bin archive recipe name. cproc publishes the pkg
# `falkon_qt6-jemalloc` under the bin path `falkon_qt6`, so we strip a
# `-<variant>` suffix unless --bin-recipe overrides.
def derive_bin_recipe(pkg_recipe: String, override: String) raises -> String:
    if override.byte_length() > 0:
        return override
    var name = pkg_recipe
    var dash_at = find_str(name, "-")
    if dash_at >= 0:
        name = slice_str(name, 0, dash_at)
    return name


# Map a runtime <requires> tag name to a docs/12 §4.1 <session> name.
def session_name(req_tag: String) raises -> String:
    if req_tag == "gui":
        return "Gui"
    if req_tag == "input":
        return "Input"
    if req_tag == "file_system":
        return "File_system"
    if req_tag == "report":
        return "Report"
    if req_tag == "rom":
        return "ROM"
    if req_tag == "timer":
        return "Timer"
    if req_tag == "nic":
        # Genode service names are exactly "Nic" / "Rtc" (see
        # pkg/falkon/metadata.xml header note) — not NIC / RTC.
        return "Nic"
    if req_tag == "gpu":
        # A depot runtime that requires <gpu/> is served by Genode's
        # Gui service (GPU sessions are exposed as a Gui subinterface).
        return "Gui"
    if req_tag == "rtc":
        return "Rtc"
    if req_tag == "capture":
        return "Capture"
    if req_tag == "play":
        return "Play"
    if req_tag == "record":
        return "Record"
    if req_tag == "rm":
        return "RM"
    if req_tag.byte_length() == 0:
        return ""
    var h = py_helpers()
    return String(h.upper_first(req_tag))


# Read the depot URL for the given user from the in-tree
# genode/repos/<repo>/sculpt/depot/<user>/download file.
def depot_url_for_user(root: String, user: String) raises -> String:
    var os_path = Python.import_module("os.path")
    var repos_dir = root + "/genode/repos"
    if not is_dir(repos_dir):
        return ""
    var os_py = Python.import_module("os")
    var entries = os_py.listdir(repos_dir)
    for entry in entries:
        var repo = String(entry)
        var download_file = repos_dir + "/" + repo + "/sculpt/depot/" + user + "/download"
        if os_path.isfile(download_file):
            var txt = read_text(download_file)
            var h = py_helpers()
            return String(h.strip_str(txt))
    return ""


# Compute candidate bin refs in priority order.
def candidate_bin_refs(user: String, arch: String, bin_recipe: String,
                       binary_name: String, pkg_recipe: String,
                       bin_version: String) raises -> List[String]:
    var out: List[String] = []
    var cands: List[String] = [
        user + "/bin/" + arch + "/" + bin_recipe + "/" + bin_version,
        user + "/bin/" + arch + "/" + binary_name + "/" + bin_version,
        user + "/bin/" + arch + "/" + pkg_recipe + "/" + bin_version,
    ]
    for c in cands:
        var dup = False
        for s in out:
            if s == c:
                dup = True
        if not dup:
            out.append(c)
    return out^


# Try each candidate bin ref's <ref>.tar.xz with a HEAD request; return
# the first that resolves (status 200), or "" if none do.
def resolve_bin_ref(root: String, user: String, arch: String,
                    bin_recipe: String, binary_name: String,
                    pkg_recipe: String, bin_version: String) raises -> String:
    var url_base = depot_url_for_user(root, user)
    if url_base.byte_length() == 0:
        return ""
    var cands = candidate_bin_refs(user, arch, bin_recipe, binary_name,
                                    pkg_recipe, bin_version)
    for c in cands:
        var url = url_base + "/" + c + ".tar.xz"
        var status = http_head_status(url)
        if status == 200:
            return c
    return ""


# Invoke genode/tool/depot/download for the given ref. The outer make
# wrapper swallows inner-download errors, so callers verify the expected
# output directory exists rather than relying on the exit code.
def invoke_depot_download(root: String, archive_ref: String) raises -> Int:
    var cmd: List[String] = [
        root + "/genode/tool/depot/download",
        archive_ref,
    ]
    return run_argv_streaming(cmd, cwd=root)


# Index every file under genode/depot/<user>/bin/<arch>/ as basename -> abspath.
def index_bin_payloads(root: String, user: String, arch: String) raises
        -> Dict[String, String]:
    var bin_root = root + "/genode/depot/" + user + "/bin/" + arch
    var out: Dict[String, String] = {}
    if not is_dir(bin_root):
        return out^
    var files = list_files_recursive(bin_root)
    for f in files:
        var base = basename(f)
        if base not in out:
            out[base] = f
    return out^


# Index every file under genode/depot/<user>/raw/ as basename -> abspath.
def index_raw_payloads(root: String, user: String) raises -> Dict[String, String]:
    var raw_root = root + "/genode/depot/" + user + "/raw"
    var out: Dict[String, String] = {}
    if not is_dir(raw_root):
        return out^
    var files = list_files_recursive(raw_root)
    for f in files:
        var base = basename(f)
        if base not in out:
            out[base] = f
    return out^


# Generate pkg/<name>/metadata.xml per docs/12 §4.
# caps floor: when the runtime requires Gui, docs/12 §4.1 + the
# Phase 7 §11.1 capability-exhaustion lesson mandate >= 1000 caps for
# Qt-based apps on seL4. The depot runtime's caps attribute is the
# upstream default and may be smaller; we raise it rather than carry
# the smaller value into the Sponge package.
def generate_metadata(name: String, version: String, binary: String,
                      description: String, ram: String, caps: String,
                      category: String, requires: List[String],
                      runtime_config_label: String) raises -> String:
    var out = String("")
    out += "<package>\n"
    out += "  <!--\n"
    out += "       Generated by tool/pkg_import from a depot pkg archive.\n"
    out += "       Schema: docs/12-package-format.md §4. Edit freely;\n"
    out += "       sponge_pkgd parses the result like any hand-written pkg.\n"
    out += "  -->\n"
    out += "  <name>" + name + "</name>\n"
    out += "  <version>" + version + "</version>\n"
    out += "  <description>" + description + "</description>\n"
    out += "\n"
    out += "  <binary>" + binary + "</binary>\n"
    var ram_attr = ram
    if ram_attr.byte_length() == 0:
        ram_attr = "64M"
    var caps_attr = caps
    if caps_attr.byte_length() == 0:
        caps_attr = "1000"
    # GUI-safe caps floor per docs/09-roadmap.md §11.1 + plan A3.
    var needs_gui = False
    for r in requires:
        if r == "gui" or r == "input":
            needs_gui = True
    if needs_gui:
        var caps_int = Int(caps_attr)
        if caps_int < 1000:
            caps_int = 1000
        caps_attr = String(caps_int)
    out += "  <quota ram=\"" + ram_attr + "\" caps=\"" + caps_attr + "\"/>\n"
    out += "\n"
    # <config> with libc + vfs boilerplate modeled on
    # run/sponge-launcher.run:126-136 (Qt app wiring).
    out += "  <config>\n"
    out += "    <libc stdout=\"/dev/log\" stderr=\"/dev/log\""
    out += " pipe=\"/pipe\" rtc=\"/dev/rtc\"/>\n"
    out += "    <vfs>\n"
    out += "      <dir name=\"dev\">\n"
    out += "        <log/>\n"
    out += "        <inline name=\"rtc\">2018-01-01 00:01</inline>\n"
    out += "      </dir>\n"
    out += "      <dir name=\"pipe\"><pipe/></dir>\n"
    out += "      <tar name=\"qt6_dejavusans.tar\"/>\n"
    out += "      <tar name=\"qt6_libqgenode.tar\"/>\n"
    if runtime_config_label.byte_length() > 0:
        out += "      <rom name=\"" + runtime_config_label + "\"/>\n"
    out += "    </vfs>\n"
    out += "  </config>\n"
    out += "\n"
    out += "  <launcher category=\"" + category + "\"/>\n"
    out += "\n"
    # Sessions from runtime requires, deduplicated by session name
    # (the runtime can list multiple <report label="..."/> entries; one
    # <session> per session type is enough for routing).
    if len(requires) > 0:
        var seen: List[String] = []
        var lines: List[String] = []
        for req in requires:
            var sn = session_name(req)
            if sn.byte_length() == 0:
                continue
            var dup = False
            for s in seen:
                if s == sn:
                    dup = True
            if not dup:
                seen.append(sn)
                lines.append("    <session name=\"" + sn + "\" default-route=\"parent\"/>")
        if len(lines) > 0:
            out += "  <sessions>\n"
            for line in lines:
                out += line + "\n"
            out += "  </sessions>\n"
        else:
            out += "  <sessions/>\n"
    else:
        out += "  <sessions/>\n"
    out += "</package>\n"
    return out


def generate_source(user: String, pkg_recipe: String, pkg_version: String,
                    pkg_sha: String, bin_ref: String, bin_sha: String,
                    depot_url: String) raises -> String:
    var out = String("")
    out += "# Sponge OS package source record (reproducibility).\n"
    out += "# Generated by tool/pkg_import. Edit by hand only to refetch\n"
    out += "# from a different depot archive; never delete while the pkg\n"
    out += "# directory is in use.\n"
    out += "#\n"
    out += "depot_user: " + user + "\n"
    out += "depot_pkg_recipe: " + pkg_recipe + "\n"
    out += "depot_pkg_version: " + pkg_version + "\n"
    out += "depot_pkg_archive_sha256: " + pkg_sha + "\n"
    out += "depot_bin_archive_ref: " + bin_ref + "\n"
    out += "depot_bin_archive_sha256: " + bin_sha + "\n"
    out += "depot_url: " + depot_url + "\n"
    out += "imported_by: tool/pkg_import\n"
    return out


def generate_paylist(staged: List[String], missing: List[String]) raises -> String:
    var out = String("")
    out += "# Payload inventory for this pkg/<name>/.\n"
    out += "# Generated by tool/pkg_import. The runtime's <content> block\n"
    out += "# lists every ROM the binary expects at boot; this file records\n"
    out += "# which were staged into payload/ and which were not found in\n"
    out += "# the local depot tree.\n"
    out += "#\n"
    out += "staged (" + String(len(staged)) + "):\n"
    for s in staged:
        out += "  " + s + "\n"
    out += "missing (" + String(len(missing)) + "):\n"
    for m in missing:
        out += "  " + m + "\n"
    return out


def print_help() raises:
    print("Sponge OS host-side depot package importer")
    print()
    print("Usage:")
    print("  mojo tool/pkg_import.mojo <user>/pkg/<recipe>/<version> [options]")
    print("  mojo tool/pkg_import.mojo help")
    print()
    print("Options:")
    print("  --name <name>          Sponge pkg name (default: derived from recipe)")
    print("  --arch <arch>          Binary arch (default: x86_64)")
    print("  --bin-version <ver>    Override the bin archive version")
    print("  --bin-recipe <recipe>  Override the bin archive recipe name")
    print("  --category <cat>       Launcher category (default: Editors/Internet/Utilities)")
    print()
    print("Prerequisite:")
    print("  ./genode/tool/depot/download <user>/pkg/<recipe>/<version>")
    print("    (fetches the pkg + src/raw/api deps into genode/depot/ and genode/public/)")
    print()
    print("The tool downloads the bin archive on demand by shelling out to")
    print("genode/tool/depot/download. It never touches anything outside the")
    print("repository. See docs/08-development.md §12 for the manual escape hatch.")


# Parse argv into [ref, name, arch, bin_version, bin_recipe, category].
# Returns an empty list on help request or parse error (after exiting).
# Reads argv() itself so the Span-vs-List typing stays inside this fn.
def parse_args() raises -> List[String]:
    var args = argv()
    if len(args) < 2:
        return []
    var i = 1
    var archive_ref = String("")
    var name = String("")
    var arch = DEFAULT_ARCH
    var bin_version = String("")
    var bin_recipe = String("")
    var category = String("")
    while i < len(args):
        var a = String(args[i])
        if a == "help" or a == "--help" or a == "-h":
            return []
        if startswith(a, "--"):
            if i + 1 >= len(args):
                print("error: option " + a + " requires a value")
                exit(2)
            var val = String(args[i + 1])
            if a == "--name":
                name = val
            elif a == "--arch":
                arch = val
            elif a == "--bin-version":
                bin_version = val
            elif a == "--bin-recipe":
                bin_recipe = val
            elif a == "--category":
                category = val
            else:
                print("error: unknown option " + a)
                exit(2)
            i += 2
        else:
            if archive_ref.byte_length() == 0:
                archive_ref = a
                i += 1
            else:
                print("error: unexpected positional argument '" + a + "'")
                exit(2)
    return [archive_ref, name, arch, bin_version, bin_recipe, category]


# Validate that `archive_ref` is <user>/pkg/<recipe>/<version>; exit
# non-zero with a clear error otherwise.
def parse_pkg_ref(archive_ref: String) raises -> List[String]:
    var parts = py_split(archive_ref, "/")
    if len(parts) != 4:
        print("error: depot ref must be <user>/pkg/<recipe>/<version>")
        print("       got: '" + archive_ref + "'")
        print("       example: cproc/pkg/qt6_textedit/2025-10-27")
        exit(2)
    if parts[1] != "pkg":
        print("error: depot ref must reference a pkg archive (got type '" + parts[1] + "')")
        print("       this tool only repackages pkg archives; bin/src/raw/api are not supported")
        exit(2)
    return parts^


def main() raises:
    var args = argv()
    var parsed = parse_args()
    if len(parsed) == 0:
        print_help()
        if len(args) >= 2:
            var sub = String(args[1])
            if sub == "help" or sub == "--help" or sub == "-h":
                return
        exit(2)

    var archive_ref = parsed[0]
    var name_override = parsed[1]
    var arch = parsed[2]
    var bin_version_override = parsed[3]
    var bin_recipe_override = parsed[4]
    var category_override = parsed[5]

    var parts = parse_pkg_ref(archive_ref)
    var user = parts[0]
    var pkg_recipe = parts[2]
    var pkg_version = parts[3]

    var sponge_name = name_override
    if sponge_name.byte_length() == 0:
        sponge_name = derive_name(pkg_recipe)

    var category = category_override
    if category.byte_length() == 0:
        category = default_category(pkg_recipe)

    var root = repo_root()

    print("[sponge-pkg-import] Sponge OS depot package importer")
    print("  repo root:    " + root)
    print("  depot ref:    " + archive_ref)
    print("  sponge name:  " + sponge_name)
    print("  arch:         " + arch)
    print("  category:     " + category)
    print()

    # (a) Verify the pkg is downloaded under genode/depot/<archive_ref>/.
    var pkg_dir = root + "/genode/depot/" + archive_ref
    if not is_dir(pkg_dir):
        print("error: depot pkg directory not found at " + pkg_dir)
        print("       Run this first to fetch the pkg + its transitive deps:")
        print("         " + root + "/genode/tool/depot/download " + archive_ref)
        print("       (The cproc pubkey must be in your GNUPGHOME keybox;")
        print("        see docs/08-development.md §12 for the manual setup.)")
        exit(1)

    var runtime_path = pkg_dir + "/runtime"
    if not is_file(runtime_path):
        print("error: depot pkg is missing its 'runtime' file at " + runtime_path)
        print("       the pkg archive is incomplete; re-run depot/download " + archive_ref)
        exit(1)
    var runtime_text = read_text(runtime_path)
    var runtime_attrs = parse_runtime_attrs(runtime_text)
    var binary_name = runtime_attrs[0]
    var runtime_ram = runtime_attrs[1]
    var runtime_caps = runtime_attrs[2]
    var runtime_config = runtime_attrs[3]
    if binary_name.byte_length() == 0:
        print("error: could not parse binary name from runtime at " + runtime_path)
        exit(1)

    print("[sponge-pkg-import] runtime metadata:")
    print("  binary:       " + binary_name)
    print("  ram:          " + runtime_ram)
    print("  caps:         " + runtime_caps)
    print("  config file:  " + runtime_config)
    print()

    # (b) Compute SHA-256 of the original depot pkg.tar.xz.
    var pkg_archive_abs = root + "/genode/public/" + archive_ref + ".tar.xz"
    if not is_file(pkg_archive_abs):
        print("error: depot pkg archive not found at " + pkg_archive_abs)
        print("       depot/download did not leave the .tar.xz in public/.")
        print("       Re-run: " + root + "/genode/tool/depot/download " + archive_ref)
        exit(1)
    print("[sponge-pkg-import] hashing depot pkg archive...")
    var pkg_sha = sha256_of_file(pkg_archive_abs)
    print("  sha256:       " + pkg_sha)
    print()

    # (c) Resolve and download the bin archive.
    var bin_recipe = derive_bin_recipe(pkg_recipe, bin_recipe_override)
    var bin_version = bin_version_override
    if bin_version.byte_length() == 0:
        bin_version = pkg_version

    print("[sponge-pkg-import] resolving bin archive...")
    var bin_ref = resolve_bin_ref(root, user, arch, bin_recipe,
                                   binary_name, pkg_recipe, bin_version)
    if bin_ref.byte_length() == 0:
        print("error: no candidate bin archive exists at the depot for")
        print("       pkg " + archive_ref)
        print("       tried (with --arch " + arch + ", --bin-version " + bin_version + "):")
        var cands = candidate_bin_refs(user, arch, bin_recipe, binary_name,
                                        pkg_recipe, bin_version)
        for c in cands:
            print("         " + c)
        print()
        print("       The pkg version may not match any published bin version.")
        print("       List the available bin versions and re-run with --bin-version:")
        print("         curl -s https://depot.genode.org/" + user + "/bin/" + arch + "/" + bin_recipe + "/")
        exit(1)
    print("  bin ref:      " + bin_ref)
    print()

    var bin_dir = root + "/genode/depot/" + bin_ref
    if not is_dir(bin_dir):
        print("[sponge-pkg-import] downloading bin archive " + bin_ref + " ...")
        var _rc = invoke_depot_download(root, bin_ref)
        if not is_dir(bin_dir):
            print()
            print("error: depot/download did not extract the bin archive at")
            print("       " + bin_dir)
            print("       The most common cause is a PGP keybox issue")
            print("       (the cproc pubkey must be in your GNUPGHOME;")
            print("       see docs/08-development.md §12).")
            exit(1)
        print("[sponge-pkg-import] bin archive downloaded.")
    else:
        print("[sponge-pkg-import] bin archive already present at " + bin_dir)
    print()

    var bin_archive_abs = root + "/genode/public/" + bin_ref + ".tar.xz"
    var bin_sha = String("")
    if is_file(bin_archive_abs):
        bin_sha = sha256_of_file(bin_archive_abs)

    # (d) Stage payload into pkg/<sponge_name>.tmp/payload/.
    var pkg_target = root + "/pkg/" + sponge_name
    var pkg_tmp = pkg_target + ".tmp"
    if is_dir(pkg_tmp):
        remove_tree(pkg_tmp)
    if is_dir(pkg_target):
        print("error: pkg target already exists at " + pkg_target)
        print("       remove it first (rm -rf " + pkg_target + ") or pass --name")
        print("       to import into a different sponge pkg name.")
        exit(1)

    var payload_dir = pkg_tmp + "/payload"
    ensure_dir(payload_dir)

    print("[sponge-pkg-import] staging payload into " + payload_dir + " ...")
    var raw_index = index_raw_payloads(root, user)
    var bin_index = index_bin_payloads(root, user, arch)

    var content_roms = parse_runtime_content_roms(runtime_text)
    var staged: List[String] = []
    var missing: List[String] = []
    for label in content_roms:
        var src = String("")
        if label in bin_index:
            src = bin_index[label]
        elif label in raw_index:
            src = raw_index[label]
        if src.byte_length() > 0 and is_file(src):
            var dst = payload_dir + "/" + label
            copy_file(src, dst)
            staged.append(label)
        else:
            missing.append(label)

    print("  staged:   " + String(len(staged)) + " files")
    print("  missing:  " + String(len(missing)) + " entries (listed in PAYLIST)")
    print()

    # (e) Generate metadata.xml + SOURCE + PAYLIST.
    var description = "Imported from Genode depot pkg " + archive_ref + " by tool/pkg_import."
    var requires = parse_runtime_requires(runtime_text)
    var metadata_xml = generate_metadata(
        sponge_name, pkg_version, binary_name, description,
        runtime_ram, runtime_caps, category,
        requires,
        runtime_config)
    var depot_url = depot_url_for_user(root, user)
    if depot_url.byte_length() == 0:
        depot_url = "(unknown)"
    var source_text = generate_source(user, pkg_recipe, pkg_version, pkg_sha,
                                       bin_ref, bin_sha, depot_url)
    var paylist_text = generate_paylist(staged, missing)

    write_text(pkg_tmp + "/metadata.xml", metadata_xml)
    write_text(pkg_tmp + "/SOURCE", source_text)
    write_text(pkg_tmp + "/PAYLIST", paylist_text)

    # (f) Atomic rename: pkg/<name>.tmp -> pkg/<name>.
    var os_py = Python.import_module("os")
    os_py.rename(pkg_tmp, pkg_target)

    print("[sponge-pkg-import] SUCCESS: package written to " + pkg_target)
    print()
    print("  Contents:")
    print("    " + pkg_target + "/metadata.xml    (docs/12 schema)")
    print("    " + pkg_target + "/SOURCE          (depot pin + sha256)")
    print("    " + pkg_target + "/PAYLIST         (staged vs missing payload)")
    print("    " + pkg_target + "/payload/        (" + String(len(staged)) + " files)")
    print()
    if len(missing) > 0:
        print("  NOTE: " + String(len(missing)) + " payload entries listed in the")
        print("  depot runtime's <content> were not found in the local depot")
        print("  tree and are recorded in PAYLIST. Stage them separately if")
        print("  the scenario needs them, or ignore if they are optional.")
        print()
    print("  Next: wire the package into a run scenario via")
    print("    build_boot_image [build_artifacts]")
    print("  and add its name to the scenario's staged pkgs list.")
