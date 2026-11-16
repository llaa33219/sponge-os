> **Breadcrumb:** [06 - vct Design](../06-vct.md) → §6-§8 (Output Formats, Backend Call Structure, Implementation Status)

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
future work; they are not part of the Alpha media. `vct settings
list` and `vct settings open` (§4.8) are implemented Phase 16
Extension commands (routed by the parser, but outside the Alpha
media surface), so they stay excluded from this count.

The implementation roadmap is defined under the milestones in
`docs/09-roadmap.md`.