# Sponge OS shared libraries

This directory holds the sources of the libraries that multiple
Sponge OS components would link against. The Genode build system
defines a library through `lib/mk/<lib>.mk` and places its sources
under `lib/src/<lib>/`.

## Current status

🟡 Early stage — no shared libraries yet.

Backend communication does **not** use a shared library. The settled
vct↔backend design (docs/04-components.md §5) is **Report/ROM sessions
bridged by `report_rom`** — the same capability-based IPC the rest of
Genode uses. There is no RPC stub, no IDL, and no `sponge_rpc` library:

- vct writes a *request* report; `report_rom` relays it to the backend
  as a ROM. The backend writes a *result* report; `report_rom` relays it
  back to vct as a ROM.
- vct's thin client for this channel lives in
  `src/vct/pkg_client.{h,cc}` (`PkgClient`), whose request/result ROM
  labels are constructor parameters so the same client reaches both
  `sponge_pkgd` (`request`/`result`) and `sponge_configd`
  (`config_request`/`config_result`).
- `sponge_configd` additionally broadcasts the whole config store as a
  `config` ROM so future watchers (`sponge_themed`, sponge-de) react to
  changes without issuing requests.

Known limitation (recorded, not yet hit): `report_rom` is a
single-writer slot per label, so concurrent callers on the same label
would collide. Each backend therefore uses **distinct** labels
(`request`/`result` vs `config_request`/`config_result`), and a
request-id plus backend-side mutex is deferred to the phase where
concurrent callers actually appear.

## Planned libraries

The following may be extracted later, once genuine cross-component
sharing emerges (avoiding premature abstraction):

- `sponge_pkg_metadata` — package metadata parsing, if more than
  `sponge_pkgd` ever needs to read `pkg_<name>.xml`. Today only
  `sponge_pkgd` parses it, so the code lives in the component.
- `sponge_config_store` — storage/validation/application helpers for
  configuration, if a second writer (beyond `sponge_configd`) appears.
  Today `sponge_configd` owns the only store.

Whenever a library is added, create `lib/mk/<lib>.mk` and place its
sources under `lib/src/<lib>/`. For the exact format, see the
[Genode library guide](https://genode.org/documentation/developer-resources/porting).
