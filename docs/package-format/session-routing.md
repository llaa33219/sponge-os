# 12 - Package Format: Session Routing and `--explain` Mapping (§7, §8)

> **Breadcrumb:** [`docs/12-package-format.md`](../12-package-format.md)
> holds §1-§3 and §10-§12; this file holds §7 and §8. Section numbers
> are preserved across the split, so `§N` references resolve against the
> whole document set (the parent's part table links every part).

---

## 7. Session Routing Defaults

A package declares the sessions it needs and the service each one
should land at by default. `sponge_pkgd` generates a routing entry per
session and writes it into the `init` configuration of the runtime that
hosts the package (§4.5) alongside the component node it just created.

### 7.1 The Three Layers of Routing

Routing decisions are taken in this order, highest priority first:

| Layer | Source | Override semantics |
|---|---|---|
| 1. **User override** | `vct component route <pkg> --set <session>=<target>` | Wins always. The user has the final word. |
| 2. **Package metadata** | The `<session default-route="...">` attribute | The package author's recommendation. |
| 3. **System policy (`sponge_pkgd`)** | A built-in fallback table (e.g. `Gui -> nitpicker`, `Input -> input_drv`, `Report -> report_rom`) | Used only when metadata says nothing. |

This hierarchy is the "automation default, control escape hatch" rule
from `docs/02-philosophy.md` §3.3 applied to session routing. The
metadata is the "automation default"; the user override is the "door
that is always open".

### 7.2 Generation Rules

When `sponge_pkgd` materializes the `<session>` block from a metadata
file, the generated `init` config uses Genode's `<route>` notation,
mirroring the style used in `run/sponge-minimal.run`. These rules are
implemented by the Phase 7 runtime-config generator, not only defined by
the metadata format:

```xml
<start name="nano">
  <resource name="RAM" quantum="48M"/>
  <config> ... </config>
  <route>
    <service name="Gui">         <parent/>  </service>
    <service name="Input">       <parent/>  </service>
    <service name="File_system"> <parent/>  </service>
    <any-service> <parent/> </any-service>
  </route>
</start>
```

The conversion rules are:

1. **Session route selection.** `<session name="X" default-route="Y">`
   becomes `<service name="X"><child name="Y"/></service>` when `Y`
   names a child hosted inside the same nested runtime init that hosts
   the package (§4.5). The value
   `parent`, or the name of an outer-system service such as `nitpicker`,
   `vfs`, or `event_filter`, becomes `<service name="X"><parent/></service>`.
   It is never emitted as `<child name="nitpicker"/>`, `<child name="vfs"/>`,
   or another child route. The same parent-route notation applies to a
   user route override.
2. **Session labels and read-only materialization.** A session `label`
   attribute is emitted on the generated route service as Genode's label
   hint. For a read-only `File_system` session, `readonly="yes"` selects
   the package label suffix `<pkg>-ro`. An explicit `label` remains the
   label source when provided, while the generated `vfs` policy still
   records the package's read-only intent.
3. **VFS subpath materialization.** `subpath="..."` on a `File_system`
   session emits a matching policy in `vfs`'s generated configuration,
   with `root="<subpath>"` and `writeable="no"` for a read-only session.
   The policy label is the materialized session label, normally
   `<pkg>-ro`. This rule is generated as part of the same plan.
4. **Inline package configuration.** When metadata contains `<config>`,
   its inner XML is copied verbatim into the package's `<start>` node.
   The generator does not reinterpret or flatten the fragment, so Qt/libc
   applications can provide their required libc, vfs, font, and plugin
   configuration in one package metadata file.

For parent routes, the hosting app runtime extends its
`<parent-provides>` list so these services can resolve through the outer
system. The final list is the same in all three app runtimes: `ROM`,
`PD`, `CPU`, `LOG`, `Timer`, `Gui`, `Input`, `Report`, `File_system`,
and `NIC`. Each app runtime unions only its own members' declared
sessions onto that base set, and the class split grants nothing new:
hardware services stay driver-only (§4.5). Phase 7 adds `Gui`, `Input`,
`Report`, `File_system`, and `NIC` to the existing
`ROM`/`PD`/`CPU`/`LOG`/`Timer` list, retaining `Timer` rather than
adding a duplicate.

### 7.3 Inspecting and Editing

Every routing decision is inspectable. The generated `<start>` block
becomes part of the live `init` configuration, which means:

- `vct component route <pkg>` prints the current routes for `<pkg>`.
- `vct component config <pkg>` prints the full `<start>` node.
- `vct leitzentrale` opens the Leitzentrale window for direct
  graphical editing (Phase 6; the format supports it from day one).

These three inspection paths correspond exactly to the three escape
hatch forms in `docs/02-philosophy.md` §3.3.

---

## 8. Mapping to `--explain` Output

The four-step plan in `docs/06-vct.md` §5.2 is produced directly from
a metadata file plus its expanded dependency graph. This section walks
the trace end-to-end so the format and the plan can never drift apart.

Take a `firefox` package whose `metadata.xml` declares:

```xml
<package>
  <name>firefox</name>
  <version>118.0</version>
  <description>Mozilla Firefox web browser.</description>

  <binary>firefox</binary>
  <quota ram="512M" caps="2000"/>

  <launcher category="Internet"/>

  <dependencies>
    <pkg>nss_drv</pkg>
    <pkg>libnss</pkg>
  </dependencies>

  <sessions>
    <session name="Gui"          default-route="nitpicker"/>
    <session name="Input"        default-route="input_drv"/>
    <session name="File_system"  default-route="vfs"
             readonly="yes" subpath="/app/firefox"/>
  </sessions>
</package>
```

### 8.1 Step 1: Fetch Package Metadata

Reads `pkg/firefox/metadata.xml`. Prints the three identity fields and
the `<dependencies>` list:

```
1. Fetch package metadata
   - firefox 118.0
   - Dependencies: nss_drv, libnss
```

### 8.2 Step 2: Add to Component Tree

Runs §6's resolver with `root = firefox`. `nss_drv` is already a child
of `init` (it shipped with the base image), so it is skipped via the
`installed` set and annotated as `(already present, reused)`. `libnss`
and `firefox` are new:

```
2. Add to component tree
   Under init:
     - firefox (requires Gui, Input, File_system sessions)
     - nss_drv (already present, reused)
```

The "(requires ... sessions)" parenthetical is generated from the
`<sessions>` block.

### 8.3 Step 3: Configure Session Routing

Walks every `<session>` in `firefox`'s metadata and emits the
corresponding `<service>` entry under the new `<start name="firefox">`
node:

```
3. Configure session routing
   firefox.Gui         -> nitpicker
   firefox.Input       -> input_drv
   firefox.File_system -> vfs (read-only: /app/firefox)
```

If a user override exists (from layer 1 of §7.1), it appears here
instead of the metadata default.

### 8.4 Step 4: Register Launcher Entry

Reads `<launcher category="...">` and registers an entry with
`sponge_configd` so the Sponge DE launcher shows a button in the right
group:

```
4. Register launcher entry
   Category: Internet
```

Omitting `<launcher>` makes step 4 disappear entirely from the plan.

### 8.5 Summary

| `--explain` step | Source in metadata | Resolution rule |
|---|---|---|
| 1. Fetch package metadata | `<name>`, `<version>`, `<description>`, `<dependencies>` | Direct read. |
| 2. Add to component tree | `<dependencies>` | §6 DFS expansion with reuse check. |
| 3. Configure session routing | `<sessions>` | §7 three-layer routing. |
| 4. Register launcher entry | `<launcher>` | Pass-through to `sponge_configd`. |

The same four sources power `vct install firefox`,
`vct install firefox --explain`, and `vct install firefox --manual`.
Only the level of automation differs.
