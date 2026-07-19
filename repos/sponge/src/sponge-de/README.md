# sponge-de — Sponge Desktop Environment

The default desktop environment of Sponge OS. Qt-based.

## Role

- Panel, launcher, notifications, window-management helpers
- User-settings GUI (shares the same backend as `vct config`)
- Theme system application

For the full design, see [`docs/05-sponge-de.md`](../../../../docs/05-sponge-de.md).

## Current status

🟡 Phase 3 (in progress). Qt6 integration is wired and the component
builds and links successfully:

- ✅ Qt6 6.8.3 builds and links inside Genode 26.05
- ✅ Component uses `Libc::Component::construct` (Pattern B bootstrap
  with `qpa_init`)
- ✅ `Main` is a `QWidget` subclass that creates a single window
- 🔜 Gui session rendering verified via a run scenario (needs full
  driver stack: nitpicker + framebuffer + input)
- 🔜 Keyboard and mouse input through the `Event` session

The theme system skeleton (`theme/theme_loader.{h,cc}`) is present but
not yet wired into the Qt build (see `docs/10-theme-format.md`).

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