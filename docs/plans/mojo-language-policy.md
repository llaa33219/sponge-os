# Mojo Language Policy (Decision Record)

> Status: user-approved documentation-only policy change, approved and
> registered 2026-11-16. Decision IDs here are **DLP.x**.
> Evidence basis: [`../evidence/mojo-ffi-probe.md`](../evidence/mojo-ffi-probe.md)
> (seL4 QEMU receipts for the only Mojo-on-Genode path proven so far).
> This record changes policy text only: it renames nothing, implements
> nothing, and makes no runtime claim beyond what the evidence file
> records. Downstream AGENTS.md, architecture, and development-doc
> updates follow this record; they do not precede it.

---

## 1. Goal

Make **Mojo the preferred and default language for Sponge-owned code**,
both host-side tooling and Genode/seL4 components, subject to the honest
support boundary the probe evidence establishes. The goal is one
language where one language is feasible, with C++ kept as a deliberate
adapter tool, not as the default.

## 2. Recorded User Approval (2026-11-16, binding)

The user approved this documentation-only policy change making Mojo the
preferred/default language for Sponge-owned host and Genode/seL4 code.
Do not re-litigate the decisions in §4.

## 3. Evidence Baseline (2026-11-16, probe truth)

The supported subset is exactly what
[`../evidence/mojo-ffi-probe.md`](../evidence/mojo-ffi-probe.md)
records, and nothing more:

- **Proven:** `mojo build --emit object` (experimental) plus
  `@export`/`abi("C")` entry points linked into a Genode component via
  `SRC_O`; a libc-free `Genode::Heap` KGEN shim feeding stdlib
  `List[Int64]`; receipts `mojo-ffi-probe: PASS sum=42` and
  `mojo-ffi-probe: PASS add=42 list-sum=45` on seL4 QEMU;
  `./tool/build run sponge-minimal` green in the same environment.
- **Binding caveats:** `--emit object` is experimental, KGEN symbols are
  version-sensitive, and no freestanding stdlib profile exists.
- **Not proven, never claimed from this record:** async, Python interop,
  POSIX I/O, and pure-Mojo Genode session plumbing (component entry,
  parent sessions, RPC). Those need their own probes with their own
  receipts before any policy line leans on them.

## 4. Binding Decisions (do not re-litigate)

| ID | Decision | Rationale |
|---|---|---|
| **DLP.1** | **Mojo is the preferred and default language for all Sponge-owned code**, host-side tooling and Genode/seL4 components alike. Language choice defaults to Mojo; any C++ in Sponge-owned code needs a reason in this table or in DLP.3/DLP.5. | User approval 2026-11-16. One language across host and target removes the permanent translation tax. The proven FFI path (evidence file §2-§3) makes Mojo on Genode real, not aspirational. |
| **DLP.2** | **Native Mojo is preferred over wrapping C/C++ libraries where feasible.** When a feature can be written natively in Mojo or obtained by binding an existing C/C++ library, prefer the native Mojo implementation. Feasibility is judged against the supported subset in the evidence file, not against wishlists. | A wrapper forever drags the wrapped library's build, license, and ABI into the repo. Native Mojo keeps ownership in one language. "Where feasible" is the escape hatch: when the subset cannot express the work, the binding stays legitimate (DLP.5). |
| **DLP.3** | **New Sponge-owned components default to native Mojo.** A new component in the umbrella `repos/sponge/` or a split Sponge-owned repository starts in Mojo unless a recorded reason says otherwise (unsupported subset, Genode framework surface that only C++ can serve, or a dependency that is itself C++). | New code is where the default language pays off first; there is no legacy to justify C++ in a fresh component. The documented probe subset is the feasibility reference at design time. |
| **DLP.4** | **Existing Sponge-owned C/C++ SHOULD migrate incrementally to Mojo where the required subset is supportable, with tests green at every step.** Each migrated unit lands as its own logical change (AGENTS.md §4.2) and keeps the run scenarios green. Migration is opportunistic and per-unit, triggered by touching that unit or by a dedicated migration task, never scheduled as a flag day. | AGENTS.md §4.2 one-logical-change discipline plus the proven subset. Incremental keeps the tree bootable at every commit; a flag day would strand the whole desktop on one unproven compile. |
| **DLP.5** | **Thin C++ Genode adapters and upstream Genode/Qt code remain valid and are not migration targets.** (a) Thin C++ glue at the Mojo/Genode boundary, such as `Component::construct` entry glue and the `SRC_O` link seam the probe used, stays C++ while pure-Mojo Genode session plumbing is unproven. (b) Everything under `genode/` (upstream Genode, the fork patch set) and upstream Qt code stays as it is; AGENTS.md §5.2 forbids re-implementing Genode and this policy does not touch it. | The evidence file explicitly does not prove pure-Mojo session plumbing. Until a probe says otherwise, the adapter is the honest seam. Upstream code is not Sponge-owned code, so DLP.1 does not reach it. |
| **DLP.6** | **No big-bang rewrite.** There is no repository-wide C++-to-Mojo conversion event, no freeze on C++ commits, and no deadline that forces one. DLP.3 governs new code, DLP.4 governs existing code, and both allow the tree to stay green throughout. | Rewrites that outrun verification produce the exact failures AGENTS.md §5.3 warns about: non-working code that looks like it works. |
| **DLP.7** | **Current-document workflow, mandatory before writing any Mojo:** (1) load the `mojo-syntax` skill; (2) consult the current official Mojo documentation (<https://mojolang.org/docs/>) for the constructs in play; (3) write the code; (4) verify by build and by the relevant run scenario (AGENTS.md §4.2). Pretrained Mojo knowledge is treated as stale by default. | Mojo evolves rapidly and pretrained patterns go stale; AGENTS.md §3.5 already mandates the `mojo-syntax` skill for host tools. The probe's caveats (experimental `--emit object`, version-sensitive KGEN symbols) are concrete proof that the ground shifts between releases, so the docs read is not optional. |
| **DLP.8** | **Approved repository names for the Mojo-first native line: `sponge-wm`, `sponge-de`, `sponge-compositor`.** G0 implements the split under exactly these names as pinned submodules. The compositor repository currently contains only its scaffold; the WM and DE moves are behavior-preserving. | User-approved names recorded here so later plans cannot drift into aliases. The names cover the WM, the desktop environment, and the compositor roles that the Phase 16 native window-management line (D16W.x) introduced. |
| **DLP.9** | **This record began as documentation-only.** Its registration commit shipped no code, rename, migration task, or run-scenario change. Downstream text updates and the later G0 repository split are separate changes that cite this record. The separate [`phase16-sponge-compositor.md`](phase16-sponge-compositor.md) records the approved compositor design; this policy record implements none of its gates. | AGENTS.md §5.4: docs match code. Policy text leads here because the change is itself a policy decision; implementation obligations start with the follow-up updates, not with this file. |

## 5. Supported-Subset Gate (applies to every DLP.3/DLP.4 decision)

Before a component or a migration unit is committed as "native Mojo",
its required language and runtime features must sit inside the proven
subset of [`../evidence/mojo-ffi-probe.md`](../evidence/mojo-ffi-probe.md),
or carry a new probe receipt that extends it. As of 2026-11-16 that
means:

- The C-ABI object path (`--emit object`, `@export`/`abi("C")`, `SRC_O`
  linkage, C++ entry glue) is available, with the experimental-status
  caveat accepted at each toolchain bump.
- Allocation is available through the libc-free `Genode::Heap` KGEN
  shim; stdlib container work like `List[Int64]` is in scope.
- Async, Python interop, POSIX I/O, and pure-Mojo Genode session
  plumbing are **out of scope until probed**. A design that needs one of
  them either probes it first or keeps that seam in C++ (DLP.5).

## 6. Non-Goals

- **No compositor implementation in this record.** The separate
  [`phase16-sponge-compositor.md`](phase16-sponge-compositor.md) records
  the approved design, but this policy change implements none of it.
- **No rename or restructure in this change.** No component directory,
  target name, or repository is touched.
- **No claim of unproven Mojo support.** Async, Python interop, POSIX
  I/O, and pure-Mojo Genode session plumbing are not claimed anywhere in
  this record.
- **No change to host-tool policy beyond the default flip.** `tool/`
  scripts were already Mojo (AGENTS.md §3.5); that stays.

## 7. Open Questions (recorded, not blocking)

- When a probe shows pure-Mojo Genode session plumbing working, does the
  C++ entry glue of DLP.5(a) shrink to zero or remain as a stable seam?
  Not decided here.
- Does the experimental `--emit object` path graduate to stable before
  or after the first migrated component depends on it? Track against the
  official compilation docs (DLP.7 step 2).
- KGEN symbol churn policy: re-probe on every toolchain bump is the
  default (DLP.7), but who owns the pin in `docs/11-environment.md`?
  Recorded for the follow-up text updates (DLP.9).
