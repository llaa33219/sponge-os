# sponge_pkgd — package backend daemon

The package-management backend for Sponge OS. It is a long-lived,
signal-driven Genode component that resolves package metadata and
dependency graphs and answers `vct install` requests.

## Communication channel (settled design)

`sponge_pkgd` does **not** expose an RPC interface. It communicates
with `vct` through Report/ROM sessions bridged by `report_rom`
(docs/04-components.md §5, docs/12-package-format.md):

```
vct --[Report "request"]--> report_rom --[ROM "request"]--> sponge_pkgd
sponge_pkgd --[Report "result"]--> report_rom --[ROM "result"]--> vct
```

The corresponding `report_rom` policies (full labels include the
child-name prefix):

```
policy | label: sponge_pkgd -> request | report: vct -> request
policy | label: vct -> result          | report: sponge_pkgd -> result
```

This is the same capability-based IPC idiom the rest of the system
uses; `report_rom` is the decoupling layer.

## What it does today (Phase 4a/4b)

- Watches the `request` ROM via `Attached_rom_dataspace` + `sigh`.
- On change, parses `<request op="..." pkg="..."/>`.
- Resolves the package and its dependencies (deterministic DFS with
  cycle detection, docs/12-package-format.md §6) by consulting the
  `pkg_index.xml` manifest, then opening each package's metadata ROM
  `pkg_<name>.xml`.
- `explain` (4a): emits the structured install plan, no side effects.
- `install` (4b / Phase 7 lifecycle): adds the resolved packages to an
  in-memory installed set and regenerates the pkg_runtime config (a
  nested init). Packages whose metadata declares `<autostart/>` get a
  `<start>` node immediately; everyone else registers as STOPPED and
  needs an explicit `launch` to run (docs/12-package-format.md §9.2.1).
- `launch` (Phase 7): transitions an installed-but-stopped package to
  running by adding its `<start>` node and regenerating. Result status
  is one of `ok` / `not-installed` / `already-running` /
  `binary-missing` / `insufficient-resources`. There is no stop
  operation in Alpha.
- `remove` (4b): drops the package and its now-unused dependencies,
  regenerates the config (also dropping the `<start>` node if the
  package was running).
- `list` (4c): emits the installed set (name-sorted, matching the config
  generator's order) with a per-package `running="yes"|"no"` attribute,
  so callers see exactly what will be regenerated and what is running.
- Emits a structured `<result>` per request.

### Request/result contract

Every request produces exactly one `<result>`. Two rules make that true
even under retries and double-clicks:

**Duplicate requests are answered, not swallowed.** Requests are
de-duplicated on the `op|pkg|seq` signature so a re-delivered ROM
signal cannot re-run `install`/`remove` side effects. The Sponge DE
launcher sends no `seq`, though, so a double-click is a genuine
duplicate — so the last result is cached as a *replay recipe* (the op,
the package, the status, and the added/removed name list) and
re-emitted on the channel the duplicate arrived on. Replay re-invokes
the same result helper with the same arguments, so the second answer is
byte-identical to what a fresh identical request would produce: a second
launch of a running package answers `already-running`, immediately,
instead of leaving the launcher polling until its 6 s timeout.

**`launch` answers `binary-missing` instead of a hopeful `ok`.** A
package can be fully installed (metadata staged, resolvable, present in
`pkg_index.xml`) while its binary was never staged in the image. The
pre-flight check runs **before** the package enters the running set and
before the config is regenerated, so `pkg_runtime` is never asked to
start a child its `init` would silently drop — that path only warns
(`Rom_connection_failed` / `Out_of_ram` / `Out_of_caps` around child
creation in `sandbox/library.cc`).

The check consults an optional `pkg_binaries` manifest ROM, opted in by
a `<binaries/>` node in this component's `<config>`:

```
<binaries>
  <binary name="hello"/>
  <binary name="bin/terminal"/>
</binaries>
```

Entries are the *effective* binary paths — `binary_prefix` composed with
the package's `<binary>`, exactly what the config generator emits. It
mirrors `pkg_index.xml` in shape and is generated at boot-image
assembly from the actual `pkg/` contents. Without `<binaries/>` the
check is inert and launch behaves exactly as before — that is the
manual escape hatch (AGENTS.md §1.1).

It is a manifest rather than a probe-style ROM open on purpose: a
missing boot module is a **fatal** ROM-session denial under Genode's
base `Env` policy (`base/component.cc` answers a denied session with
"stop because parent denied …" and `sleep_forever()`; it does not
throw), so probing the binary would kill `sponge_pkgd` in exactly the
case it is meant to diagnose — and would fail on every launch in
disk-served mode, where the binaries live in `pkg_runtime`'s `rom_sys`
domain (chroot `/system`), not in pkgd's. Same denial-safe, index-gated
pattern `pkg_index.xml` already uses, generalized from metadata to
binaries.

### `launch` and `install` answer `insufficient-resources`

`binary-missing` is the resource-blind half of the same lie. Even with
the binary staged, a package whose declared quota does not fit
`pkg_runtime`'s remaining budget used to be answered `ok` and then
evaporate: Genode's sandbox clamps the child to whatever quota the
parent has left (`genode/repos/os/src/lib/sandbox/child.cc:483-486`,
`ram_quota = avail_ram`), so it dies before its first instruction and
the only trace anywhere is a kernel warning.

The same pre-flight shape therefore also gates on the **budget**:

```xml
<result status="insufficient-resources" op="launch" pkg="falkon"
        need_ram="1073741824" avail_ram="61776536"
        need_caps="2000"    avail_caps="1034"/>
```

and, like `binary-missing`, it is decided **before** the package enters
`_running` and **before** the config is regenerated. `install` is gated
too: an `<autostart/>` package gets its `<start>` node the moment an
install lands, so it is a spawn exactly like a launch. That pre-flight
walks the install plan in dependency order, decrementing the budget per
admitted package, and runs **before any mutation** — a refused install
leaves no root, no installed entry and no config write behind.

**Where the budget comes from.** pkgd does not know what the top-level
init granted `pkg_runtime`, so it does not keep a second copy of that
number. It reads `pkg_runtime`'s own sandbox state report (the
`runtime_state` ROM, fanned out by `report_rom`), whose top-level
`<ram avail=/>` and `<caps avail=/>` nodes are the quota it has not yet
donated to a child — exactly the pool a new `<start>` node comes out of.
That is a fact about the running system: no grant arithmetic, and it
already accounts for quota handed to other children.

Two parsing rules the report imposes, both already learned the hard way
elsewhere in the tree:

- it is **HID, not XML** — `Xml_node` throws `Invalid_syntax` on it and
  every read then silently yields zero (the Sponge DE recorded the same
  trap in `sponge-de/main.cc`);
- every round figure is **suffixed** (`"64M"`, not `67108864`), because
  the Generator prints through `Number_of_bytes::print()`. A plain
  numeric conversion would read `64M` as 64 *bytes* and admit an
  out-of-budget package.

The generated `<report>` node therefore carries `init_caps="yes"`
alongside `init_ram`; without it the caps dimension is simply absent.

**Margin.** Refusing at exactly `avail >= need` would admit a
razor-thin package that the sandbox then clamps anyway — recreating the
silent failure deterministically. `ADMISSION_MARGIN_RAM` (8 MiB) and
`ADMISSION_MARGIN_CAPS` (64) cover init's per-child bookkeeping plus one
period of report staleness, while staying far below the ~11 % product
headroom budgeted in `docs/17` §6.

**Opt-in, and fail-open.** The gate is armed by an `<admission/>` node,
because the `runtime_state` ROM session must be routed by the scenario
and a denied one is fatal. Armed or not, a *not yet published* report
**admits** with a log line: the report first appears ~2 s into boot,
when the budget is at its largest, so failing closed would refuse
legitimate first launches, and failing open leaves un-wired deployments
byte-identical to before — the manual escape hatch AGENTS.md §1.1 asks
for.

```xml
<config | binary_prefix: bin/>
  + admission
```

Wired in all five `run/sponge-desktop-disk-*.run` product scenarios
(verified: `resource admission armed` and `alpha-probe: PASS` in the
same boot). Deliberately **not** in `run/sponge-alpha.run`, whose
`pkg_runtime` grant (400 caps / 32M) is still sized for the hello-only
topology rather than the eight-package desktop profile it stages;
arming it there would refuse every baked package. See
`docs/evidence/pkg-admission-control.md` §8.

Receipts: `run/sponge-pkg-admission.run` (`admission-probe: PASS`),
`docs/evidence/pkg-admission-control.md`.

## Runtime config generation (Phase 4b / Phase 7 lifecycle)

pkgd owns the ENTIRE pkg_runtime `<config>` and regenerates it on every
install/remove/launch. The generator is deterministic — `<start>` nodes
sorted by name, fixed attribute order, no volatile fields — so init's
config-diff leaves already-running children untouched across an
unrelated install. An empty-but-valid config is emitted at startup so
pkg_runtime boots clean before any install.

The report buffer is **pre-sized** (`RUNTIME_CONFIG_BUFFER`, 32 KiB)
rather than grown on demand, for two independent reasons: one large
package must not be able to churn a quarter-megabyte of report buffer
per write, and a fixed buffer removes the session close+reopen window
altogether. The size is derived from the measured generated output:
22 797 bytes for all 11 shipped packages (`binary_prefix="bin/"`),
20 639 bytes for the 8-package desktop bake profile, against 4 096
before.

**The close+reopen window does NOT kill running children.** An earlier
revision of this file claimed it did (that `report_rom` relays an empty
`config` ROM during the window, so `pkg_runtime`'s `init` applies an
empty config and tears down every `<start>` node). That was measured in
QEMU and **falsified**: `report_rom` clears the module content when the
writer goes away but never notifies the readers
(`genode/repos/os/include/report_rom/rom_module.h:219-228` — no
`notify_client()` in `_unregister`), so `pkg_runtime` keeps its last
valid config and is notified exactly once, with the complete new one.
See `docs/evidence/config-regen-survival.md` for the receipts and the
reproduction scenario `run/sponge-config-regen-survival.run`.

Test-only knobs (all config-gated, inert unless a scenario writes them,
and **they must be written with a value** — a child config is delivered
in HID, and a valueless HID tag is silently dropped by
`genode/repos/base/src/lib/base/hid.cc:82-86`):

| Knob | Effect |
|---|---|
| `<repro_pad: yes>` | pads every write by `REPRO_PAD_BYTES` (32 000) so the buffer is forced to grow |
| `<repro_pad_grow: yes>` | pads write *N* with *N* × 32 KiB, so EVERY regeneration forces a growth — the knob the configuration-regeneration survival scenario uses |
| `<repro_empty_from: N>` | from write *N* on, publishes an EMPTY but valid config; the positive control that proves the scenario's kill detector is not blind |

Each knob logs an audit line when it fires, so a run can distinguish
"the knob was active" from "the knob silently never matched".

Phase 7 adds the installed-vs-running gate (docs/12 §9.2.1): the
generator emits a `<start>` node only for packages in the running set
(`_running`). The running set is the union of (a) installed packages
whose metadata declares `<autostart/>` and (b) packages explicitly
added by `launch`. It is re-derived by `_sync_running_state()` after
every install/remove/launch/restore, so the invariant
`_running ⊆ installed_names` always holds before the config is
regenerated.

## Persistent installed-set store (Phase 4 follow-up #2)

The set of explicitly-installed roots is mirrored to a tiny versioned
XML store on a `File_system` session, so installs survive a reboot.
The store holds **only the root names**; the full installed set is
re-derived on load by the same `_sync_installed_from_roots` path that
install/remove use, so the state→config generator is untouched.

Persistence is **opt-in per deployment**: it activates only when this
component's `<config>` carries a `<vfs>` node (see
`docs/12-package-format.md` §13 for the format, failure semantics, and
the inspect/edit/reset/disable escape hatches). With no `<vfs>` node
(the Phase 4 scenarios, or any deployment that declines persistence)
the daemon opens no `File_system` session and behaves byte-identically
to the in-memory build.

On construct: load the store → restore `_roots[]` → re-derive
`_installed[]` → regenerate the pkg_runtime config and the installed
broadcast, so a restored boot restarts the previously-installed
components with no user action. The store is rewritten after every
successful install/remove, before the change is broadcast.

The reference proof is `run/sponge-pkg-persist.run` (base-linux,
`lx_fs`-backed, two boots over the same host directory).

## What is deliberately not implemented

- `update`, version constraints (docs/12 §10).
- Launcher/sponge_configd integration (Phase 5).
- Crash-consistent writes (write-temp-then-rename / checksum). A torn
  write is detected as corrupt on the next boot and the daemon restarts
  empty with a warning, never crashes (docs/12 §13.2).

## Minimum privilege

The component requests `Report` and `ROM` sessions always, and an
optional `File_system` session (via the Vfs library) only when its
`<config>` carries a `<vfs>` node — i.e. only when the deployment has
explicitly enabled persistence. Likewise, the `pkg_binaries` manifest
ROM is opened only when `<config>` carries `<binaries/>`, and the
`runtime_state` ROM only when it carries `<admission/>`, so no
deployment pays for a capability it did not ask for. Everything it needs
to read requests/metadata and write results, and nothing more
(AGENTS.md §1.2). It is purely signal-driven and needs no Timer session.

## Runtime resource growth (measured, not assumed)

A package that outgrows its declared quota while running has a second
chance, and `docs/evidence/dynamic-resource-topup.md` measures what that
chance is worth on Genode 26.08 / base-sel4: a sandbox answers the
child's `resource_request()` **immediately, but only out of its own
remaining slack**. Beyond that the request is parked and **nothing above
the sandbox revives it** — there is no upward forwarding, and the
top-level init has nobody to ask. The only revive path is a new
configuration applied to the hosting sandbox by its owner — which, for
`pkg_runtime`, is this component.

That is why the admission margin above exists rather than being zero: the
runtime backstop can only hand out what admission has not already
promised to someone else.
