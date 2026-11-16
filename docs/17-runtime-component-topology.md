# Runtime Component Topology — the 0.2.0 product media

> Verified against `run/sponge-desktop-disk-uefi-usb.run` (17:35 image
> lineage). This is the inventory of every component above the seL4
> kernel, what it does, and — critically — which shared brokers the
> "isolated" components actually depend on. Written after the 2026-10-04
> real-hardware regression (vct_tty's missing `<provides>` froze the
> whole desktop): the isolation model held, but liveness did not, and
> the difference is the point of this document.

> **Document structure.** The section bodies moved verbatim into
> `docs/topology/`. The numbered stubs below are the map: each stub
> names the file and section that now holds the content, so every
> `docs/17 §N` reference still resolves.

## 1. The tree

Content moved verbatim to [`docs/topology/tree-and-chains.md`](topology/tree-and-chains.md) §1.

## 2. The three data chains

Content moved verbatim to [`docs/topology/tree-and-chains.md`](topology/tree-and-chains.md) §2.

## 2a. The storage providers — three-area layout and the `vfs_pkg` / `vfs_log` split

Content moved verbatim to [`docs/topology/tree-and-chains.md`](topology/tree-and-chains.md) §2a.

## 3. Why a terminal component froze the desktop (the honest analysis)

Content moved verbatim to [`docs/topology/incident-analyses.md`](topology/incident-analyses.md) §3.

## 4. The mouse follow-up (17:35 image) — topology says: suspicious

Content moved verbatim to [`docs/topology/incident-analyses.md`](topology/incident-analyses.md) §4.

## 4a. The SPONGE DEBUG scroll-storm report (2026-10-15) — measured: saturation, not a leak

Content moved verbatim to [`docs/topology/incident-analyses.md`](topology/incident-analyses.md) §4a.

## 5. Design gap register (what "microkernel-proper" would add)

Content moved verbatim to [`docs/topology/design-gap-register.md`](topology/design-gap-register.md) (part 1: the freeze and liveness rows) and [`docs/topology/design-gap-register-2.md`](topology/design-gap-register-2.md) (part 2: the containment, supervision and policy rows).

## 6. Quota ledger (the five desktop-disk scenarios)

Content moved verbatim to [`docs/topology/quota-ledger.md`](topology/quota-ledger.md), including §6.0 and §6.1.

### 6.0 `driver_runtime`'s grant

Content moved verbatim to [`docs/topology/quota-ledger.md`](topology/quota-ledger.md) §6.0.

### 6.1 The remainder clamp (RAM only)

Content moved verbatim to [`docs/topology/quota-ledger.md`](topology/quota-ledger.md) §6.1.
