# sponge_layouter — native layout policy component

The policy/state half of the native Sponge WM (Criterion 16,
`docs/plans/phase16-sponge-wm.md` D16W.1): floating placement and
8-zone resize, minimize parking, work-area maximize/restore, and
Master-Stack tiling with reflow — plus the protocol v1 `window_layout`
producer and the protocol v1.1 `window_request` / `focus` producers.

The pure policy model lives in `layout_model.{h,cc}` (no Env, no
sessions, no allocation, no exceptions — the `<sponge/wm_protocol.h>`
purity contract). `main.cc` is Genode session glue only. The
deterministic case list in `model_tests.cc` exercises the production
model host-side; see **Tests** below.

State machine reference: `docs/plans/wm-state-table.md` (the baseline
rows plus the "Native WM Extension"). Frozen shapes:
`docs/05-sponge-de.md` §4.6 (protocol v1) and
`docs/plans/phase16-sponge-wm.md` §11 (internal v1.1).

## Sessions

Consumed (ROM sessions, all except `configd` routed through
`report_rom` in `run/sponge-wm-native*.run`):

| Label | Producer | Semantics |
|---|---|---|
| `window_list` | sponge_wm | identity source (the `id` attribute is the internal `wid`); COMPLETE snapshots only — a vanished wid is destroyed |
| `focus_request` | TasklistController (sole writer) | deduplicated on `id`; raises + steers focus; restores a minimized window in the same step (U3) |
| `rules` | TasklistController (sole writer) | last-wins per publication; assigns update existing windows (see Coordinate frames) |
| `focus_state` | sponge_wm | the sole truth of `window_layout` `focused="yes"` |
| `decorator_margins` | sponge_decorator | may be absent/zero initially and update later |
| `configd` | sponge_configd broadcast (optional) | `wm.layout.mode` = `floating` \| `tiling`, default `floating` when absent |

Produced (Report sessions via `report_rom`):

| Label | Shape | Semantics |
|---|---|---|
| `window_layout` | protocol v1 | one `<boundary>` = the **work area**, windows with screen-absolute INNER geometry, parked windows at `(-32000,-32000)`, `focused` only per `focus_state` |
| `window_request` | v1.1 | complete desired state: INNER screen-absolute geometry (`x=y=-32000` parks) and the **sticky** `<window wid action="close"/>` node kept in every snapshot until the wid disappears from `window_list` (the layouter never pretends a window closed early) |
| `focus` | v1.1 | steering snapshot only (empty root = no focus) |

The component also opens one **Gui session with role `layouter`** to
`sponge_wm` (`+ policy | label_prefix: sponge_layouter | role: layouter`):
core->layouter Input forwarding over chrome arrives on that session's
Input interface, and the public `Gui::Session::info()` ROM supplies the
work-area fallback below. The component never writes `focus_request`
or `rules` (D16W.6) and never publishes `window_chrome` (that is the
core's job).

## Config schema (`<config>` of the component)

```
+ config | rules: rom          # "rom": the tasklist rules ROM is the
                               # update source (static rules remain
                               # initial placement + fallback)
  + rules                      # static inline rules (initial placement)
    + screen screen
    + assign | label: ... | target: screen
             | xpos: ... | ypos: ... | width: ... | height: ...
             | maximized: no
  + work_area | x: 0 | y: 28 | width: 1024 | height: 740   # optional
  + panel_height: 28           # optional, fallback only (default 28)
```

`rules="rom"` selects the tasklist `rules` ROM for updates; when the
ROM holds no valid `<rules>` document the static inline document comes
back into effect (upstream window_layouter semantics).

## Work area

1. **Explicit** `<work_area x= y= width= height=/>` — authoritative.
   All four `run/sponge-wm-native*.run` pin `0,28,1024,740` for their
   1024x768 / 28 px panel topology.
2. **Public-info fallback** (future dynamic resolutions): the top-level
   rect of the public Gui info ROM (`Gui::Session::info()`, the same
   "mode information" source `Gui::Connection::window()` reads), minus
   a top `<panel_height>` band (default 28).
3. **Last resort**: `0,28,1024,740`-shaped default (`0,panel_height,
   1024,768-panel_height`) with a loud warning.

The work area is reported as the `window_layout` boundary (binding),
which keeps the frozen 64 px tasklist maximize inference working:
work-area maximize sets OUTER == work area and derives INNER by the
decorator margins.

## Coordinate frames

Two rules sources, two frames (deliberate compatibility decision):

- **Static inline `<rules>`** assigns are **screen-absolute** content
  geometry — the committed run/probe contract (the run headers name
  the assign values as the window placement; the native probes assert
  them as screen-absolute `window_layout` coordinates, e.g. `50,320`).
- **`rules` ROM** (tasklist) assigns are **target-relative to the
  reported boundary origin** — frozen protocol v1. The TasklistController
  subtracts the boundary `xpos/ypos`, this component adds them back
  ("boundary-origin conversion"). With the work area at `(0,28)` the
  round trip is real and drift-free (model-tested over 25
  republishes); feeding absolute values back would drift by 28 px per
  republish. "Last-wins" is per DISTINCT publication: a document
  identical to the current one is not re-applied (which would snap
  user-moved windows back to the echoed geometry).

A parked assign is `xpos` AND `ypos` at or below -32000 (the
established write value is exactly `-32000/-32000`); a single sentinel
coordinate does not park.

> FIXME (open design question): unify the two frames in a protocol
> bump once the committed run configs are revisitable — the frozen
> §4.6 wording ("assign xpos/ypos are target-relative") describes the
> tasklist ROM, while the inline authoring values are pinned as
> screen-absolute by the acceptance choreography.

## State model (per window)

`Normal-Visible`, `Normal-Visible-Focused`, `Maximized`, `Minimized`
exactly as `docs/plans/wm-state-table.md`; placement (master, stack,
floating, parked, maximized) is derived from mode + state, never an
extra state. Creation order is first-appearance order in `window_list`
snapshots. `focused="yes"` comes only from `focus_state`; `focus`
steering and `focus_request` never set it directly.

- **Floating placement**: the first matching static assign (exact
  `label`, then `label_prefix`/`label_suffix`); unmatched windows
  cascade at the work-area origin + margins in 32 px steps with the
  client size from `window_list`.
- **Move / 8-zone resize**: only floating windows (tiled geometry is
  layouter-owned in v1; tiled drag/resize is a no-op), exact pointer
  deltas, inner minimum 64x64. Non-resizeable windows always float and
  never resize.
- **Minimize**: parks at `(-32000,-32000)` keeping the size; restore
  via tasklist rules or `focus_request` (restore-with-focus) returns
  the saved geometry (or re-enters the tiling arrangement).
- **Maximize**: OUTER == work area, INNER = work area inset by the
  margins; the toggle restores the saved pre-maximize geometry
  exactly. A tasklist republish of the maximized rect with
  `maximized="yes"` does not clobber the saved restore target.
- **Close**: the chrome closer arms a sticky `action="close"` in
  `window_request`; the window stays published until `window_list`
  drops it.
- **Tiling (Master-Stack)**: OUTER placement — the first eligible
  window (resizeable, non-minimized, non-maximized) in creation order
  is the left-half master, the remaining eligible windows split the
  right half into equal bands top to bottom in creation order
  (integer band math identical to `test/native_wm_tiling_probe`'s
  acceptance contract). A lone master stays left-half; non-resizeable
  windows always float; minimized windows park and leave the
  arrangement; maximized windows fill the work area and leave the
  arrangement. Reflow on open/close/minimize/restore/maximize/
  maximize-restore/mode switch. Leaving tiling keeps every window's
  current geometry as its floating geometry (no hidden pre-tile
  history).

## Input behavior (component behavior, not protocol)

Hit-testing uses the shared frozen `sponge_wm_protocol` chrome helpers
(`chrome_hit`, `button_rect`, `sizer_zone`, `title_band`): title move,
8-zone resize grips, and the close/fullscreen/minimize buttons
(24x16, pad 4, right-aligned in the title band). A **click** is press +
release within 5 px landing in the same button rect. Any chrome press
focuses+raises the window (`focus` steering). During move/resize drags
the component takes the public `exclusive` input grab and releases it
afterward.

Disambiguation note (necessary): the frozen 16x16 corner grip squares
overlap the close button's center column (its center sits exactly on
the corner boundary for title bands thinner than 32 px), so points
inside a shared `button_rect` are the button and everything else falls
through to `chrome_hit` (corner > edge grip > title/move). The probe
click targets and the 8-zone grip points are unaffected.

## Mode

`wm.layout.mode` arrives on the `configd` broadcast
(`<key name="wm.layout.mode" value="floating|tiling"/>`). Absent
broadcast/keys and unrecognized values read as `floating` (the
registered configd default). Without a routed `configd` ROM the
component logs once and stays floating.

## Build and tests

Genode target (this directory):

```
make -C genode/build/x86_64 sponge_layouter
```

Host deterministic self-test (production model code, no Genode
runtime — full command in `self_test.cc`'s header):

```
g++ -std=c++20 -O1 \
    -I genode/repos/base/include \
    -I genode/repos/base/include/spec/64bit \
    -I genode/repos/base/include/spec/x86_64 \
    -I genode/repos/base/include/spec/x86 \
    -I genode/repos/base/src/include \
    -I repos/sponge/include \
    repos/sponge/src/sponge_layouter/self_test.cc \
    repos/sponge/src/sponge_layouter/model_tests.cc \
    repos/sponge/src/sponge_layouter/layout_model.cc \
    repos/sponge/lib/src/sponge_wm_protocol/wm_protocol.cc \
    genode/repos/base/src/lib/base/node.cc \
    genode/repos/base/src/lib/base/hid.cc \
    genode/repos/base/src/lib/base/xml_generator.cc \
    genode/repos/base/src/lib/base/output.cc \
    -o /tmp/sponge_layouter_self_test \
&& /tmp/sponge_layouter_self_test
```

Current result: `self-test: PASS (144 checks)`.

The case list covers window create/destroy, creation order, floating
placement (static + cascade), title move, all 8 resize zones (exact
deltas + 64x64 clamp), focus steering vs. focus truth, sticky close
requests and close/recreate slot reset, minimize/restore+focus,
work-area maximize/restore (outer ==
work area), tiling open/close/minimize/restore/maximize/mode-switch
reflow, non-resizeable float, and >=20-cycle geometry stability (25
rules republishes with the boundary-origin round trip + 20 mode-switch
cycles), plus non-vacuous negative controls (same-id `focus_request`,
sloppy button release, tiled drag/resize, non-resizeable resize,
single sentinel coordinate, maximized drag, client-area drag,
maximized-rect rules republish, identical rules re-feed after a user
move). Expected tiling/chrome geometry is
computed by independent copies of the committed acceptance probe math,
so a model/probe misunderstanding cannot self-certify.

## Honest status

The component builds and its policy model is proven host-side. The
native runtime scenarios (`run/sponge-wm-native*.run`) remain **blocked**
on the sibling components `sponge_wm` (core) and `sponge_decorator`
(chrome) and are deliberately not claimed PASS from this deliverable.
Integration note for the core: role `layouter` Gui sessions must
forward chrome input losslessly (screen-absolute pointer coordinates)
and answer `Gui::Session::info()` with the screen/mode rect; the core
applies `window_request` including the sticky `action="close"` 0x0
close convention and treats `focus` as raise+focus steering.
