# themed_decorator theme metadata (Phase 11 W4).
#
# This file is the SOURCE OF TRUTH for the geometry encoded in
# tool/decor_assets.mojo's generated `decor.tar` ROM module.
#
# === FILE FORMAT CONSTRAINT ===
#
# The decorated ROM entry `theme/metadata` MUST start with `<theme>`.
# The themed_decorator constructs a Genode::Node directly from the
# raw bytes (genode/repos/gems/src/app/themed_decorator/theme.cc:99-104)
# via:
#
#     static File file("theme/metadata", alloc);
#     return Genode::Node(
#         Genode::Const_byte_range_ptr(file.data<char>(), file.size()));
#
# Genode::Node has NO `#` comment syntax — it parses a strict XML
# subset. If the file starts with `# ...`, the parser fails to find
# the root element, with_optional_sub_node("aura"/"decor") returns
# nothing, and the margins default to 0 (Margins_from_metadata,
# theme.cc:119-146). That degenerate geometry makes the title
# rect empty, which is exactly why a drag on the title bar hits no
# drag zone and the window never moves.
#
# Therefore: this README.md is the explanatory companion; the
# companion `metadata.txt` is a PURE XML document starting with
# `<theme>`. `tool/decor_assets.mojo` enforces this with a fail-loud
# validation (the first non-blank byte must be `<`).
#
# === GEOMETRY DECLARATION ===
#
# Aura margins (the outer "frame" between window edges and decor):
#   <aura top="8" bottom="8" left="8" right="8"/>
# Decor margins (the title-bar/closer/maximizer strip):
#   <decor top="20" bottom="8" left="1" right="1"/>
# Title bar (the part the drag-press lands on):
#   <title xpos="16" ypos="9" width="32" height="20"/>
# Closer / maximizer glyph rects (in theme-coordinate space):
#   <closer xpos="36" ypos="10"/>
#   <maximizer xpos="10" ypos="10"/>
#
# === WHY THESE VALUES ===
#
# COPIED VERBATIM from the upstream sample tar:
#   genode/repos/gems/src/app/themed_decorator/theme/metadata
#
# The aura/decor margins are the upstream defaults; the title-bar
# 32x20 rect sits inside the top 20-px decor strip. These defaults
# satisfy the Phase-10 drag regression gate (the title bar is wide
# enough to land a drag press) AND the W4 themed-chrome scenario's
# threshold (the new title-bar x = 73 lands the drag at the actual
# title geometry).
#
# === HOW TO CHANGE ===
#
# 1. Edit metadata.txt (this README's sibling).
# 2. Re-run ./tool/decor_assets.
# 3. The new decor.tar is staged by run/sponge-de-themed-chrome.run
#    on the next scenario build (the script copies decor.tar into
#    bin/ before build_boot_image so base-sel4 packs it into
#    image.elf; docs/11 §10.4).
#
# === MANUAL ESCAPE HATCH ===
#
# The metadata is a single source of truth. To ship a totally
# different chrome, replace the four PNGs under
# tool/decor_assets_data/pngs/ AND update metadata.txt, then re-run.
# To customize fonts, edit the upstream
# genode/repos/gems/src/app/themed_decorator/theme/font.tff
# (it's a raw byte set, not theme-derived) and re-run. The asset
# is re-vendored — never regenerated.
