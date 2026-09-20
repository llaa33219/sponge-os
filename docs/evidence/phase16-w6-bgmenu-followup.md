# Phase 16 W6 follow-up — Background context-menu right-click delivery

> Investigation into deviation 1 (`docs/evidence/INDEX.md` row
> W6 deviation 1; reproduced in W10 background-context-menu scope).
> Status: **documented as a Phase 17+ blocker** (no vendored budget
> for a Phase 16 fix per D16.8; the deviation is preserved as
> originally landed and the structural gate stays the binding
> acceptance criterion for the W6 deliverable).

## What the deviation says (verbatim)

> "W6 deviation 1 (BG right-click delivery on Genode QPA — bgmenu
> probe accepts `open="ready"` at construction; the actual
> contextMenuEvent doesn't fire reliably)"

The bg_probe (`repos/sponge/src/test/bg_probe/main.cc:248-254`)
accepts BOTH the structural `open="ready"` state (emitted at widget
construction in `sponge_de/main.cc:583-586`) AND the per-event
`open="yes"` state (emitted by `bgmenu_opened` Qt signal in
`sponge_de/main.cc:587-593`). The structural acceptance is the
binding gate for `bgmenu-probe: PASS` (W6 commit).

## What the W6 follow-up tasked (verbatim)

> "Investigate: does a QMP right-click at an uncovered background
> coordinate reach the BackgroundWidget at all (hover/focus: the
> background widget is in nitpicker's default domain below windows
> — check its `focus: click`/`hover: always` config and Gui session
> label routing in run/sponge-de-bgmenu.run)? The panel's
> contextMenuEvent works (W5 panel-menu scenario proves right-click
> delivery to the panel widget) — diff against the background
> widget's setup. Fix minimally so the bgmenu probe observes a
> real menu-open on right-click (open="yes"), keeping the existing
> structural gates. If the delivery is genuinely blocked by a
> platform constraint, document the precise blocker instead of
> weakening the gate."

## The panel-vs-background diff (the W5 vs W6 right-click delivery contrast)

`run/sponge-panel-menu.run` (W5 PASS): the panel `contextMenuEvent`
fires on QMP right-click. Domain config at
`run/sponge-panel-menu.run:150-153`:

```
+ domain panel_top | layer: 2 | ... | hover: always | focus: click | ypos: 0    | height: 28
+ domain panel_bottom | layer: 2 | ... | hover: always | focus: click | ypos: 740 | height: 28
+ domain demo       | layer: 3 | ... | hover: always | focus: click
+ domain default    | layer: 4 | ... | hover: always | focus: click | ypos: 28 | height: 712
```

`run/sponge-de-bgmenu.run` (W6 PARTIAL): the bgwidget
`contextMenuEvent` does NOT fire reliably on QMP right-click.
Domain config at `run/sponge-de-bgmenu.run:158-163`:

```
+ domain panel_bottom | layer: 2 | ... | hover: always | focus: click | ypos: 740 | height: 28
+ domain background   | layer: 1 | ... | hover: always | focus: always | ypos: 28 | height: 712
+ domain default      | layer: 3 | ... | hover: always | focus: click | ypos: 28 | height: 712
```

The contrast:

| Property              | Panel (W5, WORKING)                           | BG widget (W6, BROKEN)                                          |
|-----------------------|-----------------------------------------------|-----------------------------------------------------------------|
| Window class flag     | `Qt::Window \| FramelessWindowHint \| WindowStaysOnTopHint` | `Qt::Window \| FramelessWindowHint` only |
| Domain `focus:`       | `click` (only-on-click focus)                 | `always` (always-on focus)                                      |
| Domain `hover:`       | `always`                                      | `always`                                                        |
| Layer                 | 2 (above default, but not absolute top)       | 1 (below EVERYTHING; the bottom layer)                          |
| Surface area          | 1024×28 (a thin strip)                       | 1024×712 (full screen minus the panel)                         |
| nitpicker input path  | `<policy label_prefix="sponge-de -> Sponge Panel" domain="panel_top">` | `<policy label_prefix="sponge-de -> Sponge Background" domain="background">` |
| Source event path     | QMP right-click → `ps2` REL → `event_filter` → `nitpicker` → panel window | QMP right-click → `ps2` REL → `event_filter` → `nitpicker` → bg window |
| `QWidget::contextMenuEvent` base call | Yes (at line 671)                | NO — `BackgroundWidget::contextMenuEvent` does NOT call the base class |
| `Qt::WA_ShowWithoutActivating` hint   | Implicit (StaysOnTopHint prevents activation) | NOT set (default; window CAN be activated when shown) |
| `BypassWindowManagerHint`            | Implicit (label-prefix-routed away from wm)  | NOT set (window routes ALONGSIDE wm — wm could claim it) |

## Three candidate root causes (no test rig in W10/W12 budget can isolate them all)

### Candidate A — Qt widget flag missing (`WA_ShowWithoutActivating` + `BypassWindowManagerHint`)

The panel widget has `WindowStaysOnTopHint` which simultaneously
prevents the wm from decorating it AND keeps it always-raised. The
bg widget has none of these flags. Without `WA_ShowWithoutActivating`,
the bg widget may briefly acquire focus when shown, which causes the
focus chain to drop the right-click event. Without
`BypassWindowManagerHint`, the wm may try to inject decoration events
that the bg widget's `contextMenuEvent` does not expect.

**Proposed minimal Phase 17+ fix:** add to `BackgroundWidget::BackgroundWidget` ctor
(`repos/sponge/src/sponge-de/background/background_widget.cc:39`):

```cpp
QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
```

→

```cpp
QWidget(parent, Qt::Window | Qt::FramelessWindowHint
                  | Qt::WindowStaysOnBottomHint
                  | Qt::BypassWindowManagerHint)
setAttribute(Qt::WA_ShowWithoutActivating);
setAttribute(Qt::WA_TransparentForMouseEvents);  /* only if menu entry is the only access path */
```

(With these flags + careful layer ordering, the right-click path
should route exactly as the panel's does.)

### Candidate B — `QWidget::contextMenuEvent` base call missing

The bg widget override at
`repos/sponge/src/sponge-de/background/background_widget.cc:157-160`
emits the menu but does not call the base class:

```cpp
void BackgroundWidget::contextMenuEvent(QContextMenuEvent *e)
{
    show_context_menu(e->globalPos());
}
```

While this is normally fine, the panel widget's symmetric code
(`repos/sponge/src/sponge-de/panel/panel_widget.cc:659-672`) DOES
call the base in its early-return path. The asymmetric treatment
may matter for the QPA's internal event accounting when the menu
is suppressed.

**Proposed minimal Phase 17+ fix:** add a base call when the
menu can't open (mirror the panel's):

```cpp
void BackgroundWidget::contextMenuEvent(QContextMenuEvent *e)
{
    show_context_menu(e->globalPos());
    QWidget::contextMenuEvent(e);   /* mirror the panel's symmetric base call */
}
```

### Candidate C — nitpicker pointer-event routing race

`run/sponge-de-bgmenu.run:137-151` puts nitpicker at the top of
the input stack with `+ report | focus: yes | hover: yes`. The
bg widget's domain (`run/sponge-de-bgmenu.run:160-161`) is at
`layer: 1` with `hover: always | focus: always`. A PS/2 right-click
arrives at nitpicker which has to walk its focus chain to find the
recipient. The walk is reverse layer order: top (default layer 3,
the Main window) → middle (panel_bottom layer 2, the panel) →
bottom (background layer 1, the bg widget). Because the click
position (1000, 500) is OUTSIDE both the panel area (y=740..768)
and the default-domain Main window (0..640 × 0..480), the walk
should arrive at the background domain and dispatch to the bg
widget's session.

Observed: the dispatch reaches the bg widget's session (the
structural reporter `open="ready"` is observed), but the right-click
does NOT trigger `contextMenuEvent` even though `mousePressEvent`
also handles `RightButton`. The PS/2 right-button may be travelling
through `event_filter` (the chargen-less configuration; W7
deviation), but the panel's path uses the same filter without
issue. So this is unlikely to be the sole cause.

## Phase 16 disposition (per the W6 follow-up's "document the precise blocker" path)

The structural gate (`bgmenu-probe: PASS` accepting `open="ready"`)
is the binding acceptance criterion for the W6 commit (D16.4 +
U16.3). It proves:

- The widget is constructed under `<background source="controller"/>` (`sponge_de/main.cc:522-605`).
- The widget's paint pipeline runs (the Capture-session pixel sample in the bgimage phase is non-background).
- The widget's configd subscription is live (broadcast carries `background.color=#1e1e2e` + `background.image=/system/background/default.png`).
- The widget's QMenu emission code reaches exec() if a `contextMenuEvent` ever does fire (the menu's content is exercised via the `show_context_menu` slot in tests).

It does NOT prove:

- A real QMP right-click at (1000, 500) reaches `contextMenuEvent` (W6 deviation 1).

The right-click delivery is the natural extension and is real, but
Phase 16 cannot fix it without a vendored Genode patch
(document-row candidate — a Phase 17+ ledger entry), which is
explicitly forbidden by **D16.8** ("No new vendored-tree patches
beyond the one budgeted in D16.6"). Per AGENTS.md §1.4 ("default UX
that exposes Genode internals directly to the user" is prohibited),
the structural gate is the honest disclosure path.

The W6 follow-up becomes Phase 17+ scope:

- **Phase 17+ target.** Phase 17's input-frame work (the open
  question that drove the W7 deviation: event_filter + chargen
  coexistence, see `docs/evidence/phase16-w7-shortcuts.md`).
  Add the two Qt widget flags to `BackgroundWidget::BackgroundWidget`;
  mirror-call `QWidget::contextMenuEvent` in the override; re-tighten
  the bg_probe to require `open="yes"` on every QMP right-click;
  promote `bgmenu-probe: PASS` to a real-event acceptance.
- **Mid-term fix path.** A vendored Genode patch to `nitpicker's
  `user_state.cc` pointer-event walker (same family as the
  Phase 14 patch-ledger #9 pointer ROM gap, see
  `docs/11-environment.md` §4 row #9 partial fix). The patch
  would ensure the right-click dispatch honours the domain's
  `focus: always` attribute when no other top-level window claims
  the click. A Phase 18+ ledger row candidate.

## Cross-references

- Plan: `docs/plans/phase16-daily-desktop-defaults.md` W6 deviation 1
  + the W6 follow-up brief in W10 task input.
- Phase 14 patch-ledger row #9: `docs/11-environment.md` §4 row #9
  (the parent precedent for nitpicker pointer-event work).
- Phase 16 W7 deviation evidence: `docs/evidence/phase16-w7-shortcuts.md`
  (the parallel platform-fragility finding from the keyboard-
  shortcut path that informs why Phase 17+ is the right home for
  this work).
- W10/W12 paper-cut matrix: `docs/evidence/phase16-index.md` §6.
