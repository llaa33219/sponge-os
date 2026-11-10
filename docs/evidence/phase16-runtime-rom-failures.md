================================================================================
Phase 16 runtime — attribution of the three product application ROM failures
Measurement report (source archaeology)  •  Date: 2026-10-28
Baseline: 61512a7ce4 (docs/evidence/phase16-runtime-baseline.md, run 2/2)
Scope:   run/sponge-desktop-disk-uefi-usb.run on QEMU/OVMF (seL4)
Method:  read-only. No scenario was executed, no implementation changed,
         no base patch added, no real-hardware claim is made. All run-script
         line numbers refer to the baseline blob (the run script is identical
         at 61512a7ce4 and at this report's HEAD); all Genode line numbers
         refer to the vendored fork tree at the baseline pin.
================================================================================

## 0. HEADLINE

The three deterministic product application failures are three DIFFERENT
mechanism classes, not one bug with three faces:

  (a) terminal — a STAGING gap. rom_sys answers the font request correctly
      and fast: `Error: /VeraMono.ttf not found` + denial. The file was
      never staged onto the media because the product scenario's only
      staging step is a silent conditional copy of `bin/VeraMono.ttf`, and
      nothing in the product build path produces that file. Isolated.

  (b) files and (c) falkon — the package child's BINARY ROM session create
      ("session 4" = `Env::binary()`) is never answered by the provider
      chain and expires at the fork's 60 s create deadline
      (`CREATE_TIMEOUT_MS`), after which the designed expiry path leaves
      the `state=DENIED` / `dangling session` lifetime errors behind.
      The evidence does NOT isolate one first cause for these two; three
      mechanism families survive discrimination (shared-provider stall,
      forwarded-create completion regression, swallowed quota-refusal).
      The dangling-session lines are DOWNSTREAM of the deadline, by design
      of the expiry path, and are not evidence of a separate fault.

The 60000 ms in the message is a deadline CONSTANT of ledger rows #28–#31
(`genode/repos/base/include/base/child.h:432`); its presence does not
implicate the create-chain patch as the cause. Counter-evidence inside the
same baseline: the terminal package's own ROM creates traversed the same
forwarded chain and completed (one of them producing a fast, answered
denial) minutes before the files/falkon creates went unanswered.

## 1. SIGNATURE TAXONOMY (how each mechanism class announces itself)

This section is the discrimination key used below. Each class has a
distinct observable signature in this topology; conflating them is what
would mis-blame the create-chain patch.

| mechanism class | signature in this topology | example / citation |
|---|---|---|
| missing staged file / boot module | provider LOOKS UP the file and answers: `Error: <path> not found` from cached_fs_rom, then a fast denial reaching the client (`stop because parent denied ROM-session: label="…"`; the flat topologies add `environment ROM session denied` retry lines) | VeraMono failure itself (§2); historical `rom_sys: /terminal not found` in `docs/evidence/launch-stall-root-cause.md` follow-up 2 |
| wrong ROM label / route | either `no route to service`/silent denial at route matching, or the provider logs `not found` for the WRONG path (the label that arrives differs from the label the route expects) | run:1709-1722 documents the silent-denial variant for `label:` rules |
| nested `binary_prefix` gap | nested sub-init children requested as unprefixed `X` → rom_sys looks up `/system/X` → fast `not found` (the 2026-10-03 shape in `launch-stall-root-cause.md` follow-up 2) | plan P2.3; pkgd rewrite pass `repos/sponge/src/sponge_pkgd/main.cc:1103-1175` |
| provider stall | NO provider output at all (no `not found`, no denial): the create RPC goes unanswered and the CLIENT-side deadline expires | files/falkon baseline (§3, §4) |
| quota refusal | provider throws `Insufficient_ram_quota`/`Insufficient_cap_quota` (cached_fs_rom's floor: `genode/repos/os/src/server/cached_fs_rom/main.cc:330-341`); with row #31 recovery the client re-issues +4 KiB; with the refusal swallowed the create rides to the deadline with NO provider output | `docs/evidence/forwarded-create-starvation.md` §4a (erosion 6144→3714 over three hops of 810 B, `child.cc:147` `session_costs()`) |
| session-lifetime downstream | `destroying session with a live parent-side id` (`genode/repos/base/src/lib/base/session_state.cc:134-143`) and `dangling session in parent-side ID space` (`genode/repos/base/include/base/session_state.h:140-143`) — destruction-time invariant errors | both appear ONLY after a create/close deadline kept the parent-side ID reserved (`child.cc:888-905`) |

The failed create in (b)/(c) is identified precisely: the warning text is
emitted by `Genode::Child::_expire_pending_op` for `Op::Create`
(`genode/repos/base/src/lib/base/child.cc:888-891`, message form
`create of <service> (session <id>) failed - the provider did not answer
within <deadline> ms`), the warning prefix is the child-policy name
(`files` / `falkon`), and `session 4` is the binary ROM environment
session: `Parent::Env::binary()` is client ID 4
(`genode/repos/base/include/parent/parent.h:67-75`). So in both cases the
create that never completed is the launched package child's own binary.

## 2. FAILURE (a) — terminal: VeraMono.ttf absent from rom_sys

### 2.1 Observed evidence (baseline F2, deterministic 2/2)

    [init -> rom_sys] Error: /VeraMono.ttf not found
    [init -> system -> pkg_runtime -> terminal -> terminal] Error: stop
      because parent denied ROM-session: label="VeraMono.ttf",
      ram_quota=10K, cap_quota=3

(`docs/evidence/phase16-runtime-baseline.md` F2; the enforced boot marker
`pkg_runtime -> terminal -> vfs` never appears.) The prefix
`[init -> rom_sys]` also confirms rom_sys is a TOP-LEVEL child of the
boot init, not a `system`-sandbox child.

### 2.2 Routing/staging chain (file:line)

Request path (all run-script citations = baseline blob):

1. The terminal component's font config: `pkg/terminal/metadata.xml` —
   nested `<start name="terminal">` config mounts
   `<rom name="VeraMono.ttf"/>` and draws it via
   `<ttf name="regular" path="/VeraMono.ttf" size_px="14"/>`.
2. The ROM request travels up: terminal → sub-init (`<default-route>
   <any-service><parent/>…` in the same metadata) → `pkg_runtime`
   (`+ any-service | + parent | + any-child`, the `+ start pkg_runtime`
   route block of `/system/init/system.config`, run:1049 ff.) → `system`
   (same `any-service → parent` shape) → the top-level `+ start system`
   route (run:1697-1734).
3. No specific rule matches (`bin/`-less label, no `.lib.so` suffix, not
   falkon): the catch-all `+ service ROM | + child rom_sys` (run:1726).
4. rom_sys is `cached_fs_rom` over vfs_sys (run:1528-1536), whose
   File_system policy roots it at `/system` (run:1479). cached_fs_rom
   resolves the request as `Path(label.last_element())`
   (`genode/repos/os/src/server/cached_fs_rom/main.cc:348-349`); `Path`
   normalizes a relative name against `pwd="/"`
   (`genode/repos/os/include/os/path.h:220-252`) — which is exactly why
   the log prints `/VeraMono.ttf`. Lookup target: `/system/VeraMono.ttf`.
5. `try_open` logs `Error: /VeraMono.ttf not found` and throws
   `Service_denied` (`cached_fs_rom/main.cc:305-315`) — a fast, answered
   denial, which the terminal treats as fatal.

Staging path (the broken link):

6. The product scenario stages terminal payload flat at `/system` from
   `bin/` — `foreach t { VeraMono.ttf bash-minimal.tar … }` with a SILENT
   guard `if {[file exists "bin/$t"]}` (run:525-531; comment run:499-504
   says these "resolve through rom_sys at the /system root").
7. The ten noux tars ARE produced by the product build (`noux-pkg/*`
   targets in the scenario's build list) and therefore exist in `bin/`.
   VeraMono.ttf is NOT a build target: the only producers of
   `bin/VeraMono.ttf` anywhere in the tree are explicit copies from the
   prepared `ttf-bitstream-vera` port
   (`genode/contrib/ttf-bitstream-vera-<hash>/ttf/bitstream-vera/
   VeraMono.ttf`) performed by the DEDICATED scenarios:
   `run/terminal_runtime.inc:141-147` (`terminal_runtime::stage_vera_metadata`,
   errors loudly if the port is missing) and `run/sponge-alpha.run:195-204`.
   The product run sources neither and contains no
   `terminal_runtime` / `stage_vera` / `ttf-bitstream` reference anywhere.
8. `git log -S "VeraMono" -- run/sponge-desktop-disk-uefi-usb.run` shows
   exactly one commit, `09da75ea43` (2026-10-04), which introduced the
   conditional copy loop. Its commit message claims the terminal payload
   "flat at /system" and a verified bash prompt — on the autostart-repro
   image, i.e. a build tree in which `bin/VeraMono.ttf` already existed as
   residue of a prior sponge-alpha/sponge-terminal run. The staging has
   been conditional-and-unowned since that commit.
   The sibling product scenarios (`run/sponge-desktop-disk.run`,
   `-uefi.run`, `-nvme.run`) carry NO VeraMono staging at all.

### 2.3 Discriminated hypotheses

H-a1 **Silent staging skip (no producer for `bin/VeraMono.ttf` in the
product build path).** Supported. Explains every observation: rom_sys
received the request (route correct), looked up the right path
(`/VeraMono.ttf` = last_element `VeraMono.ttf` under root `/system`), and
answered "not found" because the copy at run:525-531 was skipped. The
same loop stages the tars that DO resolve (the sub-init mounts them), so
the loop mechanics work; only the font lacks a producer. The `if {[file
exists]}` guard makes the skip silent (no warning), so nothing in the
scenario output announces the gap.

H-a2 **`ttf-bitstream-vera` port not prepared.** Plausible trigger variant
of H-a1 but NOT the mechanism class: the port IS prepared in the audited
tree (`genode/contrib/ttf-bitstream-vera-cd36848…/ttf/bitstream-vera/
VeraMono.ttf` exists), and even when prepared, the product run never
copies from `contrib/` — so port preparation alone cannot fix the product
media. The dedicated helper would fail loudly (`terminal_runtime.inc:143-145`)
if the port were missing; the product scenario cannot, because it never
looks.

H-a3 **Wrong label / wrong route / `last_element()` mapping bug.**
Refuted. The denial label (`VeraMono.ttf`) and the lookup path
(`/VeraMono.ttf`) agree with the metadata's ROM name; the same
`label.last_element()` mapping resolves other flat files at the `/system`
root in the same boots (`bake_manifest.json`, `config.defaults` via
run:852-853; `sponge_configd: applied 5 baked default(s)` and
`sponge_pkgd: seeded 9 baked package root(s)` in the baseline §3 prove
those rom_sys lookups succeeded). A mapping bug would break the bake
files too.

H-a4 **Provider stall / create-chain break.** Refuted for (a): a stalled
chain cannot produce a prompt, answered `not found` + denial. The font
request is direct evidence that this ROM create chain WORKED at terminal
launch time.

### 2.4 Most likely supported mechanism

H-a1: `/system/VeraMono.ttf` was never staged because the product
scenario's only staging step (run:525-531) silently skips when
`bin/VeraMono.ttf` is absent, and no product-build step produces that
file. (Build-dir residue from dedicated scenarios masks the bug in
contaminated trees — which is how `09da75ea43` was verified.)

### 2.5 Minimal next fix/test

Fix (one staging ownership decision; implement in the P2 wave per plan
D16R.5, not here): give the product scenario a real producer for
`bin/VeraMono.ttf` — the same single `cp` from the prepared
`ttf-bitstream-vera` contrib dir that `run/terminal_runtime.inc:141-147`
already performs (or source that helper), and make the payload copy loop
fail loud instead of `if {[file exists]}`-skipping its file list.

Test: run the baseline scenario on a CLEAN build tree
(`SPONGE_LAUNCHTEST=on`, per baseline §1) and assert (i) absence of
`rom_sys: Error: /VeraMono.ttf not found`, (ii) appearance of the
currently-unreachable enforced marker `pkg_runtime -> terminal -> vfs`.
The clean-tree condition is part of the test: the residue bug is invisible
otherwise.

## 3. FAILURE (b) — files (`bin/sponge_files`): ROM create timeout

### 3.1 Observed evidence (baseline F3, deterministic 2/2)

    Warning: files: create of ROM (session 4) failed - the provider did
      not answer within 60000 ms
    Error: destroying session with a live parent-side id: service=ROM
      … label="pkg_runtime -> bin/sponge_files" … state=DENIED
    Error: dangling session in parent-side ID space: service=ROM …
      label="pkg_runtime -> bin/sponge_files" … state=CLOSED

(`docs/evidence/phase16-runtime-baseline.md` F3.) Request acked first
(`prod-launch-probe: launch files -> ok` = pkgd answered; baseline E4
notes the ack proves nothing about boot).

### 3.2 Routing/staging chain (file:line)

1. `pkg/files/metadata.xml:42` — `<binary>sponge_files</binary>` (binary
   name deliberately differs from package name `files`).
2. pkgd composes the effective binary name
   `bin_path(_binary_prefix, c.binary)` = `bin/sponge_files`
   (`repos/sponge/src/sponge_pkgd/main.cc:2284-2295`, with
   `_binary_prefix` = `bin/` from the `+ config | binary_prefix: bin/`
   attribute, run:875; attribute read `main.cc:3593-3595`).
3. Generated `<start name="files">` under `pkg_runtime`
   (`+ start pkg_runtime | caps: 230000 | ram: 512G`, run:1049). Its
   binary is an environment session of the child: client ID 4
   (`parent/parent.h:73`), label `bin/sponge_files`
   (`genode/repos/base/include/base/child.h:807-810` +
   `_construct_args`, `child.h:724-740`).
4. Forwarding chain (the "provider" in the timeout message):
   `pkg_runtime` (`+ any-service | + parent | + any-child` in its route
   block) → `system` (same shape) → top-level `+ start system` route →
   catch-all `+ service ROM | + child rom_sys` (run:1726) → `cached_fs_rom`
   → vfs_sys (`root: /system`, run:1479) → rump ext2 → part_block → disk
   (chain quoted in
   `docs/evidence/phase16-runtime-patch-io-audit.md` §3.1).
   Label scoping at each parent: `Child::session()` prefixes the
   requesting child's name (`genode/repos/base/src/lib/base/child.cc:148-150`),
   which is why the session state at the `system` boundary reads
   `pkg_runtime -> bin/sponge_files` — exactly the label form the falkon
   route is written against (run:1702).
5. Physical staging: the product run copies `bin/sponge_files` →
   `/system/bin/sponge_files` (staging loop run:467-475; the binary is a
   build target, `sponge_files`, in the scenario's build list), and
   rom_sys would resolve `label.last_element()` = `bin/sponge_files` →
   `/system/bin/sponge_files` (run:441 documents this layout contract).
   **The binary IS staged** (build list + staging loop; the `if {[file
   exists]}` guard applies, but the build produces the binary).
6. Staging pre-flight is INERT on product media: pkgd's
   `_binary_available` (`main.cc:3166-3179`) only runs when its config
   carries the `<binaries/>` manifest node (`_init_binary_manifest`,
   `main.cc:3133-3163`), and the product pkgd config children are
   `launcher_request/admission/driver_admission/tty_request/bake` only
   (run:875-880). So `launch files -> ok` does NOT re-verify staging.

### 3.3 Discriminated hypotheses (the evidence does not isolate one)

H-b1 **Shared-provider stall below the ROM servers (vfs_sys / storage).**
rom_sys and rom_pkg both read through the single vfs_sys instance
(policies run:1479-1481; both `cached_fs_rom`s route `File_system` to
vfs_sys, run:1532/1542/1565). If vfs_sys's entrypoint/rump pipeline
wedges (e.g. around the terminal sub-tree teardown that immediately
precedes this launch window — its vfs mounts ten tar ROM sessions), both
ROM servers go silent together, which matches files AND falkon failing
identically. Signature fit: no provider output at all (no `not found`),
client deadline expires. Discriminator: ANY rom_sys/rom_pkg-served ROM
create after the window (e.g. re-opening `bake_manifest.json`) would also
hang.

H-b2 **Forwarded-create completion regression (ledger rows #30/#31
machinery).** The create is forwarded two container hops and completed by
`Service::complete_create()` polling on the 50 ms `Pending_op_timer` tick
(`docs/evidence/forwarded-create-starvation.md` §3; rows #30/#31 in
`docs/11-environment.md` §4). An open regression of exactly this family
is documented at the baseline pin: the pkg-driver uplink session "never
completes" across nested sandboxes
(`forwarded-create-starvation.md` §7/§8; regression window
`e0c2706225..205b84fcad`, intermittently 1 fail/2 pass at `82f0143d1f`).
If the completion stops resolving forwarded creates after the first
package launch/teardown churn, the create rides to the deadline with no
provider-side fault. Counter-evidence that must be explained by any H-b2
variant: the same forwarding chain completed the terminal package's
creates (binary + a full denial round-trip for VeraMono) earlier in the
same boot — so the break, if any, is state-dependent (post-teardown),
not global. Note also a fingerprint difference from §8: the uplink
regression showed `close of … failed` ×4 and NO `create of … failed`,
whereas here `create of … failed` is the primary line.

H-b3 **Swallowed quota-refusal (erosion below cached_fs_rom's floor).**
Each forwarding hop keeps `session_costs()` (~810 B,
`child.cc:147`; erosion measured 6144→3714 over three hops in
`forwarded-create-starvation.md` §4a); cached_fs_rom refuses donations
below `max(4096, sizeof(Session_component))`
(`cached_fs_rom/main.cc:330-341`). Row #31's `complete_create()` +
`Local_connection_base::reissue()` recovery resolves such refusals — if
the recovery mis-handles this shape (an env-session create forwarded
through TWO `Try_parent_service` hops), the refusal is swallowed and the
create expires at the deadline — the exact pre-fix signature described in
§4a ("the create stayed pending until its deadline"). Counter-evidence:
terminal's env creates succeeded through the same hops, so either their
donations differed or the recovery works when it works; and the fixed
recovery is supposed to cover this. Discriminator: provider-side
create-attempt counts (re-issue attempts visible at rom_sys) and the
donation value on the wire.

REFUTED classes (per §1):

- **Missing binary / silent staging skip** — refuted as the direct cause
  by signature: a missing file yields the fast, logged `not found` +
  denial (§2 proves rom_sys behaves that way on this media). Staging
  itself is in place (§3.2.5).
- **Wrong label / nested `binary_prefix` double-prefixing** — refuted:
  the observed labels `pkg_runtime -> bin/sponge_files` /
  `pkg_runtime -> bin/falkon` are exactly the single-prefixed names pkgd
  composes (§3.2.2), and falkon's matches the run:1702 route exactly. A
  `bin/bin/…` shape would be visible in the DENIED label and would fall
  through to the wrong provider.
- **Quota refusal at pkgd admission** — refuted: `launch files -> ok`
  means admission passed and the config was regenerated (baseline E4;
  `docs/evidence/pkg-admission-control.md` shows a refusal answers
  `insufficient-resources` without starting anything).

### 3.4 Most likely supported mechanism

Not isolated to one; the supportable statement is: **the binary ROM
create for `files` is left unanswered across the shared
pkg_runtime→system→rom_sys chain after the terminal launch window, and
expires at the create deadline.** H-b1 and H-b2 are the leading
mechanisms (both explain the two-provider silence and the post-terminal
timing; the documented create-family regression at this pin makes H-b2 a
live suspect-by-window per `forwarded-create-starvation.md` §8.5, not a
proven cause); H-b3 remains open because its recovery path should have
covered it. Deterministic 2/2 in the baseline vs the §8.5 intermittency
of the uplink regression slightly favors a deterministic state effect
(teardown-driven) over pure timing — two runs are not a frequency
estimate.

### 3.5 Minimal next fix/test

No fix claim here (ordering: plan P0.2/P4.2 classification first, then
P3/P2). Minimal discriminating TEST, one instrumented boot of the
baseline scenario:

1. After the files launch window, issue a trivial rom_sys-served ROM
   create (re-open `bake_manifest.json`). Hangs ⇒ H-b1/H-b2-class stall
   below/at the create machinery; answers ⇒ failure is specific to the
   binary env-create class (points at H-b3/re-issue).
2. Count ROM session-create attempts at rom_sys for label
   `bin/sponge_files` (cached_fs_rom instrumentation or a verbose
   variant). 0 attempts ⇒ request lost/refused above the provider
   (H-b2/H-b3); ≥1 attempt stuck in `try_open` ⇒ H-b1; repeated attempts
   without completion ⇒ H-b3's re-issue loop.
3. Grep the serial for the companion lines the machinery predicts: the
   same expiry warning at the two forwarding levels
   (`pkg_runtime: create of ROM …`, `system: create of ROM …`,
   `child.cc:888-891` prefixes the policy name), `too many pending
   session operations` (`child.cc:683`, registry-full variant), any
   `INSUFFICIENT_*`, and the §8 close-path fingerprint
   (`close of … failed`).

Flat-topology control already in hand: `run/sponge-files.run` passes
(README W12 sweep) with the same binary, same pkgd, same binary_prefix
machinery (empty prefix there) — only the ROM topology differs. The
failure is topology/chain-specific, not package-specific.

## 4. FAILURE (c) — falkon (`bin/falkon`): ROM create timeout

### 4.1 Observed evidence (baseline F4, deterministic 2/2)

    Warning: falkon: create of ROM (session 4) failed - the provider did
      not answer within 60000 ms
    Error: … label="pkg_runtime -> bin/falkon" … state=DENIED +
      matching dangling-session error (run 2 only)

(`docs/evidence/phase16-runtime-baseline.md` F4.) The DENIED/dangling
pair appearing only in run 2 is itself evidence that those lines track
teardown timing, not causation (§5).

### 4.2 Routing/staging chain (file:line)

1. `pkg/falkon/metadata.xml:27` — `<binary>falkon</binary>`; pkgd
   composes `bin/falkon` (same machinery as §3.2.2).
2. This binary is NOT in `/system/bin` and never was: the staging loop's
   file list (run:467-475) deliberately excludes falkon (its 509 MiB
   closure lives in the payload area). The bake stages
   `pkg/falkon/payload/*` → `/system/pkg/falkon/payload/`
   (`run/bake.inc:648-676`, `_stage_payload`; payload dir contains the
   `falkon` binary + 63 closure files). rom_pkg is `cached_fs_rom` with
   the vfs_sys policy `root: /system/pkg/falkon/payload` (run:1481,
   1561-1569).
3. Provider route (top-level, run:1702):
   `+ service ROM | label: pkg_runtime -> bin/falkon | + child rom_pkg |
   label: falkon` — an EXACT-label route whose expected label form is
   identical to the DENIED label observed. Payload libraries use
   `label_prefix: pkg_runtime -> falkon` (run:1703), matched before the
   `.lib.so → rom_lib` rule (run:1704, comment run:1555-1560).
4. rom_pkg resolves `label.last_element()` = `falkon` →
   `/system/pkg/falkon/payload/falkon`. **The provider that should serve
   this binary is rom_pkg, not rom_sys**; **the binary is NOT staged in
   `/system/bin`** — by design.

### 4.3 Discriminated hypotheses

The same three surviving families as §3.3 apply, and falkon adds one
discriminator of its own: its binary is served by a DIFFERENT provider
(rom_pkg) through a DIFFERENT route (exact-label, run:1702) than files
(rom_sys catch-all, run:1726), yet the failure shape is identical
(same message, same `session 4`, same label form). Therefore:

- A rom_sys-internal fault (cache/heap, single-provider wedge) is
  refuted as the sole cause by falkon failing too, and a rom_pkg-internal
  fault is refuted by files failing too. Any provider-level hypothesis
  must live in their SHARED dependency (vfs_sys/rump/storage — H-b1) or
  in the shared forwarding/completion machinery above them (H-b2) or in
  the shared client-side env-create path (H-b3).
- **Wrong label/route for falkon specifically** — refuted: the observed
  label matches run:1702 exactly; a mismatch would fall through to the
  rom_sys catch-all and produce a `not found` for `/system/bin/falkon`
  (fast denial), which is not what happened.
- **Missing payload staging** — refuted as the direct cause by signature
  (missing file ⇒ fast logged `not found`, cf. §2); the bake img-mode
  staging (`run/bake.inc:84`, `:648-676`) places the payload on the
  baseline media (baseline §3: `sponge_pkgd: seeded 9 baked package
  root(s)`).
- **seL4 CSpace exhaustion (the historical falkon blocker)** — not this
  failure: that signature is a started falkon stopped by
  `out of selector`/`denied RM-session` AFTER first instructions
  (`docs/evidence/task-10-phase14-falkon-rescue.md`,
  `docs/evidence/p4-falkon-disk.log`); here falkon's binary ROM never
  resolves, so the component never starts.

### 4.4 Most likely supported mechanism

Shared with §3.4: the binary ROM create is left unanswered across the
chain and expires at the deadline; the two-provider symmetry upgrades the
"shared layer" requirement and leaves H-b1 (vfs_sys/storage stall) and
H-b2 (forwarded-create completion) as the leading mechanisms, H-b3 open.

### 4.5 Minimal next fix/test

The §3.5 three-way test covers falkon unchanged (substitute label
`bin/falkon` and note the provider is rom_pkg: attempt-count
instrumentation goes to rom_pkg). Add one falkon-specific observation:
because run:1702 is an exact-label route, the test should also assert the
top-level route query actually selected rom_pkg (e.g. a one-line log in
the route or a rom_pkg-side create-attempt counter) — that separates
"request never reached rom_pkg" (H-b2 upstream) from "rom_pkg received
and stalled/refused" (H-b1/H-b3).

## 5. THE DENIED / DANGLING LINES ARE DOWNSTREAM (by design of the expiry path)

The two lifetime errors are destruction-time invariant checks, not
independent faults:

- `destroying session with a live parent-side id` —
  `genode/repos/base/src/lib/base/session_state.cc:134-143`
  (`Session_state::destroy()`).
- `dangling session in parent-side ID space` —
  `genode/repos/base/include/base/session_state.h:140-143`
  (`~Session_state()` while `id_at_parent` is still constructed).

Their mechanism, deliberately chosen by the create-expiry path: when a
create runs into its deadline, the container closes the session at the
parent and releases the client, but does NOT release the parent-side ID —
"the parent may still create the session" and a re-used ID would corrupt
the parent's ID space (`genode/repos/base/src/lib/base/child.cc:888-905`,
comment at the `Op::Create` branch; same reasoning at `child.h:385-391`
for closes). If the child is torn down before the parent's close answer
arrives, the state is destroyed with that ID still live and the two
errors print. They are therefore the TAIL of the failed create, exactly
the `state=DENIED → CLOSED` sequence the baseline shows, and the run-2-
only appearance for falkon (baseline F4) is a teardown-timing artifact.
Treating these lines as first causes would point at session-lifetime code
(row #28/#30) while the first cause is the unanswered create upstream.

## 6. WHY THE 60000 ms DOES NOT BLAME THE CREATE-CHAIN PATCH

- The value is `CREATE_TIMEOUT_MS = 60000`
  (`genode/repos/base/include/base/child.h:432`; ordinary ops use
  `TIMEOUT_MS = 5000`, `child.h:418`). It is a deadline constant of the
  row #28–#31 machinery; ANY unanswered create prints it. The message
  shape proves only "no answer within the deadline".
- The same machinery completed creates earlier in the SAME boots: the
  terminal package's binary and nested binaries resolved (its nested
  `terminal` child ran — `pkg_runtime -> terminal -> terminal` in F2),
  and the VeraMono create got a prompt answered denial. A global
  create-chain break cannot produce that.
- The one documented create-family regression at this pin
  (`forwarded-create-starvation.md` §8, window `e0c2706225..205b84fcad`,
  "suspect family by construction, not a proven cause") has a different
  fingerprint (close failures, no create expiry) and is intermittent
  (1 fail/2 pass at `82f0143d1f`), while the baseline failures are
  deterministic 2/2. H-b2 borrows its plausibility from that window but
  must not be asserted without the §3.5 discrimination.
- Conversely the patch must not be EXONERATED either: rows #28–#31 own
  the deadline, the completion polling and the env re-issue path
  (H-b2/H-b3 live there). The disciplined framing is the one
  `forwarded-create-starvation.md` §8.5 uses: suspect family by
  construction, not proven cause.

## 7. ORDERING IMPLICATIONS FOR P1/P2 (this report's consumer)

Mapped to `docs/plans/phase16-runtime-overhaul.md`:

- (a) terminal font — a staging-ownership fix in the product run scripts
  (P2-wave; the payload staging half of P2.6's pattern applied to the
  terminal class). Cheap, isolated, unblocks the enforced launch gate's
  first marker. Also fix the silent `if {[file exists]}` class of staging
  guard wherever it guards required files (the same pattern hides any
  future payload gap).
- (b)/(c) files+falkon — feed P0.2/P4.2 (create-chain capture and
  classification) and P3.1 (provider-stall isolation): the §3.5 test is
  the cheapest discriminator between FD1 (session-create/provider chain)
  and FD4-class provider sharing. Until it runs, per D16R.5 no topology
  change should assume which of H-b1/H-b2/H-b3 holds.
- The inert `<binaries/>` pre-flight on product media
  (`main.cc:3133-3179`, run:875-880) is a P1/P2 input: launch
  pre-flight cannot currently catch staging gaps on the product
  topology, which is how (a) survived.

## 8. LIMITS AND OPEN QUESTIONS

- QEMU/OVMF + seL4 only; no scenario was executed for this report
  (source archaeology against the committed baseline runs).
- Line numbers refer to the baseline blobs — verified identical at
  61512a7ce4 and at this audit's HEAD (`0a667176de`) for
  `run/sponge-desktop-disk-uefi-usb.run` and
  `repos/sponge/src/sponge_pkgd/main.cc`; during the audit a concurrent
  worktree held uncommitted edits to both (P1 extraction scaffolding) —
  neither working-tree state is part of this report.
- The plan's former P2.3 statement ("nested sub-init children still resolve
  without the `bin/` prefix") was stale. The nested rewrite landed in
  `09da75ea43`, and the baseline observation already showed terminal's
  binaries resolving while only the FONT failed. Section 9 closes this
  question with unchanged-library and product-runtime verification.
- H-b1/H-b2/H-b3 are not ranked beyond "leading/open" — the §3.5 test is
  the prerequisite for any root-cause claim.

================================================================================

## 9. P2.3 nested binary-prefix resolution (2026-10-29)

**Verdict:** the planned rewrite already existed; no library change was
needed. `09da75ea43` added the nested-start rewrite, and P1.2 commit
`71b2c4359e` extracted it unchanged into `sponge_binary_prefix`. The flat
scanner rewrites nested `<start>` and explicit `<binary>` nodes at any depth:
terminal and VFS starts gain `bin/`-prefixed binaries, `vfs_rom`'s explicit
`cached_fs_rom` binary becomes `bin/cached_fs_rom`, and `/bin/bash` remains
untouched. Top-level path composition and empty-prefix identity are separate
and unchanged.

The unchanged host test passed:

```sh
g++ -std=c++20 -O1 \
  -I genode/repos/base/include \
  -I genode/repos/base/include/spec/64bit \
  -I genode/repos/base/include/spec/x86_64 \
  -I genode/repos/base/include/spec/x86 \
  -I repos/sponge/include \
  repos/sponge/lib/src/sponge_binary_prefix/self_test.cc \
  repos/sponge/lib/src/sponge_binary_prefix/binary_prefix.cc \
  -o /tmp/opencode/sponge_binary_prefix_self_test \
&& /tmp/opencode/sponge_binary_prefix_self_test
# self-test: PASS (equivalence + golden snapshots)
```

The product runtime gate then ran on base-sel4/QEMU/OVMF with the terminal
sub-init binaries and all 12 payload files staged by `runtime-terminal.inc`:

```sh
SPONGE_LAUNCHTEST=on make -j1 -C genode/build/x86_64 \
  run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc
# alpha-probe: PASS
# launch-gate PASS — terminal request
# launch-gate PASS — terminal package boots
# PASS — launch path verified: a launcher request boots the
#        terminal package end-to-end (files/falkon stages logged).
```

The run's required terminal markers were `prod-launch-probe: launch terminal
-> ok` and the real nested `[init -> system -> pkg_runtime -> terminal -> vfs]`
child. This is actual launcher-to-nested-child evidence, not merely a boot
marker. The same run reached `alpha-probe: PASS` and compositor first paint.
It is not a real-hardware result.

Only the UEFI-USB product scenario currently stages the complete terminal
binary/payload set, so this receipt does not claim nested terminal resolution
on media that omit those files. P2.3 is closed by verification of the existing
rewriter; the P1.2 golden suite remains the regression gate for its exact
nested/path-like/top-level behavior.

## 10. Runtime resolution: read-only SYNC and product launch wiring (2026-11-08)

**Root cause and fixes.** The remaining no-window failures were not one
mechanism. On UEFI/USB, `Backend::sync()` issued a SYNC operation even on a
read-only Block session. The refusal/completion path could leave `vfs_sys`
unable to answer later opens, so every runtime's ROM create expired after
60 s and pkgd had already answered `ok`. `Backend::sync()` now returns
immediately when the session is not writeable. This does not weaken write
durability: writeable sessions still perform SYNC; the upstream
`rump_ext2` write/read gate passes.

`Cached_fs_rom::Transfer` also leaked its `File_handle` after every completed
transfer and left `_cached_rom.transfer` dangling on the constructor-failure
path. Its destructor now closes the handle and clears the pointer; the catch
path resets it too. This is an independent lifecycle fix, not the explanation
for the UEFI stall.

The product-only launch gaps found immediately after the storage fix are
also closed:

- `runtime_simple::stage_binaries` stages `files_demo.tar` and an empty
  `files_request` on every grouped product scenario instead of only BIOS.
- UEFI/USB builds and starts `pc_rtc` + `system_rtc`, and the system routes
  `Rtc` to `system_rtc`.
- `pc_nic` is seeded in the bake manifest and autostarts in `driver_runtime`.
  Its Uplink is bridged by `nic_uplink`, the same proven shape used by BIOS;
  falkon's lwip client consequently receives `10.0.2.15`.

**Regression gate.** `SPONGE_LAUNCH_CASE=terminal|files|falkon` is an explicit
per-app fresh-boot matrix, not a claim that the heavy multi-app CNode envelope
is solved. Each case requires the selected request to answer `ok` and wm's
`window_list` to contain the corresponding label. All three serial runs pass:

```sh
SPONGE_LAUNCHTEST=on SPONGE_LAUNCH_CASE=terminal make -j1 \
  -C genode/build/x86_64 run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc
# prod-launch-probe: window terminal -> registered
# prod-launch-probe: windows PASS

SPONGE_LAUNCHTEST=on SPONGE_LAUNCH_CASE=files make -j1 \
  -C genode/build/x86_64 run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc
# prod-launch-probe: window files -> registered
# prod-launch-probe: windows PASS

SPONGE_LAUNCHTEST=on SPONGE_LAUNCH_CASE=falkon make -j1 \
  -C genode/build/x86_64 run/sponge-desktop-disk-uefi-usb KERNEL=sel4 BOARD=pc
# prod-launch-probe: window falkon -> registered
# prod-launch-probe: windows PASS
```

Falkon also reaches `lwIP Nic interface up address=10.0.2.15` before its
window. A manual QMP screendump shows the actual browser window on the
2560x1600 framebuffer. The tests run on QEMU/OVMF only and do not constitute
real-hardware certification.

**Residual limitation, deliberately not hidden.** Concurrent or repeated
construction of the heavy Qt/WebEngine closure can still fail with CNode
errors. The earlier sequential experiments are not evidence that this class
is fixed. The new matrix proves each user-facing application independently;
it does not assert simultaneous multi-app operation. That separate resource
lifecycle problem remains outside this fix's scope.

## 11. Simultaneous-window CNode exhaustion (2026-11-08)

**Regression gate.** `SPONGE_LAUNCH_CASE=multi` now launches terminal, files,
and falkon in one fresh boot. A passing run must observe all three label
prefixes in the same wm `window_list` snapshot, emit
`prod-launch-probe: multi simultaneous n=3`, retain all three in a second scan
about five seconds later (`multi siblings held n=3`), and report no vanished
window before `windows PASS`. The run script enforces the terminal VFS marker,
the simultaneous witness, and the final verdict. Removing an application or
merging scans cannot satisfy this gate.

**RED evidence.** The first run was captured as a transient raw log and
removed after this curated record was written. Terminal and files registered,
but falkon did not. The probe ended with
`prod-launch-probe: missing label prefix: pkg_runtime_browser -> falkon` and
`windows FAIL`. The serial log contained 152 `Cnode construction failed`
lines, 147 `Source slot invalid or empty` lines, three `mapping failure for
thread` lines, and two `failed to create initial thread` lines. It contained no
`insufficient-resources`, `out of caps`, or `Insufficient memory` admission
failure. The CNode failures begin immediately after pkgd acknowledges falkon.

**Root cause.** On x86_64 each PD constructor eagerly attempted all 128
second-level CNodes. At 16 KiB of backing per node this consumes 2 MiB per PD
from the dedicated 16 KiB untyped pool, although the pool reservation assumes
only 64 nodes per PD. Construction therefore exhausted the backing pool before
retype. A second defect checked element zero instead of element `i` before
copying each second-level CNode into the first-level CNode, so failed nodes
were still copied and produced the empty-source-slot errors.

**Minimal fix, preserving receive semantics.** The component-local capability
space contains 6144 selectors and each second-level CNode covers 512 slots, so
only 12 nodes are addressable. Core now eagerly constructs exactly those 12
nodes (192 KiB per PD), checks the constructed state of element `i`, skips
unmaterialized entries when removing first-level slots during teardown, and
bounds the user receive-selector allocator to the same 6144 selectors. This
keeps every legal kernel receive path backed by an existing CNode.

Two superficially smaller alternatives were rejected. Lazy construction alone
is incomplete because `Capability_space::alloc_rcv_sel()` selects a receive
slot in the component and the seL4 kernel writes the received capability there
without a core allocation hook. Routing large CNodes to the ordinary RAM
allocator is also invalid: general RAM is minted as 4 KiB untyped objects,
while these CNodes require 16 KiB untyped objects, and seL4 cannot merge
untypeds. Increasing the pool quota would hide rather than remove the mismatch.

**Validation status.** The automated UEFI/USB `multi` gate now passes: the log
contains `multi simultaneous n=3`, `multi siblings held n=3`, and `windows
PASS`, with zero occurrences of `Cnode construction failed`, `Source slot
invalid or empty`, `mapping failure for thread`, or `failed to create initial
thread`. The QMP visual, BIOS grouped-UI, and `rump_ext2` checks remain
separate and must not be inferred from this serial gate.

**QMP and independent visual review.** Two fresh 2560×1600 QMP captures record
the initial Falkon-focused desktop and the state after dragging Falkon right.
The second shows Sponge Files and Falkon rendered side by side with independent
chrome and live file-manager content; terminal remains represented in the panel
tasklist while occluded. Two independent read-only reviewers returned PASS.
They also verified from `_scan_multi`, `_wait_multi`, and the run gate that one
application cannot satisfy two prefixes and that sequential registrations
cannot replace the one-snapshot witness. The visual run contains one transient
mapping-cache flush warning and a deferred falkon RAM upgrade, but no CNode
construction or empty-source failure; all three windows remained registered.
This is not a claim that every future memory-pressure condition is removed.

**Other regressions.** The first BIOS `SPONGE_GROUPED_UI=on` attempt stopped at
a late launcher-popup close wait, the previously observed timing flake, with no
CNode construction failure. The single documented retry passed
`grouped-ui-probe: PASS` and `Run script execution successful.`, including its
five-window concurrent set and typed-input checks. `run/rump_ext2` also passed,
including four 4 MiB write/read rounds. These are QEMU results, not real-
hardware certification.

**Final media.** `./tool/dist` rebuilt the canonical UEFI/USB image from the
tracked run configuration (overwriting the temporary capture-only layout), and
its boot gate reached `alpha-probe: PASS` with zero wedge or starvation lines.
The staged image is `var/dist/sponge-os-0.2.0-x86_64-sel4.img`; its verified
SHA-256 is
`8dfc11a1bfb285afdfbe3420e04d4910d0267535ef3a7e042e7c32a665ace485`.
The matching `.sha256` sidecar passes `sha256sum -c`.

## 12. Close-completion race and same-ID recreation (2026-11-09)

**Field signature.** Real hardware logged `requested conflicting session ID 97`
and `existing session: label="system -> wm -> bin/window_layouter"
state=CLOSING`. No `create of ROM ... failed` or `requested session cap for
unknown ID` appeared. This is a close/create race, not an `ld.lib.so`, quota, or
GRUB problem: a create arriving under an ID whose close is still in flight is
immediately denied, while the client is allowed to run ahead of a close it never
confirmed.

**Root cause.** `with_new_session` returns `Session_error::DENIED` as soon as an
ID is occupied and has no queue/retry. `Component_env::close` blocked for only
one wake of the shared Blockade, so an unrelated session or the close-deadline
wake could return early without the parent releasing the ID. The parent-side
`state=CLOSING` hold itself is correct and must remain until the close resolves.

**Minimal fix.** `Component_env::close` now re-polls `session_cap(id)` after
every wake and re-blocks while the answer is invalid-without-error
(`CLOSE_REQUESTED`), returning only on an error (the parent no longer knows the
session). It does not re-issue `_parent.close()` and leaves `try_close`
unchanged. A create arriving under an ID whose old session is still closing is
parked in a parent-side pending-create slot and answered pending, then completed
when the late close frees the ID. `Root_proxy` close handling answers `CLOSED`
for unknown IDs, so a close processed before its create cannot leave the old
state permanently `CLOSING`. For a known session it now sends that answer before
the potentially blocking provider `Root_client::close`; the provider close still
runs to completion before the next request is dispatched. No unmapped create is
accepted and no quota or GRUB setting changes.

**RED → GREEN.** The focused `child_pending_op` regression drives
`with_new_session` directly with a deferred close followed by immediate same-ID
create. Before the fix it reproduces `requested conflicting session ID 55`,
`state=CLOSING`, and `VERDICT FAIL`. After the fix, the parked create becomes
pending, completes after the late close, all original checks plus the new
collision checks pass, `VERDICT PASS`, and the conflict-line count is zero.
A delayed-provider-close probe also proves that `CLOSED`, parent-state release,
and same-ID recreation complete while the provider close is held, and that the
provider close subsequently completes. The final base-linux run passes all 29
checks and prints `Run script execution successful`. `run/log` also passes.
`./tool/build verify` and the UEFI/USB `SPONGE_LAUNCH_CASE=multi` gate both pass
(`multi simultaneous n=3`, `multi siblings held n=3`, `windows PASS`). On
seL4/pc the six collision checks also pass; the separate older close-race phase
still times out against the harness's pre-existing `caps: 100` ceiling, so this
is not a full-suite seL4 green claim.

**Final image.** The rebuilt normal desktop image is
`var/dist/sponge-os-0.2.0-x86_64-sel4.img`, SHA-256
`d241ccc370306a14cba68cb916d2dcc30d6877f0dc9a5db10b5aec744048fec4`.
Its sidecar passes `sha256sum -c`. The close fix is Genode commit `36b50a6500`
and is recorded in `docs/11-environment.md` §4 as ledger row 38.

## 13. `pdf_view` product-media staging (2026-11-10)

**Field signature.** On the real UEFI/USB machine the desktop and the other
application windows appeared, but `pdf_view` alone never produced a window.
This was not the close/create signature above and did not justify another quota
or GRUB change.

**Root cause.** The shared `runtime_simple` recipe built `app/pdf_view` but did
not build or stage the MuPDF shared-library closure, did not stage `sample.pdf`,
and did not apply the served-media `binary="no"` transform. The working recipe
existed only as a local block in `run/sponge-desktop-disk.run`; the UEFI/USB
product scenario inherited none of those three required pieces.

**Fix.** `run/runtime-simple.inc` now owns the complete recipe for every grouped
product-media variant: it builds and stages the real `mupdf`, `freetype`, `jpeg`,
`jbig2dec`, and `openjpeg` `*.lib.so` artifacts (not ABI stubs), generates the
NUL-free two-page `sample.pdf`, and transforms the staged `pkg_pdf_view.xml` to
`<rom name="sample.pdf" binary="no"/>`. The former BIOS-local copy is removed.
`prod_launch_probe` and the UEFI/USB run script gained a single-app `pdf_view`
case without changing the existing three-app `multi` case or any quota.

**Verification.** The focused OVMF/USB
`SPONGE_LAUNCHTEST=on SPONGE_LAUNCH_CASE=pdf_view` run booted the grouped
runtime, started MuPDF, registered `pkg_runtime_simple -> pdf_view` in wm, and
passed both the launch-request and window-registration gates. The final
`SPONGE_LAUNCH_CASE=multi` regression still reports `multi simultaneous n=3`,
`multi siblings held n=3`, and `windows PASS`. The production dist boot reports
`alpha-probe: PASS` with no wedge or starvation lines. Direct inspection of the
final image's P3 confirms all five real libraries, the `binary="no"` metadata,
and a 923-byte, two-page PDF identical to the staged source. The user then
confirmed the successful `pdf_view` launch on the real UEFI/USB machine with this
image, closing the field failure. No startup-latency improvement is claimed
without timing evidence.
