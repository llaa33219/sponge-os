# Dynamic quota top-up: can a child that hits its quota be given more at runtime? (2026-10-11)

**Verdict: partially, and only downward — no `init` patch is needed or
wanted.**

- A child **can** be given more at runtime **out of its own sandbox's
  remaining slack**, immediately and without any configuration change.
  This is the vendored immediate-grant path (patch-ledger row 21,
  `504f580f48`).
- A child **cannot** be given more by anything **above** its sandbox.
  There is no upward forwarding at all: when the sandbox's slack is
  insufficient the request is parked, and no amount of free memory
  further up the tree revives it.
- The **only** thing that revives a parked request is a **new
  configuration applied to the hosting sandbox by whoever owns that
  sandbox's config**. In Sponge that owner is `sponge_pkgd`, which
  already regenerates the whole `pkg_runtime` config.

Scenario: `run/sponge-resource-request.run` → `sponge-resource-request: PASS`
(kernel-agnostic topology, verified on base-sel4/QEMU).

---

## 1. Why this question had to be answered

`sponge_pkgd`'s admission control judges a launch against the budget
`pkg_runtime` reports **at that moment**. What happens to a package
that later outgrows its declared quota is a separate question, and the
answer decides how admission must be positioned:

- if the system widens quotas underneath a running package, admission
  is only a transient UX gate and the margin can be small;
- if it cannot, the declared quota is effectively a **ceiling for the
  life of the process**, and the margin has to carry the whole error
  budget.

The brief for this work explicitly forbade building dynamic granting as
a feature unless the mechanism already works in stock `init`. This
document records what stock `init` does. It does.

## 2. Mechanism under test

`genode/repos/os/src/lib/sandbox/child.cc`,
`Sandbox::Child::resource_request()`:

```cpp
bool const ram_satisfiable  = (req_ram.value  == 0) || (avail_ram  >= req_ram.value  + 4096);
bool const caps_satisfiable = (req_caps.value == 0) || (avail_caps >= req_caps.value + 8);

if (ram_satisfiable && caps_satisfiable) {
    _with_pd([&] (Pd_session &pd) { pd.transfer_quota(ref_account_cap(), req_ram); ... });
    _resources.assigned_quota.ram.value += req_ram.value;
    log(name(), ": resource request granted immediately");
    _child.notify_resource_avail();
    return;
}

warning(name(), ": resource request deferred (insufficient slack), "
                "waiting for a config change");
_requested_resources.construct(args);
```

Two things are load-bearing:

1. **The answer comes from `_env.pd()` — the sandbox's own budget.**
   There is no call upward. `Sandbox::Child` never asks its parent for
   quota.
2. **There is nowhere above to ask.** `genode/repos/os/src/init/main.cc:100`
   registers a `resource_avail` handler on the top-level init with the
   comment *"prevent init to block for resource upgrades (never
   satisfied by core)"*. The chain terminates at the top.

## 3. Experimental design

Three nested sandboxes, one probe binary (`repos/sponge/src/test/resq_probe`),
each child issuing exactly one explicit
`env().parent().resource_request("ram_quota=N")`:

| sandbox | grant | child quota | request | expected |
|---|---:|---:|---:|---|
| `sandbox_a` | 512M | 8M | 1M | immediate grant (slack-rich parent) |
| `sandbox_b` | 8M | 1M | 7M | parked — slack is ~75 KB |
| `sandbox_c` | 64M + 20M ballast | 2M | 48M | parked, then revived **only** by a config rewrite |

`config_bump` (`repos/sponge/src/test/config_bump`) publishes
`sandbox_c`'s config twice: the initial topology at *t*=0 (that
sandbox's config comes from this writer, so nothing exists without it)
and a raised quota at *t*=12 s. Without it a "deferred" reading would be
indistinguishable from a merely slow grant.

## 4. Receipt

```
[init -> config_bump] config-bump: published the initial topology (2 child/children)
[init -> sandbox_b -> resq_b] resq-b: REQUESTED ram_quota=7340032 (budget before the request: 75570)
[init -> sandbox_b] Warning: resq_b: resource request deferred (insufficient slack), waiting for a config change
[init -> sandbox_a -> resq_a] resq-a: REQUESTED ram_quota=1048576 (budget before the request: 7415602)
[init -> sandbox_a] child "resq_a": resource request granted immediately
[init -> sandbox_c -> resq_c] resq-c: REQUESTED ram_quota=50331648 (budget before the request: 1124146)
[init -> sandbox_c] Warning: resq_c: resource request deferred (insufficient slack), waiting for a config change
[init -> sandbox_c -> hog] resq-hog: ballast — holding 19998514 usable bytes, requesting nothing
[init -> config_bump] config-bump: published the raised quota (2 child/children)
[init -> sandbox_b -> resq_b] resq-b: BUDGET-UNCHANGED at 75570 (peak 75570)
[init -> sandbox_c -> resq_c] resq-c: BUDGET-GREW 1124146 -> 43717922 (+42593776)
[init -> sandbox_a -> resq_a] resq-a: BUDGET-UNCHANGED at 7415602 (peak 7415602)
[init -> sandbox_c -> resq_c] resq-c: PASS
sponge-resource-request: PASS
```

Reading the four verdicts:

1. **`resq_a` — the immediate path works.** `sandbox_a` has hundreds of
   MiB of slack; init answers on the spot. Ledger row 21.
2. **`resq_b` — nothing above revives it.** The parent's slack is
   ~75 KB, so the request is parked — and stays parked for the full 60 s
   observation window, while the **top-level init still holds well over
   400 MiB** and no sandbox between the two was ever consulted. That is
   the upward-forwarding question, answered negatively.
3. **`resq_c` — parked identically**, even though `sandbox_c` is far
   larger than `sandbox_b`: what matters is the parent's *remaining*
   budget, not its grant.
4. **`resq_c` — a config change revives it.** After `config_bump`
   raises `resq_c`'s quota from 2M to 52M at *t*=12 s, the parked
   request resolves and the child's budget grows by ~42.6 MB.
   `config_bump` is `sandbox_c`'s **sibling**, not its parent — nothing
   above the sandbox contributed.

## 5. Two measurement traps found on the way

Both are recorded in the probe's source so the next attempt does not
repeat them.

- **`Pd_session::Stats` is not a grant observable on base-sel4.** A PD's
  physical memory region is sized from its configured quota at
  construction, so a granted request need not move `avail()` at all —
  in the run above `resq_a` was granted immediately and its `avail()`
  did not change by one byte. `init`'s own log line is the authoritative
  observable. Hence the division of labour: **the probe is the stimulus
  and the reporter, the run scenario is the judge.**
- **Genode's HID generator folds a lone `name` attribute into a node's
  CONTENT.** `<probe name="c"/>` arrives at the reader as `probe c`,
  with no `name` attribute left. `config_bump` emits `id` instead.

## 6. How this composes with admission control

The two mechanisms answer different questions, and the verdict is that
they do not overlap:

| | admission control | resource request |
|---|---|---|
| decides | *may this package start at all?* | *may this running package grow?* |
| decides before | the `<start>` node is emitted | the component is already running |
| scope | pkg_runtime's whole budget | the sandbox's **own remaining** slack |
| failure mode it removes | silent clamp, invisible death | silent stall, invisible freeze |

Admission control is therefore the **predictable UX gate**: a user who
clicks a package that cannot fit gets `insufficient-resources` with the
numbers, not a window that never appears. The resource-request path is
a **runtime backstop with a strictly smaller scope**: it can hand a
running package a top-up only out of what `pkg_runtime` has not already
promised to someone else — which is precisely why the admission gate
must reserve `ADMISSION_MARGIN_RAM` (8 MiB) / `ADMISSION_MARGIN_CAPS`
(64) rather than admitting down to the last byte.

It is also why admission control cannot be replaced by "let init sort
it out": an out-of-budget launch is not a stall that resolves, it is a
child that dies before its first instruction.

### What is NOT offered as a feature

Upward forwarding (a sandbox asking its parent for quota) does not exist
in stock `init` and is not being added. It would need a patch to
`sandbox/child.cc` and a top-level init that could actually answer —
and `docs/17-runtime-component-topology.md` §5 already records that the
top level has no operator and no route upward. Making `pkgd` raise a
running child's quota by rewriting `pkg_runtime`'s config *is* possible
today (it owns that config), but it is deliberately not built: the gate
above makes the over-budget case unreachable by construction, so the
backstop would be untested code on a path that should never be reached.
If a real package turns out to outgrow its declared quota, that is the
moment to build it — with `run/sponge-resource-request.run` already
standing as the harness.