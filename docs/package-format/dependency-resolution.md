# 12 - Package Format: Dependency Resolution (§6)

> **Breadcrumb:** [`docs/12-package-format.md`](../12-package-format.md)
> holds §1-§3 and §10-§12; this file holds §6. Section numbers are
> preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

---

## 6. Dependency Resolution

Resolution is a depth-first expansion with cycle detection, run
deterministically before any side effect is taken. The result is a
topologically ordered list of packages where each package appears
after every package it depends on.

### 6.1 Algorithm

```
resolve(root):
    plan = []
    installed = set(packages currently in the live init tree,
                    read via the same Report session vct status uses)
    visiting = empty set     # packages on the current DFS path
    done     = empty set     # packages already expanded

    visit(root)

    return plan               # in install order (deps first)

visit(name):
    if name in done:       return       # already fully expanded
    if name in installed:  return       # reuse, do not reinstall
    if name in visiting:   error("dependency cycle: <stack>")

    load pkg/<name>/metadata.xml
    add name to visiting
    for each <pkg> child in <dependencies>:
        visit(that_pkg_name)
    remove name from visiting
    add  name to done
    append name to plan
```

Properties:

- **Deterministic.** Same inputs produce the same plan, every run.
- **Cycle-safe.** A cycle in `<dependencies>` produces a clear English
  error naming the cycle path, never a stack overflow.
- **Reuses existing components.** A dependency that is already a child
  of `init` is skipped silently. This is what produces the
  `nss_drv (already present, reused)` line in
  `docs/06-vct.md` §5.2 step 2.
- **Single-pass.** Each package is parsed at most once.

### 6.2 What is Not in Scope for Phase 4

- Version constraints (`>= 1.0`, `~= 2.3`, conflict sets).
- Optional dependencies (`<pkg optional="yes">`).
- Conditional dependencies (`if arch == x86_64`).
- A way to request "do not auto-install this dependency" other than
  the global `--no-deps` flag.

These are listed again in §10 as future extensions.

### 6.3 Errors

Resolution fails with a clear message and no side effects in these
cases:

| Situation | Behavior |
|---|---|
| Requested package does not exist | `error: package '<name>' not found in repository` |
| A dependency names a non-existent package | `error: '<pkg>' requires '<missing>', which is not in the repository` |
| A cycle is detected | `error: dependency cycle: a -> b -> c -> a` (named in order of traversal) |
| A dependency graph contains a duplicate | Treated as one entry; duplicates are coalesced by `<name>` |

The user can then either edit the offending metadata file (control) or
adjust the request (e.g. drop a dependency through a finer-grained
subcommand).
