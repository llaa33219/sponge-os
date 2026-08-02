# vct — Very Convenient Tool

The single entry point for managing Sponge OS.

## Role

- Install / remove / update packages
- Manage component configuration and session routing
- Query system state
- Enter the Leitzentrale window (`vct leitzentrale`)

For the full design, see [`docs/06-vct.md`](../../../../docs/06-vct.md).

## Current status

🟡 Pre-Alpha — Phases 0–6 complete; Phase 7 (Alpha, first usable
version) adds `shutdown`, `reboot`, `update`, `search`, and `launch`
to bring the user-facing surface to 15 subcommands.

The following is implemented and runs as a real Genode component on
Genode 26.05 on both base-linux and base-sel4 (the production target):

- Version output (`vct --version` / `vct version`)
- Startup message
- Config-ROM argument parsing (`args.h` / `args.cc`)
- Subcommand dispatch for 10 routed subcommands: `status`, `help`,
  `version`, `component list`, `install`, `remove`, `list`, `config`
  (positional `vct config <key> [value]` and `vct config list`),
  `theme apply`, `leitzentrale` (`command_router.cc`)
- `--json`, `--verbose`, `--explain`, `--manual`, `--lang ko` flags
  honored
- `vct status` reads the live init state report via a sub-init +
  `report_rom` relay and prints real RAM, caps, and component count
- `vct component list` enumerates the live component tree with
  per-child RAM and cap usage
- `vct install` / `vct remove` / `vct list` drive the `sponge_pkgd`
  Report/ROM channel (dependency resolution, install / remove,
  installed-set broadcast)
- `vct config <key> [value]` / `vct config list` drive the
  `sponge_configd` Report/ROM channel (`config_get` / `config_set` /
  `config_list`)
- `vct theme apply` writes `theme.active` through `sponge_configd`;
  `sponge_themed` resolves the new theme, Sponge DE picks it up live
- `vct leitzentrale` toggles the expert window via `sponge_configd`
  and bridges to `lz_watch` for `diff` / `keep` / `revert`

Phase 7 (in design and implementation; see `docs/06-vct.md` §4.2,
§4.7, §8 and `.omo/plans/phase7-alpha.md`) adds:

- `vct shutdown` and `vct reboot` — platform-driver `System` session
  (poweroff / reset); user-invoked only.
- `vct update [pkg]` and `vct search <term>` — read-only operations
  against the on-image `sponge_pkgd` metadata; no network fetching.
- `vct launch <pkg>` — drives the same `sponge_pkgd` launch channel
  that the Sponge DE launcher menu uses (AGENTS.md §3.3 rule 5).

Verified boot output (base-linux and base-sel4; the `vct status`
output below was captured at the original status milestone and still
matches the current implementation):

```
Genode 26.05
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] === Sponge OS status ===
[init -> vct] init RAM:    512K / 8M (avail 7M)
[init -> vct] init caps:   45 / 200 (avail 155)
[init -> vct] components:  1
```

For the full design, see
[`docs/06-vct.md`](../../../../docs/06-vct.md); the Phase 7 plan is
in `.omo/plans/phase7-alpha.md`.

## Source layout

```
src/vct/
├── target.mk            # Genode build target (LIBS += sponge_backend_client)
├── main.cc              # Component::construct entry point
├── vct_main.{h,cc}      # Vct::Main: config ROM read + dispatch
├── args.{h,cc}          # Config-ROM argument parser (<args><arg/>...</args>)
├── command.h            # Command interface (execute takes Args const &)
├── command_router.{h,cc}# Subcommand dispatch (Args.subcommand -> Command)
├── commands.{h,cc}      # Help/Version/Status/ComponentList/Install/Remove/List
├── init_state.{h,cc}    # InitStateReader: live init state report parser
└── README.md            # this file
```

## Design notes

- vct is designed as a short-lived component (created and destroyed
  every time the user runs it).
- All real work is delegated to the backend services (`sponge_pkgd`,
  `sponge_configd`, ...). vct itself is a thin CLI.
- vct receives its arguments through a config ROM provided by `init`
  (see `docs/06-vct.md` §3 for the locked design).
- Live init state is read from a ROM module labelled "state", which is
  produced by a `report_rom` component relaying the state report of a
  nested `init` (the standard Genode pattern used by sculpt_manager).
- The Report/ROM client used to talk to the backends lives in the
  shared library `sponge_backend_client`
  (`lib/src/sponge_backend_client/`, header
  `<sponge/backend_client.h>`, class `Sponge::Backend::ReportRomClient`).
  It is shared with sponge-de's launcher so the two components always
  agree on the channel's request/result shapes.
