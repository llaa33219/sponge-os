# Resource admission control: `insufficient-resources` (2026-10-11)

**Verdict: the silent-clamp launch failure is closed.** A package
whose declared quota does not fit `pkg_runtime`'s remaining budget is
now refused *before* it enters the running set and *before* the config
is regenerated, and the refusal carries the four figures the caller
needs to explain it.

Scenario: `run/sponge-pkg-admission.run` → `admission-probe: PASS`
(kernel-agnostic topology, verified on base-sel4/QEMU).

---

## 1. The defect this closes

`sponge_pkgd::_do_launch()` used to add the package to the running set,
regenerate the `pkg_runtime` config, and answer `ok` — without ever
consulting the budget. The consequence was decided by Genode's sandbox,
not by Sponge:

```
genode/repos/os/src/lib/sandbox/child.cc:483-486
    Ram_quota const avail_ram = Genode::Child::effective_quota(quota_limit.ram);
    if (ram_quota.value > avail_ram.value) {
        warning(name(), ": configured RAM exceeds available RAM, proceed with ", avail_ram);
        ram_quota = avail_ram;
    }
```

init clamps the child's quota to whatever it has left, the child dies
before its first instruction, and the only trace anywhere is a kernel
warning the user never sees. `sponge-pkg-lifecycle.run` already covers
the sibling defect (a staged-but-absent **binary**, answered
`binary-missing`); this is its resource twin.

## 2. Where the budget comes from

Not from a configured copy of the grant. `pkgd` does not know what the
top-level init granted `pkg_runtime`, and duplicating that number would
be a second source of truth that silently rots against
`docs/17-runtime-component-topology.md` §6.

`pkgd` reads `pkg_runtime`'s **own sandbox state report** — the
`runtime_state` ROM, fanned out by `report_rom` — whose top-level
`<ram avail=/>` and `<caps avail=/>` nodes are `pkg_runtime`'s own PD
statistics (`genode/repos/os/src/lib/sandbox/types.h`, `Resource_info::generate`):
the quota it has **not yet donated to a child**, i.e. exactly the pool a
new `<start>` node has to come out of.

Two implementation facts that cost real debugging time and are recorded
in the source so they are not re-learned:

- **The report is HID, not XML.** `Xml_node` on it throws
  `Invalid_syntax` and every read silently yields zero — the same trap
  the Sponge DE already recorded
  (`repos/sponge/src/sponge-de/main.cc:498-506`). `pkgd` parses it with
  `Hid_node`.
- **Every round figure is printed suffixed.** `Num_bytes::print()` emits
  `"64M"`, not `67108864`. A plain numeric conversion reads that as
  *64 bytes* and would admit an out-of-budget package, so both figures
  go through a suffix-aware parser (`_quota_bytes`).

The generated `<report>` node gained `init_caps="yes"`; without it the
caps dimension is simply absent from the report.

## 3. Admission margin

Refusing at exactly `avail >= need` would admit a razor-thin package
that init then clamps anyway — recreating the silent failure
deterministically instead of by luck. The margin covers what a child
costs on top of its declared quota (init's per-child bookkeeping: PD
session, CNode slab, child object, parsed `<start>` node) plus one
period of report staleness (the report is periodic with
`delay_ms="2000"`).

```
ADMISSION_MARGIN_RAM  = 8 MiB
ADMISSION_MARGIN_CAPS = 64
```

8 MiB / 64 caps is ~8 % of a typical package quota and two orders of
magnitude above the per-child overhead, while staying far below the
~11 % product headroom budgeted in `docs/17` §6 — the gate must not eat
the headroom the quota ledger reserves.

## 4. Fail-safe

Admission is **opt-in** via an `<admission/>` node in `pkgd`'s config
(a denied ROM session is fatal under Genode's base `Env` policy — it is
`sleep_forever()`, not a catchable throw — so it cannot be opened
eagerly). An unarmed or not-yet-published report **admits**, with a log
line.

That is deliberate. The state report first appears ~2 s into boot,
when the budget is at its largest, so failing closed there would refuse
legitimate first launches for no benefit; failing open also leaves every
deployment that has not wired the relay behaving exactly as before,
which is the manual escape hatch `AGENTS.md` §1.1 asks for. Each
unarmed decision is logged so a run can distinguish "the gate is off"
from "the gate refused".

## 5. Receipt — `run/sponge-pkg-admission.run`

Topology: `pkg_runtime` granted **caps 3200 / ram 160M**; an autostart
`witness` (4M/100) publishing a monotonic beat counter; `fat_a` and
`fat_b` (96M/2000 each) sharing the existing `config_witness` binary.

```
[init -> sponge_pkgd] sponge_pkgd: resource admission armed (pkg_runtime state report)
[init -> admission_probe] admission-probe: pkg_runtime budget avail ram=166644316 caps=3130
[init -> admission_probe] admission-probe: install witness -> ok
[init -> admission_probe] admission-probe: install fat_a -> ok
[init -> admission_probe] admission-probe: install fat_b -> ok
[init -> admission_probe] admission-probe: launch fat_a -> ok
[init -> admission_probe] admission-probe: after the hog, pkg_runtime has 61776536 bytes free
[init -> sponge_pkgd] sponge_pkgd: launch fat_b -> insufficient-resources (need_ram=100663296 avail_ram=61776536 need_caps=2000 avail_caps=1034)
[init -> admission_probe] admission-probe: refusal numbers need_ram=100663296 avail_ram=61776536 need_caps=2000 avail_caps=1034
[init -> admission_probe] admission-probe: witness beat 1 -> 61 (unaffected)
[init -> admission_probe] admission-probe: remove fat_a -> ok
[init -> admission_probe] admission-probe: after the release, pkg_runtime has 162436730 bytes free
[init -> admission_probe] admission-probe: launch fat_b -> ok
[init -> admission_probe] admission-probe: PASS
```

What each assertion rules out:

| Assertion | Rules out |
|---|---|
| `fat_b` refused with `insufficient-resources` | a gate that reports `ok` and lets the sandbox clamp |
| `need_ram > avail_ram`, `need_caps > avail_caps` | a refusal citing an incoherent budget |
| reported `avail_ram <= ` the probe's own observation | a refusal citing a budget `pkgd` is not actually reading |
| `fat_b` still `running="no"` in the installed broadcast | the package entering the running set (and the config being regenerated) despite the refusal |
| witness beat `1 -> 61` | collateral damage: the refusal restarting an already-running child |
| removing `fat_a` then re-launching `fat_b` succeeds | a gate that latches, or a permanent refusal |

## 6. Surfaces

- `sponge_pkgd` — `_admits()` / `_report_insufficient()` in
  `repos/sponge/src/sponge_pkgd/main.cc`; the duplicate-request replay
  keeps the cached figures so a replayed refusal cites the same budget
  rather than racing the periodic report.
- `vct` — `launch` renders the token with the numbers in both the human
  and the `--json` output.
- Sponge DE — `LauncherController::_poll_launch_result()` maps the token
  to a panel warning carrying the numbers, reusing the existing
  `launch_problem` → `PanelWidget::set_user_message` path. No new UI.
- Install is gated too: an `<autostart/>` package gets its `<start>` node
  the moment an install lands, so the pre-flight runs over the install
  plan (in dependency order, decrementing the budget per admitted
  package) **before any mutation**.

## 7. Deliberately not changed

The three probe accept-lists (`stability_probe`, `workflow_probe`,
`wm_tasks_probe`) do **not** learn the new token. For those scenarios an
over-budget refusal is a failure; accepting it would turn a red into a
green.

## 8. Deployment status

Wired (`<admission/>` + the `runtime_state` route + the report_rom
policy + `pkg_runtime`'s `Report label_last: state` route) in all five
`run/sponge-desktop-disk-*.run` product scenarios. All four pieces are
now structurally checked in each file (the route must appear exactly
once, inside `pkg_runtime`; a check added after a real defect, see
below).

**Runtime-verified in one scenario only.** `sponge-desktop-disk`
boots to `resource admission armed (pkg_runtime state report)` and
`alpha-probe: PASS` in the same boot — the assertion that matters,
because a denied ROM session is `sleep_forever()`, not a catchable
throw. The other four are structurally correct but were not each run to
a PASS marker; see the honest status below.

### `sponge-desktop-disk-nvme`: wired, but blocked by a pre-existing gap

This variant does **not** currently reach its PASS marker, for reasons
that predate this work. `sponge_pkgd` dies on startup:

```
[init -> rom_sys] Error: /bake_manifest.json not found
[init -> system -> sponge_pkgd] Error: stop because parent denied ROM-session: label="bake_manifest"
```

The scenario's `sponge_pkgd` config carries `<bake/>` and a
`bake_manifest` ROM route, but the scenario **never stages
`bake_manifest.json`** — it never calls `bake::stage`, unlike
`sponge-desktop-disk.run:359`. A denied ROM session is fatal, so pkgd
stops in `_init_bake()` (`main.cc:3068`), which runs **before**
`_init_admission()` (`main.cc:3084`) — admission is never reached.

This is not caused by the admission wiring. Verified against the
pre-work tree: at `171b50700f` the `<bake/>` node was already present
and `bake_manifest.json` was already only a route, never a staged file.

Removing `<bake/>` was tried and **reverted**: the gate was added
deliberately by `c68f95d415` (2026-09-22), two days *after* the
scenario's own "INTENTIONALLY OMITTED" comment (`113073260e`,
2026-09-20), so the comment is stale and deleting the gate is not a
change this work should make. With the gate removed the run does reach
`resource admission armed` — confirming the wiring is correct — but
then fails later and independently at `install terminal returned:
error`, because the scenario stages only `pkg_hello` + `sponge-de`
while Phase-16 W3 extended `alpha_probe` to install seven packages.
That second gap is also pre-existing and out of scope here.

**Net:** nvme is correctly wired and will arm the gate as soon as its
`bake_manifest` staging gap is closed, but admission is not reachable
at runtime there today. Closing that gap is separate work.

### `run/sponge-alpha.run`: not wired, deliberately

Alpha's `pkg_runtime` is granted **400 caps / 32M** — sized for the
hello-only Phase-7 topology, not for the eight-package `desktop`
profile it stages by default (the residual gap already tracked in
`docs/17` §6). Arming admission there would refuse all eight baked
packages and turn alpha's current silent clamp into eight visible
refusals, regressing the scenario's own `alpha-probe: PASS` criterion.
Alpha therefore behaves byte-for-byte as before, and the relay lands
together with the quota resize that `docs/17` §6 already carries as
separate work.