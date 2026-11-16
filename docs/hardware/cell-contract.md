# 15 - Hardware Compatibility: Cell Contract (§3)

> **Breadcrumb:** parent [15 - Hardware Compatibility](../15-hardware-compatibility.md); this file holds its §3 (Cell contract format), moved verbatim.
> Siblings in `docs/hardware/`: [surface-matrix.md](surface-matrix.md) (§1.1, §1.4-§1.5), [known-gaps.md](known-gaps.md) (§4), [drivers-qemu-gate.md](drivers-qemu-gate.md) (§7-§8).
> Note: bare "§N" references inside the moved text resolve through the parent document's numbered stubs.

---

## 3. Cell contract format (validated by `tool/hw_compat.mojo assert`)

Every cell in §1 and §2 follows one of the two formats below.
`tool/hw_compat.mojo assert` reads this document and validates every
cell against the format + the cross-product counts + the per-rule
checks (plan step 7 / plan risk 13 + risk 22 + risk 23 + risk 24).

### 3.1 Non-gap cell (verified or smoke-only)

```text
status: verified|smoke-only
scenario: run/<name>.run
marker: <exact PASS or load-bearing success marker>
evidence: docs/evidence/<path>
qemu: 11.0.3
boot_time_seconds: <measured integer>
budget_seconds: <declared integer>
target: qemu
qemu-envelope: <empty for Phase 12 cells>
```

Rules the validator enforces on non-gap cells (plan step 7):

- The `status` value is exactly `verified` or `smoke-only`.
- `scenario` resolves to an existing file under `run/`.
- `evidence` resolves to an existing file under `docs/evidence/`.
- `evidence` contains the `marker` substring as a byte-for-byte match.
- `qemu`, `boot_time_seconds`, and `budget_seconds` are present.
- `boot_time_seconds ≤ budget_seconds` (over-budget = loud failure).
- For USB-boot evidence (`*usb-boot.log`), the evidence additionally
  contains the literal `BIOS-side USB boot verified`.
- `qemu-envelope:` is empty for Phase 12 cells (and empty for any
  non-gap cell — only the Phase 15 real-hardware gap row carries
  the field, and a gap row may NOT carry scenario/marker/evidence
  per §3.2 below).

### 3.2 Gap cell

```text
status: gap
reason: <non-empty reason>
target: phase-XX
qemu-envelope: <empty for Phase 12 gap cells; OR `run/<scenario>.run` for Phase 15 real-hardware rows>
```

Rules the validator enforces on gap cells (plan step 7 + Phase 15 D15.11):

- The `status` value is exactly `gap`.
- `reason` is non-empty.
- `target` is non-empty and is either a phase designation (`phase-XX`)
  OR (Phase 15 D15.11) the literal `real-hardware` for a real-
  hardware row.
- A `target: real-hardware` row is admitted ONLY when BOTH:
  (a) `qemu-envelope:` is non-empty AND names an existing scenario
  file under `run/`; AND
  (b) `status:` is exactly `gap` (any stronger status — `verified`
  or `smoke-only` — is rejected with exit 2; the gate enforces this
  unconditionally, so the row keeps `gap` even though 15-3 LANDED
  2026-09-16 (first boot 2026-08-23); D15.11 + R15.7).
- A `target: real-hardware` row WITHOUT a `qemu-envelope:` is
  rejected with exit 2 and the exact message
  `real hardware is a Phase 15 deliverable; not a Phase 12 cell`
  (the original Phase 12 reject rule; preserved verbatim).
- The `qemu-envelope` cell column is empty for every non-real-
  hardware row (Phase 12 + Phase 15 surface cells).

### 3.3 Aggregate rules

- The cross-product cell counts are exactly 4 verified, 1 smoke-only,
  and 12 gap (Phase 12 11 gap + Phase 15 D15.11 1 real-hardware
  gap). Any other distribution is a loud failure.
- The verified count is non-zero (at least one verified cell).
- Exactly ONE row carries `target: real-hardware` (D15.11). Multiple
  real-hardware rows are rejected (the validator's real-hardware
  branch fires on the first such row and exits 2).
- The document is never modified by the validator. `tool/hw_compat.mojo`
  has no `generate`, `update`, or repository-writing path
  (plan step 6 / risk 23).
