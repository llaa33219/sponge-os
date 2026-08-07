# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Sponge OS themed_decorator theme tar generator (Phase 11 W4).
#
# Generates the `decor.tar` ROM module that the Sponge-themed
# window-chrome scenario (run/sponge-de-themed-chrome.run) VFS-mounts
# under themed_decorator's VFS. The packaged tar carries five entries:
#
#   theme/default.png    — stretchable 9-slice frame texture
#   theme/closer.png     — title-bar close button glyph
#   theme/maximizer.png  — title-bar maximizer button glyph
#   theme/font.tff       — TFF-format trimmed-TTF (read literally by
#                          upstream's Tff_font parser)
#   theme/metadata       — geometry declaration (aura/decor margins,
#                          title/closer/maximizer rects)
#
# === Why this tool exists (and why it is NOT a ttf2tff conversion) ===
#
# The vendored Genode tree at genode/repos/gems/src/app/themed_decorator/
# ships a sample tar (plain_decorator_theme.tar) that contains all
# five entries. The shipped `theme/font.tff` is a TFF-format trimmed
# TTF; the decorator's Tff_font parser reads the bytes verbatim — there
# is NO host-side TTF-to-TFF conversion step in the vendored tree (or
# upstream Genode).
#
# Per the Phase 11 W4 risk-register row 16 enforcement, this tool
# re-vendors the upstream `theme/font.tff` BYTE-FOR-BYTE
# (genode/repos/gems/src/app/themed_decorator/theme/font.tff). The
# vendored tree's font is the source of truth. A stale-font
# situation is handled by the tool falling back to a 1-byte empty
# placeholder with a warning (the decorator still boots; the title
# bar reverts to the Genode default font). The byte-for-byte copy is
# verified by sha256 against the upstream asset.
#
# === What the tool does to PNGs ===
#
# The metadata.txt file declares the title-bar height and the
# closer/maximizer glyph rects. The default.png is a 9-slice texture
# that the decorator paints at the window's top/bottom/left/right
# borders. The closer/maximizer PNGs are 18x18 grayscale glyphs.
#
# The PNGs are GENERATED rather than static bytes when the tool is
# run with the `--regen-pngs` flag: this writes the PNGs to the
# staging dir using ImageMagick (mogrify/convert). Without the
# flag, the tool uses the checked-in PNGs under
# tool/decor_assets_data/pngs/ (the upstream asset set, the source
# of truth). Either way, the PNGs are not regenerated unless
# `--regen-pngs` is set; the default is to use the checked-in
# assets (the upstream tree's bytes), which is the deterministic
# "same tar every time" path that the Phase-10 drag regression gate
# relies on.
#
# === Manual escape hatch (AGENTS.md §3.5 control door) ===
#
# The tool is a generator. The metadata file is the source of truth
# for the chrome's geometry. To redesign the chrome:
#
#   1. Edit tool/decor_assets_data/metadata.txt
#   2. (optionally) Edit tool/decor_assets_data/pngs/*.png
#   3. Run ./tool/decor_assets
#   4. The new decor.tar is staged by run/sponge-de-themed-chrome.run
#      on the next scenario build (the script copies the tar into
#      bin/ before build_boot_image so base-sel4 packs it into
#      image.elf; docs/11 §10.4).
#
# To customize the font, edit the upstream
# genode/repos/gems/src/app/themed_decorator/theme/font.tff (it's a
# raw byte set, not theme-derived) and re-run. The asset is
# re-vendored — never regenerated.
#
# === Usage ===
#
#   mojo tool/decor_assets.mojo [<output-tar>]
#   ./tool/decor_assets [<output-tar>]
#   ./tool/decor_assets --regen-pngs [<output-tar>]
#   ./tool/decor_assets help
#
# Default output: <repo>/var/sponge/decor.tar (the staging dir the
# run script picks up via VFS).
#
# Exit codes: 0 success, 1 missing input / host tool / verification
#   failure, 2 usage error.

from std.python import Python, PythonObject
from std.sys import argv, exit

comptime DEFAULT_OUTPUT = "var/sponge/decor.tar"
comptime REPO_ROOT_FROM_TOOL = ".."

# Vendored Genode path relative to the repo root. The vendored
# themed_decorator's font.tff is the source of truth (the upstream
# sample tar carries the same bytes).
comptime UPSTREAM_FONT_TFF = "genode/repos/gems/src/app/themed_decorator/theme/font.tff"

# Where the checked-in PNG and font assets live (sibling of tool/).
# The tool copies these bytes into the tar by default.
comptime METADATA_TXT = "tool/decor_assets_data/metadata.txt"
comptime LOCAL_FONT_TFF = "tool/decor_assets_data/font.tff"
comptime LOCAL_PNG_DIR = "tool/decor_assets_data/pngs"


def which(command: String) raises -> Bool:
    """True iff `command` is on PATH (the same helper as tool/mkdata.mojo)."""
    var shutil = Python.import_module("shutil")
    var builtins = Python.import_module("builtins")
    var found = shutil.which(command)
    return Bool(py=builtins.bool(found))


def run_cmd_capture(cmd: List[String]) raises -> Tuple[Int, String]:
    """Run a command, capture stdout+stderr as a single string. Returns
    (rc, output). Used for hash verification and PNG-generation steps."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    var p = subprocess.Popen(
        py_args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    var comm = p.communicate()
    var out_obj = comm[0]
    var out_str = String("")
    if Bool(py=builtins.bool(out_obj)):
        out_str = String(out_obj.decode("utf-8", "replace"))
    return (Int(py=p.returncode), out_str^)


def run_cmd_stream(cmd: List[String]) raises -> Int:
    """Run a command streaming stdout/stderr to the terminal, return
    the exit code. Used for the destructive tar step so the operator
    sees live progress."""
    var subprocess = Python.import_module("subprocess")
    var builtins = Python.import_module("builtins")
    var py_args = builtins.list()
    for part in cmd:
        py_args.append(part)
    return Int(py=subprocess.call(py_args))


def sha256_of(path: String) raises -> String:
    """Compute SHA-256 of a file (hex lowercase). Returns '' on
    filesystem error so the caller can decide how to react."""
    var os_path = Python.import_module("os.path")
    var builtins = Python.import_module("builtins")
    var hashlib = Python.import_module("hashlib")
    if not Bool(py=os_path.isfile(path)):
        return String("")
    var h = hashlib.sha256()
    var open_fn = builtins.open
    var mode = "rb"
    var f = open_fn(path, mode)
    # Read in 64 KiB chunks (matches tools like sha256sum).
    var chunk = f.read(65536)
    while Bool(py=builtins.bool(chunk)):
        h.update(chunk)
        chunk = f.read(65536)
    f.close()
    return String(h.hexdigest())


def file_exists(path: String) raises -> Bool:
    var os_path = Python.import_module("os.path")
    return Bool(py=os_path.isfile(path))


def dir_exists(path: String) raises -> Bool:
    var os_path = Python.import_module("os.path")
    return Bool(py=os_path.isdir(path))


def make_parent_dir(path: String) raises:
    """Create the parent directory of `path` if absent (makedirs with
    exist_ok). Uses Python os.makedirs because the Mojo stdlib's
    std.os.mkdir has no recursive= keyword yet (verified
    2026-08-07)."""
    var os_path = Python.import_module("os.path")
    var os_module = Python.import_module("os")
    var dirname = String(os_path.dirname(path))
    if dirname.byte_length() > 0 and not Bool(py=os_path.isdir(dirname)):
        os_module.makedirs(dirname, exist_ok=True)


def cmd_help() raises:
    print("Sponge OS themed_decorator theme tar generator (Phase 11 W4)")
    print()
    print("Generates the `decor.tar` ROM module that")
    print("themed_decorator VFS-mounts. Five entries: theme/default.png,")
    print("theme/closer.png, theme/maximizer.png, theme/font.tff,")
    print("theme/metadata.")
    print()
    print("Usage:")
    print("  mojo tool/decor_assets.mojo [<output-tar>]")
    print("  ./tool/decor_assets            (default:")
    print("                                " + DEFAULT_OUTPUT + ")")
    print("  ./tool/decor_assets --regen-pngs")
    print("  ./tool/decor_assets help")
    print()
    print("Honors AGENTS.md §3.5: the tool only mutates the repository")
    print("(genode/ and var/), never anything outside the repo. The")
    print("manual escape hatch is documented in")
    print("docs/08-development.md §") ; print("'themed_decorator assets'.") ; print()
    print("Default PNG path: use the checked-in upstream asset set under")
    print("tool/decor_assets_data/pngs/ (deterministic, byte-stable).")
    print()
    print("With --regen-pngs, the PNGs are regenerated using ImageMagick")
    print("at the metadata-declared sizes (the texture stays the 9-slice")
    print("format; the closer/maximizer PNGs are 18x18 grayscale).")
    print()
    print("The font is ALWAYS re-vendored from the upstream")
    print("genode/repos/gems/src/app/themed_decorator/theme/font.tff")
    print("byte-for-byte (no host-side TFF conversion — risk-register row")
    print("16 enforcement). The fallback for a missing upstream path is a")
    print("1-byte empty placeholder with a warning (the decorator still")
    print("boots; the title bar reverts to the Genode default font).")


def check_host_tools(regen_pngs: Bool) raises -> Bool:
    """Print a one-line status for each host tool the tool needs. When
    `regen_pngs` is True, mogrify/convert is required; otherwise tar is
    the only third-party dependency."""
    var subst = Python.import_module("os.path")
    var tar_present = which("tar")
    var convert_present = which("convert")
    var mogrify_present = which("mogrify")

    print("[sponge-decor-assets] host tool check")
    print("  tar      " + String("ok" if tar_present else "MISSING"))
    if regen_pngs:
        print("  convert  " + String("ok" if convert_present else "MISSING"))
        print("  mogrify  " + String("ok" if mogrify_present else "MISSING"))
        if not (tar_present and convert_present and mogrify_present):
            print()
            print("[sponge-decor-assets] --regen-pngs requires")
            print("ImageMagick (apt: imagemagick) and tar.")
        return tar_present and convert_present and mogrify_present
    if not tar_present:
        print()
        print("[sponge-decor-assets] tar is required (apt: tar).")
    return tar_present


def makedirs_p(path: String) raises:
    """Wrapper around os.makedirs(path, exist_ok=True). Mojo's std.os.mkdir
    has no recursive= keyword yet (verified 2026-08-07 at beta-2)."""
    var os_module = Python.import_module("os")
    os_module.makedirs(path, exist_ok=True)


def stage_metadata(staging: String) raises -> String:
    """Copy tool/decor_assets_data/metadata.txt to staging/theme/metadata.
    Returns the destination path. The upstream-themed_decorator parses
    the metadata as a theme node (theme.cc:99-104) via:
        Genode::Node(Genode::Const_byte_range_ptr(file.data<char>(), file.size()))
    Genode::Node has NO `#` comment syntax — it parses a strict XML
    subset. If the file starts with `# ...`, with_optional_sub_node("aura")
    returns nothing and Margins_from_metadata (theme.cc:119-146) yields
    0/0/0/0 margins, which collapses the title rect and breaks the
    drag gate.

    Therefore: FAIL LOUD if the first non-blank byte is not `<`.
    The companion tool/decor_assets_data/README.md is where the
    explanatory text lives (this metadata.txt is a pure XML doc)."""
    var builtins = Python.import_module("builtins")
    var pyopen = builtins.open
    var metadata_src = String(pyopen(METADATA_TXT, "r").read())
    # First non-blank byte must be `<` (start of an XML element).
    var first_is_lt = False
    var md_bytes = metadata_src.as_bytes()
    var space_byte = UInt8(0x20)
    var tab_byte = UInt8(0x09)
    var lf_byte = UInt8(0x0a)
    var cr_byte = UInt8(0x0d)
    var lt_byte = UInt8(0x3c)
    for i in range(len(md_bytes)):
        var c = md_bytes[i]
        if c == space_byte or c == tab_byte or c == lf_byte or c == cr_byte:
            continue
        if c == lt_byte:
            first_is_lt = True
        break
    if not first_is_lt:
        print("[sponge-decor-assets] ERROR: metadata.txt does NOT start with `<`.")
        var preview = metadata_src[byte=0:64]
        print("  First 64 bytes: " + preview)
        print("  Genode::Node cannot parse `# ...` comments; the decorator")
        print("  would boot with margins=0 and the drag gate would fail.")
        print("  Move any prose into tool/decor_assets_data/README.md and")
        print("  ensure metadata.txt starts with `<theme>`.")
        exit(1)

    var shutil = Python.import_module("shutil")
    var theme_dir = staging + "/theme"
    makedirs_p(theme_dir)
    var dst = theme_dir + "/metadata"
    shutil.copyfile(METADATA_TXT, dst)
    return dst


def stage_fonts(staging: String) raises -> Tuple[String, String]:
    """Re-vendor the upstream `theme/font.tff` byte-for-byte into the
    staging tar root. The vendored path is the source of truth. If
    the vendored tree is missing (e.g. fresh checkout without a
    build), fall back to the checked-in copy under
    tool/decor_assets_data/font.tff, then to a 1-byte empty
    placeholder with a warning.

    Returns (used_path, status) where status is one of:
      "upstream"  — vendored tree's bytes were copied
      "cached"    — tool/decor_assets_data/font.tff was used (upstream
                    missing)
      "stub"      — a 1-byte empty placeholder was written (both
                    upstream and cache missing)"""
    var builtins = Python.import_module("builtins")
    var os_path = Python.import_module("os.path")
    var theme_dir = staging + "/theme"
    makedirs_p(theme_dir)
    var dst = theme_dir + "/font.tff"

    if file_exists(UPSTREAM_FONT_TFF):
        var shutil = Python.import_module("shutil")
        shutil.copyfile(UPSTREAM_FONT_TFF, dst)
        var upstream_sum = sha256_of(UPSTREAM_FONT_TFF)
        var staged_sum = sha256_of(dst)
        if upstream_sum != staged_sum:
            print("[sponge-decor-assets] ERROR: staged font.tff SHA-256 "
                  + "(" + staged_sum + ") does not match upstream ("
                  + upstream_sum + ")")
            return (dst, "hash-mismatch")
        return (dst, "upstream")

    if file_exists(LOCAL_FONT_TFF):
        var shutil = Python.import_module("shutil")
        shutil.copyfile(LOCAL_FONT_TFF, dst)
        print("[sponge-decor-assets] warning: vendored path "
              + UPSTREAM_FONT_TFF + " missing; using cached "
              + LOCAL_FONT_TFF)
        return (dst, "cached")

    # Worst case: write a 1-byte empty placeholder. The decorator still
    # boots (Tff_font::Allocated_glyph_buffer tolerates a 0-byte file),
    # but the title bar reverts to the default font.
    var f = builtins.open(dst, "wb")
    f.close()
    print("[sponge-decor-assets] warning: falling back to empty font.tff "
          + "(vendored path missing); title bar reverts to default font")
    return (dst, "stub")


def stage_pngs(staging: String, regen: Bool) raises -> Tuple[Bool, String]:
    """Stage the three PNGs (default.png, closer.png, maximizer.png)
    into staging/theme/. Default behavior: copy the checked-in bytes
    under tool/decor_assets_data/pngs/ (the upstream-themed_decorator
    asset set, byte-stable). With --regen-pngs, regenerate the PNGs
    using ImageMagick at the metadata-declared sizes.

    Returns (ok, status) where status is:
      "checked-in"  — copied the upstream PNGs byte-for-byte
      "regenerated" — regenerated via ImageMagick
    """
    var shutil = Python.import_module("shutil")
    var builtins = Python.import_module("builtins")
    var theme_dir = staging + "/theme"
    makedirs_p(theme_dir)

    if not regen:
        for name in List[String](["default.png", "closer.png", "maximizer.png"]):
            var src = LOCAL_PNG_DIR + "/" + name
            if not file_exists(src):
                print("[sponge-decor-assets] ERROR: checked-in PNG missing: "
                      + src)
                return (False, "checked-in")
            shutil.copyfile(src, theme_dir + "/" + name)
        return (True, "checked-in")

    # --regen-pngs: regenerate the PNGs via ImageMagick. The shape
    # follows the upstream asset set: default.png is a 64x64 9-slice
    # texture (the Icon_painter stretches it at the borders), closer
    # and maximizer are 18x18 grayscale glyphs. The actual bytes
    # differ from the upstream set under --regen-pngs (the upstream
    # was authored by hand); the visual is the same shape.
    print("[sponge-decor-assets] --regen-pngs: regenerating PNGs via "
          + "ImageMagick")
    # default.png: 64x64 9-slice. Use a 4x4 grid of 16x16 patches
    # with a center color band so the slice has discernible stretches.
    var rc1 = run_cmd_stream(
        List[String]([
            "convert", "-size", "64x64",
            "xc:#3b3b4f",
            "-fill", "#45475a", "-draw",
            "rectangle 0,0 16,16 rectangle 48,0 64,16 "
            "rectangle 0,48 16,64 rectangle 48,48 64,64",
            "-fill", "#cdd6f4", "-draw",
            "rectangle 24,28 40,36",
            theme_dir + "/default.png",
        ]))
    if rc1 != 0:
        print("[sponge-decor-assets] ERROR: convert default.png failed (rc="
              + String(rc1) + ")")
        return (False, "regenerated")
    # closer.png: 18x18 grayscale (X glyph)
    var rc2 = run_cmd_stream(
        List[String]([
            "convert", "-size", "18x18",
            "xc:#ffffff",
            "-fill", "#000000", "-draw",
            "line 3,3 14,14 line 3,14 14,3 stroke-width 2",
            theme_dir + "/closer.png",
        ]))
    if rc2 != 0:
        print("[sponge-decor-assets] ERROR: convert closer.png failed (rc="
              + String(rc2) + ")")
        return (False, "regenerated")
    # maximizer.png: 18x18 grayscale (square outline)
    var rc3 = run_cmd_stream(
        List[String]([
            "convert", "-size", "18x18",
            "xc:#ffffff",
            "-fill", "#000000", "-draw",
            "rectangle 3,3 14,14 stroke-width 2",
            theme_dir + "/maximizer.png",
        ]))
    if rc3 != 0:
        print("[sponge-decor-assets] ERROR: convert maximizer.png failed (rc="
              + String(rc3) + ")")
        return (False, "regenerated")
    return (True, "regenerated")


def assemble_tar(staging: String, output: String) raises -> Int:
    """Run `tar -cf <output> -C <staging> theme`. The Genode VFS <tar>
    loader handles a plain uncompressed tar."""
    var os_path = Python.import_module("os.path")
    if not dir_exists(staging):
        print("[sponge-decor-assets] ERROR: staging dir missing: " + staging)
        return 1
    if not dir_exists(staging + "/theme"):
        print("[sponge-decor-assets] ERROR: staging/theme missing")
        return 1
    # List the staged entries so the operator sees what was packaged.
    # Use find on the staging dir to enumerate the files we actually
    # packaged (an ls of the tar would only work AFTER the tar is built).
    print("[sponge-decor-assets] staging tar entries:")
    var (_, find_output) = run_cmd_capture(
        List[String](["find", staging, "-mindepth", "1", "-type", "f"]))
    var find_lines = String(find_output).split("\n")
    for line_py in find_lines:
        var line = String(line_py)
        if line.byte_length() == 0:
            continue
        # Strip the staging prefix to show the tar-relative path.
        # removeprefix returns a StringSlice; wrap with String() to materialize.
        var rel = String(line)
        var prefix = staging + "/"
        if rel.startswith(prefix):
            rel = String(String(rel).removeprefix(prefix))
        print("  " + rel)

    # Build the tar. The tarball path must be reachable and writable.
    make_parent_dir(output)
    print("[sponge-decor-assets] creating tar: " + output)
    var rc = run_cmd_stream(
        List[String](["tar", "-cf", output, "-C", staging, "theme"]))
    if rc != 0:
        print("[sponge-decor-assets] ERROR: tar exited " + String(rc))
        return 1
    return 0


def verify_tar(tar_path: String) raises -> Bool:
    """Sanity-check the produced tar: `tar tf` lists the expected five
    entries. The upstream Genode run script reads the tar via VFS
    <tar> straight through, so the only contract is the set of
    embedded files."""
    var (_, tf_output) = run_cmd_capture(
        List[String](["tar", "tf", tar_path]))
    var entries = String(tf_output).split("\n")
    var seen_theme_dir = False
    var seen_default = False
    var seen_closer = False
    var seen_maximizer = False
    var seen_font = False
    var seen_metadata = False
    for line_py in entries:
        var line = String(line_py)
        if line.byte_length() == 0:
            continue
        if line == "theme/":
            seen_theme_dir = True
        elif line == "theme/default.png":
            seen_default = True
        elif line == "theme/closer.png":
            seen_closer = True
        elif line == "theme/maximizer.png":
            seen_maximizer = True
        elif line == "theme/font.tff":
            seen_font = True
        elif line == "theme/metadata":
            seen_metadata = True
    if not (seen_theme_dir and seen_default and seen_closer
            and seen_maximizer and seen_font and seen_metadata):
        print("[sponge-decor-assets] ERROR: tar missing one or more "
              + "expected entries:")
        print("  theme/         " + String("yes" if seen_theme_dir else "NO"))
        print("  theme/default.png     "
              + String("yes" if seen_default else "NO"))
        print("  theme/closer.png      "
              + String("yes" if seen_closer else "NO"))
        print("  theme/maximizer.png   "
              + String("yes" if seen_maximizer else "NO"))
        print("  theme/font.tff        "
              + String("yes" if seen_font else "NO"))
        print("  theme/metadata        "
              + String("yes" if seen_metadata else "NO"))
        return False
    return True


def cleanup_staging(staging: String) raises:
    """Best-effort cleanup of the staging tree. The tool is idempotent
    — the staging dir is recreated on every run."""
    var shutil = Python.import_module("shutil")
    if dir_exists(staging):
        shutil.rmtree(staging)


def main() raises:
    var args = argv()
    var output = String(DEFAULT_OUTPUT)
    var regen_pngs = False

    # Parse args.
    var i = 1
    while i < len(args):
        var a = String(args[i])
        if a == "help" or a == "--help" or a == "-h":
            cmd_help()
            return
        if a == "--regen-pngs":
            regen_pngs = True
            i += 1
            continue
        if a.startswith("-"):
            print("error: unknown option '" + a + "'")
            print()
            cmd_help()
            exit(1)
        # Positional: the output tar path.
        if output != DEFAULT_OUTPUT:
            print("error: unexpected extra argument '" + a + "'")
            exit(1)
        output = a
        i += 1

    # Resolve to absolute paths so the operator can `cd` anywhere.
    var os_path = Python.import_module("os.path")
    if not String(os_path.isabs(output)):
        output = String(os_path.abspath(output))

    # The staging dir is a sibling of the output tar. It is created
    # under the same parent dir so the staging tree and the tar are
    # co-located (the geometry of the run script's tmpdir).
    var staging = String(os_path.dirname(output))
        + "/.decor-assets-staging"

    print("[sponge-decor-assets] Sponge OS themed_decorator tar generator")
    print("  output:        " + output)
    print("  staging:       " + staging)
    print("  regen-pngs:    " + String("yes" if regen_pngs else "no"))
    print("  metadata.txt:  " + METADATA_TXT)
    print("  font.tff:      " + UPSTREAM_FONT_TFF
          + " (re-vendored byte-for-byte)")
    print()

    # Pre-flight: host tools (tar is always required; mogrify/convert
    # only on --regen-pngs).
    if not check_host_tools(regen_pngs):
        exit(1)
    print()

    # Pre-flight: required input files.
    if not file_exists(METADATA_TXT):
        print("[sponge-decor-assets] ERROR: metadata file missing: "
              + METADATA_TXT)
        exit(1)
    if not regen_pngs:
        for name in List[String](["default.png", "closer.png", "maximizer.png"]):
            var p = LOCAL_PNG_DIR + "/" + name
            if not file_exists(p):
                print("[sponge-decor-assets] ERROR: checked-in PNG missing: "
                      + p)
                exit(1)

    # Clean any prior staging tree, then build the new one.
    cleanup_staging(staging)
    makedirs_p(staging)

    # Stage metadata.
    var meta_dst = stage_metadata(staging)
    print("[sponge-decor-assets] staged: " + meta_dst)

    # Stage fonts (re-vendored from upstream).
    var (font_dst, font_status) = stage_fonts(staging)
    print("[sponge-decor-assets] staged font.tff: " + font_dst
          + " (source: " + font_status + ")")
    if font_status == "hash-mismatch":
        cleanup_staging(staging)
        exit(1)

    # Stage PNGs.
    var (png_ok, png_status) = stage_pngs(staging, regen_pngs)
    if not png_ok:
        cleanup_staging(staging)
        exit(1)
    print("[sponge-decor-assets] staged PNGs (source: " + png_status + ")")

    # Assemble the tar.
    var rc = assemble_tar(staging, output)
    cleanup_staging(staging)
    if rc != 0:
        exit(1)

    # Verify the produced tar.
    if not verify_tar(output):
        exit(1)

    # Print the final byte/block count for the record.
    var (_, ls_output) = run_cmd_capture(
        List[String](["ls", "-l", output]))
    print()
    print("[sponge-decor-assets] tar verified: " + output)
    print(String(ls_output).rstrip())
    print()
    print("[sponge-decor-assets] done. Stage into a scenario with:")
    print("  exec cp -f " + output
          + " <run-dir>/bin/decor.tar       # before build_boot_image (seL4)")
    print("  exec cp -f " + output
          + " <run-dir>/genode/decor.tar    # base-linux dynamic load")
    print()
    print("Manual escape hatch:")
    print("  1. Edit " + METADATA_TXT)
    print("  2. Re-run ./tool/decor_assets to regenerate the tar.")
    print("  3. The new tar is staged by run/sponge-de-themed-chrome.run")
    print("     on the next scenario build.")
