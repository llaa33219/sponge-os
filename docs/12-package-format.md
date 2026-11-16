# 12 - Package Format and Local Repository

> This document defines the Sponge OS package metadata format and the
> layout of the local package repository used in Phase 4 (the
> `sponge_pkgd` milestone). The format is intentionally minimal: it
> covers only what `vct install` needs in Phase 4, and it leaves remote
> repositories, signatures, and version constraints as documented future
> extensions (see §10).

---


**Document parts.** The metadata format, repository layout, dependency
resolution, session routing, and the §13/§14 extension sections are
maintained in part files under `docs/package-format/`. Section numbers
continue across this parent and its parts; every section below either
lives here or links to its part file.

| Sections | File |
|---|---|
| §1-§3, §10-§12 | this file |
| §4 Metadata File Format | [`docs/package-format/metadata-format.md`](package-format/metadata-format.md) |
| §5 Local Repository Layout, §9 Repository-Level Concerns | [`docs/package-format/repo-layout.md`](package-format/repo-layout.md) |
| §6 Dependency Resolution | [`docs/package-format/dependency-resolution.md`](package-format/dependency-resolution.md) |
| §7 Session Routing Defaults, §8 Mapping to `--explain` Output | [`docs/package-format/session-routing.md`](package-format/session-routing.md) |
| §13 Installed-Set Store (Runtime Persistence) | [`docs/package-format/installed-set-store.md`](package-format/installed-set-store.md) |
| §14 Provider-Owned Settings Entries | [`docs/package-format/provider-settings.md`](package-format/provider-settings.md) |

---

## 1. Scope

Phase 4 reads packages from a single local directory and writes their
configuration into the live `init` component tree through the same path
that `vct component list` reads (see `docs/03-architecture.md` §4.1 and
`docs/06-vct.md` §5).

In scope for this document:

- The shape and content of one package's metadata file.
- The directory layout of the local repository on disk and inside the
  boot image.
- The deterministic algorithm for resolving dependencies.
- How a package's required sessions are routed by default, and how
  those defaults can be overridden.
- How the four `--explain` steps in `docs/06-vct.md` §5.2 are produced
  from a metadata file plus its expanded dependency graph.

Out of scope (see §10):

- Remote repositories, mirrors, and content addressing.
- Signatures, hashes, and trust.
- Version constraints (`>=`, `~=`, conflict sets).
- The RPC interface of `sponge_pkgd` (a parallel design task owns it).
- The architectural wiring between vct, sponge_pkgd, and init.

This document deliberately talks only about files, directories, and
resolution semantics, so it stays compatible with any RPC or process
shape the backend decides on.

---

## 2. Design Principles

Every format decision is grounded in the three philosophies
(`docs/02-philosophy.md`). They are not trade-offs here either.

| Principle | How the format satisfies it |
|---|---|
| **Convenience** | Sensible defaults are encoded in the metadata itself (route, quota, launcher category), so `vct install <pkg>` works with one argument. |
| **Control** | Every default the metadata declares is overridable. Users inspect and edit raw XML, change routes through `vct component route`, or skip the metadata entirely through `vct component start`. |
| **Automation** | The metadata is parseable deterministically. `sponge_pkgd` consumes the same file that `--explain` shows; there is no second source of truth. |
| **Transparency** | The metadata file is plain text, lives on disk, and is the only thing `vct install` reads about a package. There are no hidden indices or compiled caches. |

---

## 3. Format Choice

Three structured-text candidates were considered.

| Candidate | Pros | Cons |
|---|---|---|
| **INI** | Tiny, used by `docs/10-theme-format.md`. | Consumers (`sponge_pkgd`, the generated `init` config) are Genode components that already speak XML natively. INI would need a second parser. |
| **JSON** | Universally known. | Requires a JSON parser in Genode; the Genode `util/xml_node.h` helper is already linked into every component, and JSON has no first-class story for nested attributes. |
| **Genode-style XML** | Already used by every `init` config and every component runtime declaration. Parsed by `Genode::Xml_node` (`genode/repos/base/include/util/xml_node.h`). The same parser that reads `init`'s config can read package metadata. | Slightly more verbose. |

**Decision: Genode-style XML.** The metadata format must be consumable
by Genode components (`sponge_pkgd`, init config generation, and any
future tooling that lives inside the boot image). XML is the format
those components already parse, so adopting it removes one translation
layer and keeps the package metadata inspectable in the same tools
(`xml_node` introspection, the init `<config>` relay) that vct already
uses.

INI remains the right call for `docs/10-theme-format.md` because the
theme format is consumed only by `sponge-de` (a Qt component), where
human-editability outweighs parser reuse.

---

## 4. Metadata File Format

Every package carries exactly one `metadata.xml`. The grammar, the
annotated `hello` and `nano` examples, error handling, the
`app`/`driver` package kinds with runtime classes, and exclusive
groups are specified in
[`docs/package-format/metadata-format.md`](package-format/metadata-format.md)
(§4.1-§4.6).

---

## 5. Local Repository Layout

The `pkg/` tree on the developer host, the boot-image view, the
boot-time manifest (`pkg_index.xml`), and Alpha install semantics are
specified in [`docs/package-format/repo-layout.md`](package-format/repo-layout.md)
(§5.1-§5.5), together with the repository-level concerns of §9.

---

## 6. Dependency Resolution

The deterministic depth-first resolver with cycle detection, its
Phase-4 non-goals, and its error table are specified in
[`docs/package-format/dependency-resolution.md`](package-format/dependency-resolution.md)
(§6.1-§6.3).

---

## 7. Session Routing Defaults

The three layers of routing, the generation rules the Phase 7
runtime-config generator implements, and the inspection and editing
escape hatches are specified in
[`docs/package-format/session-routing.md`](package-format/session-routing.md)
(§7.1-§7.3).

---

## 8. Mapping to `--explain` Output

The four-step `--explain` plan walked end-to-end against a worked
`firefox` metadata file lives in
[`docs/package-format/session-routing.md`](package-format/session-routing.md)
(§8.1-§8.5).

---

## 9. Repository-Level Concerns

Where the repository lives, installed-versus-running semantics,
update and removal behavior, and the no-hidden-state rule are
specified in [`docs/package-format/repo-layout.md`](package-format/repo-layout.md)
(§9.1-§9.3).

---

## 10. Evolution Path

The following features were excluded from the original Phase 4 format.
Some are still future work, while Phase 7 has delivered the host-side depot
interop described below. They are listed here so future contributors know
what was considered, and so the metadata schema does not accidentally close
any door.

| Feature | Sketch | Why deferred |
|---|---|---|
| Remote repositories | A second config ROM names a list of remote roots fetched over a Sponge-controlled transport. | Phase 4 only needs a local directory; remote adds failure modes (offline, partial, signature) that are easier to reason about once the local path is stable. |
| Genode depot archive interop | **Delivered in Phase 7 as host-side repackaging.** `tool/pkg_import` converts a downloaded depot package into `pkg/<name>/`, writes the package metadata and payload layout, and records a `SOURCE` pin file for the depot reference and content fingerprint. | Runtime fetching is explicitly not part of Alpha. The downloaded archive must be available to the host-side import step, and `sponge_pkgd` never contacts a depot or other network source. |
| Signatures | An optional `<signature>` element with an Ed25519 signature over a canonical form of `metadata.xml` plus the payload hashes. | Requires a trust-root story that is bigger than Phase 4. |
| Version constraints | A `<dependencies>` extension with `<pkg name="foo" version=">=1.0,~=1.2"/>`. | Adds a constraint solver. Phase 4 matches by `<name>` only. |
| Content addressing | A `<hash>` element with the SHA-256 of the payload tree. | Requires a fetch cache that does not exist yet. |
| Mirrors and failover | Multiple roots with priority order. | Builds on the remote-repository support. |
| Per-system overrides | A separate `<pkg-override>` ROM that pkgd consults before applying metadata. | Adds a precedence story that competes with `vct component route`; keep one source of override for now. |
| Lockfiles | A `pkg/lock.xml` recording the resolved set for a run. | Useful for reproducible installs; not needed for single-image development. |
| Provider-owned Settings entries | The `<settings>` element family: 0..N entries per package, each with a stable `id`, `title`, closed-enum `category`, optional `icon`/`keywords`, and a payload-relative `target` binary. | **Implemented in code; specified in §14.** Schema, discovery, lifecycle, privileges, and failure semantics are specified in §14 so the format closes no door. Parsers older than the §14 parser warn-and-ignore the element (additive evolution). |

When any of these land, the rule is to extend the schema
additively: new child elements and new optional attributes are
allowed at any time. Removing or renaming an existing element
requires a major-version bump recorded in `<format version="2">`,
which a parser must respect (matching the
`docs/10-theme-format.md` §6 versioning rule).

---

## 11. Open Design Questions

Per `AGENTS.md` §6, items genuinely undecided at the end of this
document:

- **Repository root configurability.** Should the `pkg/` path be a
  compile-time constant, a `sponge_pkgd` config ROM, or a per-image
  `init` attribute? The format itself does not depend on the answer.
- **Whether `<quota>` is a hint or a hard cap.** `init` enforces the
  RAM and caps values regardless, so the metadata's role is
  documentary. Confirm whether `vct` should refuse to install a
  package whose quota would exceed available resources, or only warn.
- **Launcher category vocabulary.** Categories are free-form strings
  today. A closed enum (`Internet`, `Editors`, `Utilities`,
  `System`, `Games`) would make the launcher UI cleaner but locks the
  schema. Phase 4 keeps them open.
- **How `<dependencies>` interact with `init`'s own child tree.** If
  the user has manually added a `firefox` component under `init` that
  is **not** the one generated by `sponge_pkgd`, should `vct install
  firefox` reuse it, replace it, or refuse? Today the reuse check
  matches on name only, which may not be precise enough.
- **Whether `metadata.xml` is the only file a package carries, or
  whether multi-file packages are needed.** The current layout
  supports a payload directory, but the schema has no formal
  description of which payload files exist. A future
  `<payload manifest="...">` element could solve this without
  breaking the Phase 4 format.

---

## 13. Installed-Set Store (Runtime Persistence)

The one piece of runtime state the package subsystem writes: the XML
store of explicitly-installed roots, its failure semantics, write-back
points, the control escape hatch, and bake-manifest seeding. Specified
in [`docs/package-format/installed-set-store.md`](package-format/installed-set-store.md)
(§13.1-§13.5).

---

## 14. Provider-Owned Settings Entries

The provider-owned Settings contract: entry schema, parse and
validation rules, identity and discovery, launch path and privileges,
configuration ownership, lifecycle, focus, failure, removal, and the
quick-menu operations. Specified in
[`docs/package-format/provider-settings.md`](package-format/provider-settings.md)
(§14.1-§14.8). The authoring workflow is
[`docs/16-package-authoring.md`](16-package-authoring.md) §9.

---

> §12 keeps its number but is placed last in this parent file, after
> the §13 and §14 stubs, so References no longer appears to precede
> content that lives in the part files.

## 12. References

- `docs/02-philosophy.md`: the three philosophies and the escape
  hatch rule.
- `docs/03-architecture.md` §4.1: the data-flow example for
  `vct install firefox`.
- `docs/04-components.md` §1.2: `sponge_pkgd` as a backend service.
- `docs/06-vct.md` §5.2: the canonical `--explain` output the format
  must reproduce.
- `docs/09-roadmap.md` §6: Phase 4 completion criteria.
- `docs/10-theme-format.md`: sister format spec. INI for human-
  authored themes, XML for Genode-consumed metadata.
- `docs/05-sponge-de.md`: the separate `sponge_settings` control
  center design that consumes the §14 contract.
- `docs/06-vct.md` §4.8: the planned `vct settings` command surface
  on the same pkgd metadata/launch backend.
- `docs/16-package-authoring.md` §9: the settings-provider authoring
  workflow built on §14.
- `docs/plans/phase16-runtime-overhaul.md`: D16R.6 and P2.4, the
  runtime-class decision that `<runtime_class>` implements.
- `genode/repos/base/include/util/xml_node.h`: the parser this
  format targets.
