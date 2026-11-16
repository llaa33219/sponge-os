# Sponge DE Design: User Scenarios

> **Breadcrumb:** [05 - Sponge DE Design](../05-sponge-de.md) / User Scenarios
>
> Full text of `docs/05-sponge-de.md` §5, the living user
> scenarios (kept separate from the frozen WM protocol contract of
> §4.6). Section numbers are kept unchanged; the parent keeps
> numbered stubs so existing `docs/05-sponge-de.md §5.4` style
> references still resolve.

---

## 5. User Scenarios

How Sponge DE should present itself to an everyday user:

### 5.1 Right After Boot

- A clean wallpaper and a minimal panel.
- Genode terminology (`init`, `nitpicker`, and the like) is not visible.
- The panel contains the launcher, the tasklist (one button per
  running app, dimmed when minimized, highlighted when focused),
  and the clock — nothing more.
- A system tray and additional applets arrive in a later release; the
  panel today is intentionally minimal.
- A "first-time user guide" is optional and not forced.

### 5.2 Launching an App

- Click the launcher icon on the panel, or press `Meta` to open the
  launcher.
- The app list is generated automatically from packages installed
  through `vct install`.
- One click runs the app. Internally:
  - Sponge DE asks `sponge_launcher` (or the integrated module).
  - The launcher asks `sponge_pkgd` for the app's component
    information.
  - `sponge_pkgd` asks `init` to start the component.

### 5.3 Panel context menu (right-click on the panel)

A right-click on the panel (anywhere outside the launcher's
existing popover) opens a `QMenu` with the live controls that
the everyday user actually touches. The menu is the
discoverability path for panel configuration; its *Settings*
entry opens (or focuses, if already open) the Panel provider's
settings window (§5.4) — the same provider that owns the
`panel.*` keys. The quick controls themselves change settings
live through the **quick-menu change lifecycle** (§5.4,
"Quick-menu change lifecycle"; W4 in
`docs/plans/phase16-daily-desktop-defaults.md`): each control's
value-change signal first ensures the Panel provider is running
via pkgd on the quick menu's own launch labels (the normal
`(package, settings-id)` path), then sends a closed domain
operation (for example `Panel height=40`, never an arbitrary
config key/value) over the panel quick-request / quick-result
pair. The provider validates the operation and performs the
write on its own config request label, and configd's
label-to-key-domain ACL limits that label to the `panel.*`
domain (D16.1 / D16.10). sponge-de itself holds no config write
channel, and the quick pair is not a config relay: its
vocabulary is closed and owned by the provider.

The menu is implemented in `PanelWidget::contextMenuEvent`
(`repos/sponge/src/sponge-de/panel/panel_widget.cc`); the menu
is anchored to the `QContextMenuEvent`'s global position (the
Genode QPA returns `(0,0)` from `QCursor::pos()` — known
Phase 10+ lesson — so the event-local position is the only
valid anchor). The forwarding runs through
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` (Phase 11
risk #2 codification), so the GUI-thread marshal is enforced at
compile time via the `QWidget::` setter signatures.

The dual nitpicker panel-domain topology (U16.2 / D16.2) is the
runtime backing for the Position item: two pre-built
`PanelWidget` instances (`panel_top`, `panel_bottom`) are routed
to `<domain name="panel_top">` and `<domain name="panel_bottom">`
by their distinct `label_last` Gui session suffixes ("Sponge
Panel" / "Sponge Panel Bottom"). The run scenario declares
both domains with non-overlapping `ypos` / `height` ranges
(`panel_top ypos=0 height=28`, `panel_bottom ypos=screen_h-28
height=28`). The `ConfigController`'s existing `panel.position`
poll drives the visibility toggle (no new timer) — the
matching-role widget `show()`'s itself, the other `hide()`'s.
The previously-Phase-14-W11-row-#6 paper-cut ("panel.position
is boot-time-only") is closed here.

The two pre-built widgets are gated on `<panel_bottom
source="dual"/>` in the sponge-de component config. Regression
scenarios that do not wire the second Gui session (sponge-de-
sel4-interactive, sponge-panel-config, sponge-panel-config-sel4)
omit this gate and continue to construct a single PanelWidget
in singleton mode (applyPosition no-op, panel always shows) —
the dual-domain code path is gated on the sibling pointer so the
regression scenarios remain byte-compatible.

- **Height**, a spin box slider in 4 px steps, range `16..128`,
  default `28`. Sourced from the existing live
  `panel.height` `sponge_configd` key (Phase 11); the menu sends
  the change through the quick-menu change lifecycle (§5.4):
  ensure-start of the Panel provider, then the closed
  `Panel height=<n>` operation over the panel quick pair. The
  provider writes the key on its own `panel_settings ->
  config_request` label (per-label key ACL per D16.1 in
  `docs/plans/phase16-daily-desktop-defaults.md`; sponge-de
  holds no config write channel).
- **Visible widgets**, three checkboxes for `clock`, `launcher`,
  and `tasklist`. Sourced from the `panel.visible_widgets` enum-
  list key; Phase 16 W2 extends the configd validator to accept
  the `tasklist` token (the panel-widget has parsed it locally
  since Phase 14 W7, but the configd side rejected writes that
  included it; D16.10).
- **Position**, a radio group with `Top` / `Bottom` enabled and
  `Left` / `Right` displayed-but-disabled with a "Phase 17+"
  tooltip (present implementation state; the 2026-11-10 scope
  update moved their target to Phase 16 Extension criterion 22,
  tooltip text and disabled state unchanged until then). The
  enabled pair maps to the live `panel.position`
  configd key and materialises through the **dual nitpicker panel
  domains** topology (D16.2 / U16.2):
  `<domain name="panel_top">` and `<domain name="panel_bottom">`,
  pre-declared in the run script with non-overlapping `ypos` /
  height ranges, each routed by `label_last` to the matching
  sponge-de `Gui` session (`Sponge Panel` / `Sponge Panel
  Bottom`). The `PanelWidget` toggles visibility of the two
  pre-built `QWidget`s on the existing 250 ms configd poll (no
  new timer). The previously-Phase-14-W11-row-#6 paper-cut
  ("panel.position is boot-time-only") is closed in Phase 16 W5.
- **Settings**, opens (or focuses, if already open) the Panel
  provider's settings window over the quick menu's own launch
  labels (the normal `(package, settings-id)` launch path via
  pkgd; §5.4 quick-menu change lifecycle; window identity
  `(sponge-de, panel)`).

All menu actions route through `QMetaObject::invokeMethod(...,
Qt::QueuedConnection)` (Phase 11 risk #2 codification), so the
GUI-thread marshal is enforced at compile time via the
`QWidget::` setter signatures.

### 5.4 Changing Settings (Settings control center)

Settings live in provider-owned windows, not in a universal
dialog. The shell that lists and launches them is the separate
on-demand `sponge_settings` component (decision D16.3 in
`docs/plans/phase16-daily-desktop-defaults.md`); the window that
changes a setting belongs to the provider that owns the key or
store. `vct config` on the CLI stays the intentional admin /
manual escape hatch (`AGENTS.md` §1.1 control door).

**Discovery.** Packages declare `0..N` settings entries in their
metadata (separate from the singular `<launcher>`; a package may
ship a launcher, settings entries, both, or neither). Stable
identity is `(package, settings-id)`, unique per package and
stable across reinstall. Each entry declares a title, a category
from a closed enum (`desktop`, `hardware`, `system`,
`applications`, `other`; unknown/absent → `other`), optional
icon/keywords, and a package-payload binary target. Entries must
not contain absolute
paths, shell commands, interpreters, or arbitrary Exec strings.
`sponge_pkgd` validates and resolves every entry and republishes
the installed set via installed metadata; `sponge_settings` holds
that ROM and refreshes on the ROM update signal (never polling),
grouping entries by category.

**Launch.** Clicking an entry sends only `package` + `settings-id`
over the shell's dedicated settings launch request/result labels;
`sponge_pkgd` starts the declared binary target as its own
component with its own start/lifecycle identity (settings targets
are never exclusive, even when they share the main app's binary).
The shell opens those channels only when its component config
carries `<settings_launch source="pkgd"/>`; without that opt-in the
listing remains usable and activation fails boundedly as
`package-manager-unavailable`.
Any package can join with no shell code changes and no central
allowlist; there is no in-process plugin or embedding path.

**Focus.** Window identity derives from `(package, settings-id)`.
A second activation of an already-open target focuses and raises
the existing singleton window; it is not an already-running
refusal and creates no duplicate.

**Provider-owned change.** The provider owns its UI, validation,
writes, lifecycle, and errors. First-party shared DE keys stay in
configd's closed typed registry, but every first-party provider
has a distinct config request label, and configd enforces a
label-to-key-domain allowlist on every set (the Panel provider
cannot write `background.*`; the rejection is a structured error
and the store is untouched). Required first-party providers:
Panel, Background, Theme, Shortcuts, Defaults, Display, WM,
Terminal. The historical tab surfaces split among their owners —
panel settings to the Panel provider, theme to Theme, background
to Background, shortcuts to Shortcuts, reset-to-defaults to
Defaults. Terminal settings belong to the terminal package;
Konsole or any other package ships its own provider the same way.
Third-party providers never receive shared-DE configd write
access; they validate and persist in a package-private store.
`sponge_pkgd` never relays config writes. sponge-de consumes
domain broadcasts but is not a settings writer: the universal
`de_config_request` / `SettingsController` writer is removed by
the landed W4 provider split and is not kept as a compatibility
path (D16.9).

**Quick-menu change lifecycle (cold start and change request).**
The panel and background quick menus change live settings
through their owning first-party providers, never through a
configd write channel of their own. This lifecycle is binding
for every quick-menu change, in the cold case (the provider is
not running) exactly as in the warm case:

1. **Ensure-start.** A quick-menu action first ensures the
   owning first-party settings target is running via
   `sponge_pkgd`, over the normal `(package, settings-id)`
   launch path. The quick menu has its own single-writer launch
   labels, separate from `sponge_settings`'s and `vct`'s. pkgd
   only starts (or focuses) the provider; it never carries the
   operation or its value.
2. **Closed domain operation.** Once the provider is live,
   sponge-de sends a closed domain operation (for example
   `Panel height=40`, not an arbitrary config key/value) over a
   dedicated per-provider quick-request / quick-result
   Report/ROM pair. Each pair's vocabulary is closed and owned
   by its provider. This is not a universal `SettingsController`
   and not an arbitrary config relay.
3. **Provider-side validation and write.** The provider
   validates the operation and performs the configd write on its
   own ACL-limited label (for example `panel_settings ->
   config_request`); configd's label-to-key-domain ACL still
   enforces the final write. The quick menu never gets a configd
   write channel. The provider answers on the quick-result label
   with a structured success or error. A start failure, a
   provider that is unavailable, or a validation failure is
   surfaced without hanging the DE and leaves the config store
   unchanged.
4. **Warm path and re-start.** If the provider is already
   running, the operation goes directly over its quick pair. If
   it has exited, step 1 repeats. Settings-target singleton /
   focus semantics are unchanged. Third-party packages get no
   quick-control channel: the quick pairs exist only for the
   named first-party domains (Panel, Background, and the other
   required providers).

Providers are not kept persistent just to serve quick edits, and
the menu never refuses a change because the provider is closed:
ensure-start is the designed cold path (W4 in
`docs/plans/phase16-daily-desktop-defaults.md`).

**Session opt-in gates.** Three explicit config declarations decide
which of these sessions exist at all. Each is a capability/routing
declaration the deployment owns and inspects (`AGENTS.md` §1.2),
not hidden global state: base answers a denied ROM or Report
session with "stop because parent denied" + `sleep_forever()` (the
`system_state` precedent, §3), so a component must never attempt a
settings session its topology does not route.

1. sponge-de's component config `<quick_settings source="providers"/>`
   gates QuickMenuController/quick provider session construction
   and attachment. Without the opt-in the controller is attached
   to no menu and no quick session is attempted.
2. `sponge_settings`'s component config
   `<settings_launch source="pkgd"/>` gates `settings_request` /
   `settings_result` channel opening. Without it the listing
   remains usable and activation fails boundedly as
   `package-manager-unavailable`.
3. The first-party provider package config `<settings quick="yes"/>`,
   supplied by `pkg/sponge-de/metadata.xml`, gates provider
   quick-pair sessions. Without it the provider is window-only and
   opens no quick session.

**Direct launch.** `vct settings list` / `vct settings open` share
the same backend (installed metadata + launch channel) with
separate single-writer labels and `--json` output. The panel and
background context-menu *Settings* entries open or raise their
owning provider over the quick menu's own launch labels, the
same normal `(package, settings-id)` launch path as the
quick-menu change lifecycle above (the *Settings* entry is the
ensure-start of that lifecycle with the window raised instead of
an operation sent).

**Privileges and failure containment.** `sponge_settings` holds
only the installed-metadata ROM, its own launch request/result
labels, and Gui/Timer sessions as needed. It has no configd
write channel, no package File_system session, no provider code
loading, and no GL-heavy requirement; closing it reclaims the
resources. A provider crash leaves the control center and sibling
providers alive, and the entry can be relaunched. With pkgd down,
opening a target fails fast with `package-manager-unavailable`
(a cached discovery list may still be shown; there is no hang or
retry loop). Removing a package deletes its entries on the next
metadata update; a stale open attempt returns `not-installed`.
Changes apply immediately, or the provider says clearly when a
restart is required.

### 5.5 Control for Advanced Users

- `vct leitzentrale` opens the Leitzentrale window for direct
  manipulation of the system component tree.
- In this release the only way to open Leitzentrale is the
  `vct leitzentrale` CLI command; a panel menu entry is deferred to a
  later release.

### 5.6 Phase 16 — Daily-Usable Desktop Defaults & Configuration

Phase 16 (2026-09-20 close-out, 10 of 12 criteria delivered; 2
delivered with honest disclosure) adds the everyday configuration
surface that turns the desktop into a daily driver. See
`docs/evidence/phase16-index.md` §2 for the per-criterion
verification table; the §4 regression sweep there records 34 / 36
fully PASS + 2 / 36 PARTIAL (the W6 deviation 1 + the W9 sibling
flakiness) + 0 FAIL.

The eight Phase 16 module / component changes:

1. **Settings control center + providers (W4 target; replaces the
   interim dialog).** The settings surface is the separate
   on-demand `sponge_settings` control-center component plus
   package-declared provider windows (identity `(package,
   settings-id)`), with `sponge_pkgd` validating and republishing
   entries, discovery on the installed-metadata ROM update signal
   (no polling), focus-on-second-open (window identity derives
   from `(package, settings-id)`), per-label configd key ACLs for
   shared DE keys, package-private stores for third-party
   providers, direct `vct settings list` / `vct settings open`
   (`--json`), the quick-menu change lifecycle of §5.4
   (ensure-start via pkgd + closed per-provider quick
   operations), and the failure/removal contract of §5.4. The
   required first-party providers are Panel, Background, Theme,
   Shortcuts, Defaults, Display, WM, and Terminal (Terminal
   settings belong to the terminal package; Konsole or any
   package provides its own). Status (2026-11-13): all six
   acceptance scenarios pass on base-sel4 after the session
   opt-in gate fixes (§5.4 "Session opt-in gates"):
   `run/sponge-settings-center.run`,
   `run/sponge-settings-providers.run` (including the per-ID
   `.visible_widgets` quick operation and the label ACL),
   `run/sponge-settings-thirdparty.run`,
   `run/sponge-settings-failure.run`,
   `run/sponge-settings-vct.run`, plus
   `run/sponge-settings-focus.run` (hint-driven second
   `focus_request`). Receipts, host suites, and the changed-target
   build results are in
   `docs/evidence/criterion18-settings.md`. Physical-hardware
   receipts (machine, parent commit, Genode pin, image/scenario,
   first paint + input-driven change) are still outstanding, so
   acceptance stays open (roadmap Criterion 18). The legacy dialog
   module `repos/sponge/src/sponge-de/settings/` and the universal
   writer `config/settings_controller.*` are removed in the landed
   W4 provider split (D16.9).
2. **`background/` module (W6).** A fullscreen frameless
   `QWidget` painted from `background.color` (hex-validated) or
   `background.image` (allowlist-validated; default
   `/system/background/default.png`). `contextMenuEvent` opens
   a three-item `QMenu` (Settings / Launch / Show desktop); the
   show-desktop toggle reuses the Phase 14 W7 panel tasklist
   state machine (U16.3). Acceptance scenario:
   `run/sponge-de-bgmenu.run` →
   `bgmenu-probe: PASS` (structural `open="ready"`; the
   per-event `open="yes"` is timing-sensitive on the Genode QPA
   — see the W6 follow-up note in
   `docs/evidence/phase16-w6-bgmenu-followup.md` + the
   Phase 17+ minimal fix recipe).
3. **Panel context menu + dual-domain topology (W5).** Right-click
   on the panel opens the QMenu (Height spinbox 16..128 step 4;
   Visible widgets checkboxes clock/launcher/tasklist; Position
   radio group top/bottom enabled, left/right disabled with
   "Phase 17+" tooltip per D16.2 (unchanged; target moved to Phase
   16 Extension criterion 22 per the 2026-11-10 scope update);
   Settings entry). The height / widgets / position edits travel
   the quick-menu change lifecycle (§5.4). Position
   becomes LIVE via dual nitpicker panel domains
   `panel_top` / `panel_bottom` per U16.2 + D16.2; closes the
   Phase 14 W11 row #6 / #14 paper-cut (`panel.position` was
   boot-time-only). Acceptance scenario:
   `run/sponge-panel-menu.run` →
   `panel-menu-probe: PASS` (position-switch pixel assertion:
   bottom (512, 740) vs top (512, 14)).
4. **Keyboard shortcut framework (W7).** Extensible via the
   `shortcuts.bindings` configd key (structured
   `action<keycode-list>` value; closed `launcher` /
   `focus_next` / `dismiss` action enum). Initial shipped
   bindings: Super → launcher, Alt-Tab → focus cycle forward,
   Escape → dismiss topmost popover. Acceptance scenarios:
   `run/sponge-de-shortcuts.run` (`shortcuts-probe: PASS`) +
   `run/sponge-de-shortcuts-extend.run`
   (`shortcuts-extend-probe: PASS`).
5. **Multi-panel (W8).** Arbitrary panel count on arbitrary edges
   via `PanelCollection`; per-panel `panel.<id>.{height,position,
   visible_widgets}` pattern keys (the W2 pattern-key registry
   infrastructure). Distinct Gui session label suffix per instance
   (F5 nitpicker `label_prefix` trap defense). Acceptance
   scenario: `run/sponge-de-multipanel.run` →
   `multipanel-probe: PASS` (cross-panel click assertions live)
   + `run/sponge-de-multipanel-idspace.run`
   (`pattern-keys-probe: idspace = PASS`).
6. **Mouse resize hookup (W9).** `<resizeable="yes"/>` package-
   metadata opt-in (textedit, files, calculator, terminal —
   NOT falkon in Phase 16). `sponge_pkgd` carries the flag to
   the layouter `<assign>` block. Qt6 QPA already resizes on
   Gui window resize (verified; no app-side shim). Sibling
   acceptance: `run/sponge-de-themed-chrome-resize.run` (each
   of 8 zones individually proven on the vendored themed
   decorator + minimizer patch — the same patch closes the
   Phase 14 D14.8(d) `<minimizer/>` deferral; ledger row #17).
   Criterion-9 headline: `run/sponge-de-release-resize.run` on
   the motif decorator (release-media topology) →
   `release-resize-probe: PASS` (3x consecutive independent
   runs).
7. **Registry re-architecture (W2).** `sponge_configd` closed
   registry widened: `MAX_KEYS = 16 → 32` (interim; pattern
   keys in a separate array), `MAX_PATTERN_KEYS = 32` array
   for `panel.<id>.{height,position,visible_widgets}` with
   charset validator `[a-z0-9_-]{1,16}`, four new flat keys
   (`background.color` / `background.image` /
   `shortcuts.bindings` / `panel.ids`), and the
   `panel.visible_widgets` enum-list extended
   `{clock, launcher} → {clock, launcher, tasklist}`. Acceptance
   scenarios: `run/sponge-configd-pattern-keys.run` +
   `run/sponge-configd-shortcuts.run` +
   `run/sponge-configd-badkey.run` (validator parity).
8. **Bake wiring (W3).** All six product scenarios
   (`sponge-alpha.run` + the 5 desktop-disk variants) ship with
   `<bake/>` + bake ROM routes + the `bin/bake/*` boot modules;
   `run/sponge-desktop-defaults-firstboot.run` is the first-boot
   acceptance scenario on the disk-desktop image. The
   `alpha_probe` (`repos/sponge/src/test/alpha_probe/main.cc`)
   extended the launcher-set assertion to all 7 desktop
   packages' `{name, category}` pairs and added the 8 baked-
   key configd-broadcast assertions.

The base Phase 16 vendored budget (**one new row #17**, themed
decorator sizer + minimizer; `docs/11-environment.md` §4) is
historical, not a cap on Extension work (D16.8). No vendored
patch is absorbed silently: any additional patch required by
Extension work must be individually justified, recorded in the
ledger before application, independently revertable, and
re-audited at the final Criterion 27 cleanup; Sponge-owned
components and public interfaces (especially the custom WM) are
preferred over vendored changes. No specific new patch is claimed
required or approved. The
honest limitations register (Phase 17+ carry-overs) lives in
`docs/evidence/phase16-index.md` §6.

> **Scope update (2026-11-10, user-directed; revised 2026-11-11; WM
> status synced 2026-11-12).**
> The Phase 16 Extension gained twelve further completion criteria
> (`docs/09-roadmap.md` §10, Criteria 16-27). "Settled" statements
> below describe the current upstream baseline: the custom Sponge
> WM (criterion 16) is delivered and verified (receipts:
> `docs/evidence/phase16-wm-index.md`), and the left/right panel
> positions' target moved to Phase 16 Extension (criterion 22).
> Nothing in the current UI has changed yet: the left/right entries
> stay disabled with their "Phase 17+" tooltip until that criterion
> is implemented.
