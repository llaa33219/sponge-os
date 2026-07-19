# Sponge OS shared libraries

This directory holds the sources of the libraries that multiple
Sponge OS components link against. The Genode build system defines a
library through `lib/mk/<lib>.mk` and places its sources under
`lib/src/<lib>/`.

## Current status

🟡 Early stage — no shared libraries yet.

## Planned libraries

The following libraries will be added incrementally:

- `sponge_rpc` — RPC interface bindings between backend services and
  UI components. Lets vct and sponge-de call the same backends
  consistently.
- `sponge_pkg_metadata` — package metadata parsing (Phase 4).
- `sponge_config_store` — storage, validation, and application
  helpers for configuration (Phase 4).

Whenever a library is added, create `lib/mk/<lib>.mk` and place its
sources under `lib/src/<lib>/`. For the exact format, see the
[Genode library guide](https://genode.org/documentation/developer-resources/porting).