# 08 - Development Guide

> How to develop, build, run, and test Sponge OS against the vendored
> Genode tree.
>
> The **environment contract** (what is vendored, what is pinned, what
> is git-ignored, and the manual fallback for every automated step)
> lives in [`docs/11-environment.md`](11-environment.md). This document
> is the workflow on top of that contract.

---

## 1. Prerequisites

Moved to [`development/prerequisites.md`](development/prerequisites.md) (§1.1-§1.5).

---

## 2. Repository Structure Overview

```
sponge-os/
├── README.md              # User-facing introduction
├── AGENTS.md              # Contribution guidelines (top of doc hierarchy)
├── docs/                  # Design documents (incl. this file and 11-environment.md)
├── genode/                # Vendored Genode 26.05 (git subtree, pinned)
│   ├── repos/             # Upstream Genode repos
│   │   └── sponge -> ../../repos/sponge  # committed relative symlink
│   └── tool/, etc/, ...   # Genode's own helpers
├── repos/
│   └── sponge/            # The Sponge OS repo (Genode repo convention)
│       ├── src/           # Component sources (vct, sponge-de, sponge_launcher)
│       ├── lib/           # Shared libraries
│       ├── include/sponge/# Shared headers
│       ├── tool/          # Sponge-owned compiler wrappers (genode-x86-{gcc,g++}-wrapper)
│       └── run/           # Relative symlinks → ../../../run/ (one per .run scenario)
├── tool/                  # Build and dev helpers (Mojo: build, check-compile, ...)
├── run/                   # Genode run scenarios (sponge-minimal, sponge-de, ...)
└── var/                   # Local caches (git-ignored): qt6 host tools, distfiles
```

For the meaning of each directory and which paths are git-ignored, see
`AGENTS.md` §2 and [`docs/11-environment.md`](11-environment.md) §2.

---

## 3. Build

Moved to [`development/build.md`](development/build.md) (§3.1-§3.8).

---

## 4. Run Scripts (Scenarios)

Moved to [`development/run-scripts.md`](development/run-scripts.md) (§4.0.1, §4.1-§4.5).

---

## 5. Testing

Moved to [`development/testing-and-workflow.md`](development/testing-and-workflow.md) (§5.1-§5.3).

---

## 6. Coding Workflow

Moved to [`development/testing-and-workflow.md`](development/testing-and-workflow.md) with §5 and §7-§10.

---

## 7. Coding Rules Summary

Moved to [`development/testing-and-workflow.md`](development/testing-and-workflow.md) with §5, §6, and §8-§10.

---

## 8. Contribution Workflow

Moved to [`development/testing-and-workflow.md`](development/testing-and-workflow.md) (§8.1-§8.3).

---

## 9. Documentation Rules

Moved to [`development/testing-and-workflow.md`](development/testing-and-workflow.md) with §5-§8 and §10.

---

## 10. Debugging

Moved to [`development/testing-and-workflow.md`](development/testing-and-workflow.md) with §5-§9.

---

## 11. Building Distribution Media (Alpha)

Moved to [`development/dist-media.md`](development/dist-media.md) (§11.1-§11.4).

## 12. Importing Depot Packages (Alpha)

Moved to [`development/depot-packages.md`](development/depot-packages.md) (§12.1-§12.4). A citation of `docs/08-development.md §12.1` resolves to §12.1 of that file.



## 16. Hardware Matrix (headless pre-real-hw gate)

Moved to [`development/hardware-matrix.md`](development/hardware-matrix.md).
