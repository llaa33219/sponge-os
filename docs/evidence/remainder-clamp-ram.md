# Remainder-clamp RAM grants (`system` + `pkg_runtime` take what is left)

> **Claim under test.** `run/sponge-desktop-disk*.run` (the five product
> disk scenarios) declare `ram: 512G` for `pkg_runtime` and for the
> nested `system` init, on the theory that Genode's init silently clamps
> a child's RAM quantum to the parent's remaining availability at
> creation time — so a deliberately huge quantum means *"take everything
> left"*, and the run scripts become physical-RAM-size agnostic.
>
> **Verdict: CONFIRMED, headlessly, twice.** Same unchanged file boots
> to `alpha-probe: PASS` at QEMU `-m 6G` and at `-m 4G`, and the two
> clamps deliver visibly different grants (4521 MiB vs 2473 MiB). No
> vendored patch, no `repos/sponge/src/` change, no `sponge-alpha.run`
> change.

---

## 1. The mechanism (what the code actually does)

`genode/repos/os/src/lib/sandbox/child.cc:481-486`, inside
`Sandbox::Child::init()`:

```c++
Ram_quota const avail_ram = Genode::Child::effective_quota(quota_limit.ram);

if (ram_quota.value > avail_ram.value) {
    warning(name(), ": configured RAM exceeds available RAM, proceed with ", avail_ram);
    ram_quota = avail_ram;
}
```

`quota_limit` comes from `Library::_avail()` (`library.cc:123, 96-108`),
which is the sandbox PD's live `stats.ram.avail()` minus the sandbox's
`Preservation` reserve — i.e. **what is genuinely still free at the
instant this child is created**. `effective_quota()`
(`genode/repos/base/include/base/child.h:661-667, 763-766`) subtracts the
fixed env-session cost. The caps path at `child.cc:490-495` is the
identical clamp.

So a declared quantum larger than what is left is not an error: init
logs one `warning(...)` line naming the child and hands over the
remainder. That is precisely the intended semantics here.

The clamp is **not** patched, **not** worked around, and **not**
depended on in the vendored tree — it is upstream behaviour, used as-is.

## 2. The ordering invariant (why "last" is load-bearing)

The remainder is only *"everything left"* if nothing is created after
the clamp. Both levels therefore require the clamped child to be the
**last** `+ start` node in its config:

| config | clamped child | last `+ start`? |
|---|---|---|
| `system.config` (nested init) | `pkg_runtime` | yes — all 5 scenarios |
| top-level init config | `system` | yes — all 5 scenarios |

`pkg_runtime`-last was already the case, introduced by `248afa12d6`
("start pkg_runtime LAST") for the real-hardware config-delivery timing;
this change makes the same ordering a **quota** invariant as well. The
`uefi-usb` `SPONGE_LAUNCHTEST` block rewrites the `sponge-de` node *in
place*, so its injected `prod_launch_probe` + replacement `sponge-de`
still land above `pkg_runtime` and the invariant survives.

**No scenario needed reordering.** Measured with a per-file scan of the
`+ start` nodes inside each `system.config` block and of the top-level
config; all five satisfy the invariant as committed.

## 3. Measurement — `run/sponge-desktop-disk` (BIOS/direct-kernel path)

Two boots of the *same* run file, the only difference being the `-m`
value on the QEMU command line.

| | `-m 6G` | `-m 4G` |
|---|---:|---:|
| init pool (core's line) | `5686 MiB RAM and 523288 caps assigned to init` | `3638 MiB RAM and 523288 caps assigned to init` |
| `system` grant after clamp | `5 040 606 368 B` = 4807.1 MiB | `2 893 122 720 B` = 2759.1 MiB |
| `pkg_runtime` grant after clamp | `4 740 801 500 B` = 4521.2 MiB | `2 593 317 852 B` = 2473.2 MiB |
| admission gate | `resource admission armed (pkg_runtime state report)` | same |
| `install hello/terminal/textedit/files/calculator/pdf_view/falkon` | 7 × `ok` | 7 × `ok` |
| terminal verdict | **`alpha-probe: PASS`**, `Run script execution successful.` | **`alpha-probe: PASS`**, `Run script execution successful.` |

Raw log excerpts: `remainder-clamp-desktop-disk-6g.log`,
`remainder-clamp-desktop-disk-4g.log`.

### 3.1 The arithmetic closes exactly

Fixed children of the nested init (from the run file):
`report_rom 8 + wm 16 + layouter 8 + decorator 32 + sponge_configd 16 +
sponge_themed 8 + sponge_pkgd 16 + sponge-de 160 + alpha_probe 16 +
cfg_probe 4` = **284 MiB**.

`system grant − pkg_runtime grant` = 285.9 MiB at **both** `-m` values —
i.e. the nested init's own ~1.9 MiB of live allocation plus the 284 MiB
it had already handed to the ten fixed children. That identity holding
at two different pool sizes is the direct proof that
`pkg_runtime`'s grant is the remainder, not a constant.

Same at the top level: Tier-0 children total 873.5 MiB (874 MiB
including the 16M `nitpicker`, which carries no `caps` attribute);
`5686 MiB pool − 873.5 MiB ≈ 4807 MiB`, the observed `system` grant.

### 3.2 What `-m` now means

`-m` sets the **ceiling** on guest RAM; it no longer has to agree with
any declared grant. The scenario values (`6G` for the two direct-kernel
variants, `8G` for the three UEFI ones) are **deliberately unchanged**
— every receipt in `docs/evidence/` was earned at those values, and
changing them would invalidate prior evidence for no functional gain.

## 4. Honest limitations

1. **Headroom now varies with the machine, and it can get thin.** At
   `-m 6G` `pkg_runtime` receives 4521 MiB for a 2536 MiB enabled
   package sum (+78%). At `-m 4G` it receives 2473 MiB — against the
   2472 MiB sum of the *seven* packages `alpha_probe` actually installs,
   i.e. essentially zero slack. That boot still PASSed (all seven
   installs returned `ok`), but it demonstrates the intended behaviour
   honestly rather than conveniently: **on a smaller machine the
   enabled package set is what shrinks**, and the refusal path is
   `sponge_pkgd`'s `insufficient-resources`, not a silent clamp. Do not
   read the 4G PASS as "4G is a supported desktop envelope" — it is
   evidence that the *grant* adapts, nothing more.
2. **The top-level slack above every declared grant is now zero.** With
   `system` taking the remainder, the top-level init retains nothing
   spare, so a Tier-0 child that later issues a `resource_request()`
   has no slack to answer from (docs/evidence/dynamic-resource-topup.md
   §"no upward forwarding"). The Tier-0 grants are unchanged and were
   already sized for the shipped payload (`cached_fs_rom` 256M each,
   `rom_pkg` 768M for falkon's 509 MiB payload); no boot in this run
   emitted a parked resource request. This is a design consequence of
   the requested "take everything left" semantics, not an accident.
3. **Only `-m` size is now size-agnostic — caps are not.** Every caps
   figure in these five scenarios is unchanged and still hand-derived.
   A box with fewer capability slots is still a cliff edge.
4. **`run/sponge-alpha.run` is untouched by design.** Its flat ISO
   topology grants `pkg_runtime` 400 caps / 32M, which remains the
   documented residual gap in `docs/17` §6.
5. The clamp path was exercised at two pool sizes on the **BIOS path
   only**. The three UEFI variants share the identical config text, but
   their OVMF reservation (~2.4 GiB, measured on the sibling) shifts the
   pool; they were **not** re-booted as part of this evidence.

## 5. Regressions

All three run after the change, same build tree, all PASS:

| scenario | marker |
|---|---|
| `run/sponge-pkg-admission.run` | `admission-probe: PASS` — budget still read live from the state report (`pkg_runtime budget avail ram=166644316 caps=3130`; that scenario keeps its own small 3200/160M grant, unaffected) |
| `run/sponge-gaprepro.run` | `launch-order regression: PASS` |
| `run/sponge-pkg-lifecycle.run` | `lifecycle-probe: PASS` |

Excerpts: `remainder-clamp-regression-sponge-pkg-admission.log`,
`remainder-clamp-regression-sponge-gaprepro.log`,
`remainder-clamp-regression-sponge-pkg-lifecycle.log`.

## 6. Where the rationale lives in the tree

Each of the five run files carries, at the `pkg_runtime` node, a
comment block headed

```
# ============ RAM: THE DELIBERATELY-HUGE 512G QUANTUM =====================
```

stating that the quantum is intentional, citing `child.cc:481-486`,
requiring the last-created position, and giving the two reasons caps
stay explicit (seL4 caps exhaustion **hangs**; and a zero-sum cap
remainder would starve any future sibling). The `system` node carries
the same mechanism one level up plus the note that `system` is last in
the top-level config. The `-m` block in each file now states that `-m`
is the ceiling only.