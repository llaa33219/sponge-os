> **Breadcrumb:** [06 - vct Design](../06-vct.md) → §4. Proposed Subcommand List

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

#### Driver packages: no `vct` change, and why that is the point

`vct install pc_nic` works through the **same** code path as
`vct install falkon`. There is no driver-specific branch anywhere in
`vct`: `InstallCommand` forwards the package name verbatim as
`client.request("install", pkg)`, with no allowlist and no hardcoded
package names (`repos/sponge/src/vct/commands.cc`), and `sponge_pkgd`
answers. That is the whole driver UX:

```
vct install pc_nic     # driver package -> the driver_runtime sandbox
vct list               # includes pc_nic; per-package kind="driver" in the result
vct launch pc_nic      # for a driver that has no <autostart/>
vct remove pc_nic      # drops it from driver_runtime
vct install pc_nic --explain    # same plan machinery, same renderers
```

The interesting part is where the *product* differs, and it is all
downstream of `vct`:

| Question | Answer, and where it comes from |
|---|---|
| Which sandbox does the driver run in? | `pkg_runtime` or `driver_runtime`, decided by the package's `<kind>` element — `sponge_pkgd` reads it, not `vct` (docs/12 §4.5). |
| Why did the install fail on resources? | `sponge_pkgd`'s admission pre-flight refuses before mutating anything and answers `insufficient-resources` with the four figures. `vct` renders that status like any other. |
| Is the driver running yet? | `<autostart/>` packages get their `<start>` node at install; the others install STOPPED and need `vct launch` (docs/12 §4.5). |
| Why did it start and then find no hardware? | Not a `vct` question at all — it is a Tier-0 `platform` policy decision, documented in docs/15 §8.2. |

**What `vct` does not yet do.** Two presentation gaps, neither of them
a flow gap, and both deliberately left rather than guessed at:

- `vct status` does not read `driver_runtime_state`, so it cannot say
  "the NIC driver is installed but its device is unbound" or "this
  driver was refused for caps". The data needed for both already
  exists — `driver_runtime`'s sandbox state report is published and
  relayed under that label — so this is a rendering gap.
- The **human-readable** `vct list` table does not print `kind`. The
  attribute is present in the `list` result and in the installed
  broadcast, so `vct list --json` and the DE launcher can both read it
  today; only the human table is undecided about how to show a driver
  among apps without implying it is launchable from the application
  launcher, which it is not.

A status line that cannot be backed by a live read is worse than no
status line, so neither is invented here.

The convention the driver set follows is worth stating here because it
is a UX contract, not an implementation detail: **on a driver package,
`<autostart/>` means "verified end to end"**. `pc_nic` carries it; the
four build-only drivers deliberately do not, so a user who installs one
gets a package that has not silently claimed to be working. Automation
default (install) and control (launch, remove, and the Tier-0 policy as
a manual path — a driver can be started straight from a run script with
no package manager involved at all) both hold.

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
  - `binary-missing`: the package is registered but its binary is not
    present in this image, so it could not have started; exit
    non-zero. `sponge_pkgd` detects this before emitting a `<start>`
    node, because `pkg_runtime`'s `init` would otherwise drop the child
    with nothing but a kernel warning.
  - `exclusive-conflict`: the package declares
    `<exclusive group="G">` and another **running** package already
    holds `G`; exit non-zero. `vct` prints the incumbent and the
    remedy inline (`vct remove <peer>` then retry), and `--json`
    carries `group` and `peer`. The framebuffer drivers are the first
    such group (`docs/12-package-format.md` §4.6), so this is the status
    a user meets when trying to start a second display driver. Like the
    refusals above it is decided before the `<start>` node exists.
  - `insufficient-resources`: the package's declared `<quota>` does not
    fit `pkg_runtime`'s remaining budget; exit non-zero. Both surfaces
    print the four figures (`needs N bytes RAM / N caps, N bytes RAM /
    N caps free`), and `--json` carries them as `need_ram`,
    `avail_ram`, `need_caps` and `avail_caps` so a caller can budget
    against the refusal rather than just re-try it. Like
    `binary-missing`, it is decided before the `<start>` node exists.
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

### 4.8 Settings Entries (Phase 16 Extension)

> **Status: implemented and host-tested.** The two commands below are
> routed by the current parser and implement the vct half of the
> provider-owned Settings contract
> ([`docs/12-package-format.md`](../12-package-format.md) §14); the
> authoring half is
> [`docs/16-package-authoring.md`](../16-package-authoring.md) §9 and the
> GUI half is the separate `sponge_settings` control center
> ([`docs/05-sponge-de.md`](../05-sponge-de.md)). The wire adapter
> (`settings_wire`) is pinned by a host self-test (`tests/`); no
> scenario or hardware verification is claimed here. The two commands
> are Phase 16 extension surface, not Alpha media, and stay outside
> the §8 command counts.

Synopsis:

```
vct settings list [--json] [--help] [--lang ko]
vct settings open <package> <settings-id> [--json] [--help] [--lang ko]
```

Flags: identical to `vct update`. Help follows the normal convention:
a concise English summary first, detailed English help after, Korean
with `--lang ko`.

Automation-default behavior:

- Both commands use the **same `sponge_pkgd` metadata and launch
  backend** that `vct list` and `vct launch` use (AGENTS.md §3.3 rule
  5: two interfaces, one backend, one state of truth). Requests
  travel on vct's own single-writer request/result labels;
  `sponge_settings` uses its own separate labels for the same backend
  (`AGENTS.md` §1.2, one writer per label). vct never shares a writer
  label with the control center and never bypasses `sponge_pkgd`.
- `vct settings list` reads pkgd's installed metadata (the same
  `installed` broadcast `docs/12-package-format.md` §14.4 specifies
  for discovery, opened directly as a minimum-privilege ROM read like
  `vct update`) and prints one line per entry:
  `package  settings-id  title  category`, grouped by the closed
  category enum in the order desktop, hardware, system, applications,
  other. Entries are whatever the installed packages declare; no
  discovery logic lives in vct. An empty result is honest: prints
  `No settings entries.` and exits 0. An unavailable or malformed
  broadcast is an explicit error with a non-zero exit, never a fake
  empty list.
- `vct settings open <package> <settings-id>` submits one launch
  request carrying the `target="settings"` discriminator and the
  named identity pair (`docs/12-package-format.md` §14.5). vct sends
  an identity, never a command string, and holds no package
  `File_system` capability. The request goes out on vct's own
  single-writer `request` label and the result is read from vct's
  shared `result` label, where an answer counts only when it carries
  the settings discriminator and a matching identity (`id` primary,
  `settings-id` alias), so a plain launch result can never satisfy a
  settings open. Audit line before acting:
  `[vct] settings open: requesting <package> <settings-id>`.
- **Started or focused:** a first open of an entry starts the provider
  and reports success with `action="started"`; a second open of a live
  entry raises and focuses the existing window and reports success
  with `action="focused"`. It never reports `already-running` and
  never spawns a second window. That status stays exclusive to
  `vct launch` (§4.2), whose semantics are unchanged.
- Errors are reported explicitly, reusing the launch statuses behind
  the discriminator; every refusal exits non-zero:
  - `not-installed`: the package is not installed, the `settings-id`
    is unknown, or the entry went stale after a removal.
  - `binary-missing` and `insufficient-resources`: same meaning and
    same figures as `vct launch` (§4.2).
  - `package-manager-unavailable`: the `sponge_pkgd` request channel
    cannot be opened, or the bounded result wait (about 6 s) expires.
    vct derives this locally and fail-fast; the status never arrives
    on the wire, and vct never keeps polling.
  - A structured pkgd error or an unrecognized result is reported
    with its message.

JSON result of `vct settings open sponge-de panel --json` (a first
open; a second open of a live entry reports `"action":"focused"`):

```json
{
  "command": "settings open",
  "package": "sponge-de",
  "settings_id": "panel",
  "status": "ok",
  "action": "started"
}
```

Human-readable output renders the `ok` status as `success`:
`settings open: success (started): <package> <settings-id>`, or
`(focused)` for the raise case. Refusals print the status names above
and a bounded explanation.

`vct settings list --json` emits a `settings_list` command record
with an `entries` array of `{package, settings_id, title, category}`
objects (empty when nothing is installed), mirroring the `vct list`
JSON style (§6.2). List errors carry `"status":"error"` and an
`error` message (`installed broadcast unavailable (sponge_pkgd
absent)` or `installed broadcast malformed`).

Manual escape hatch:

- Like `vct launch`, both commands are single-step operations with no
  `--manual` mode. The manual path to the same work is reading
  `pkg/<name>/metadata.xml` directly (`docs/12-package-format.md`
  §14.2) and starting the entry's target through the Leitzentrale
  expert controls; `vct settings open` is the one-step automation of
  exactly that, which is the AGENTS.md §3.3 pairing.
- `vct settings` never writes configuration. `vct config <key>
  [value]` (§4.4) remains the intentional admin-wide manual writer:
  it is the deliberate escape hatch that writes keys directly through
  `sponge_configd` for administrators who need to set anything
  anywhere. Provider-owned writes stay inside each provider's own
  ACL-scoped configd key domain or package-private store
  (`docs/12-package-format.md` §14.6), and no settings command turns
  vct into a relay for those writes.

---
