# sponge-de — Sponge Desktop Environment

The default desktop environment of Sponge OS. Qt-based.

## Role

- Panel, launcher, notifications, window-management helpers
- User-settings GUI (shares the same backend as `vct config`)
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
  ├─ notifications
  ├─ windows
  ├─ settings
  └─ theme
```