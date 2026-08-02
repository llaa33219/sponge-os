# sponge_files — minimal Qt6 Widgets file manager

`sponge_files` is the Sponge OS file manager (Phase 7 todo 15). It is
an installable package (`pkg/files`) that runs under the package
runtime like every other Alpha default app (terminal/textedit/files).

## Alpha scope

The component implements the four operations the todo specifies:

- **Navigate** — double-click a directory to enter it; the **Up** button
  returns to the parent. `..` is also listed as a list entry so it can
  be double-clicked.
- **Open** — double-clicking a file shows its first ~1 KiB in the
  preview pane (and logs the byte count + path).
- **Copy** — the **Copy to /writable** button copies the currently
  selected file to `/writable/<name>`. The destination must be inside
  `/writable` (the only writable area in the Alpha vfs layout).
- **Delete** — the **Delete** button deletes the currently selected
  file. Refused with a clear status in `/demo` (the read-only fixture
  area, mounted from a tar) so the probe can assert the refusal.

Out of scope (Alpha guardrails, todo 15): rename/undo, search,
drag-and-drop.

## Testability

A probe drives every operation through two channels:

1. **`files` report** (Report session) — published after every action
   with the current path, entry count, and last action name + result
   (`ok`, `refused`, `no-such-file`, ...). Lets the probe verify
   navigation/operation results without parsing pixels.
2. **`files_request` ROM** — carries the same operations the GUI emits
   (`navigate`, `up`, `open`, `copy`, `delete`, `noop`), so the probe
   can drive copy/delete without pixel-precise button clicks. The GUI
   is the manual escape hatch; the request channel is the automation
   default (AGENTS.md §1.1, §3.3 rule 2).

Synthetic double-click navigation (todo 15 acceptance) is verified by
injecting a real double-click on the list's first row through the
Event service — the component's normal double-click handler runs and
the `files` report's path changes, which the probe asserts.

## Theme

The component loads the Sponge DE theme format
([`docs/10-theme-format.md`](../../../../docs/10-theme-format.md))
once at startup. The `<theme source="themed"/>` config node selects
the live `sponge_themed` ROM; absent (or `source="default"`) reads
`default.theme` once. The focused scenario uses the fallback so it
runs without sponge_themed, but the structure supports the live themed
ROM the alpha scenario mounts.

## Source layout

```
src/sponge_files/
├── target.mk            # Qt6 qmake-driven build
├── sponge_files.pro     # qmake project (QT += core gui widgets)
├── main.cc              # Libc::Component::construct entry (Pattern B)
├── files_window.{h,cc}  # Files_window — QWidget subclass (GUI + ops)
└── theme/
    ├── theme_loader.{h,cc} # INI parser (mirrors sponge-de's, own namespace)
    └── README.md (this file's parent is the component README)
```
