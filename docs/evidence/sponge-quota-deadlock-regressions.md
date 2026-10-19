# Regression receipts — the quota-request wedge fix (ledger row #25)

`genode` pinned at `342900343b` ("fix(sandbox): a deferred quota request
must never be answered falsely"). Host: QEMU/KVM, `-smp` default, one vCPU.
Full rationale and measurements:
[`sponge-quota-deadlock.md`](sponge-quota-deadlock.md).

## Green

| scenario | exit | evidence line |
|---|---:|---|
| `sponge-quota-deadlock` (the new repro) | 0 | `requests resources` = **4**, `granted immediately` = **0**, `heartbeat restart budget (3) exhausted`, `bystander: alive` |
| `sponge-minimal` | 0 | `vct 0.2.0 / Archaeocyte` |
| `sponge-desktop-disk` | 0 | `alpha-probe: PASS` |
| `sponge-desktop-disk-uefi-usb` | 0 | scenario gates all PASS |
| `sponge-pkg-driver` | 0 | install + exclusivity gates PASS |
| `sponge-supervisor-takeover` | 0 | takeover gates PASS |

## Not green, and not ours: two pre-existing gate failures

`sponge-debug-firstpaint` and `sponge-debug-scrollstorm` both exit **2**
with `Error: Test execution timed out`, **on the patched tree and on the
unpatched baseline alike**. The baseline was measured by checking the
submodule out at `f44bc47ee5` (ledger row #24, this patch's parent) and
re-running the identical scenario.

The functional payload of both runs is present and healthy; the failure is
in the expect gate, which does not match a line that is demonstrably in
the serial log it captured:

| metric | baseline `f44bc47ee5` | patched `342900343b` | receipts band |
|---|---:|---:|---:|
| `sponge_debug: FIRSTPAINT ms=` | 729 | 828 | reported, not bounded |
| `STORM done … achieved_hz` | **20** | **20** | 19 Hz |
| `STORM done … max_gap_ms` | **132** | **196** | 163–189 ms |
| `repostorm-probe: VERDICT` | PASS | PASS | PASS |

So the fix changes nothing these scenarios measure, and neither of them
regressed. The distinguishing feature of both, and the reason this is
believed to be a harness issue rather than a guest one: they are the two
regression scenarios whose QEMU command line ends in `-serial mon:stdio`
(the monitor mux appended by `genode/tool/run/power_on/qemu`), whereas
`run/sponge-quota-deadlock.run` and the green set above spawn QEMU without
the mux and gate correctly on lines the same way.

**This is a registered open defect, not a claim of a fix.** It is
reproducible on demand with no Sponge patch applied:

```bash
git -C genode checkout f44bc47ee5
make -C genode/build/x86_64 run/sponge-debug-firstpaint   # exit 2
make -C genode/build/x86_64 run/sponge-debug-scrollstorm # exit 2
git -C genode checkout 342900343b
```

Diagnosing and fixing it is out of scope for row #25 and is left for a
follow-up; until then, the two scenarios' own in-band telemetry (above) is
the usable signal, and it is unchanged by this patch.

## What would falsify the green rows

The fix touches `init` (the sandbox quota accounting) and four `base`
allocation loops, so the failure modes to watch for are: a component that
legitimately needs more than 64 sequential quota increases (it would now
log `… not granted after 64 quota-upgrade attempts` and stop instead of
continuing), and a sandbox whose grant path regressed to the
`_transferrable_quota` bound (it would log `NOT granted` with
`assigned == configured` where an increase was available). Both are visible
in the one log line the fix introduced, which is why that line names
requested/assigned/configured/our-slack rather than a bare failure.