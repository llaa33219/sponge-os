# Phase 16 Scope Expansion

> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 16 (continues [phase16-extension.md](phase16-extension.md)).

**Scope expansion (2026-11-10, user-directed): twelve further
completion criteria.** The user directed a second expansion of this
Extension: the deferred custom Sponge WM is pulled forward, ten
further user-requested items join it, and a final patch-cleanup gate
closes the phase, numbered **Criterion 16 through Criterion 27**,
all unchecked at registration (Criterion 16 since delivered, see its
receipts). The new real-hardware gates **strengthen, not
invalidate,** the prior QEMU/base-linux receipts for base criteria 3
(panel context menu) and 11 (background image change): those
receipts remain the recorded evidence of what they actually
verified. Criterion 18 **reopens and replaces** base criterion 2's
architecture instead: the old per-tab receipts are historical
evidence of the replaced monolithic implementation and do **not**
satisfy the modular provider-owned control-center target, so base
criterion 2 no longer counts the old five-tab design as final
success. **Physical-hardware closure for the physical-hardware rows
(Criteria 17-22 and 24-26) needs a physical-machine receipt**
carrying machine identity, parent commit, Genode pin, image/scenario,
and observed interaction; QEMU runs and undocumented field reports
cannot close those rows. Criterion 27 (final Genode/seL4 patch
cleanup) is deliberately **not** labeled physical-hardware wholesale:
it carries row-specific hardware gates (a hardware-anchored patch
needs physical-hardware A/B before its removal). Existing
limitations stay true until implementation lands. Execution order
(user-directed 2026-11-12): Criteria 16 and 18 are worked
continuously to full delivery, Criterion 16 first and complete,
then Criterion 18, each landing as logical commits.

Twelve new completion criteria:

- [x] **Criterion 16: custom Sponge WM (pulled forward from the
  former long-term deferral; scope registered 2026-11-12; delivered
  and verified 2026-11-12).** Deliver
  a Sponge-native window manager under `repos/sponge/src/` using
  public Genode interfaces only, without forking or patching Genode's
  `server/wm`, preserving capability-based IPC. The delivered scope
  is **user-directed floating plus Master-Stack tiling** and **native
  modern window chrome**:
  - **Layout modes.** One global key `wm.layout.mode`
    (`floating` | `tiling`, default `floating`), typed in
    `sponge_configd` with the `wm.*` key domain reserved for the
    later `wm_settings` provider (Criterion 18); this criterion adds
    no Settings UI of its own. Floating is user-directed placement
    and 8-zone resize. Tiling is Master-Stack: the first
    non-minimized, non-maximized window in creation order takes the
    left half of the work area as master, the remaining windows
    split the right half as stack in creation order, and the
    arrangement reflows on open, close, minimize, restore, maximize,
    and mode switch.
  - **Window state rules.** Non-resizeable windows always float and
    never enter the tiling arrangement. Maximized fills the work
    area (screen minus panel) and the toggle restores the saved
    previous geometry. Minimized parks off screen and is reported at
    the existing <=-32000 convention, so the panel
    `TasklistController` needs no change.
  - **Native modern chrome in the first delivered WM version.** The
    native path draws a native modern title bar with native
    close/minimize/fullscreen buttons, supplied by the new
    `sponge_decorator` and theme-driven (AGENTS.md §3.4);
    upstream `themed_decorator` plays no role in the native path.
    Honest v1 note: maximize and fullscreen both mean work-area
    maximization in the initial version.
  - **Three components, one vertical slice.**
    `repos/sponge/src/sponge_wm` (Gui virtualization, window
    registry, input, native content association),
    `repos/sponge/src/sponge_layouter` (policy, state, layout), and
    `repos/sponge/src/sponge_decorator` (lightweight modern
    theme-driven chrome); public Genode interfaces only, no private
    gems headers, no vendored patches, and no new
    `docs/11-environment.md` §4 ledger row (AGENTS.md §1.4 rules out
    a monolith here). The native core replaces `server/wm` in the
    same delivered slice as the native chrome on purpose: a
    decorator's association with windows belongs to upstream's
    private decorator association semantics, so native chrome bolted
    onto upstream `wm` would depend on exactly the private semantics
    this criterion forbids.
  - **Frozen compatibility surface.** The `TasklistController`
    contract stays frozen at protocol v1: `window_list`,
    `window_layout`, `focus_request`, and `rules` keep their current
    shapes and meanings, and single-writer discipline holds (the
    tasklist stays the sole writer of `focus_request` and `rules`;
    the native components produce `window_list` and `window_layout`).
    Protocol v1 is documented and frozen in `docs/05-sponge-de.md`
    §4.6 before acceptance runs.
  - **Fallback and parity.** The upstream `wm` + `window_layouter` +
    `decorator` trio stays untouched and keeps running as the
    fallback, with `run/sponge-wm{,-qmp,-tasks}.run` as its anchors,
    until native parity. Parity means: the native product scenarios
    pass with the upstream trio absent from the topology, and the
    upstream anchors stay green. New scenarios use
    `run/sponge-wm-native*.run` names; no existing `run/sponge-wm*.run`
    name is renamed or repurposed.
  - **Acceptance.** QMP-driven scenarios cover move, 8-zone resize,
    focus, close, minimize/restore with focus, maximize/restore, and
    tiling open/close/mode/reflow; plus a >=20-cycle rules-drift
    soak, a no-new-ledger-row check, measured UX metrics per
    AGENTS.md §5.1, and the plan's doc/evidence obligations. No
    separate physical-hardware gate is added.
  - **Non-goals.** Workspaces, window gaps, snap layouts, animations,
    per-window rules, and master-ratio configuration stay out of
    this criterion.
  Decision-complete plan: `docs/plans/phase16-sponge-wm.md`; state
  transitions: `docs/plans/wm-state-table.md`. This remains distinct
  from the upstream `themed_decorator` drop-in (Phase 11 history) and
  from the existing `run/sponge-wm*.run` scenarios, which drive the
  upstream stack.
  - **Delivered and verified (2026-11-12, commit `191222130e`).**
    Receipts: `docs/evidence/phase16-wm-index.md`. Host suites PASS
    (protocol 209, core registry 123, layouter 144, decorator 142
    checks); base-linux native smoke PASS (incl. the non-resizeable
    fixture and same-wid destroy/recreate with a fresh close); seL4
    native QMP PASS (full move/8-zone/focus/minimize-restore/
    maximize-restore/close battery, plus decorator focus evidence
    `chrome view <2,4> focus no (2 unfocused flip(s))`); native tasks
    PASS; full 20-cycle tiling PASS with the non-resizeable fixture
    floating at (850,560)160x160 and the remaining client at master
    (8,56)496x704. Native product `SPONGE_WM=native` alpha and
    desktop-disk PASS with the upstream trio absent; default upstream
    alpha and desktop-disk PASS; upstream anchors base-linux
    `sponge-wm`, seL4 `sponge-wm-qmp`, seL4 `sponge-wm-tasks` all
    PASS. Rules soak exactly 20 cycles, no drift. UX metrics at
    target: minimize/restore/close/maximize/restore-max/focus each
    1 click, move/resize each 1 gesture, switch to tiling 1 command
    via the configd backend (same as `vct config set wm.layout.mode
    tiling`). `docs/11-environment.md` §4 remains 38 rows with no
    `genode/` changes. No native partials (the W9 themed-path
    flakiness is historical and outside the native path); the
    physical-hardware gate is a non-goal. Receipt commits:
    `3501970d83`/`2ec697239f` (product switch), `5a93452a22`/
    `47f5b8d92c`/`f1c6ec0ea8` (focus metadata), `d0b2687976`/
    `1fac8b46a6` (lifecycle), `22f62d52c9` (QMP runtime),
    `191222130e` (recreation), `73e0874cd9` (non-resizeable tiling),
    `4b2163d5f8` (upstream anchors), plus the load-bearing fixes
    `ccfef79cf1` (two-way maximize inference) and `afb9d50980`
    (qt6 stale-frame cancel before resize).
- [ ] **Criterion 17: real-hardware DE panel configuration edits.**
  Change panel settings through the DE on a physical machine and
  observe both the live result and the `sponge_configd` value.
  Acceptance is on physical hardware.
- [ ] **Criterion 18: provider-owned Settings control center
  (reopens and replaces base criterion 2's architecture).** The
  monolithic five-tab Settings is replaced by a modular,
  provider-owned control center. Binding target decisions:
  - **Shell component.** A separate, on-demand `sponge_settings`
    Genode component is the control center shell. It carries **no
    configd write channel, no package `File_system` session, no
    provider code loading, and no GL-heavy requirement**, and its
    resources are reclaimed when it closes.
  - **Package-declared entries.** A package declares **0..N
    settings entries**; each entry carries a stable
    `(package, settings-id)` identity, a title, a closed category
    enum (`desktop`, `hardware`, `system`, `applications`,
    `other`; an absent or unknown category falls back to `other`),
    an optional icon and keywords, and a
    **package-payload binary** as its provider. No arbitrary
    command strings, no absolute paths, no interpreter launches.
    `sponge_pkgd` validates declarations, publishes the installed
    metadata, and resolves provider launches.
  - **Discovery and openness.** The Settings shell holds only the
    installed metadata plus its own launch request/result labels.
    Discovery is event-driven off the pkgd installed-set updates.
    Any package can join the control center without changes to
    Settings. No plugin loading and no embedded provider code.
  - **Lifecycle.** Each settings entry gets its own start (a
    distinct lifecycle per entry); opening an entry a second time
    focuses the existing singleton window instead of spawning a
    duplicate or refusing. The provider owns its UI, validation,
    writes, lifecycle, and error reporting.
  - **Configuration authority.** Shared DE keys stay typed in
    `sponge_configd` behind per-provider request labels with
    configd-enforced per-label key-domain ACLs. Third-party
    packages never write shared DE keys and use package-private
    stores instead. The universal `SettingsController` /
    `de_config_request` channel is removed; `sponge_pkgd` never
    relays configuration writes; `vct config` remains the admin
    manual path.
  - **Provider set and real-hardware acceptance matrix.**
    First-party providers cover Panel, Background, Theme,
    Shortcuts, Defaults, Display, WM, and Terminal settings;
    arbitrary packages supply their own. Acceptance on physical
    hardware: a third-party package with **two** entries is
    discovered **without a reboot**; each provider reaches first
    paint and open; a private setting persists across provider
    restart **and** one reboot; a first-party shared-key change
    has a live effect and an outside-domain write is rejected;
    the second open focuses; a provider crash is contained and
    relaunched; pkgd-down fails fast; package removal leaves
    stale opens handled; direct `vct settings list` / `open`
    works; a control-center crash leaves the DE alive. Physical
    receipts record machine, parent commit, Genode pin,
    image/scenario, first paint, and an input-driven change.
  - **Status (2026-11-13: implementation and QEMU verification,
    not acceptance).** The provider-owned architecture is
    implemented on top of parent `b8f46f848f` (the Criterion 16
    baseline; the change set is uncommitted and no further commit
    IDs are claimed here) and verified on base-sel4 under QEMU
    after the session opt-in gate fixes: sponge-de's
    `<quick_settings source="providers"/>`, `sponge_settings`'s
    `<settings_launch source="pkgd"/>`, and the first-party
    provider package config `<settings quick="yes"/>` supplied by
    `pkg/sponge-de/metadata.xml` (explicit capability/routing
    declarations, `docs/05-sponge-de.md` §5.4 +
    `docs/12-package-format.md` §14.8). All six acceptance
    scenarios pass after the fixes:
    `run/sponge-settings-center.run`,
    `run/sponge-settings-providers.run` (including the per-ID
    `.visible_widgets` quick operation and the per-label ACL),
    `run/sponge-settings-thirdparty.run`,
    `run/sponge-settings-failure.run` (its verdict rides the
    terminal gates and post-count assertions, not per-step
    intermediate gates), `run/sponge-settings-vct.run`, and the
    extra `run/sponge-settings-focus.run` (hint-driven second
    `focus_request`), plus `run/sponge-wm-tasks.run` and
    `run/sponge-configd-persist.run` PASS. Host self-tests green:
    runtime validator 92, configd ACL 113, `sponge_settings` 75,
    vct settings 63, provider logic 75 + structural PASS,
    `settings_demo` all PASS, pkgd settings self-test PASS. Final
    changed-target build PASS for `sponge-de`, `sponge_settings`,
    `sponge_pkgd`, `sponge_configd`, `vct`, all eight providers,
    both demo providers, and the five settings probes. The
    migrated regressions (`sponge-panel-menu`, `sponge-de-multipanel`,
    `sponge-de-shortcuts`, `sponge-de-shortcuts-extend`,
    `sponge-de-bgmenu`, `sponge-de-bgimage`,
    `sponge-de-bgimage-badpath`, `sponge-alpha`) all PASS. The legacy
    dialog module and the universal `SettingsController` writer are
    removed from the tree (D16.9). Coverage gap: the providers probe
    verifies the exact quick wire protocol, but an independent
    click-through PanelWidget -> QuickMenuController -> provider UI
    runtime test has not been demonstrated (attempted 2026-11-14 and
    blocked; see the addendum below). Receipts:
    `docs/evidence/criterion18-settings.md`. **Acceptance is not
    claimed:** physical-hardware acceptance and the complete physical
    matrix above (including one-reboot persistence and the
    control-center crash on hardware) have not run, so this criterion
    remains open rather than accepted. Known seam: the Display and
    Terminal providers ship with an explicit typed-key registration
    seam (`display.*` / `terminal.*` typed keys pending configd
    registration).
  - **Addendum (2026-11-14: the real QMenu surface is BLOCKED).**
    No regression is being hidden: all six settings acceptance
    scenarios (center, providers, thirdparty, failure, vct, focus)
    still pass, the host suites and the changed-target build pass,
    and product Alpha/BIOS-disk/UEFI-USB validation passes as
    previously documented. The provider wire protocol and product
    integration are verified, but real QMenu interaction is not.
    That click-through, `run/sponge-settings-quickmenu.run`, is a
    red repro, not a passing acceptance test: Phase B (panel QMenu
    open) fails 3/3 with no menu pixels and no quick lifecycle.
    The corrected right-click does reach the real
    `PanelWidget::contextMenuEvent` (the original HMP
    `mouse_button 1` delivered BTN_LEFT; the correct BTN_RIGHT mask
    is `mouse_button 2`), but the mask fix alone does not make the
    UI work. With the upstream-wm path the forced direct-domain
    resize is removed (final popup geometry `512,509 103x203`), yet
    the run fails with `attempt to create ID for invalid view` and
    `failed to create child view for invalid parent view`, because
    the direct-nitpicker Background parent cannot be associated by
    wm; the direct-nitpicker path shows no visible menu either.
    Layer, parent, and Qt::Window identity trials failed and were
    reverted, `qnpw_verbose` is restored, and no debug
    instrumentation remains. The PanelWidget/BackgroundWidget QMenu
    lifecycle is therefore unproven and blocked with no minimal
    justified fix left: product quick-settings/provider wiring
    exists, but the QMenu surface is not accepted and remains
    non-functional, so nothing here should be read as the
    quick-menu UX working. Criterion 18 remains open with this
    structural blocker in addition to the physical-hardware matrix,
    one-reboot package-private persistence, and the hardware
    control-center crash check. Full evidence:
    `docs/evidence/criterion18-settings.md`.
- [ ] **Criterion 19: real-hardware panel context menu.** A
  physical right-click opens the panel menu and the detailed panel
  edits (height, visible widgets, position) take effect. Acceptance
  is on physical hardware.
- [ ] **Criterion 20: program icons.** Add per-program icon
  assets/metadata and render them in the DE program/launcher
  presentation, verified on physical hardware. The current
  presentation is text-only; this criterion adds the icon layer
  (the text-only state is the present truth, not a regression).
- [ ] **Criterion 21: KDE Konsole package.** New port/package plus
  DE integration, verified on physical hardware by launch, terminal
  input, and command execution. Genode 26.08 has no existing
  Konsole/KDE Frameworks port, so this is new porting work, not
  enabling an existing package. The required KDE Frameworks
  dependency work is part of this new porting effort.
- [ ] **Criterion 22: panel top/bottom/left/right positions.**
  Enable and verify all four panel positions on physical hardware.
  Top/bottom already have QEMU receipts (Phase 16 W5); left/right
  are pulled forward from Phase 17+ and currently remain disabled
  until this criterion is implemented.
- [ ] **Criterion 23: GPU-acceleration test package.** Add a test
  package and scenario that reports the actual renderer/backend and
  distinguishes hardware acceleration from Mesa softpipe, so a
  fallback path can never be reported as GPU PASS. The package
  integration scenario is required, but a physical GPU PASS is not
  implied by this criterion. The existing `intel_gpu` driver
  package is builds-only and proves nothing about acceleration;
  this criterion supplies the test.
- [ ] **Criterion 24: default background image plus DE-driven
  change.** Ship a default background image and change it through
  the owning background provider's settings UI (Criterion 18
  architecture) on physical hardware, observing the repaint and
  the `sponge_configd` value. This is a stronger
  physical-hardware gate for existing base criterion 11; arbitrary
  user-upload/file-picker scope stays Phase 17+.
- [ ] **Criterion 25: basic touchpad support.** Implement and
  verify pointer movement and primary click on a physical machine
  without requiring a USB mouse; record the machine and driver
  path. The current trackpad/i2c_hid gaps stay valid until this
  criterion is delivered.
- [ ] **Criterion 26: image-editor package.** Add an image editor
  that opens an image, performs a basic edit, and saves/exports the
  result, verified on physical hardware. The specific third-party
  choice is left to implementation.
- [ ] **Criterion 27: final Genode/seL4 patch cleanup (the
  absolute final Phase 16 gate).** This criterion cannot start
  until **every other Phase 16 criterion and feature is
  complete**, including Criterion 16 (custom Sponge WM) and the
  real-hardware rows (Criteria 17-22 and 24-26), and Phase 17
  cannot start before it closes. It re-audits **all** Genode/seL4
  patches present at that time plus every unledgered fork commit
  and ends with an enforceable removed-with-receipts or
  kept-with-positive-reason verdict per row. Gates:
  - **Complete scope, fresh audit.** The audit covers every
    ledger row existing at execution time and every unledgered
    fork commit. The ledger stands at **38 rows** while the
    existing classification (`docs/evidence/patch-classification.md`)
    covers only rows **1-33**; rows **#34-#38 and every later row
    or commit** must be classified before any pruning. The
    existing 1-33 classification and its P4.3 no-pruning closeout
    neither authorize nor replace this audit.
  - **Grounds for removal.** A patch may be removed only when (a)
    a replacement landed that makes it unnecessary, (b) a
    completed A/B proves no performance, feature, or stability
    impact when the patch is absent, or (c) evidence shows it
    should never have existed. No current row is predeclared
    removable.
  - **Anti-paperwork disposition.** Every row must end
    **removed-with-receipts** or **kept-with-a-positive-reason**.
    "Not investigated", "probably unnecessary", or a generic
    "keep" is **FAILURE**. Any row whose Drop-When is met or
    plausibly met, or whose removal is claimed to have no impact,
    requires an actual A/B attempt. Zero removals is a valid
    outcome **only after this complete audit**; there is no
    removal quota (a patch is never removed just to make the tree
    look clean), and doc-only ledger consolidation is not a code
    removal.
  - **Cluster and pair discipline.** Classify with the
    keep/remove/replace scheme, each row's Drop-When, and the
    Part C cluster/pair ordering of
    `docs/evidence/patch-classification.md`. Unledgered
    dependencies such as `73e5859da8` are in scope; paired/shared
    commits retire together; never remove one side of a required
    pair.
  - **A/B per candidate.** Each removal candidate gets a
    same-kernel, same-board A/B on its named scenario, including
    the original stress/load case and the measured performance
    metric where applicable. Hardware-anchored patches require
    physical-hardware A/B; QEMU evidence cannot authorize their
    removal. The e1000 / `run/sponge-pkg-driver` DHCP gate is
    preserved as a rejection gate.
  - **seL4 port patches.** Removing a seL4 port patch requires a
    fresh `prepare_port sel4`, the `sel4.hash` restamp, and a
    kernel rebuild as applicable.
  - **Removal protocol.** Every removal is ledger-first: update
    the evidence and the ledger row, then a solo fork
    revert/patch commit, then a solo submodule pin commit; each
    removal builds, boots, and reverts independently.
  - **Verification after each removal.** `./tool/build verify`
    and the full serial regression run after **each** removal; a
    final full product sweep runs after the last removal, **and
    also once if the audit ends with zero removals**. Documented
    partials stay documented and are never relabeled green.
  - **Evidence at execution time.** The patch-classification and
    environment ledger evidence is updated at execution time,
    not by this criterion's registration here.
