# 07 - Leitzentrale Integration

> This document explains how Sponge OS integrates the Leitzentrale, the
> expert management UI of Sculpt OS.

---

## 1. What is Leitzentrale?

[Leitzentrale](https://genode.org/documentation/articles/sculpt-23.10#section_5_1)
(German for "control room") is the expert management interface used by
the Genode team in Sculpt OS. It manipulates Sculpt's `init` component
configuration directly, giving fine-grained control over the system
component tree, session routing, and resource allocation.

Characteristics:

- Web-based UI (accessed through a Genode-internal web browser).
- Holds permission to read and write `init`'s configuration.
- Can add, remove, and restart components, change session routing, and
  more.
- Powerful, but requires understanding Genode concepts.

---

## 2. Position in Sponge OS

In Sponge OS's philosophy (see `docs/02-philosophy.md`), Leitzentrale
is the top-level escape hatch for the **control** principle.

```
   Convenience             Control
       ^                       ^
       |                       |
   vct default           vct --manual
       |                       |
       |                       |
       +------- vct -----------+
                               |
                               |
                        vct leitzentrale
                               |
                               |
                        Leitzentrale window
                        (raw init control)
```

- Everyday users rely on vct's automation (convenience).
- Power users use vct's `--manual`, `--explain`, and detailed
  subcommands (mid-level control).
- **Experts** open the Leitzentrale window and manipulate every
  component of the system at the raw level (full control).

Leitzentrale does not "bypass" vct's automation; it reaches the raw
layer underneath it directly. Both automation and control stay
preserved.

---

## 3. Entry Points

Two ways for a Sponge OS user to enter Leitzentrale:

### 3.1 vct (CLI)

```
$ vct leitzentrale
Opening Leitzentrale window...
A web view appears inside Sponge DE. URL: http://init.local/
```

vct starts the Leitzentrale component, and Sponge DE (or a dedicated
web-view component) shows it as a window.

### 3.2 Sponge DE Menu (Planned)

System menu on the panel → "Advanced Mode" → "Open Leitzentrale".

The menu item is placed where a beginner would not click it, but it is
not hidden. Clicking it shows a confirmation dialog along the lines of
"This mode directly modifies the system's detailed configuration.
Continue?".

---

## 4. Boundaries of the Integration

Design principles for how Sponge OS treats Leitzentrale:

### 4.1 Reuse, Not Re-implementation

Sponge OS does **not re-implement Leitzentrale**. It uses the
Leitzentrale that Genode / Sculpt upstream provides. This matches
`AGENTS.md` §5.2, "Genode is used as an external dependency".

What Sponge OS adds:

- vct's `leitzentrale` subcommand (the entry wrapper).
- The window in which Leitzentrale is displayed (Sponge DE or a
  dedicated viewer).
- (Optional) English or Korean guidance overlay around Leitzentrale.

### 4.2 Leitzentrale vs vct Automation

When Leitzentrale changes `init`'s configuration, the change can fall
out of sync with the model held by `sponge_configd` (Sponge OS's
configuration manager).

Design principles:

- Changes made through Leitzentrale are treated as **user changes**,
  and Sponge OS's automation does not silently overwrite them
  (see `docs/02-philosophy.md` §3.4).
- On the next boot, `sponge_configd` detects the change and notifies
  the user ("X was changed in Leitzentrale. Reflect this change in
  Sponge OS configuration?").
- The user chooses the conflict-resolution strategy (keep, revert,
  or merge).

### 4.3 Privilege Isolation

Leitzentrale holds a very strong privilege (writing `init`'s
configuration). Sponge OS controls its execution as follows:

- Started only by an explicit user action (vct command or menu click).
- Logged on start (audit trail).
- While running, `vct status` reports "Leitzentrale active".
- On exit, a summary of configuration changes is printed.

---

## 5. Example User Scenarios

### 5.1 A Power User Troubleshooting

```
$ vct install firefox
Error: cannot connect firefox's Gui session to nitpicker.

$ vct leitzentrale
Opening Leitzentrale window...
   (user inspects and fixes the nitpicker configuration in Leitzentrale)

$ vct install firefox
Installing... done
```

Leitzentrale is a powerful tool for debugging and problem solving.

### 5.2 An Expert's Custom Configuration

```
$ vct leitzentrale
   (user adjusts resource allocation for a specific component in
    Leitzentrale, or customizes the routing path)

$ vct config export my-setup.toml
   (exports the current configuration, including the Leitzentrale
    changes, as a file)
```

Changes made through Leitzentrale are reflected in `vct config export`
and so can be backed up and shared.

---

## 6. Open Design Questions

- The window in which Leitzentrale appears: a web view inside Sponge DE
  vs a dedicated viewer component.
- When to synchronize Leitzentrale changes with `sponge_configd`
  (real time vs at boot).
- Whether to provide a Korean translation or overlay of the
  Leitzentrale UI.
- Whether Leitzentrale itself can see the state of vct backends (such
  as the installed-package list), and how to synchronize it if so.

These items are settled through experiments in the Leitzentrale
integration prototype phase.