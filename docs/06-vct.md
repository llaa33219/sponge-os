# 06 - vct Design

> vct (Very Convenient Tool) is the single entry point for managing
> Sponge OS. This document defines vct's design philosophy, command
> structure, and consistency rules.

---

## 1. Name and Philosophy

**vct** stands for **V**ery **C**onvenient **T**ool. The name is the
goal — a "very convenient tool". Every vct design decision starts from
this question:

> "How many steps does it take the user to finish this task? Can it
> be one step?"

At the same time, control must not be lost: every automated task has an
escape hatch (see `AGENTS.md` §3.3).

---

## 2. Command Structure

vct follows a subcommand structure. General form:

```
vct <command> [subcommand] [options] [args]
```

Examples:

```
vct install firefox           # install a package
vct install firefox --explain # preview what automation would do
vct status                    # summary of system state
vct config set theme dark     # change configuration
vct leitzentrale              # open the Leitzentrale window
```

Common flags:

| Flag | Purpose |
|---|---|
| `--help`, `-h` | Concise English summary followed by detailed English help |
| `--lang ko` | Render user-facing messages in Korean when the runtime supports it |
| `--json` | Machine-readable output (for script integration) |
| `--version`, `-V` | vct version |
| `--verbose` | Detailed log (show automation steps) |
| `--explain` | Report what automation would do and exit (do not execute) |
| `--manual` | Turn automation off and run step by step |

`--help` starts with a concise English summary. Example:

```
$ vct install --help
Install a Sponge OS package.

Usage: vct install <package> [--explain] [--manual] [--json]

Resolves dependencies and configures the component tree automatically
by default. Use --explain to preview the steps, or --manual to run
them one by one.

Options:
  --explain     Preview the planned steps and exit (no execution).
  --manual      Turn automation off and run step by step.
  --json        Print the result as JSON.
  --no-deps     Skip automatic dependency installation (use with care).
  ...
```

The `--lang ko` flag switches the user-facing summary and step
messages to Korean when a Korean translation is available.

---

## 3. Argument Delivery (Design, Locked)

vct receives its arguments through a config ROM provided by `init`.
When vct starts, `init` supplies a `<config>` element in Genode's HID
(human-intelligible data) format:

```hid
+ config
| + arg status
| + arg --json
```

vct parses the `arg` children (each carries its value in the `name`
attribute) into its internal `Args` struct. The legacy XML form is still
accepted for existing test fixtures and tooling:

```xml
<config>
  <args>
    <arg>status</arg>
    <arg>--json</arg>
  </args>
</config>
```

The user-facing shell (when one exists) constructs this config and spawns
vct on the user's behalf. This is the standard Genode pattern.

The parser supports the common flags in `AGENTS.md` §3.3, including
`--lang ko` and `--json`.

---

## 4. Proposed Subcommand List

These subcommands are implemented incrementally toward an initial
release. **Not every command ships in the first release.**

### 4.1 System Information

| Command | Purpose |
|---|---|
| `vct status` | Summary of system state (running components, resource use) |
| `vct version` | Sponge OS and vct versions |
| `vct info <component>` | Information about a specific component |

### 4.2 Package Management

| Command | Purpose |
|---|---|
| `vct install <package>` | Install a package (automatic dependency resolution) |
| `vct remove <package>` | Remove a package |
| `vct update [package]` | Update a package (or all packages) |
| `vct search <term>` | Search packages |
| `vct list` | List installed packages |

### 4.3 Component Management (Control)

These commands expose the manual steps behind `vct install`'s
automation. They let the user run the same work step by step.

| Command | Purpose |
|---|---|
| `vct component list` | Show the current component tree |
| `vct component start <name>` | Start a component |
| `vct component stop <name>` | Stop a component |
| `vct component restart <name>` | Restart a component |
| `vct component config <name>` | Inspect or modify a component's configuration |
| `vct component route <name>` | Inspect or modify session routing |

### 4.4 System Configuration

| Command | Purpose |
|---|---|
| `vct config get <key>` | Read a configuration value |
| `vct config set <key> <value>` | Change a configuration value |
| `vct config list` | Show the full configuration |
| `vct config export` | Export configuration (for backup) |
| `vct config import <file>` | Import configuration |

### 4.5 Hardware

| Command | Purpose |
|---|---|
| `vct hardware detect` | Re-detect hardware (manual trigger for the automation) |
| `vct hardware list` | List detected hardware |
| `vct hardware driver <device>` | Information or configuration for a specific device's driver |

### 4.6 Advanced / Leitzentrale

| Command | Purpose |
|---|---|
| `vct leitzentrale` | Open the Leitzentrale window (expert control) |
| `vct raw <command>` | Call `init`'s raw configuration interface directly (high risk) |

### 4.7 System Control

| Command | Purpose |
|---|---|
| `vct shutdown` | Shut down the system |
| `vct reboot` | Reboot the system |
| `vct snapshot` | Snapshot the system state (when rollback is supported) |
| `vct rollback <snapshot>` | Roll back to a snapshot |

---

## 5. Automation vs Control: An Example

Three usage patterns for `vct install firefox`:

### 5.1 Automation (Convenience, the Default)

```
$ vct install firefox
Resolving dependencies... done
Generating component configuration... done
Installing... done

Installed package: firefox 118.0
Components added: firefox, nss_drv
Launcher entry registered: Internet -> Firefox
```

### 5.2 Preview (Transparency)

```
$ vct install firefox --explain
The following steps are planned:

1. Fetch package metadata
   - firefox 118.0
   - Dependencies: nss_drv, libnss

2. Add to component tree
   Under init:
     - firefox (requires Gui, Input, File_system sessions)
     - nss_drv (already present, reused)

3. Configure session routing
   firefox.Gui      -> nitpicker
   firefox.Input    -> input_drv
   firefox.File_system -> vfs (read-only: /app/firefox)

4. Register launcher entry
   Category: Internet

Re-run without --explain to execute.
```

### 5.3 Manual Steps (Control)

```
$ vct install firefox --manual
1/4 Fetch package metadata. [Y/n] y
   firefox 118.0, dependencies: nss_drv, libnss

2/4 Install dependencies. [Y/n] y
   nss_drv: already installed
   libnss: installing... done

3/4 Modify the component tree.
   The following node will be added under init:
     - firefox

   Continue? [Y/n] y

4/4 Configure session routing.
   Applying default routing (override with `vct component route firefox`).

Done.
```

All three patterns use the same backend service (`sponge_pkgd`); only
the level of automation differs. That is how the principle "automation
is the default, control is the door" is realized.

---

## 6. Output Formats

Every command supports two output formats:

### 6.1 Human-Readable (the Default)

- Starts with a one-line summary in the active language (English by
  default; Korean when `--lang ko` is set and a translation exists).
- Step-by-step progress.
- Clear success and failure states.
- Color when the terminal supports it (disable with `--no-color`).

### 6.2 JSON (`--json`)

For scripts and other tools (such as Sponge DE):

```json
{
  "command": "install",
  "package": "firefox",
  "status": "success",
  "version": "118.0",
  "components_added": ["firefox"],
  "duration_ms": 1234
}
```

JSON output writes human-readable logs to stderr and JSON to stdout,
so both can be used at once.

---

## 7. Backend Call Structure

vct itself is a thin CLI; the real work is done by backend components
(see `docs/04-components.md` §1.2).

```
vct command entered
   |
   v
vct component (short-lived)
   |
   +--(RPC)--> sponge_pkgd    # package operations
   +--(RPC)--> sponge_configd  # configuration operations
   +--(RPC)--> init            # component tree manipulation (when needed)
   +--(RPC)--> other backends
   |
   v
Result output (human-readable or JSON)
vct exits
```

Consequences of this structure:

- vct and Sponge DE use the same backends, so the two interfaces stay
  consistent.
- vct stays small and short-lived, with no complex internal state.
- The backend encapsulates every piece of automation logic and policy.

---

## 8. Implementation Status

vct is at the **Phase 2 minimum working** stage. The following commands
run end-to-end against real Genode state inside the boot image on both
base-linux and base-sel4 (the production target):

- `vct --version` — prints the version.
- `vct --help` — prints the help text (concise English summary plus
  detailed English help; `--lang ko` switches to Korean).
- `vct status` — reads the live `init` state report (via a sub-init +
  `report_rom` relay) and prints init RAM and the component count.
- `vct component list` — lists the live component tree with per-child RAM
  and cap usage.

`--json` is supported for `status` and `component list`. Phase 3 onwards
adds the Sponge DE window, Phase 4 adds package management backends, and
Phase 6 integrates Leitzentrale.

The implementation roadmap is defined under the milestones in
`docs/09-roadmap.md`.