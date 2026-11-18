# SPDX-License-Identifier: Apache-2.0
#
# tool/mojo_objects.mojo — pinned-toolchain Mojo object emitter (D16C.4).
#
# Emits a relocatable object from one Mojo source through the pinned
# Mojo toolchain and reports its undefined-symbol surface. This is the
# build seam for Mojo-first components: each unit compiles with
# `mojo build --emit object --relocation-model=pic` and enters the
# Genode target through `SRC_O` (docs/plans/phase16-sponge-compositor.md
# D16C.4; proven shape in docs/evidence/mojo-ffi-probe.md §2).
#
# === Contract ===
#
#   mojo tool/mojo_objects.mojo [build-flags...] -o <output.o> <source.mojo>
#
# * Every `mojo build` flag is caller-supplied and forwarded verbatim
#   (e.g. `--emit object --relocation-model=pic`). This tool adds the
#   `build` subcommand, `-o <output>`, and the source itself.
# * Exactly one `-o <output>` and exactly one `<source.mojo>` are
#   required. Missing or ambiguous source/output is rejected loudly
#   before anything runs.
# * The toolchain is the uv.lock-pinned `<repo>/.venv/bin/mojo`,
#   version-verified against `uv.lock` on every run. An unpinned
#   system `mojo` is never used for emission, and version drift fails
#   loudly (D16C.4 exact-flags / loud-drift discipline).
# * The output's parent directory is created if missing. The helper
#   writes exactly that object (plus the parent directory) and nothing
#   else; in the SRC_O flow the caller keeps outputs inside the
#   repository (e.g. `build/scene.o` next to the component).
# * Compiler and flag errors fail loudly: the compiler's own output is
#   streamed verbatim and its exit code propagates.
# * `nm -u <output>` runs after a successful emit and its output is
#   printed verbatim — never hidden. The undefined-symbol set is the
#   declared KGEN shim/adapter ABI surface (docs/plans/
#   phase16-sponge-compositor.md §6 rule 4).
#
# === Usage ===
#
#   ./tool/mojo_objects --emit object --relocation-model=pic -o build/scene.o scene.mojo
#   ./tool/mojo_objects help
#
# Exit codes:
#   0  success (object emitted, nm -u reported)
#   1  toolchain / nm / io failure
#   2  usage / validation refusal
#   otherwise: the compiler's own exit code (build failure)

from std.sys import argv, exit
from std.python import Python, PythonObject


# ============================================================================
# Captured struct (mirror of tool/bake.mojo's Captured)
# ============================================================================

# Result of running an external command with captured stdout+stderr.
struct Captured(Copyable, Movable):
    var rc: Int
    var output: String

    def __init__(out self):
        self.rc = 0
        self.output = String("")


# ============================================================================
# Plumbing (mirror of tool/bake.mojo / tool/build.mojo)
# ============================================================================

def repo_root() raises -> String:
    """Absolute path of the repository root (the parent of tool/)."""
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def venv_site_packages(root: String) raises -> String:
    """The repo venv's site-packages directory (mirror of build.mojo)."""
    var glob_mod = Python.import_module("glob")
    var os_py = Python.import_module("os.path")
    var pattern = root + "/.venv/lib/python*/site-packages"
    var found: List[String] = []
    for m in glob_mod.glob(pattern):
        var path = String(py=m)
        if Bool(py=os_py.isdir(path)):
            found.append(path)
    if len(found) != 1:
        print("error: cannot determine the venv site-packages directory")
        print("  glob: " + pattern)
        if len(found) == 0:
            print("  no match — materialize the repo venv first (uv sync)")
        else:
            print("  ambiguous: " + String(len(found)) + " matches:")
            for path in found:
                print("    " + path)
        exit(1)
    return found[0]


def pinned_env(root: String) raises -> PythonObject:
    """Child environment for pinned-toolchain invocations.

    The Mojo runtime exports PYTHONEXECUTABLE (and a stale PYTHONHOME
    can ride along); both break a spawned venv python the same way
    tool/build.mojo documents. Strip both, and activate the venv
    through VIRTUAL_ENV + PYTHONPATH instead."""
    var os_py = Python.import_module("os")
    var env = os_py.environ.copy()
    if "PYTHONEXECUTABLE" in env.keys():
        env.pop("PYTHONEXECUTABLE")
    if "PYTHONHOME" in env.keys():
        env.pop("PYTHONHOME")
    env["VIRTUAL_ENV"] = root + "/.venv"
    env["PYTHONPATH"] = venv_site_packages(root)
    env["PATH"] = root + "/.venv/bin:" + String(env["PATH"])
    return env


def run_cmd_capture(cmd: List[String], env: PythonObject) raises -> Captured:
    """Run a command, capture stdout+stderr as a single string, return
    a Captured (rc + output). Python subprocess because Mojo stdlib
    has no process facility."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    var p = subprocess.Popen(
        py_args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    var comm = p.communicate()
    var out_obj = comm[0]
    var out_str = String("")
    if Bool(py=builtins.bool(out_obj)):
        out_str = String(out_obj.decode("utf-8", "replace"))
    var c = Captured()
    c.rc = Int(py=p.returncode)
    c.output = out_str
    return c^


def run_cmd_stream(cmd: List[String], env: PythonObject) raises -> Int:
    """Run a command streaming stdout/stderr to the terminal, return rc."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    return Int(py=subprocess.call(py_args, env=env))


def which(command: String) raises -> Bool:
    """True iff `command` is on PATH."""
    var shutil = Python.import_module("shutil")
    var builtins = Python.import_module("builtins")
    var found = shutil.which(command)
    return Bool(py=builtins.bool(found))


def join_with_space(parts: List[String]) raises -> String:
    var out = String("")
    var first = True
    for p in parts:
        if not first:
            out += " "
        out += p
        first = False
    return out


def contains_substring(haystack: String, needle: String) raises -> Bool:
    """Case-sensitive substring test (Mojo String has no .contains)."""
    var builtins = Python.import_module("builtins")
    return Bool(py=builtins.bool(
        builtins.str(haystack).find(needle) >= 0))


# ============================================================================
# Pinned toolchain resolution (uv.lock / venv — never a system binary)
# ============================================================================

def pinned_mojo_version(root: String) raises -> String:
    """The Mojo version pinned in uv.lock for the `mojo` package, or ""
    if uv.lock is missing or unparsable. Exact `name = "mojo"` match so
    `mojo-compiler` and friends never shadow the pin."""
    var builtins = Python.import_module("builtins")
    var os_py = Python.import_module("os.path")
    var lock_path = root + "/uv.lock"
    if not Bool(py=os_py.isfile(lock_path)):
        return String("")
    var f = builtins.open(lock_path, "r")
    var content_obj = f.read()
    f.close()
    var lines = String(builtins.str(content_obj)).splitlines()
    var in_mojo_pkg = False
    for line_py in lines:
        var line = String(line_py)
        var trimmed = String(builtins.str(line).strip())
        if trimmed == "[[package]]":
            in_mojo_pkg = False
            continue
        if trimmed == "name = \"mojo\"":
            in_mojo_pkg = True
            continue
        if in_mojo_pkg and trimmed.startswith("version = "):
            var raw = trimmed.replace("version = ", "")
            var val = String(builtins.str(raw).strip())
            return val.replace("\"", "")
    return String("")


# ============================================================================
# Usage
# ============================================================================

def print_usage() raises:
    print("Pinned-toolchain Mojo object emitter (D16C.4 build seam)")
    print()
    print("Usage:")
    print("  mojo tool/mojo_objects.mojo [build-flags...] -o <output.o> <source.mojo>")
    print()
    print("Example:")
    print("  mojo tool/mojo_objects.mojo --emit object --relocation-model=pic \\")
    print("      -o build/scene.o scene.mojo")
    print()
    print("Every flag is forwarded verbatim to the pinned `mojo build`")
    print("(<repo>/.venv/bin/mojo, verified against uv.lock). The tool")
    print("adds the `build` subcommand, `-o <output>`, and the source.")
    print("After the emit, `nm -u <output>` is printed verbatim.")
    print()
    print("Exit codes: 0 ok, 1 toolchain/nm/io failure, 2 usage or")
    print("validation refusal; compiler failures propagate their rc.")


# ============================================================================
# Main
# ============================================================================

def main() raises:
    var args = argv()
    if len(args) == 2 and (
        String(args[1]) == "help" or String(args[1]) == "--help"
    ):
        print_usage()
        exit(0)

    # --- Parse: -o <output> extracted; the unique *.mojo token is the
    # --- source; everything else is a caller-supplied build flag.
    var raw_flags = List[String]()
    var output = String("")
    var have_output = False
    var i = 1
    while i < len(args):
        var tok = String(args[i])
        if tok == "-o":
            if have_output:
                print("error: -o given more than once")
                print_usage()
                exit(2)
            if i + 1 >= len(args):
                print("error: -o requires an output path")
                print_usage()
                exit(2)
            output = String(args[i + 1])
            if output.byte_length() == 0:
                print("error: -o requires a non-empty output path")
                print_usage()
                exit(2)
            have_output = True
            i += 2
            continue
        raw_flags.append(tok)
        i += 1

    var build_flags = List[String]()
    var source = String("")
    var source_count = 0
    for tok in raw_flags:
        if not tok.startswith("-") and tok.endswith(".mojo"):
            source = tok
            source_count += 1
        else:
            build_flags.append(tok)

    if not have_output:
        print("error: missing -o <output>")
        print_usage()
        exit(2)
    if source_count == 0:
        print("error: missing <source.mojo>")
        print_usage()
        exit(2)
    if source_count > 1:
        print("error: exactly one <source.mojo> is allowed")
        print_usage()
        exit(2)

    var root = repo_root()
    var os_py = Python.import_module("os.path")

    if not Bool(py=os_py.isfile(source)):
        print("error: source not found: " + source)
        exit(2)

    # --- Pinned toolchain: uv.lock pin + repo venv, never a system binary.
    var pin = pinned_mojo_version(root)
    if pin.byte_length() == 0:
        print("error: cannot determine the pinned Mojo version from "
              + root + "/uv.lock")
        exit(1)
    var mojo_bin = root + "/.venv/bin/mojo"
    if not Bool(py=os_py.isfile(mojo_bin)):
        print("error: pinned Mojo toolchain not found: " + mojo_bin)
        print("  materialize it first: uv sync")
        exit(1)
    var env = pinned_env(root)
    var ver = run_cmd_capture([mojo_bin, "--version"], env)
    if ver.rc != 0 or not contains_substring(ver.output, pin):
        print("error: pinned toolchain drift (D16C.4)")
        print("  uv.lock pin:                " + pin)
        print("  " + mojo_bin + " --version: " + ver.output)
        exit(1)

    # --- Create the output parent directory (the only directory we make).
    var os_mod = Python.import_module("os")
    var out_dir = String(os_py.dirname(output))
    if out_dir.byte_length() > 0:
        os_mod.makedirs(out_dir, exist_ok=True)

    # --- Build: pinned `mojo build` + caller flags + -o <output> <source>.
    var cmd = List[String]()
    cmd.append(mojo_bin)
    cmd.append("build")
    for f in build_flags:
        cmd.append(f)
    cmd.append("-o")
    cmd.append(output)
    cmd.append(source)

    print("[mojo_objects] pinned Mojo (" + pin + "): " + join_with_space(cmd))
    var rc = run_cmd_stream(cmd, env)
    if rc != 0:
        print("error: mojo build failed (rc=" + String(rc)
              + ") — compiler output above")
        exit(rc)
    if not Bool(py=os_py.isfile(output)):
        print("error: compiler reported success but produced no object: "
              + output)
        exit(1)

    # --- Report the undefined-symbol surface, verbatim and unhidden.
    if not which("nm"):
        print("error: host tool 'nm' not found on PATH (needed for the"
              + " undefined-symbol report)")
        exit(1)
    var nm_cmd = ["nm", "-u", output]
    print("[mojo_objects] nm -u " + output + ":")
    var nm_cap = run_cmd_capture(nm_cmd, env)
    if nm_cap.output.byte_length() > 0:
        print(nm_cap.output)
    else:
        print("(no undefined symbols)")
    if nm_cap.rc != 0:
        print("error: nm -u failed (rc=" + String(nm_cap.rc) + ")")
        exit(1)

    print("[mojo_objects] OK: " + output + " emitted from " + source)
