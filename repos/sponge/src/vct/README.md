# vct — Very Convenient Tool

The single entry point for managing Sponge OS.

## Role

- Install / remove / update packages
- Manage component configuration and session routing
- Query system state
- Enter the Leitzentrale window (`vct leitzentrale`)

For the full design, see [`docs/06-vct.md`](../../../../docs/06-vct.md).

## Current status

🟢 Phase 2 minimum working, verified booting on Genode 26.05 on both
base-linux and base-sel4. The following is implemented and runs as a
real Genode component:

- Version output (`vct --version` / `vct version`)
- Startup message
- Config-ROM argument parsing (`args.h` / `args.cc`)
- Subcommand dispatch for `status`, `help`, `version`, `component list`
  (`command_router.cc`)
- `--json`, `--verbose`, `--lang ko` flags honored
- `vct status` reads the live init state report via a sub-init +
  `report_rom` relay and prints real RAM, caps, and component count
- `vct component list` enumerates the live component tree with per-child
  RAM and cap usage

Verified boot output (base-linux and base-sel4, see
`docs/09-roadmap.md` Phase 2):

```
Genode 26.05
[init -> vct] vct (0.0.1-pre-alpha / Archaeocyte) starting
[init -> vct] === Sponge OS status ===
[init -> vct] init RAM:    512K / 8M (avail 7M)
[init -> vct] init caps:   45 / 200 (avail 155)
[init -> vct] components:  1
```

Backend integration (`sponge_pkgd`, `sponge_configd`, ...) lands in
Phase 4 (`docs/09-roadmap.md`).

## Source layout

```
src/vct/
├── target.mk            # Genode build target
├── main.cc              # Component::construct entry point
├── vct_main.{h,cc}      # Vct::Main: config ROM read + dispatch
├── args.{h,cc}          # Config-ROM argument parser (<args><arg/>...</args>)
├── command.h            # Command interface (execute takes Args const &)
├── command_router.{h,cc}# Subcommand dispatch (Args.subcommand -> Command)
├── commands.{h,cc}      # Help/Version/Status/ComponentList/Install/Remove/List
├── init_state.{h,cc}    # InitStateReader: live init state report parser
├── pkg_client.{h,cc}    # PkgClient: Report/ROM client for sponge_pkgd
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
