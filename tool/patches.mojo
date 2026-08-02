# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Patch ledger manager for the Sponge OS repository.
#
# The patch ledger (docs/11-environment.md §4) records the Sponge-local
# commits that sit on top of the vendored, pinned Genode 26.05 subtree at
# genode/. This tool reads the ledger table and answers the questions a
# contributor actually asks: which patches exist, does the ledger still
# match git reality, how do I carry the patch set elsewhere, and how do I
# drop a patch once upstream absorbs it.
#
# Subcommands:
#   list            Print each ledger row and whether git knows the commit.
#   verify          Check every row against git (exists, ancestor of HEAD,
#                   touched paths). Exit non-zero if a commit is missing.
#   export <dir>    Write each patch as a git-format-patch file into <dir>.
#   drop <n>        Print the manual revert instructions for patch #n.
#
# The tool is read-only against the repository: it never reverts, commits,
# or edits the ledger itself. `drop` deliberately only prints instructions
# (AGENTS.md §5.2: dropping a patch is a deliberate act, recorded as its
# own commit by a human). The manual equivalent of every subcommand is
# documented in docs/11-environment.md §4.1 (control escape hatch).
#
# Usage:
#   mojo tool/patches.mojo list
#   mojo tool/patches.mojo verify
#   mojo tool/patches.mojo export <dir>
#   mojo tool/patches.mojo drop <n>
#   mojo tool/patches.mojo help

from std.sys import argv, exit
from std.python import Python, PythonObject


struct PatchRow(Copyable, Movable):
    """One row of the patch ledger table."""

    var number: Int
    var sha_prefix: String
    var subject: String
    var what_where: String

    def __init__(out self, number: Int, sha_prefix: String, subject: String,
                 what_where: String):
        self.number = number
        self.sha_prefix = sha_prefix
        self.subject = subject
        self.what_where = what_where


def repo_root() raises -> String:
    """Locate the repository root as the parent of the tool/ directory that
    contains this script, so the tool works from any cwd."""
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def git_capture(args: List[String], cwd: String) raises -> PythonObject:
    """Run `git <args>` with the repo root as cwd and return the completed
    process object (stdout/stderr captured as text)."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    py_args.append("git")
    for part in args:
        py_args.append(part)
    return subprocess.run(py_args, cwd=cwd, capture_output=True, text=True)


def read_text_file(path: String) raises -> String:
    var builtins = Python.import_module("builtins")
    var f = builtins.open(path, "r")
    var content = f.read()
    f.close()
    return String(content)


def strip_ticks(text: String) raises -> String:
    var builtins = Python.import_module("builtins")
    return String(builtins.str(text).strip().strip("`"))


def parse_ledger(root: String) raises -> List[PatchRow]:
    """Parse the patch ledger markdown table out of
    docs/11-environment.md §4.

    The table is the source of truth; each row has the columns
    `# | Commit | Subject | What / Where | Why | Drop When`.
    """
    var doc_path = root + "/docs/11-environment.md"
    var content = read_text_file(doc_path)
    var rows: List[PatchRow] = []

    var in_table = False
    var saw_separator = False
    for line_obj in content.split("\n"):
        var line = String(line_obj)
        var stripped = String(Python.import_module("builtins").str(line).strip())

        if not in_table:
            # Header row of the ledger table marks the start.
            if stripped.startswith("| # | Commit |"):
                in_table = True
            continue

        if not saw_separator:
            # The |---|---|... separator line.
            saw_separator = True
            continue

        if not stripped.startswith("|"):
            # First non-table line ends the table.
            break

        # Split on '|' and drop the empty edges.
        var cells: List[String] = []
        for cell in stripped.split("|"):
            var c = String(
                Python.import_module("builtins").str(String(cell)).strip()
            )
            cells.append(c)
        # Leading and trailing '|' produce empty first/last cells.
        if len(cells) < 8:
            continue
        # cells[0] == "" (before leading |), then 6 columns, then "".
        var num_text = cells[1]
        var number: Int
        try:
            number = Int(py=Python.import_module("builtins").int(num_text))
        except:
            continue
        rows.append(PatchRow(
            number,
            strip_ticks(cells[2]),
            cells[3],
            cells[4],
        ))

    if len(rows) == 0:
        print("error: no ledger rows parsed from " + doc_path)
        print("The table header '| # | Commit | ...' was not found.")
        exit(1)
    return rows^


def slugify(subject: String) raises -> String:
    """Turn a commit subject into a filename-safe slug:
    lowercase, every run of non-alphanumeric bytes becomes one '-'."""
    var out = String("")
    var last_was_dash = True  # suppress a leading dash
    for i in range(subject.byte_length()):
        var ch = String(subject[byte=i])
        var lower = String(Python.import_module("builtins").str(ch).lower())
        var is_alnum = False
        if lower.byte_length() == 1:
            var b = lower.as_bytes()[0]
            if (b >= 97 and b <= 122) or (b >= 48 and b <= 57):
                is_alnum = True
        if is_alnum:
            out += lower
            last_was_dash = False
        elif not last_was_dash:
            out += "-"
            last_was_dash = True
    # Trim a trailing dash if present.
    if out.endswith("-"):
        var pieces = out.split("-")
        var rebuilt = String("")
        var n = 0
        for p in pieces:
            if n == len(pieces) - 1:
                break
            if n > 0:
                rebuilt += "-"
            rebuilt += String(p)
            n += 1
        return rebuilt
    return out


def zero_pad(n: Int) -> String:
    if n < 10:
        return "0" + String(n)
    return String(n)


def print_help() raises:
    print("Sponge OS patch ledger manager")
    print()
    print("Usage:")
    print("  mojo tool/patches.mojo <command> [args]")
    print()
    print("Commands:")
    print("  list           List ledger rows and their git resolution status")
    print("  verify         Check the ledger against git reality")
    print("  export <dir>   Write each patch as a .patch file into <dir>")
    print("  drop <n>       Print the manual revert instructions for patch #n")
    print("  help           Show this help")
    print()
    print("The ledger (docs/11-environment.md §4) is the source of truth.")
    print("This tool never reverts, commits, or edits the ledger; 'drop'")
    print("prints the manual steps only (see docs/11-environment.md §4.1).")


def cmd_list(rows: List[PatchRow], root: String) raises:
    print("[sponge-patches] patch ledger (docs/11-environment.md §4)")
    print()
    for row in rows:
        print("#" + String(row.number) + " " + row.sha_prefix + " "
              + row.subject)
    print()
    for row in rows:
        var res = git_capture(
            ["log", "-1", "--format=%H%n%s", row.sha_prefix], cwd=root
        )
        var rc = Int(py=res.returncode)
        if rc != 0:
            print("#" + String(row.number) + " " + row.sha_prefix
                  + ": MISSING (git cannot resolve this commit)")
            continue
        var full_out = String(res.stdout)
        var lines = full_out.split("\n")
        var full_sha = String(lines[0])
        var git_subject = String(lines[1]) if len(lines) > 1 else String("")
        var prefix_ok = full_sha.startswith(row.sha_prefix)
        var status = String("ok: exists")
        if not prefix_ok:
            status = "MISMATCH: full SHA does not start with ledger prefix"
        print("#" + String(row.number) + " " + row.sha_prefix + ": "
              + status + " (" + full_sha + ")")
        if git_subject != row.subject:
            print("    note: git subject differs from ledger subject:")
            print("      git:    " + git_subject)
            print("      ledger: " + row.subject)


def cmd_verify(rows: List[PatchRow], root: String) raises:
    print("[sponge-patches] verify: ledger vs git reality")
    print()
    var failures = 0
    for row in rows:
        # (a) Does the commit exist at all?
        var log_res = git_capture(
            ["log", "-1", "--format=%H", row.sha_prefix], cwd=root
        )
        if Int(py=log_res.returncode) != 0:
            print("FAIL #" + String(row.number) + " " + row.sha_prefix
                  + ": commit not found")
            print("  " + row.subject)
            failures += 1
            continue
        var full_sha = String(String(log_res.stdout).split("\n")[0])
        var prefix_ok = full_sha.startswith(row.sha_prefix)

        # (b) Is it an ancestor of HEAD?
        var anc_args: List[String] = ["merge-base", "--is-ancestor"]
        anc_args.append(full_sha)
        anc_args.append("HEAD")
        var anc_res = git_capture(anc_args, cwd=root)
        var is_ancestor = Int(py=anc_res.returncode) == 0

        # (c) Which paths does it touch? Reported, not policed: patch #5
        # legitimately adds repos/sponge/ files next to genode/ paths, so
        # there is no genode/-only rule to enforce here.
        var stat_args: List[String] = ["show", "--format=", "--name-only"]
        stat_args.append(full_sha)
        var stat_res = git_capture(stat_args, cwd=root)
        var paths: List[String] = []
        for p in String(stat_res.stdout).split("\n"):
            var path = String(
                Python.import_module("builtins").str(String(p)).strip()
            )
            if path.byte_length() > 0:
                paths.append(path)

        var ok = prefix_ok and is_ancestor
        if ok:
            print("OK   #" + String(row.number) + " " + row.sha_prefix
                  + ": exists, ancestor of HEAD")
        else:
            failures += 1
            var why = String("")
            if not prefix_ok:
                why = "full SHA " + full_sha + " does not match prefix"
            elif not is_ancestor:
                why = "not an ancestor of HEAD"
            print("FAIL #" + String(row.number) + " " + row.sha_prefix
                  + ": " + why)
        print("     touches:")
        for path in paths:
            print("       " + path)
    print()
    if failures > 0:
        print("verify: FAIL (" + String(failures) + " of "
              + String(len(rows)) + " rows failed)")
        exit(1)
    print("verify: OK (all " + String(len(rows))
          + " ledger rows match git reality)")


def cmd_export(rows: List[PatchRow], root: String, out_dir: String) raises:
    var os_py = Python.import_module("os")
    var os_path = Python.import_module("os.path")

    if not os_path.isdir(out_dir):
        os_py.makedirs(out_dir)
        print("[sponge-patches] created output directory " + out_dir)

    print("[sponge-patches] exporting " + String(len(rows))
          + " patch(es) to " + out_dir)
    print()
    var failures = 0
    for row in rows:
        var res = git_capture(
            ["format-patch", "-1", row.sha_prefix, "--stdout"], cwd=root
        )
        if Int(py=res.returncode) != 0:
            print("FAIL #" + String(row.number) + " " + row.sha_prefix
                  + ": git format-patch failed")
            failures += 1
            continue
        var filename = zero_pad(row.number) + "-" + slugify(row.subject) + ".patch"
        var full_path = out_dir + "/" + filename
        var builtins = Python.import_module("builtins")
        var f = builtins.open(full_path, "w")
        f.write(res.stdout)
        f.close()
        print("  wrote " + full_path)
    print()
    if failures > 0:
        print("export: FAIL (" + String(failures) + " patch(es) not written)")
        exit(1)
    print("export: OK")


def cmd_drop(rows: List[PatchRow], root: String, number: Int) raises:
    var found = False
    var sha = String("")
    var subject = String("")
    for row in rows:
        if row.number == number:
            found = True
            sha = row.sha_prefix
            subject = row.subject

    if not found:
        print("error: no ledger row #" + String(number))
        print("Run 'mojo tool/patches.mojo list' to see the current rows.")
        exit(1)

    # Resolve the full SHA so the printed command is unambiguous.
    var full_sha = sha
    var res = git_capture(["log", "-1", "--format=%H", sha], cwd=root)
    if Int(py=res.returncode) == 0:
        full_sha = String(String(res.stdout).split("\n")[0])
    else:
        print("warning: commit " + sha + " does not resolve in this clone;")
        print("the instructions below use the ledger prefix as-is.")
        print()

    print("[sponge-patches] drop patch #" + String(number) + ": " + subject)
    print()
    print("Dropping a patch is a deliberate act and is NOT automated")
    print("(AGENTS.md §5.2). Perform these steps by hand:")
    print()
    print("  1. Revert the patch commit:")
    print("       git revert " + full_sha)
    print()
    print("  2. Remove row #" + String(number)
          + " from the patch ledger table in")
    print("     docs/11-environment.md §4 and renumber if needed.")
    print()
    print("  3. Commit the revert together with the ledger edit as its own")
    print("     commit on main, with a message that records the drop, e.g.:")
    print("       revert(genode): drop patch #" + String(number)
          + " (absorbed upstream)")
    print()
    print("If upstream absorbed the fix via a subtree upgrade instead of a")
    print("plain revert, the alternative is re-running")
    print("'git subtree pull --prefix=genode' and dropping the row in that")
    print("same commit.")


def main() raises:
    var args = argv()

    if len(args) < 2:
        print_help()
        return

    var subcommand = String(args[1])

    if subcommand == "help" or subcommand == "--help" or subcommand == "-h":
        print_help()
        return

    var root = repo_root()
    var rows = parse_ledger(root)

    if subcommand == "list":
        cmd_list(rows, root)
        return

    if subcommand == "verify":
        cmd_verify(rows, root)
        return

    if subcommand == "export":
        if len(args) < 3:
            print("error: 'export' requires an output directory")
            print("Try: mojo tool/patches.mojo export <dir>")
            exit(1)
        cmd_export(rows, root, String(args[2]))
        return

    if subcommand == "drop":
        if len(args) < 3:
            print("error: 'drop' requires a ledger row number")
            print("Try: mojo tool/patches.mojo drop <n>")
            exit(1)
        var number = Int(py=Python.import_module("builtins").int(String(args[2])))
        cmd_drop(rows, root, number)
        return

    print("error: unknown command '" + subcommand + "'")
    print()
    print_help()
    exit(1)
