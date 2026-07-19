# sponge_launcher — Sponge OS Application Launcher

The launcher component that runs user-installed applications.

## Current status

🟡 Phase 0 scaffold. It will start out integrated into `sponge-de`.
Splitting it off into a separate component is considered once
`sponge-de` stabilizes (see `AGENTS.md` §3.4).

## Role (planned)

- Query the installed package list from `sponge_pkgd`
- Run apps on user click or keyboard input
- Category menu (Internet, Development, Media, ...)
- Search

## Build

This component has no `target.mk` yet. Development proceeds in the
form integrated into `sponge-de`; once the split is decided, a
separate target is added in this directory.

See [`docs/04-components.md`](../../../../docs/04-components.md) and
[`docs/05-sponge-de.md`](../../../../docs/05-sponge-de.md) for the
details.