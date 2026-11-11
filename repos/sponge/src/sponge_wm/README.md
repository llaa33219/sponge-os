# sponge_wm — native window-manager core (Criterion 16, M1)

`sponge_wm` is the Sponge-owned WM core (D16W.1): **Gui virtualization,
the window registry, input routing, and native content/chrome
association**, built on PUBLIC Genode interfaces only (D16W.2 — no
gems headers, no vendored patches). It replaces upstream `server/wm`
in the native topology (`run/sponge-wm-native*.run`); the upstream trio
stays untouched as the fallback (D16W.7).

The policy/state/layout component (`sponge_layouter`) and the native
chrome renderer (`sponge_decorator`) are separate components built in
parallel; this directory owns only the core.

## What it does

| Concern | Behavior (binding contracts) |
|---|---|
| Sessions | Provides **only** `Gui`. One virtual session per client window (`CONTENT`), one for `sponge_decorator` (`DECORATOR`), one for `sponge_layouter` (`LAYOUTER`). The role comes from the run-side session policy: `<policy label_prefix="..." role="decorator\|layouter"/>` + `<default-policy/>` (content). |
| Identity | The core assigns a **monotonic `wid`** per window and emits it as the frozen `window_list` `id`. The decorator's chrome-view title is the decimal `wid` (`Wm_protocol::view_title`); that title is the association key. |
| `window_list` (core -> tasklist/layouter/probes) | Frozen protocol v1 shape (docs/05-sponge-de.md §4.6): full session `label`, client `title`, client `width`/`height`, `has_alpha`/`hidden`/`resizeable` (`yes` form when true). Unsized and label-less entries are omitted per the library contract. Emitted via `Expanding_reporter` as the XML rendering. |
| `window_chrome` (core -> decorator) | v1.1 complete snapshot with `wid,label,title,resizeable,fw,fh` where `fw/fh` is the **outer** frame (inner expanded by `decorator_margins`). Published on create/destroy/title/resizeable/size changes only — never on move. Destroy is the window's absence. |
| `focus_state` (core -> layouter) | v1.1 complete truth snapshot. Truth updates from **content clicks** and from `focus` steering; the empty root means no focus. This is the sole source of `window_layout`'s `focused="yes"`. |
| `window_request` (layouter -> core) | v1.1 complete INNER geometry snapshot; `x/y/w/h` are applied to the content view (inner rect) and the chrome view (outer rect). `x=y=-32000` parks the views verbatim. `action="close"` is **sticky**: applied once as the app-visible 0x0 close (the client's `Gui::Connection::window()` reads a zero rect and the Qt QPA plugin raises a close event via the info ROM's `<capture closed="yes"/>`), re-processing is a no-op until the wid vanishes from `window_list`. A malformed snapshot is dropped as a unit. |
| `focus` (layouter -> core) | Steering to the input/stacking target: the core raises the window's chrome+content pair and routes keyboard input to it. Chrome presses raise/focus through this channel (the core does not auto-focus on chrome presses). |
| `decorator_margins` (decorator -> core) | Consumed for inner<->outer frame math. Absent/zero margins are tolerated (outer == inner). |
| Pixels | Pass-through: `framebuffer()` hands out the real nitpicker framebuffer capability of the session's backing connection; `buffer(mode)` forwards. The core sits out of the pixel path. |
| Input | Chrome events (the decorator's real connection) are forwarded **losslessly in raw screen coordinates** to the layouter's input stream (the layouter hit-tests). Content events go to the owning client, translated into the client's coordinate space (`client = screen - inner_origin + client_view_origin`). Keyboard events follow the focus truth. Content button presses are focus-truth clicks (update truth + raise). The decorator's own input stream is a **dummy** (never fed). No pointer/hover/window_control channels exist (v1.1). |
| Exclusive grabs | `Input::Session::exclusive` (the public RPC; the task's "exclusive_input") on the **layouter's** input session routes ALL core input to the layouter until released — chrome drags keep receiving motion over content. Content sessions forward their `exclusive` request to their real nitpicker connection (pass-through). |
| `resizeable` | A window is `resizeable` when its client registers a sigh on the info ROM ("interest in mode-change notifications", the upstream semantic). The shipped Qt QPA plugin registers one for every window, so Qt windows report `resizeable`. |
| Close teardown | When the client closes (or its session ends) the window vanishes from `window_list` and `window_chrome`, and the decorator destroys the chrome view (v1.1: destroy = absence from the snapshot). |
| Stacking | Core-owned: every raise moves the window's chrome first, then its content (content above its own chrome; frame-ring input still reaches the chrome because the views do not overlap outside the inner rect). |

## Layout

```
src/sponge_wm/
├── target.mk          Genode build target (binary: sponge_wm)
├── main.cc            transport hub: reporters, ROMs, input routing,
│                      placement application, Component::construct
├── wm_session.h/.cc   Gui::Session virtualization per role (public
│                      gui_session.h contract), info ROM (resize/close
│                      channel), input pump, view forwarding
├── wm_root.h/.cc      Gui service root + session-policy role matching
├── wm_registry.h/.cc  PURE registry/protocol-state model (no Env):
│                      wid table, snapshots, placement, sticky close,
│                      focus truth, margins (host-tested production code)
├── registry_tests.h/.cc  deterministic registry/protocol cases
└── self_test.cc       host-side harness (NOT in target.mk; build cmd
                       in its header comment)
```

## Tests

`registry_tests.cc` runs against the **production** model and the
**production** `sponge_wm_protocol` library (every published snapshot
is generated by the shipped emitter and parsed back by the shipped
parser). Run it host-side (no Genode runtime, no scenario):

```
g++ -std=c++20 -O1 -I genode/repos/base/include \
    -I genode/repos/base/include/spec/64bit \
    -I genode/repos/base/include/spec/x86_64 \
    -I genode/repos/base/include/spec/x86 \
    -I genode/repos/base/src/include -I repos/sponge/include \
    repos/sponge/src/sponge_wm/self_test.cc \
    repos/sponge/src/sponge_wm/registry_tests.cc \
    repos/sponge/src/sponge_wm/wm_registry.cc \
    repos/sponge/lib/src/sponge_wm_protocol/wm_protocol.cc \
    genode/repos/base/src/lib/base/node.cc \
    genode/repos/base/src/lib/base/hid.cc \
    genode/repos/base/src/lib/base/xml_generator.cc \
    genode/repos/base/src/lib/base/output.cc \
    -o /tmp/sponge_wm_registry_self_test && /tmp/sponge_wm_registry_self_test
```

These are deterministic registry/protocol-state tests. They make **no
native end-to-end claim**: real sessions, real input, real placement,
and the M1 gate (`window_list` shape + placement + render + synthetic
title-bar drag) are proven by `test/native_wm_probe` in
`run/sponge-wm-native.run` once the full trio exists. Nothing here
prints a PASS for behavior it does not exercise.

## Quotas

The run stanza sizes the core `caps: 250 | ram: 8M` (plus normal
parent upgrades). Per-session costs, measured from the public API:

| Item | Cost |
|---|---|
| Virtual session object | 1 cap (managed) |
| `input()` buffer | `Input::Event_queue::QUEUE_SIZE` (512) x `sizeof(Input::Event)` RAM + 1 cap |
| `command_dataspace()` | `sizeof(Gui::Session::Command_buffer)` (64 commands) RAM |
| `info()` ROM | 8 KiB RAM + 2 caps (withdrawn from the session's donation) |
| Real nitpicker connection | `Gui::Connection` donates 96 KiB + `Gui::Session::CAP_QUOTA + 9` (= 18) caps and auto-upgrades on demand |
| Client framebuffer | `w*h*3` bytes (`w*h*5` with alpha), donated through `buffer()` and auto-upgraded on the real connection |

A 640x480 window therefore costs ~1 MiB RAM and ~21 caps; the
decorator and layouter sessions cost one real connection each. Session
creation enforces a RAM floor (`input` buffer + 32 KiB) and the
`Gui::Session::CAP_QUOTA` cap floor and refuses smaller donations
loudly.

## Honest v1 limitations

- **`hidden` is always false.** The frozen shape carries the attribute,
  but the v1.1 control path has no hide channel and the native minimize
  convention is the `<=-32000` parking sentinel in `window_layout`
  (recorded approval 8). The attribute is therefore omitted exactly as
  the generator contract requires.
- **Resize transient.** `window_request` `w/h` is applied to the view
  immediately and the client is asked (info ROM) to resize its buffer.
  Until the client complies, nitpicker tiles the old buffer across the
  new view rect (nitpicker's texture semantics). Upstream wm has the
  same request/acknowledge shape.
- **Client self-resize.** The placement is the v1.1 authority; a
  client that resizes on its own updates `window_list` (the layouter
  sees it) but the core keeps steering the client back to the placed
  size via the info ROM ("geometry is layouter-owned", state table).
- **`focus()` delegation** is translated to the real sessions' own
  capabilities (the Qt QPA plugin does not use it; it exists for API
  completeness).
- **Keyboard routing** follows the core's focus truth even when
  nitpicker's click focus points elsewhere (that is what makes the
  tasklist focus path deterministic). Keys that never reach a core
  session (e.g. the panel's direct nitpicker session) are outside the
  core by topology.
- Relative-motion exclusive-grab behavior depends on nitpicker's own
  routing for events the core never receives; everything that reaches
  the core is routed to the grabber while the grab is held.
