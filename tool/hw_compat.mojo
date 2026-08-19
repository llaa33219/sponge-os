# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Hardware compatibility validator for the Sponge OS repository.
#
# docs/15-hardware-compatibility.md is the hand-curated public hardware
# contract (Phase 12 plan, W5). It contains a primary 5-row x 5-column
# surface matrix and a separate 16-cell cross-product ledger. This
# tool reads the cross-product ledger and validates every cell against
# the rules in docs/15 §3 + the W5 plan step 7 (risk 13 + risk 22 +
# risk 23 + risk 24 mitigations).
#
# Subcommands:
#   assert   Parse docs/15, validate every cell, and exit non-zero on
#            any rule failure. Optional --doc <path> overrides the
#            default docs/15-hardware-compatibility.md (used by the
#            negative-fixture runs in W5 step 9 — fixtures live under
#            var/ or the tool's test scratch and are never durable).
#   help     Print usage.
#
# The tool is read-only against the repository. It never edits the
# ledger, never invents a cell, never promotes a gap to a verified
# claim, and never writes any file (AGENTS.md §5.1: "Convenience" must
# be proven in code; the W5 plan step 6 explicitly forbids a
# generate/update/write path). The `--doc` flag only changes what file
# is read, never what file is written.
#
# Usage:
#   mojo tool/hw_compat.mojo assert
#   mojo tool/hw_compat.mojo assert --doc var/scratch/hw_compat-fix-missing-scenario.md
#   mojo tool/hw_compat.mojo help

from std.sys import argv, exit
from std.python import Python, PythonObject


# Exit codes (plan step 7 / risk 22).
comptime EXIT_OK = 0
comptime EXIT_RULE_FAIL = 1
comptime EXIT_REAL_HARDWARE = 2


# Mandated exact error string for any `target: real-hardware` line
# (plan step 7 / risk 22).
comptime REAL_HARDWARE_MSG = (
    "real hardware is a Phase 15 deliverable; not a Phase 12 cell"
)

# Mandated exact USB-evidence literal (plan step 7 / risk 1 + risk 12).
comptime USB_EVIDENCE_LITERAL = "BIOS-side USB boot verified"

# Status vocabulary (plan step 7 / risk 24).
comptime STATUS_VERIFIED = "verified"
comptime STATUS_SMOKE = "smoke-only"
comptime STATUS_GAP = "gap"


def repo_root() raises -> String:
    """Locate the repository root as the parent of the tool/ directory that
    contains this script, so the tool works from any cwd."""
    var os_py = Python.import_module("os.path")
    var abspath = os_py.abspath(String(argv()[0]))
    var here = os_py.dirname(abspath)
    return String(os_py.dirname(here))


def read_text_file(path: String) raises -> String:
    var builtins = Python.import_module("builtins")
    var f = builtins.open(path, "r")
    var content = f.read()
    f.close()
    return String(content)


def strip(text: String) raises -> String:
    return String(
        Python.import_module("builtins").str(text).strip()
    )


def split_lines(text: String) raises -> List[String]:
    """Split on \n the way the ledger expects (no universal newline
    translation; we read in text mode and split manually)."""
    var builtins = Python.import_module("builtins")
    var py_lines = builtins.str(text).split("\n")
    var lines: List[String] = []
    for ln in py_lines:
        lines.append(String(ln))
    return lines^


def find_section(lines: List[String], heading: String) raises -> Int:
    """Return the index of the line that starts with the given heading
    (e.g. "## 2. 16-cell cross-product ledger"). Returns -1 if not
    found. The first match wins."""
    for i in range(len(lines)):
        var line = String(lines[i])
        if line.startswith(heading):
            return i
    return -1


def split_cells(row: String) raises -> List[String]:
    """Split a markdown table row on '|' and trim each cell. Leading
    and trailing pipes produce empty first/last cells which we drop."""
    var builtins = Python.import_module("builtins")
    var raw = builtins.str(row).split("|")
    var cells: List[String] = []
    for c in raw:
        cells.append(strip(String(c)))
    return cells^


def is_separator_row(row: String) raises -> Bool:
    """A markdown separator row is one whose cells (after stripping)
    are empty or contain only '-' / ':' characters."""
    var builtins = Python.import_module("builtins")
    for cell in split_cells(row):
        var s = String(cell)
        if s.byte_length() == 0:
            continue
        # Every char must be '-' or ':'.
        var ok = True
        for i in range(s.byte_length()):
            var ch = String(s[byte=i])
            var b = ch.as_bytes()[0]
            if b != 45 and b != 58:  # '-' or ':'
                ok = False
                break
        if not ok:
            return False
    return True


def parse_int(s: String) raises -> Int:
    """Parse an integer; raises if the string is not a valid integer
    (the validator surfaces this as a rule failure)."""
    return Int(py=Python.import_module("builtins").int(String(s)))


struct Cell(Copyable, Movable):
    """One row of the 16-cell cross-product ledger, after parsing."""

    var row_index: Int
    var raw: String
    var status: String
    var scenario: String
    var marker: String
    var evidence: String
    var qemu: String
    var boot_time: String
    var budget: String
    var target: String
    var reason: String
    var qemu_envelope: String

    def __init__(
        out self,
        row_index: Int,
        raw: String,
        status: String,
        scenario: String,
        marker: String,
        evidence: String,
        qemu: String,
        boot_time: String,
        budget: String,
        target: String,
        reason: String,
        qemu_envelope: String,
    ):
        self.row_index = row_index
        self.raw = raw
        self.status = status
        self.scenario = scenario
        self.marker = marker
        self.evidence = evidence
        self.qemu = qemu
        self.boot_time = boot_time
        self.budget = budget
        self.target = target
        self.reason = reason
        self.qemu_envelope = qemu_envelope


def normalize_cell(row_index: Int, raw: String, cells: List[String]) raises -> Cell:
    """Build a Cell from a parsed cross-product row. Empty cells map to
    the empty string (the validator decides what is allowed)."""
    # Column order in docs/15 §2 (after splitting on '|' the row
    # carries a leading and trailing empty cell):
    #   0: ""
    #   1: "#"  2: Machine  3: CPU  4: Storage  5: NIC  6: Input
    #   7: Status  8: Scenario  9: Marker  10: Evidence  11: QEMU
    #   12: boot_time_seconds  13: budget_seconds  14: Target
    #   15: Reason  16: qemu_envelope (Phase 15 D15.11 — present
    #       ONLY on real-hardware gap rows; absent on qemu rows)
    #   17: ""
    var cell = Cell(
        row_index=row_index,
        raw=raw,
        status=String(cells[7]),
        scenario=String(cells[8]),
        marker=String(cells[9]),
        evidence=String(cells[10]),
        qemu=String(cells[11]),
        boot_time=String(cells[12]),
        budget=String(cells[13]),
        target=String(cells[14]),
        reason=String(cells[15]) if len(cells) > 15 else String(""),
        qemu_envelope=String(cells[16]) if len(cells) > 16 else String(""),
    )
    return cell^


def parse_cross_product(doc: String) raises -> List[Cell]:
    """Parse the 16-cell cross-product ledger out of docs/15.

    The ledger lives in the section whose heading is
    "## 2. 16-cell cross-product ledger". The first table under that
    heading is the ledger; the second table (the sub-pointer map) is
    ignored — it is a textual description, not a row list.
    """
    var lines = split_lines(doc)
    # Phase 15: the heading is now "## 2. 17-cell cross-product ledger";
    # Phase 12 baseline was "## 2. 16-cell cross-product ledger". Try
    # the Phase 15 spelling first, then fall back to the Phase 12
    # spelling.
    var start = find_section(lines, "## 2. 17-cell cross-product ledger")
    if start < 0:
        start = find_section(lines, "## 2. 16-cell cross-product ledger")
    if start < 0:
        print("error: could not find cross-product section heading in docs/15")
        print("expected line starting with: ## 2. 17-cell cross-product ledger")
        print("                  or: ## 2. 16-cell cross-product ledger")
        exit(EXIT_RULE_FAIL)

    # The header row of the cross-product table is the first table row
    # after the section heading. The next row is the markdown
    # separator. The rows after that, until a non-table line, are the
    # 16 cells.
    var header_idx = -1
    for i in range(start + 1, len(lines)):
        var line = String(lines[i])
        if line.startswith("|"):
            header_idx = i
            break
    if header_idx < 0:
        print("error: cross-product section has no markdown table")
        exit(EXIT_RULE_FAIL)

    var cells: List[Cell] = []
    # Skip the header + separator.
    var row_index = 0
    for i in range(header_idx + 2, len(lines)):
        var line = String(lines[i])
        var stripped = strip(line)
        if not stripped.startswith("|"):
            break
        var row_cells = split_cells(line)
        # Phase 12 schema: 17 total cells (15 data + leading/trailing).
        # Phase 15 D15.11 adds an optional 16th data column
        # (`qemu_envelope`) for real-hardware rows. We accept both.
        if len(row_cells) < 17:
            print(
                "error: cross-product row " + String(row_index + 1)
                + " has fewer than 15 data columns (got "
                + String(len(row_cells)) + " total, including the"
                + " leading and trailing empty cells)"
            )
            print("row: " + stripped)
            exit(EXIT_RULE_FAIL)
        cells.append(normalize_cell(row_index + 1, stripped, row_cells))
        row_index += 1

    return cells^


def evidence_contains_literal(evidence: String) raises -> Bool:
    """True when the evidence text contains the literal marker or the
    USB literal. Used by every non-gap cell that has an evidence
    pointer."""
    return evidence_contains_substring(evidence, marker="", required=False)


def evidence_contains_marker(evidence: String, marker: String) raises -> Bool:
    """True when the evidence file (root + evidence path) exists and
    contains the marker substring as a byte-for-byte match."""
    if marker.byte_length() == 0:
        return True
    return evidence_contains_substring(evidence, marker, required=True)


def evidence_contains_substring(
    evidence_rel: String, marker: String, required: Bool
) raises -> Bool:
    """Read the evidence file (root + rel path) and check for the
    marker substring. If the file cannot be opened, return False (the
    caller classifies this as a missing-evidence failure)."""
    var root = repo_root()
    var full = root + "/" + evidence_rel
    var os_path = Python.import_module("os.path")
    if not os_path.isfile(full):
        if required:
            return False
        return False
    var content = read_text_file(full)
    if marker.byte_length() == 0:
        return True
    return content.find(marker) >= 0


def scenario_exists(scenario_rel: String) raises -> Bool:
    """True when the scenario file (root + rel path) exists. The
    validator does NOT load the scenario — it only confirms the path
    resolves. The scenario is loaded at run-time by the Genode build
    tool, not by this validator."""
    var root = repo_root()
    var full = root + "/" + scenario_rel
    var os_path = Python.import_module("os.path")
    return Bool(py=os_path.isfile(full))


def is_usb_evidence(evidence_rel: String) raises -> Bool:
    """True when the evidence path is the USB-boot evidence, i.e. its
    basename ends with 'usb-boot.log' or contains 'usb-boot'. This is
    the heuristic for the plan step 7 'require the USB evidence to
    contain `BIOS-side USB boot verified`' rule."""
    return (
        evidence_rel.find("usb-boot") >= 0
        or evidence_rel.find("usb_boot") >= 0
    )


def cmd_assert(doc_path: String) raises:
    print("[sponge-hw-compat] assert: validating " + doc_path)
    print()

    var doc = read_text_file(doc_path)

    # Structural check 1: the section heading must exist.
    # Phase 12: "## 2. 16-cell cross-product ledger".
    # Phase 15 D15.11: the heading changed to "## 2. 17-cell cross-product
    # ledger" (one row added — the single real-hardware row). Accept
    # either spelling.
    if doc.find("## 2. 17-cell cross-product ledger") < 0
       and doc.find("## 2. 16-cell cross-product ledger") < 0:
        print("FAIL: cross-product section heading missing")
        print("expected line starting with: ## 2. 17-cell cross-product ledger")
        print("                  or: ## 2. 16-cell cross-product ledger")
        exit(EXIT_RULE_FAIL)

    # Structural check 2: the mandated summary row text must be
    # present (risk 24 mitigation + Phase 15 D15.11).
    # Phase 12: "Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells."
    # Phase 15: the headline was extended with the D15.11 + cell counts.
    # Accept either phrasing (the Phase 15 headline now reads
    # "Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells." for
    # backward compatibility + "Combined cross-product (validator-
    # enforced): 4 verified, 1 smoke-only, 12 gap = 17 cells." for the
    # D15.11 amendment).
    var found_summary = doc.find(
        "Phase 12 status: 4 verified, 1 smoke-only, 11 gap cells."
    ) >= 0
    var found_combined = doc.find(
        "Combined cross-product"
    ) >= 0 and doc.find(
        "1 smoke-only, 12 gap"
    ) >= 0
    if not found_summary and not found_combined:
        print("FAIL: mandated summary row missing")
        print(
            "expected exact text: 'Phase 12 status: 4 verified, 1"
            " smoke-only, 11 gap cells.'"
        )
        print(
            "                or: 'Combined cross-product (...) 1 smoke-only,"
            " 12 gap = 17 cells.'"
        )
        exit(EXIT_RULE_FAIL)

    var cells = parse_cross_product(doc)
    print("  parsed " + String(len(cells)) + " cross-product cell(s)")
    print()

    if len(cells) != 17:
        print(
            "FAIL: cross-product must contain exactly 17 cells (Phase 12"
            + " 16 + Phase 15 D15.11 1 real-hardware row; got "
            + String(len(cells)) + ")"
        )
        exit(EXIT_RULE_FAIL)

    var verified = 0
    var smoke = 0
    var gap = 0
    var failures = 0

    for cell in cells:
        var row = String(cell.row_index)
        var status = String(cell.status)

        # Plan D15.11 (Phase 15, 2026-08-17): a `target: real-hardware`
        # cell is admitted ONLY if it carries BOTH:
        #
        #   (a) a `qemu-envelope:` field naming a real-existing
        #       scenario file under run/, AND
        #   (b) `status: gap` (any stronger status — `verified` or
        #       `smoke-only` — is still rejected with exit 2 until
        #       15-3 physical-boot evidence lands; the single 17ZD90N
        #       row flips to verified only then).
        #
        # The Phase 12 reject rule (target: real-hardware without
        # an envelope) is preserved: any real-hardware cell without
        # a qemu-envelope is rejected with exit 2.
        #
        # This branch is checked unconditionally, before per-status
        # branching, so a real-hardware cell never reaches the
        # gap/verified branch until both D15.11 conditions hold.
        if cell.target == "real-hardware":
            # D15.11 (a): qemu-envelope must be present AND the file
            # must resolve to an existing scenario.
            if cell.qemu_envelope.byte_length() == 0:
                print(
                    "FAIL cell #" + row
                    + ": target: real-hardware requires a qemu-envelope:"
                    + " field (D15.11 (a) — policy lifted in Phase 15"
                    + " only for cells with a QEMU envelope link)"
                )
                print("  " + REAL_HARDWARE_MSG)
                exit(EXIT_REAL_HARDWARE)
            if not scenario_exists(cell.qemu_envelope):
                print(
                    "FAIL cell #" + row
                    + ": target: real-hardware qemu-envelope scenario does"
                    + " not exist: " + cell.qemu_envelope
                    + " (D15.11 (a))"
                )
                print("  " + REAL_HARDWARE_MSG)
                exit(EXIT_REAL_HARDWARE)
            # D15.11 (b): status must be `gap`; anything stronger
            # (verified, smoke-only) is rejected until 15-3 evidence.
            if status != STATUS_GAP:
                print(
                    "FAIL cell #" + row
                    + ": target: real-hardware with status '" + status
                    + "' is rejected (D15.11 (b) — stronger status"
                    + " requires 15-3 physical-boot evidence; the"
                    + " single 17ZD90N row may flip to verified only"
                    + " after the user-executed physical boot)"
                )
                print("  " + REAL_HARDWARE_MSG)
                exit(EXIT_REAL_HARDWARE)
            # All D15.11 conditions hold; fall through to per-status
            # branching with status=gap (the gap-cell rule).
            gap += 1
            failures += check_gap_cell(cell)
            continue

        if status != STATUS_VERIFIED and status != STATUS_SMOKE and status != STATUS_GAP:
            print(
                "FAIL cell #" + row
                + ": unrecognized status '"
                + status
                + "' (expected verified, smoke-only, or gap)"
            )
            failures += 1
            continue

        if status == STATUS_VERIFIED:
            verified += 1
            failures += check_non_gap_cell(cell)
        elif status == STATUS_SMOKE:
            smoke += 1
            failures += check_non_gap_cell(cell)
        else:
            gap += 1
            failures += check_gap_cell(cell)

    print()
    print("  cross-product counts: " + String(verified) + " verified, "
          + String(smoke) + " smoke-only, " + String(gap) + " gap")

    # Phase 12 plan step 7: exactly 4 verified, 1 smoke-only, 11 gap.
    # Phase 15 D15.11 / R15.7: +1 gap (the single real-hardware row)
    # is admitted in Phase 15 (cell #17, target: real-hardware,
    # qemu-envelope required, status: gap); no other Phase-15 changes
    # to the cross-product counts. Total: 4 verified, 1 smoke-only,
    # 12 gap = 17 cells.
    if verified != 4:
        print(
            "FAIL: expected exactly 4 verified cells (got "
            + String(verified) + ")"
        )
        failures += 1
    if smoke != 1:
        print(
            "FAIL: expected exactly 1 smoke-only cell (got "
            + String(smoke) + ")"
        )
        failures += 1
    if gap != 12:
        print(
            "FAIL: expected exactly 12 gap cells (Phase 12 11 gap +"
            + " Phase 15 D15.11 1 real-hardware gap; got "
            + String(gap) + ")"
        )
        failures += 1

    # Plan step 7: at least one verified cell (the verified count is
    # already checked above as 'exactly 4', so this is a redundant
    # belt-and-braces check).
    if verified < 1:
        print("FAIL: verified count is zero")
        failures += 1

    print()
    if failures > 0:
        print(
            "assert: FAIL (" + String(failures) + " rule violation(s))"
        )
        exit(EXIT_RULE_FAIL)

    print("assert: OK (4 verified, 1 smoke-only, 12 gap — all rules pass)")
    exit(EXIT_OK)


def check_non_gap_cell(cell: Cell) raises -> Int:
    """Return the number of rule violations for a verified or
    smoke-only cell. Each violation prints one line."""
    var row = String(cell.row_index)
    var status = String(cell.status)
    var failures = 0

    # Required fields: scenario, marker, evidence, qemu, boot_time,
    # budget, target. Reason is allowed-empty for non-gap cells.
    if cell.scenario.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `scenario` field"
        )
        failures += 1
    elif not scenario_exists(cell.scenario):
        print(
            "FAIL cell #" + row + " (" + status + "): scenario file"
            + " does not exist: " + cell.scenario
        )
        failures += 1

    if cell.evidence.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `evidence` field"
        )
        failures += 1
    else:
        if not evidence_contains_marker(cell.evidence, cell.marker):
            print(
                "FAIL cell #" + row + " (" + status
                + "): evidence does not contain marker"
            )
            print("  evidence: " + cell.evidence)
            print("  marker:   " + cell.marker)
            failures += 1
        if is_usb_evidence(cell.evidence):
            if not evidence_contains_marker(cell.evidence, USB_EVIDENCE_LITERAL):
                print(
                    "FAIL cell #" + row + " (" + status
                    + "): USB evidence missing literal '"
                    + USB_EVIDENCE_LITERAL + "'"
                )
                print("  evidence: " + cell.evidence)
                failures += 1

    if cell.marker.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `marker` field"
        )
        failures += 1

    if cell.qemu.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `qemu` field"
        )
        failures += 1

    if cell.boot_time.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `boot_time_seconds` field"
        )
        failures += 1
    if cell.budget.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `budget_seconds` field"
        )
        failures += 1

    # Plan step 7: fail if measured time exceeds budget.
    if cell.boot_time.byte_length() > 0 and cell.budget.byte_length() > 0:
        try:
            var bt = parse_int(cell.boot_time)
            var bg = parse_int(cell.budget)
            if bt > bg:
                print(
                    "FAIL cell #" + row + " (" + status
                    + "): boot_time_seconds (" + String(bt)
                    + ") exceeds budget_seconds (" + String(bg) + ")"
                )
                failures += 1
        except:
            print(
                "FAIL cell #" + row + " (" + status
                + "): boot_time_seconds or budget_seconds is not an integer"
            )
            failures += 1

    if cell.target.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (" + status
            + "): missing `target` field"
        )
        failures += 1
    elif cell.target != "qemu":
        print(
            "FAIL cell #" + row + " (" + status
            + "): non-gap cell must have target: qemu (got '"
            + cell.target + "')"
        )
        failures += 1

    return failures


def check_gap_cell(cell: Cell) raises -> Int:
    """Return the number of rule violations for a gap cell. Plan step 7
    / risk 23: a gap cell must have a non-empty reason and a non-empty
    target phase; it must NOT fabricate a scenario/PASS marker."""
    var row = String(cell.row_index)
    var failures = 0

    if cell.reason.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (gap): missing `reason` field"
        )
        failures += 1

    if cell.target.byte_length() == 0:
        print(
            "FAIL cell #" + row + " (gap): missing `target` field"
        )
        failures += 1

    # Plan step 7: a gap cell must not fabricate a scenario or PASS
    # marker. The non-empty rule (against silent gap→verified
    # promotion) is enforced by forbidding scenario/marker/evidence
    # on a gap cell.
    if cell.scenario.byte_length() > 0:
        print(
            "FAIL cell #" + row
            + " (gap): gap cells must not carry a `scenario` field"
        )
        failures += 1
    if cell.marker.byte_length() > 0:
        print(
            "FAIL cell #" + row
            + " (gap): gap cells must not carry a `marker` field"
        )
        failures += 1
    if cell.evidence.byte_length() > 0:
        print(
            "FAIL cell #" + row
            + " (gap): gap cells must not carry an `evidence` field"
        )
        failures += 1

    return failures


def print_help() raises:
    print("Sponge OS hardware compatibility validator")
    print()
    print("Usage:")
    print("  mojo tool/hw_compat.mojo <command> [args]")
    print()
    print("Commands:")
    print("  assert [--doc <path>]  Validate the cross-product ledger in")
    print("                         docs/15-hardware-compatibility.md.")
    print("                         The --doc flag overrides the path")
    print("                         (used by the W5 step-9 negative")
    print("                         fixtures). The validator never")
    print("                         writes any file; --doc only changes")
    print("                         what is read.")
    print("  help                   Show this help")
    print()
    print("Exit codes:")
    print("  0  all rules pass (4 verified, 1 smoke-only, 11 gap)")
    print("  1  one or more rule violations")
    print("  2  a 'target: real-hardware' cell was found")
    print()
    print("This tool is read-only against the repository. There is no")
    print("generate, update, or auto-population subcommand (plan")
    print("step 6 / risk 23). The matrix in docs/15 is hand-curated.")


def main() raises:
    var args = argv()

    if len(args) < 2:
        print_help()
        return

    var subcommand = String(args[1])

    if subcommand == "help" or subcommand == "--help" or subcommand == "-h":
        print_help()
        return

    if subcommand == "assert":
        var doc_path = String("docs/15-hardware-compatibility.md")
        if len(args) >= 4 and String(args[2]) == "--doc":
            doc_path = String(args[3])
        cmd_assert(doc_path)
        return

    print("error: unknown command '" + subcommand + "'")
    print()
    print_help()
    exit(EXIT_RULE_FAIL)