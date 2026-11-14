# sponge-de — Sponge Desktop Environment

The default desktop environment of Sponge OS. Qt-based.

## Role

- Panel, launcher, notifications, window-management helpers
- Quick-settings menus (`QuickMenuController`) over provider-owned
  settings windows; the DE itself writes no settings keys
- Theme system application

For the full design, see [`docs/05-sponge-de.md`](../../../../docs/05-sponge-de.md).

## Current status

✅ Phase 3 (complete). The component renders a themed panel plus a demo
window on nitpicker, and input reaches the widgets:

- ✅ Qt6 6.8.3 builds and links inside Genode 26.05
- ✅ Component uses `Libc::Component::construct` (Pattern B bootstrap
  with `qpa_init`)
- ✅ `Main` is a `QWidget` subclass that creates a single window
- ✅ Gui session rendering verified by `run/sponge-de-test.run`
  (headless: the `sponge_de_probe` component matches the composited
  window pixels through a Capture session)
- ✅ Keyboard and mouse input verified: the probe injects a synthetic
  click through nitpicker's Event service and sponge-de confirms it via
  its `input` report; the demo button's `clicked` signal fires
- ✅ Theme system wired: `default.theme` ROM → `theme/theme_loader`
  → panel/window styling (see `docs/10-theme-format.md`)
- ✅ Live theme reload via `sponge_themed` + ThemeController
  (`run/sponge-theme.run`)
- ✅ Launcher menu fed by `sponge_pkgd` (`run/sponge-launcher.run`)
- ✅ Upstream window-management stack integrated (`run/sponge-wm.run`):
  app windows route through Genode's `wm` + `window_layouter` +
  `decorator` and become decorated/movable (verified by a synthetic
  drag); the panel stays a fixed nitpicker-domain surface
- ✅ **Phase 14 W4: notifications.** `sponge-de` subscribes to the
  `sponge_notifier` daemon's `notifications` ROM and renders a themed
  popover (panel/notifier_widget.{h,cc}). Posts on theme apply
  (`ThemeController`), config change (`ConfigController`), and
  package install completion (`LauncherController`). Opt-in via
  `<notifier source="daemon"/>` in the component config. Acceptance
  probe `run/sponge-notify.run` (base-sel4) emits the sentinel and
  verifies the popover + the TTL expiry; the companion
  `run/sponge-notify-without-notifier.run` demonstrates the
  absent-daemon warning path.
- ✅ **Panel warning surface.** The panel carries one warning label with
  two producers: `PanelWidget::set_health()` (component-health
  watcher over the `system_state` ROM) and
  `PanelWidget::set_user_message()` (transient, own TTL, falls back to
  the health text). `LauncherController` emits `launch_problem(pkg,
  reason)` for every non-success launch outcome — result timeout,
  `not-installed`, `binary-missing`, `insufficient-resources`,
  `already-running`, unknown status — and for a launcher whose
  request/result channels cannot be opened (`pkg` empty). The message
  reads `⚠ launch '<pkg>' failed: <reason>` for ~10 s. A budget refusal
  carries its numbers into the label (`not enough free memory or
  capabilities (needs X RAM / Y caps, A RAM / B caps free)`) because
  "it did not start" alone gives the user nothing to act on.
- ✅ **Persistent launch-error witness (Phase 16 P2.5).** Every
  package-scoped launch failure also writes a single persistent
  `de_op_error` report (`<op_error op="launch" pkg="..." reason="..."/>`),
  enabled before `app.exec`. The record is not cleared by provider recovery
  or later success; panel messages keep their 10 s TTL so they cannot mask
  later health warnings. Empty-package channel failures are not recorded.
- ✅ **Optional `system_state` ROM.** Only the five desktop-disk
  product scenarios route `system_state`, and base answers a denied
  session with "stop because parent denied" + `sleep_forever()` (not
  an exception), so a plain `Attached_rom_dataspace` cannot make it
  optional. `rom/optional_rom_dataspace.{h,cc}` requests it through
  `Env::try_session()` — the non-fatal variant upstream's
  `event_filter` uses — so unrouted topologies log
  `system_state ROM unavailable; health watcher disabled` and run on,
  while the product media keeps the watcher.
- ✅ **W4 provider split: the universal settings writer is retired.**
  The Phase 16 `SettingsController`, its five-tab settings dialog,
  and their shared `de_config` writer are deleted from sponge-de.
  `settings/quick_menu_controller.{h,cc}` is the reusable
  quick-settings bridge (`docs/05-sponge-de.md` §5.4,
  `docs/12-package-format.md` §14.8): the panel context menu's
  Height / Visible widgets / Position controls run its closed
  `change_panel_*` operations over the owning provider's
  quick-request/quick-result pair, and the panel / background
  Settings entries open the provider-owned settings windows through
  `LauncherController::request_settings_open()`: one ordinary
  `(package, settings-id)` launch request on the launcher's own
  labels. A quick change first ensures the provider is started
  (`started` and `focused` are both success), then sends one closed
  `<quick_request>` and polls the matching result on a non-blocking
  bounded QTimer chain, so nothing hangs the GUI thread; every
  refusal becomes a bounded `quick_change_failed()` message. Quick
  channel sessions exist only when the DE config carries
  `<quick_settings source="providers"/>`; without that opt-in the
  controller is attached to no menu and no quick session is
  attempted (an explicit capability/routing declaration, like the
  `system_state` note above, so an unrouted topology never hits
  base's fatal denied-session `sleep_forever` path). The
  module builds as part of the DE (`sponge_de.pro`).

The interactive escape hatch is `run/sponge-de.run` (fb_sdl window on
the host display, base-linux).

## Source layout

```
src/sponge-de/
├── target.mk            # Qt6 qmake-driven build (LIBS = qt6_qmake ...)
├── sponge_de.pro        # qmake project (QT += core gui widgets)
├── main.cc              # Libc::Component::construct entry (Pattern B)
├── sponge_de_main.{h,cc}# Sponge_DE::Main — QWidget subclass
├── theme/
│   ├── theme_loader.{h,cc} # INI parser skeleton (Phase 5, standalone)
│   └── README.md
├── themes/
│   └── default.theme    # Catppuccin Mocha default theme
├── panel/
│   ├── panel_widget.{h,cc}  # the themed panel
│   ├── notifier_widget.{h,cc} # the themed popover (Phase 14 W4)
│   ├── notifier_controller.{h,cc} # ROM bridge (Phase 14 W4)
│   ├── notify_poster.{h,cc} # Report session writer (Phase 14 W4)
│   └── README.md
├── settings/
│   └── quick_menu_controller.{h,cc} # quick-settings bridge (W4 provider split)
└── README.md            # this file
```

## Plan for gradual module separation

Sponge DE starts as a single component, but its internal modules
(panel, launcher, notifications) keep loose boundaries. Splitting them
into separate components is considered from Phase 5 onward.

```
sponge-de (currently: single component)
  ├─ panel
  ├─ launcher (initially integrated, candidate for split into sponge_launcher later)
  ├─ notifications (subscribes to sponge_notifier, Phase 14 W4)
  ├─ windows
  ├─ settings
  └─ theme
```

Note (W4 provider split, D16.9): the `settings/` module is no longer
a settings GUI. The universal `SettingsController`, its five-tab
dialog, and their `de_config` writer are retired (see Current
status); `settings/` now holds only `quick_menu_controller.{h,cc}`,
the quick-settings bridge. The settings windows themselves are
provider-owned components in ordinary packages: `pkg/sponge-de/metadata.xml`
declares the first-party entries (Panel, Background, Theme,
Shortcuts, Defaults, Display, WM) and `pkg/terminal/metadata.xml`
the Terminal entry. The Display and Terminal providers still carry
no typed configd keys (the typed-key seam of
[`docs/12-package-format.md`](../../../../docs/12-package-format.md)
§14.6), and nothing here invents them. The separate `sponge_settings`
shell lists and opens the entries and carries no settings UI of its
own ([`docs/05-sponge-de.md`](../../../../docs/05-sponge-de.md) §5.4).
