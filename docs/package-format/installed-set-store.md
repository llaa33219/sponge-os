# 12 - Package Format: Installed-Set Store (§13)

> **Breadcrumb:** [`docs/12-package-format.md`](../12-package-format.md)
> holds §1-§3 and §10-§12; this file holds §13. Section numbers are
> preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

---

## 13. Installed-Set Store (Runtime Persistence)

> Phase 4 follow-up #2. Closes the "installs do not survive reboot"
> limitation from `docs/09-roadmap.md` §6.

§1–§12 define the *static* package inputs (`metadata.xml`, the
repository layout, the resolver). This section defines the one piece of
*runtime* state the package subsystem writes: the set of packages a
specific system has explicitly installed, so a reboot restores them
without user action.

The store holds **only the explicitly-installed root names**. The full
installed set (roots plus transitive dependencies) is never persisted:
it is re-derived on load by the same deterministic resolver
(`_sync_installed_from_roots`) that `install`/`remove` already use, so
the state→config generator is untouched and a dependency whose
metadata changed between reboots is resolved against the current
metadata, not a stale snapshot.

### 13.1 Format

A single XML file, by default `/store/installed.xml` on whatever
`File_system` session the deployment routes to `sponge_pkgd`:

```xml
<sponge-installed version="1">
  <root name="hello"/>
  <root name="nano"/>
</sponge-installed>
```

- The root element is always `sponge-installed`; the `version`
  attribute is the format version (currently `1`).
- One `<root name="…"/>` per explicitly-installed package, in the
  user's install history. Roots are stored **name-sorted** on write so
  the file is byte-stable for a given set (matching the determinism
  contract of the runtime-config generator, §7.2). Order is not
  semantically meaningful — the resolver re-sorts.
- There is no timestamp, no counter, no per-package version pin, and no
  dependency list. A future version may add version pins (§10); the
  `version` attribute is the upgrade seam.

The file is plain UTF-8 XML with no binary content, so it is
human-readable and `grep`/`diff`-able — a user can inspect exactly what
will be restored.

### 13.2 Failure Semantics

Every failure mode resolves to the **same safe state**: an empty root
set plus a `Genode::warning`, never a crash. Specifically:

| Condition | Behaviour |
|---|---|
| Store file absent (fresh system / first boot) | Start empty; logged as informational. |
| Store file empty or larger than the internal buffer | Start empty; warning. |
| Store unreadable (I/O error) | Start empty; warning. |
| Root element not `sponge-installed` | Start empty; warning. |
| `version` attribute missing or unsupported | Start empty; warning. |
| Malformed XML (truncated / torn write) | Start empty; warning. |
| A `<root>` names a package no longer in the repository | That root is dropped at resolution time (the resolver reports it missing); the rest are restored. |

There is deliberately **no checksum**: a torn write (power loss
mid-`save`) is detected as malformed XML on the next boot and the
daemon restarts from empty rather than trusting partial data. This
trades "lose the last install on a crash" for "never restore garbage".
Crash-consistent writes (write-temp-then-rename, or a checksum) are a
documented future improvement, not a Phase 4 requirement.

### 13.3 Write-Back Points

The store is rewritten **after every successful `install` and `remove`,
before the change is broadcast** to watchers (the affected runtime config and
the installed-set report). This ordering means a reboot between the
`install` return and a watcher reading the new state still observes the
install: the durable copy was flushed first.

A failed write (e.g. the backing file system is full or read-only) is
logged but **never blocks** the install/remove: the in-memory state and
the broadcast still reflect the requested change, only the across-reboot
durability is lost for that one mutation. The next successful mutation
re-persists the full current root set.

The store is **single-writer**: there is exactly one `sponge_pkgd` per
system and it serializes all requests, so concurrent writers are not a
concern. Multiple systems sharing one backing store is unsupported (the
format has no system identity and the last writer would win).

### 13.4 Activation and the Control Escape Hatch

Persistence is **opt-in per deployment**, following the project's three
philosophies (`docs/02-philosophy.md`):

- **Automation = default when configured.** Once `sponge_pkgd`'s
  `<config>` carries a `<vfs>` node mounting a writable `File_system`
  session, persistence is automatic: load-on-construct,
  save-on-mutation. No per-command flag, no user action per install.
- **Control = a door that is always open.** The store is a plain XML
  file on the backing `File_system`. A user can:
  - **Inspect** it directly (`cat …/installed.xml`) to see exactly what
    will be restored.
  - **Edit** it (add/remove `<root>` lines) to change what the next boot
    restores.
  - **Reset** it (delete the file, or empty it to `<sponge-installed
    version="1"/>`) to clear the installed set.
  - **Disable** persistence entirely by removing the `<vfs>` node from
    `sponge_pkgd`'s `<config>`, which reverts to the in-memory Phase 4
    behaviour (installs live only for the session).
- **Convenience.** The common case (installs should survive reboot)
  needs no user action beyond the one-time `<vfs>` wiring; the escape
  hatches are for the uncommon cases.

When `<config>` has no `<vfs>` node — the Phase 4 scenarios, or any
deployment that declines persistence — `sponge_pkgd` opens no
`File_system` session and behaves byte-identically to the in-memory
daemon. This keeps the non-persistent scenarios and any minimal
deployment working unchanged (minimum privilege, AGENTS.md §1.2).

The reference two-boot proof is `run/sponge-pkg-persist.run`
(base-linux, `lx_fs`-backed): boot 1 installs a package and writes the
store; boot 2 boots a fresh `core`/`init` against the same backing
directory, issues a `list`-only request, and asserts the package is
present — which can only be the case if it was restored from the store.

### 13.5 Bake-Manifest Seeding (First Boot)

Product media stage a bake manifest on the disk (generated by
`tool/bake` from `pkg/bake/<profile>.profile`; see
`docs/11-environment.md` for the staging layout). When `sponge_pkgd`'s
`<config>` carries a `<bake/>` node and the restored root set is empty
(fresh boot, or an unrestored deployment), the daemon seeds every
`staged_packages` manifest entry as an install root before the first
installed-set broadcast — so the shipped media presents the pre-staged
desktop (launcher entries, runtime configs) with no user action.
This is the package-side counterpart of `sponge_configd`'s boot-time
bake-defaults application.

The seeding is gated on the empty root set, so a persistent store
(§13) keeps the user's own install/remove decisions authoritative
across reboots. Scenarios without the `<bake/>` node or the
`bake_manifest` ROM route behave exactly as before.
