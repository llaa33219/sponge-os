# Configuration-regeneration survival: does an Expanding_reporter buffer
# growth kill running pkg_runtime children? (2026-10-11)

**Verdict: the hypothesis is FALSIFIED. No vendored `init` patch was
landed.** A `sponge_pkgd` config regeneration that crosses the
`Expanding_reporter` buffer boundary does **not** kill already-running
`pkg_runtime` children. The mechanism that was supposed to do it is
absorbed inside `report_rom`, which never tells a reader that the writer
of a module went away.

The committed rationale in `repos/sponge/src/sponge_pkgd/README.md`
("the growth path destructs and reconstructs the Report session, and
while that window is open the `runtime` report carries no payload — so
`report_rom` relays an empty `config` ROM to `pkg_runtime`") is wrong in
its second half. The window exists; it is simply **invisible** to
`pkg_runtime`.

---

## 1. The hypothesis under test

1. `sponge_pkgd` regenerates the whole `pkg_runtime` config through
   `Genode::Expanding_reporter` (`_generate_runtime_config`,
   `_runtime_reporter`).
2. On overflow, `Expanding_reporter::_increase_report_buffer()`
   destructs and reconstructs the report session
   (`genode/repos/os/include/os/reporter.h:195-199`).
3. In that window the `runtime` report carries no payload.
4. `report_rom` therefore hands `pkg_runtime` an invalid/empty `config`
   ROM.
5. `init::_handle_config` (`genode/repos/os/src/init/main.cc:45-70`)
   applies that node with no validity guard →
   `List_model::update_from_node` finds no sub-nodes → every `<start>`
   is abandoned → **all children are killed**.

Steps 2 and 3 are true. Steps 4 and 5 never happen.

## 2. The absorbing mechanism (identified, code-level)

`report_rom`'s module clears its content when the writer disappears, and
does **not** notify the readers:

```c++
/* genode/repos/os/include/report_rom/rom_module.h:219-228 */
void _unregister(Writer const &writer)
{
        _writers.remove(&writer);

        /* clear content if its origin disappears */
        if (_last_writer == &writer) {
                Genode::bzero(_ds->local_addr<char>(), _size);
                _size = 0;
                _last_writer = nullptr;
        }
}
```

No `notify_client()` anywhere in that path. The **only** notification is
in `write_content()` (`rom_module.h:266-301`), which runs when a writer
actually submits bytes. So the sequence seen by `pkg_runtime` is:

| t | pkgd | report_rom module | pkg_runtime `config` ROM |
|---|---|---|---|
| 1 | generate → buffer overflow | unchanged | unchanged, VALID (previous config) |
| 2 | reporter destructed | content zeroed, `_last_writer = nullptr`, readers **not** notified | unchanged, VALID |
| 3 | reporter reconstructed, generate → submit | `write_content()` → `mark_as_outdated()` + `notify_client()` | one single update with the COMPLETE new config |

`Attached_rom_dataspace::node()`
(`genode/repos/base/include/base/attached_rom_dataspace.h:147-153`)
therefore never returns the empty `Node()` in this window — it returns
the previous valid content. `_handle_config` always applies a valid
node, and the config diff leaves the unchanged `<start>` nodes running.

Two corollaries worth recording:

- **`init` needs no guard for this path.** A validity guard in
  `_handle_config` would be dead code against this failure mode: the
  dataspace is valid throughout. It would only be a defensive change to
  the vendored tree with no reproduced failure to justify it, which
  AGENTS.md §5.2 forbids.
- The pre-sizing of the report buffer (commit `48ac138f2`) remains a
  *correct* change on its own terms (a fixed-size buffer removes the
  close+reopen window entirely, and one large package must not be able
  to churn 200 KiB of report buffer per write). It is just not
  load-bearing for child survival.

## 3. The experiment

Scenario `run/sponge-config-regen-survival.run` (kernel-agnostic
topology; run on `base-sel4` in QEMU):

```
init + timer + report_rom + sponge_pkgd + pkg_runtime (binary init) + regen_probe
pkg_runtime child: the "witness" package (autostart) — payload
  config_witness publishes a monotonically increasing counter n on its
  Report session; n restarts at 1 on every process start.
```

- `regen_probe` installs `witness`, waits until it counts, records the
  counter, then per cycle performs one package operation on a second,
  never-autostarted package (`second`), which forces `pkgd` to
  regenerate the whole `pkg_runtime` config.
- After each regeneration it samples the counter for a bounded window
  and requires it to stay non-decreasing and to keep advancing.
- A counter that returns to 1 is the unambiguous signature of
  "pkg_runtime abandoned and recreated the child".

### 3.1 Forcing growth on EVERY write

`<repro_pad_grow: yes>` makes write *N* pad its output with *N* × 32 KiB
so that every regeneration provably overflows the 32 KiB reporter
buffer. The pad is emitted as `pad_bytes/64` `<p v="..."/>` nodes whose
each serialized form is **more** than 64 bytes, so the emitted padding
is *at least* the reported value — the overflow is a fact, not an
estimate. `pkgd` prints the audit line

```
sponge_pkgd: repro_pad_grow write=N emitted_pad>=<N*32768> buffer=32768
```

Write 1 already exceeds the initial buffer. Every later write pads 1.5×
as much as the previous one while the live buffer is at most the
previous write's size rounded up to a 4096-byte multiple plus the ~2 KiB
fixed scaffolding, so the buffer can never already be large enough —
each write re-enters `_increase_report_buffer()`.

### 3.2 Receipts

Growth run — `docs/evidence/config-regen-survival-growth.log`:

```
sponge_pkgd: repro_pad_grow write=1 emitted_pad>=32768   buffer=32768
sponge_pkgd: repro_pad_grow write=2 emitted_pad>=65536   buffer=32768
[init -> pkg_runtime -> witness] config-witness: start
regen-probe: witness live at n=3
  cycle 1 (write 3, pad>=98304)   survived (n=24)
  cycle 2 (write 4, pad>=131072)  survived (n=46)
  cycle 3 (write 5, pad>=163840)  survived (n=68)
  cycle 4 (write 6, pad>=196608)  survived (n=89)
regen-probe: PASS
```

Four forced growth events with the witness already running, one
`config-witness: start` line in the whole log, counter strictly
increasing across every regeneration. **No kill.**

Positive control — `docs/evidence/config-regen-survival-control.log`
(`SPONGE_REPRO_CONTROL=1`, `<repro_empty_from: 3>`: from write 3 on,
`pkgd` publishes an EMPTY but VALID config, i.e. exactly what
`pkg_runtime` would see if the transient window were visible to it):

```
config-witness: start
regen-probe: witness live at n=3
  cycle 1 — install second
sponge_pkgd: publishing EMPTY config (write 3) — control channel
regen-probe: FAIL witness was killed (counter reset ...)
```

The control proves two things at once:

- the detector is **not blind** — a kill is observed within one cycle
  (< 4 s), so the PASS above is a real observation and not a dead probe;
- `pkg_runtime` **does** receive and apply every regenerated config
  promptly, so the growth run's survival is not caused by the config
  never arriving.

## 4. Collateral finding: the pre-existing `<repro_pad>` knob never fired

While building the reproduction, the knob did not activate. Root cause:
a child config is delivered in **HID**, not XML
(`genode/repos/base/src/lib/ldso/main.cc:792-807` — `generate_xml`
defaults to false), and Genode's HID attribute parser **silently drops a
valueless tag** (`genode/repos/base/src/lib/base/hid.cc:82-86` requires
`key: value`):

```c++
auto with_tag_value = [] (Span const &s, auto const &fn) {
        _with_ident(s, [&] (Span const &tag, Span const &remain) {
        if (tag.num_bytes && remain.num_bytes && remain.start[0] == ':')
                ... }); };        /* bare "repro_pad" -> never reaches fn */
```

`run/sponge-gaprepro.run:602` writes `+ config | … | repro_pad`
(valueless) — so that scenario never forced a growth either; its
`repro_delay_ms` sibling has a value and does work. Confirmed
empirically: an instrumented `pkgd` reported its whole config as the
9 bytes `config\n\n`.

**Not fixed here, deliberately.** Activating the knob in
`sponge-gaprepro.run` would add a 32 KiB pad to every `pkgd` config
write in a product scenario, changing a shipping topology's runtime
behaviour on the strength of a documentation fix. The correct change is
`repro_pad: yes`, and it should land together with a decision on what
`gaprepro` is supposed to prove. Until then `pad_bytes` is 0 in every
shipped scenario, i.e. the padding path is dead code in production —
which is the intended "inert" state anyway.

## 5. What is now durable

- `run/sponge-config-regen-survival.run` — the reproduction scenario,
  with `SPONGE_REPRO_CONTROL=1` as the positive-control channel.
- `repos/sponge/src/test/regen_probe/` — the monotonicity verifier.
- `repos/sponge/src/test/config_witness/` + `pkg/witness`,
  `pkg/second` — the witness payload and the regeneration trigger.
- The `<repro_pad_grow>` / `<repro_empty_from>` knobs in `sponge_pkgd`
  (config-gated, inert without the knobs, `AGENTS.md` §5.3).
- This file plus the two log receipts.

## 6. If the symptom ever does appear

The falsified chain can still be broken in *other* ways, and the
observation to make first is which one: `sponge_pkgd` dying mid-write
(an uncaught `Service_denied` from `report_rom` if a second writer ever
appeared on the same module — `rom_registry.h:150-156`), a `pkg_runtime`
that is restarted by its parent, or a genuinely empty config reaching
`init`. Each of those is visible in the log with a distinct signature;
this scenario's probe (counter reset vs. silence vs. `config-witness:
start` twice) already separates them.