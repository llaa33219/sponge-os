# Criterion 18 evidence: provider-owned Settings control center (implementation and QEMU verification; QMenu surface BLOCKED)

> Status: the provider-owned architecture is implemented and
> verified on base-sel4 under QEMU (2026-11-13), and the real
> QMenu / quick-menu UI surface is **BLOCKED** (2026-11-14, see
> "Blocked: the real QMenu / quick-menu UI surface" below). Every
> result below is post-fix: it was taken after the three session
> opt-in gates landed. Criterion 18 stays **open**, not accepted:
> physical-hardware acceptance and the complete physical matrix
> have not run, and the QMenu surface carries a structural
> blocker. This note records what is proven and what is not.

## Run context

- **Machine / virtualization**: base-sel4 kernel, QEMU (the standard
  `run/` scenario vehicle, q35/Skylake-Client, QMP input). No
  physical machine is involved in any receipt below.
- **Parent commit**: `b8f46f848f` ("docs(wm): refresh native
  acceptance evidence"), the Criterion 16 baseline. The Criterion 18
  change set (the provider split, `sponge_settings`, first-party and
  third-party providers, pkgd/configd/vct settings work, the three
  session opt-in gates below, and the new scenarios) sits in the
  working tree on top of that parent, uncommitted. Nothing here
  claims commit IDs beyond that parent.
- **Genode pin**: `genode/` submodule at
  `36b50a65000270376cae56df833a9f0967168fd5`
  (`26.08-43-g36b50a6500`, the `sponge-os-genode` fork pin recorded
  in `docs/11-environment.md` §3; Genode 26.08 upstream
  `d18ee9e917200b059549e21f2dd9a55bfe61a0eb`).
- **Removed in this tree**: the legacy five-tab dialog module
  `repos/sponge/src/sponge-de/settings/` and the universal writer
  `config/settings_controller.*` (`de_config_request`), with their
  retired scenarios `run/sponge-de-settings.run` and
  `run/sponge-de-settings-regression.run` (D16.9).

## Session opt-in gates

Three explicit config declarations decide which settings sessions
exist at all. Each is a capability/routing declaration that the
deployment owns and can inspect (`AGENTS.md` §1.2), not hidden global
state. Base answers a denied ROM or Report session with "stop because
parent denied" + `sleep_forever()`, so a component must never attempt
a settings session its topology does not route. The three gates are:

- **sponge-de component config `<quick_settings source="providers"/>`**
  gates QuickMenuController/quick provider session construction and
  attachment. Without that opt-in the controller is attached to no
  menu and no quick session is attempted.
- **`sponge_settings` component config `<settings_launch source="pkgd"/>`**
  gates `settings_request`/`settings_result` channel opening. Without
  it the listing remains usable and activation fails boundedly as
  `package-manager-unavailable`.
- **First-party provider package config `<settings quick="yes"/>`**
  gates provider quick-pair sessions and is supplied by
  `pkg/sponge-de/metadata.xml`. Without it the provider is
  window-only and opens no quick session.

Design homes: `docs/05-sponge-de.md` §5.4 and
`docs/12-package-format.md` §14.8.

## Scenario results (base-sel4, QEMU, post-fix)

All six acceptance scenarios pass after the fixes:

| Scenario | Result | What it proves |
|---|---|---|
| `run/sponge-settings-center.run` | PASS | Event-driven discovery lists both third-party entries (`greeting`, `about`); each open starts exactly once; the second open focuses with no duplicate start; `settings_focus_hint` seq 1 emitted. |
| `run/sponge-settings-providers.run` | PASS | Cold quick-menu action ensure-starts the Panel provider over the launcher pair; the closed `Panel height=40` quick request lands with the `panel.height=40` configd broadcast; the per-ID widgets quick operation (`op="widgets" id="alpha"`) lands as `panel.alpha.visible_widgets=clock,launcher`; a settings entry ensure-starts via `settings_request`; the per-label ACL refuses an outside-domain write (`theme_settings -> panel.height=99`) with `key-outside-domain` and the store stays byte-identical. |
| `run/sponge-settings-thirdparty.run` | PASS | Two independent `settings_demo` entries discovered and first-painted; keystroke validation rejects the bad character; values persist as exact per-provider store XML across provider close/restart (close-commit); removal answers `not-installed`; reinstall answers `started` with un-aliased stores; package-private `File_system` writable; zero shared-DE configd routes or capabilities for third-party providers. |
| `run/sponge-settings-failure.run` | PASS | Provider exit observed via the explicit `exited` attribute (claim release with decoded code); a later open starts fresh (two `started`, no false `focused`); an absent child is never an exit; sibling providers and the shell survive the exit; stale open after removal answers `not-installed`; a caller without a live pkgd response fails fast with `package-manager-unavailable`. The verdict rides the scenario's terminal gates (probe `choreography complete`; the late caller's `package-manager-unavailable`) plus its post-count assertions over the accumulated output (exactly one claim release for one explicit exit, zero sibling closes/releases, and the per-entry result counts in this row); the older per-step intermediate gates are not asserted and are not claimed to remain. |
| `run/sponge-settings-vct.run` | PASS | Real `vct` end-to-end on base-sel4: install; `settings list` (human + `--json`) carrying `greeting`/`about` with categories and ids; `settings open settings_demo greeting` answers `started` once then `focused` twice with no duplicate provider start; an unknown id answers `not-installed` on both renderings; serialized single-writer sequencing. |
| `run/sponge-settings-focus.run` | PASS | Second activation routes the hint-driven second `focus_request` (pkgd `settings_focus_hint` drives the probe's `focus_request`) and the existing singleton window is focused instead of spawning a duplicate. |

The seventh scenario of the family, `run/sponge-settings-quickmenu.run`,
is the real click-through quick-menu test. It is a **red repro**, not
a passing acceptance test (see the blocked-surface section below).
All six scenarios above still pass alongside it, and no regression
is being hidden.

Regression anchors alongside the change set:

| Scenario | Result | What it proves |
|---|---|---|
| `run/sponge-wm-tasks.run` | PASS | WM task-lifecycle probe stays green alongside the settings change set (regression anchor). |
| `run/sponge-configd-persist.run` | PASS | Typed configd registry persistence stays green with the per-label ACL write model in place. |

## Host self-tests (run on the host, all green)

| Suite | Result |
|---|---|
| Runtime validator self-test (`lib/src/sponge_runtime_config_validate/self_test.cc`) | 92 |
| configd ACL tests (`src/sponge_configd/acl_tests.cc`) | 113 |
| `sponge_settings` self-test (`src/sponge_settings/tests/`) | 75 |
| vct settings self-test (`src/vct/tests/self_test.cc`) | 63 |
| Provider logic tests (`src/settings_providers/tests/sp_logic_tests.cc`) | 75 + structural PASS (`structural_checks.sh`) |
| `settings_demo` host self-test (`src/settings_demo/tests/`) | all PASS |
| pkgd settings self-test (`src/sponge_pkgd/self_test.cc`, `<settings>` rules) | PASS |

## Build results

The final changed-target build passes for `sponge-de`,
`sponge_settings`, `sponge_pkgd`, `sponge_configd`, `vct`, all eight
providers (`panel_settings`, `background_settings`, `theme_settings`,
`shortcuts_settings`, `defaults_settings`, `display_settings`,
`wm_settings`, `terminal_settings`), both demo providers
(`settings_demo_greeting`, `settings_demo_about`), and the five
settings probes (`settings_providers_probe`,
`settings_thirdparty_probe`, `settings_failure_probe`,
`settings_vct_probe`, `settings_focus_probe`).

## Product validation (as previously documented)

Product Alpha / BIOS-disk / UEFI-USB validation passes as
previously documented: `run/sponge-alpha.run` (Alpha),
`run/sponge-desktop-disk.run` (BIOS disk), and
`run/sponge-desktop-disk-uefi-usb.run` (UEFI USB). No new product
receipt is claimed here, and these product boots stand apart from
the QMenu surface blocker below.

## Migrated regressions (all PASS)

The scenarios that previously anchored the dialog-era behavior were
migrated onto the provider-owned entry points and re-run green:
`sponge-panel-menu`, `sponge-de-multipanel`, `sponge-de-shortcuts`,
`sponge-de-shortcuts-extend`, `sponge-de-bgmenu`, `sponge-de-bgimage`,
`sponge-de-bgimage-badpath`, `sponge-alpha`.

## Blocked: the real QMenu / quick-menu UI surface (2026-11-14)

First, the precise disclosure: the provider wire protocol and
product integration are verified, but real QMenu interaction is
not. The click-through acceptance scenario
`run/sponge-settings-quickmenu.run` remains a **red repro**, not a
passing acceptance test, and the PanelWidget / BackgroundWidget
QMenu lifecycle is unproven and **BLOCKED**. The evidence chain:

- **Right-click delivery is corrected and proven.** The original
  HMP right-click in the quickmenu choreography pressed
  `mouse_button 1`, which QEMU's HMP maps to BTN_LEFT (the
  `hmp_mouse_button` bitmask is 1=L, 2=R, 4=M). A QPA diagnostic
  showed that press reaching the panel platform window
  (`requestActivateWindow`) while `PanelWidget::contextMenuEvent`
  never fired, because QWidgetWindow's context-menu synthesis
  requires Qt::RightButton. After correcting the mask to
  `mouse_button 2` (BTN_RIGHT), the right-click does reach the
  real `PanelWidget::contextMenuEvent`. This mask fix alone does
  not make the UI work.
- **Phase B (panel QMenu open) fails 3/3.** Across three bounded
  right-click attempts the open phase yields no menu pixels in the
  Capture evidence and no quick lifecycle whatsoever: no
  ensure-start, no quick request/result, no configd broadcast.
  The menu never becomes visible.
- **The upstream-wm path removes the forced resize but fails on
  view association.** Routing the popup through the upstream wm
  stack with `window()=Undefined` removes the forced
  direct-domain resize (the `103x203 -> 1024x712` stretch): the
  popup keeps its final geometry `512,509 103x203`. The run then
  fails with `attempt to create ID for invalid view` and
  `failed to create child view for invalid parent view`, because
  the direct-nitpicker Background parent cannot be associated by
  wm and the cross-server child-view association never completes.
- **The direct-nitpicker path shows no menu either.** Leaving the
  popup on the direct nitpicker path produces no visible menu.
  Both candidate paths fail, so the surface is BLOCKED and no
  single minimal justified fix remains.
- **Trials reverted, tree clean.** The layer, parent, and
  Qt::Window identity trials all failed and were reverted.
  `qnpw_verbose` is restored to its default and no debug
  instrumentation remains in the tree.

Current product quick-settings/provider wiring exists and its
wire-level behavior is proven, but the QMenu surface is not
accepted and remains non-functional. No document in this tree
should imply the quick-menu UX is working. This note prescribes
no new implementation approach; the blocker is recorded as
evidence, not as a design proposal.

Final disposition (2026-11-14): **BLOCKED**. No single minimal
justified change remains.

## Not verified (honest limitations)

- **Click-through coverage.** The providers probe verifies the exact
  quick wire protocol (the same `<quick_request>` / `<quick_result>`
  documents the menu drives), but an independent click-through
  PanelWidget -> QuickMenuController -> provider UI runtime test has
  not been demonstrated. The attempt is
  `run/sponge-settings-quickmenu.run`, a red repro whose surface is
  BLOCKED (the section above). A verified wire protocol does not
  imply a working QMenu UX.
- **Physical-hardware acceptance is NOT done.** No physical-hardware
  matrix has run (machine, parent commit, Genode pin, image/scenario,
  first paint, input-driven change per receipt). The one-reboot
  persistence of a package-private setting and the control-center
  crash test on hardware are also untested. Without these, Criterion
  18 remains open rather than accepted.
- **No physical receipts exist.** Every receipt in this note is a
  QEMU run or a host self-test. Nothing here is a physical receipt.
- **Display/terminal typed-key seam.** The `display_settings` and
  `terminal_settings` providers ship as an explicit typed-key
  registration seam: their `display.*` / `terminal.*` typed keys are
  not yet registered in configd's closed registry
  (`TODO(configd follow-up)`), and the provider surfaces report
  "typed keys: pending configd registration". They open, validate,
  and reject outside-domain writes like the rest, but their real
  typed keys land with the configd follow-up.

## Verdict

The provider-owned architecture is implemented and QEMU-verified
after the session opt-in gate fixes: all six settings acceptance
scenarios (center, providers, thirdparty, failure, vct, focus)
pass, the host self-tests pass, the changed-target build passes,
product Alpha/BIOS-disk/UEFI-USB validation passes as previously
documented, and the migrated regressions pass. Gone from the tree:
the legacy dialog and the universal `SettingsController` writer.
The precise disclosure: the provider wire protocol and product
integration are verified, but real QMenu interaction is not. That
QMenu surface (PanelWidget / BackgroundWidget) is BLOCKED with no
minimal justified fix remaining, and
`run/sponge-settings-quickmenu.run` stays a red repro, so the
quick-menu UX is not accepted and remains non-functional. The
post-fix review is pending separately and no final product
acceptance review has occurred; no review acceptance is claimed
here. Acceptance is a separate, still-open gate. Criterion 18
remains **open**, with this structural QMenu blocker in addition
to the physical-hardware matrix, the one-reboot package-private
persistence test, and the hardware control-center crash check.
All six acceptance scenarios still pass and no regression is
being hidden.
