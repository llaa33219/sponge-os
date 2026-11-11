# sponge_decorator — native theme-driven window chrome

Lightweight native decorator for the Sponge WM stack (D16W.1/M3,
`docs/plans/phase16-sponge-wm.md`). It draws the modern window chrome —
title bar with title text, native close/minimize/fullscreen buttons, a
thin frame, and the eight resize affordances — entirely from theme
files per AGENTS.md §3.4. No Qt, no OpenGL, no libc, public Genode
interfaces only (D16W.2).

The decorator is a **pure renderer + margins publisher**. It performs
no hit-testing, reads no input, and applies no policy: `sponge_layouter`
owns all interaction, exactly as the frozen split in protocol v1.1
describes.

## Protocol v1.1 surface

| Channel | Direction | Use |
|---|---|---|
| `window_chrome` (ROM) | sponge_wm -> decorator | complete snapshot `<window_chrome><window wid label title resizeable fw fh/>...`; create/destroy/title/resizeable/size only, never moves. Destroy is absence. |
| `theme` or `default.theme` (ROM) | run-provided | INI theme source (see below) |
| `decorator_margins` (report) | decorator -> layouter + core + probes | `<decorator_margins><floating top bottom left right/></decorator_margins>`, published at startup and on every theme change |

Association with client windows runs through `sponge_wm`'s content
association (D16W.3): for every snapshot entry the decorator creates
one Gui session and one view **titled with the decimal `wid`**
(`Wm_protocol::view_title`), at size `fw x fh` (the OUTER frame). The
core positions and stacks the view; the decorator never issues move
commands.

All chrome geometry comes from the shared frozen helpers in
`<sponge/wm_protocol.h>` (`title_band`, `button_rect`, `sizer_zone`)
via `chrome_geometry.cc` — buttons 24x16, pad 4, right aligned
right-to-left CLOSE, FULLSCREEN, MINIMIZE (fullscreen only when
`resizeable`, remaining buttons hug the right edge); the title band is
the top margin; the resize zones are the outer `min(margin, 8)` edge
bands plus 16x16 corners. The renderer draws buttons exactly where
`button_rect` places them, so the pixels match the layouter's
hit-testing by construction.

## Theme mechanism

The run provides the theme ROM. `<theme source="themed"/>` in the
component config selects the live `theme` report ROM (sponge_themed's
`<theme name="...">INI</theme>`, decoded like the sponge-de
ThemeController); anything else selects the raw `default.theme` INI
module. Exactly one ROM is opened (opening an unrouted ROM is a fatal
parent denial).

INI semantics are the existing Sponge theme format
(`docs/10-theme-format.md`): full-line `#` comments only, `[section]`
headers, first-`=` split, trimmed values, case-sensitive keys,
`#RRGGBB` or the nine lower-case named colors, base-10 unsigned
integers, best-effort parse with a warning flag.

The decorator consumes a documented subset. Every field resolves as
**`[decor]` key > `[colors]` alias > fallback constant**:

| Field | `[decor]` key | `[colors]` alias | fallback |
|---|---|---|---|
| margins.top | `margin_top` | — | `28` |
| margins.bottom | `margin_bottom` | — | `2` |
| margins.left | `margin_left` | — | `2` |
| margins.right | `margin_right` | — | `2` |
| title_bg | `title_bg` | `window_bg` | `#313244` |
| title_bg_unfocused | `title_bg_unfocused` | `panel_bg` | `#1e1e2e` |
| title_text | `title_text` | `title_text` | `#cdd6f4` |
| title_text_unfocused | `title_text_unfocused` | — | `#7f849c` |
| frame_border | `frame_border` | `window_border` | `#45475a` |
| button_bg | `button_bg` | `separator` | `#45475a` |
| button_bg_unfocused | `button_bg_unfocused` | — | `#313244` |
| button_glyph | `button_glyph` | `title_text` | `#cdd6f4` |
| button_glyph_unfocused | `button_glyph_unfocused` | — | `#7f849c` |
| button_close_glyph | `button_close_glyph` | `error_bg` | `#f38ba8` |
| grip_bg | `grip_bg` | — | transparent (grips not painted) |

The fallback palette equals the shipped `default.theme` values, so a
theme without a `[decor]` section yields chrome coherent with the DE
through the `[colors]` aliases alone.

Margin validation (`valid_margins`): `top` must be in `[16, 255]` so
the frozen 24x16 buttons fit the band; `bottom`/`left`/`right` in
`[0, 255]`. Invalid values warn and keep the fallback. Unknown keys
inside `[decor]` warn (typo guard); keys in the canonical sections
(`[meta]`/`[colors]`/`[fonts]`/`[layout]`/`[window]`) are valid theme
content and ignored silently.

Notes:

* The `[decor]` section is this component's documented key mechanism.
  The canonical sponge-de loader treats it as unknown keys and logs
  warnings (warn-only, values still apply) — accepted noise until a
  docs/10-theme-format.md revision registers the section. This
  component's README is the binding documentation for it.
* Theme font keys (`[fonts]`) are **not applied** in v1: the title
  renders with the fixed bundled TFF at its native size (see "Font
  asset"). Runtime-scaled TrueType (`Ttf_font`) or the `font` VFS
  recipe are the integration path.
* `layout.panel_height` is not consulted; the title band height is the
  published `margins.top`.

## Visual states

* **Focused / unfocused**: the theme and renderer fully implement both
  tints (band, title text, buttons — the close glyph dims with the
  others when unfocused). `Chrome_view::focused(bool, ...)` switches
  and repaints. **v1.1 delivers no focus signal to the decorator**
  (`window_chrome` is frozen without a focus attribute and no focus
  route reaches this component), so at runtime the chrome paints the
  focused style. Wiring real per-window focus needs a protocol signal
  (recorded below); no channel is invented here.
* **Buttons**: the visual states available without input are the
  normal appearance and the fullscreen affordance's presence
  (`resizeable`). Hover/pressed states require pointer input, which
  the decorator does not handle (the layouter does); they are out of
  scope by contract.
* Glyphs are flat 9x9 bitmaps (X, bar, square outline) drawn at the
  button centers — no animations, blur, or gradients.

## Resize affordances (input mask)

The chrome view uses `Framebuffer::Mode { alpha = true }` with all
three planes written per draw:

* **pixels** — only the frame regions (title band, margin strips,
  buttons, title text, optional `grip_bg` tint).
* **alpha** — 255 exactly over the painted frame; 0 over the client
  cutout, so the client's content shows through the chrome view.
* **input mask** — 1 over the whole outer frame except the client
  cutout, plus the four 16x16 corner squares (the frozen zones reach
  into the client area); 0 over the client cutout so clicks pass
  through to the client.

That mask is how the **eight resize affordances** are realized:
edge-band and corner pixels route input to the core (which forwards
gestures to the layouter), without the decorator doing any
hit-testing. By default the affordances are invisible (modern flat
style); a theme may tint them via `grip_bg`. Title-band and button
pixels likewise consume input so title drags and button clicks reach
the core instead of falling through — the decorator itself never
reads them.

## Font asset

`data/font.tff` is the TFF font shipped by Sponge's
`tool/decor_assets_data/font.tff` (byte-identical; itself the
Phase-11-W4 re-vendoring of upstream `themed_decorator`'s
`theme/font.tff`, trimmed-TTF format read by the public `Tff_font`).
It is linked into the binary via `SRC_BIN` — no run-script staging
needed. Provenance: DejaVu-derived bitmap font, redistributed under
the same terms as the existing `tool/decor_assets_data` copy.

Swapping the font asset: replace `data/font.tff`; if
`Tff_font::glyph_buffer_size()` of the new file exceeds 2048 bytes,
raise `Main::_glyphs` in `main.cc` accordingly (the current file needs
1020).

## Modules

| File | Role | Testability |
|---|---|---|
| `theme.{h,cc}` | Theme model, INI parsing, margin validation, fallback palette, focus-state style selection | pure, host-tested |
| `chrome_geometry.{h,cc}` | Frozen-geometry assembly (`Chrome_layout`), glyph bitmaps, title fit/origin | pure, host-tested |
| `renderer.{h,cc}` | Pixel/alpha/input-mask painting (session-free) | geometry level host-tested |
| `chrome_view.{h,cc}` | Per-window Gui session/view/buffer, redraw | Genode-side |
| `main.cc` | Config/ROM/report wiring, snapshot diffing, margins publishing | Genode-side |
| `tests/` | Deterministic pure cases + host self-test | — |

## Build and test

Genode build (component only):

    make -C genode/build/x86_64 sponge_decorator

Deterministic tests (host, no Genode runtime; full command in
`tests/self_test.cc`):

    g++ -std=c++20 -O1 \
        -I genode/repos/base/include \
        -I genode/repos/base/include/spec/64bit \
        -I genode/repos/base/include/spec/x86_64 \
        -I genode/repos/base/include/spec/x86 \
        -I genode/repos/base/src/include \
        -I genode/repos/os/include \
        -I repos/sponge/include \
        repos/sponge/src/sponge_decorator/tests/self_test.cc \
        repos/sponge/src/sponge_decorator/tests/test_cases.cc \
        repos/sponge/src/sponge_decorator/theme.cc \
        repos/sponge/src/sponge_decorator/chrome_geometry.cc \
        repos/sponge/lib/src/sponge_wm_protocol/wm_protocol.cc \
        genode/repos/base/src/lib/base/node.cc \
        genode/repos/base/src/lib/base/hid.cc \
        genode/repos/base/src/lib/base/xml_generator.cc \
        genode/repos/base/src/lib/base/output.cc \
        -o /tmp/sponge_decorator_self_test \
    && /tmp/sponge_decorator_self_test

Covered: theme parsing (INI semantics, alias chain, validation
warnings, NUL-padded ROM content), margin validation, the published
`decorator_margins` shape round-trip, view geometry (frozen button and
zone placement against `button_rect`/`sizer_zone`), glyph bitmap and
placement math, title fit/origin math. The tests check drawing
**geometry** — there are no pixel/runtime claims.

## Status and integration notes (honest)

* **Builds clean** under the existing `genode/build/x86_64` tree with
  `LIBS = base sponge_wm_protocol`; the pure tests pass (119 checks).
* **No native pixel acceptance is claimed**: `run/sponge-wm-native*.run`
  scenarios need the parallel `sponge_wm`/`sponge_layouter` components
  and a booted scenario; no run scripts were touched here. The run
  configuration already routes everything this component needs
  (`window_chrome`, `theme`/`default.theme`, `decorator_margins`, Gui
  via sponge_wm) — no run-side integration work is required.
* **Focus routing**: per-window focused/unfocused switching needs a
  focus signal in a future protocol revision (e.g. a `focused`
  attribute on `window_chrome` via a protocol bump, or a routed focus
  channel). The renderer, theme keys, and `Chrome_view::focused()` are
  ready; nothing is faked meanwhile.
* **Theme font keys** (`[fonts]` sizes/families) are not applied (see
  "Theme mechanism").
* **Capacity**: at most 32 chrome views (`MAX_VIEWS` in `main.cc`);
  extra snapshot entries are dropped with a warning.
* The `[decor]` theme section needs registration in
  `docs/10-theme-format.md` when docs changes are in scope.
