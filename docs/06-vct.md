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
vct config theme dark         # change a configuration value (positional)
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
  --no-deps     Skip automatic dependency installation (not yet implemented).
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

| Command | Status | Purpose |
|---|---|---|
| `vct install <package>` | Phases 0–6 (delivered) | Install a package (automatic dependency resolution) |
| `vct remove <package>` | Phases 0–6 (delivered) | Remove a package |
| `vct list` | Phases 0–6 (delivered) | List installed packages |
| `vct update [package]` | Phase 7 (delivered) | Re-resolve installed roots against the on-image metadata |
| `vct search <term>` | Phase 7 (delivered) | Search the on-image repository by name or description |
| `vct launch <package>` | Phase 7 (delivered) | Start an installed package through `sponge_pkgd` |

#### `vct update [package]`

Synopsis:

```
vct update [package] [--json] [--help] [--lang ko]
```

Flags:

| Flag | Purpose |
|---|---|
| `--json` | Machine-readable status output (see §6.2) |
| `--help`, `-h` | Concise English summary followed by detailed English help; Korean with `--lang ko` |
| `--lang ko` | Korean output (where the runtime supports it) |

Automation-default behavior:

- No `package` argument: re-resolves every installed root against the
  on-image repository metadata (`pkg_index.xml` plus the
  `pkg_<name>.xml` ROMs).
- With a `package` argument: re-resolves that installed root and its
  transitive dependency graph.
- Reports version deltas honestly, using one of two line shapes:
  - `already current: <name> <installed_version>`
  - `repo carries <repo_version>, installed <installed_version> — effective after next image build`
- No network fetching. The on-image repository is fixed at build time
  (Alpha semantics, `docs/12-package-format.md` §5.5); a newer
  repository version becomes effective only after the image is
  rebuilt and the updated package is pre-staged.
- No auto-upgrade. The command never mutates the installed set or the
  running state. Version strings are reported as-is and are not
  ordered by the Alpha format.

Manual escape hatch:

- `vct update` has no `--manual` mode (it is a single-shot read-only
  operation; the four `--manual`-style prompts of `vct install` do
  not apply here).
- To inspect what is currently installed, run `vct list`
  (or `vct list --json` for machine output).
- To inspect a single package's metadata, read
  `pkg/<name>/metadata.xml` directly per
  `docs/12-package-format.md` §9.3 — there is no hidden state.
- To change the installed set, use `vct install <pkg>` /
  `vct remove <pkg>`.

#### `vct search <term>`

Synopsis:

```
vct search <term> [--json] [--help] [--lang ko]
```

Flags: identical to `vct update`.

Automation-default behavior:

- Scans the on-image repository metadata (`pkg_index.xml` plus the
  `pkg_<name>.xml` ROMs) for matches against `<name>` and
  `<description>`.
- Prints one line per match: `name  version  one-line description`.
- An empty result is honest: prints `No matches.` and exits 0. An
  empty result is never an error and never a non-zero exit.
- No network fetching; the on-image repository is fixed at build time.

Manual escape hatch:

- `vct search` has no `--manual` mode (single-shot read-only).
- To list the installed subset, use `vct list`.
- To inspect a package's full metadata, read
  `pkg/<name>/metadata.xml` directly per §9.3.

#### `vct launch <package>`

Synopsis:

```
vct launch <package> [--json] [--help] [--lang ko]
```

Flags: identical to `vct update`.

Automation-default behavior:

- Sends the `launch <name>` request to the same `sponge_pkgd`
  Report/ROM channel that `vct install` and `vct list` already use
  (`docs/12-package-format.md` §9.2.1). For an installed package
  without a running node, `sponge_pkgd` adds the `<start>` node to
  `pkg_runtime` and the package transitions from installed to
  running.
- The Sponge DE launcher menu uses **the same backend** — clicking a
  launcher entry sends the same `launch` request to the same
  `sponge_pkgd` channel, per AGENTS.md §3.3 rule 5. Two interfaces
  (CLI and GUI), one backend, one state of truth.
- Errors are reported explicitly:
  - `not-installed`: the named package is not in the installed set;
    exit non-zero.
  - `already-running`: the named package already has a `<start>`
    node; exit non-zero.
- Audit line printed before acting:
  `[vct] launch: requesting start of <name>`.

Manual escape hatch:

- `vct launch` has no `--manual` mode (launch is a single-step
  operation in the Alpha lifecycle; no stop operation exists yet —
  see `docs/12-package-format.md` §9.2.1).
- To inspect the live component tree after launch, use
  `vct component list` (reads the same init state report).
- To launch from the DE side, click the entry in the Sponge DE
  launcher menu; it sends the same `launch` request to the same
  `sponge_pkgd` channel. There is no separate DE-only launch path.

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
| `vct config <key>` | Read a configuration value |
| `vct config <key> <value>` | Change a configuration value |
| `vct config list` | Show the full configuration |
| `vct config export` | Export configuration for backup (not yet implemented) |
| `vct config import <file>` | Import configuration (not yet implemented) |

The positional form (`vct config <key> [value]`) is the implemented
form. The verb form (`config get` / `config set`) shown in earlier
drafts is **not implemented** and the parser does not route it: the
second positional selects between read (no value) and write (value
present), and the literal `list` dispatches to the bulk read. Export
and import stay on the table so the API surface is honest, but the
parser does not route them yet either (AGENTS.md §5.3).

#### `vct bake`

```
vct bake list [--profile <name>] [--json] [--lang ko]
vct bake show [--profile <name>] [--json] [--lang ko]
vct bake reset [--profile <name>] [--manual] [--json] [--lang ko]
vct bake --help
```

`list` reports the single profile discoverable in this medium's
`bake_manifest.json`; non-baked media reports `No bake manifest on this
media.` rather than inventing a profile. `show` reads the same manifest,
the baked `config.defaults`, and `sponge_configd`'s broadcast ROM to show
profile/version/applied state plus each baked value beside its current
value. `vct status` includes `bake: <profile> @ v<version>` or `bake: none`.

`reset` sends the normal configuration-backend request
`bake.applied=no`. `sponge_configd` then validates and reapplies every
baked key, restores the baked theme, sets `bake.applied=yes`, persists the
store, and broadcasts the result. Only keys present in `config.defaults`
plus `theme.active` are reset; user keys outside that baked set are left
untouched. `--manual` exposes this as read state, request reseed, then
persist/broadcast steps. `--profile` filters against the profile actually
present on the media; it never fetches or synthesizes another profile.

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

| Command | Status | Purpose |
|---|---|---|
| `vct shutdown` | Phase 7 (delivered) | Shut down the system |
| `vct reboot` | Phase 7 (delivered) | Reboot the system |
| `vct snapshot` | future | Snapshot the system state |
| `vct rollback <snapshot>` | future | Roll back to a snapshot |

Snapshot and rollback are listed for API completeness but are not
routed by the current parser. They are not part of the Alpha media;
land them only when a snapshot/rollback backend exists.

#### `vct shutdown`

Synopsis:

```
vct shutdown [--json] [--help] [--lang ko]
```

Flags:

| Flag | Purpose |
|---|---|
| `--json` | Machine-readable status output (see §6.2) |
| `--help`, `-h` | Concise English summary followed by detailed English help; Korean with `--lang ko` |
| `--lang ko` | Korean output (where the runtime supports it) |

Automation-default behavior:

- Opens a `System` session routed to the platform driver
  (`acpi` / `platform` in the `run/sponge-alpha.run` drivers sub-init).
- Prints an audit line before acting:
  `[vct] shutdown: requesting poweroff`.
- Invokes poweroff. On QEMU the guest shuts down and the run tool
  observes the clean exit; on real hardware the ACPI power button is
  signalled.

Manual escape hatch:

- If the `System` session is not routable (no platform driver in the
  scenario), the command fails with a clear `service unavailable`
  error, exits non-zero, and the guest keeps running. The user can
  then take direct control through QEMU's monitor: `system_powerdown`
  via `-qmp`, or the `Ctrl-A x` keyboard escape followed by
  `system_powerdown`.
- For headless QEMU where the monitor is unreachable, the
  `qemu-system-x86_64 ... -action panic=shutdown -action
  reboot=shutdown` flags are an equivalent host-side fallback. These
  QEMU-monitor escape hatches are documented in
  `docs/13-installation.md` for users whose firmware refuses the ACPI
  power button.
- `vct shutdown` is purely user-invoked. No automation (cron, hook,
  policy) ever calls it.

#### `vct reboot`

Synopsis:

```
vct reboot [--json] [--help] [--lang ko]
```

Flags: identical to `vct shutdown`.

Automation-default behavior:

- Opens a `System` session routed to the platform driver.
- Prints an audit line before acting:
  `[vct] reboot: requesting reset`.
- Invokes reset. On QEMU the guest reboots and the run log shows the
  boot banner twice in sequence.

Manual escape hatch:

- Identical to `vct shutdown`. The QEMU-monitor fallback for reset is
  `system_reset` (with the same `-qmp` / `Ctrl-A x` access pattern).

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
- Color when the terminal supports it (disable with `--no-color`, not yet implemented).

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

vct has reached the **Phases 0–6 milestone** for the 10 commands
below. Each line names its backend so the rule "vct is a thin CLI, the
backends do the work" (`docs/04-components.md` §1.2) stays visible.
Every command runs end-to-end against real Genode state inside the
boot image on both `base-linux` and `base-sel4` (the production
target).

| Command | Backend |
|---|---|
| `vct status` | live `init` state report (sub-init + `report_rom` relay) |
| `vct --help` / `vct help` | static help text (`--lang ko` for Korean); no backend |
| `vct --version` / `vct version` | static version string; no backend |
| `vct component list` | live `init` state report (sub-init + `report_rom` relay) |
| `vct install <pkg>` | `sponge_pkgd` Report/ROM channel (`explain`, `install`) |
| `vct remove <pkg>` | `sponge_pkgd` Report/ROM channel (`remove`) |
| `vct list` | `sponge_pkgd` Report/ROM channel (`list`) |
| `vct config <key> [value]` / `vct config list` | `sponge_configd` Report/ROM channel (`config_get` / `config_set` / `config_list`) |
| `vct theme apply <name>` | `sponge_configd` (writes `theme.active`); consumed by `sponge_themed` |
| `vct leitzentrale [off\|status\|diff\|keep\|revert]` | `sponge_configd` (toggle) + `lz_watch` (diff/keep/revert) |

`--json` is honored by every command that produces structured output
(`status`, `component list`, `install`, `remove`, `list`, `config`,
`config list`, `theme apply`, `leitzentrale`).

### Phase 7 additions (Alpha media)

Phase 7 (this milestone) extends vct to cover the day-to-day tasks
the Alpha media needs. The five commands below are **specified by
this document** (todo 2 of the Phase 7 plan) and implemented in the
`run/sponge-alpha.run`-gated scenarios (todos 17 and 18 of the same
plan):

| Command | Backend |
|---|---|
| `vct shutdown` | platform driver `System` session (poweroff) |
| `vct reboot` | platform driver `System` session (reset) |
| `vct update [pkg]` | `sponge_pkgd` (re-reads on-image metadata; no fetch) |
| `vct search <term>` | `sponge_pkgd` (reads on-image metadata; no fetch) |
| `vct launch <pkg>` | `sponge_pkgd` Report/ROM channel (`launch`; same channel the Sponge DE launcher uses per AGENTS.md §3.3 rule 5) |

With the Phase 7 additions the Alpha surface is **15 user-facing
subcommands**. `vct snapshot` and `vct rollback` stay in §4.7 as
future work; they are not part of the Alpha media.

The implementation roadmap is defined under the milestones in
`docs/09-roadmap.md`.