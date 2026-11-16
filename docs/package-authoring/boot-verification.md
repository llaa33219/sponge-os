# 16 - Package Authoring: Boot Verification and Pitfalls (§4, §5)

> **Breadcrumb:** [`docs/16-package-authoring.md`](../16-package-authoring.md)
> holds §1-§2, §6-§8, and §10; this file holds §4 and §5. Section
> numbers are preserved across the split, so `§N` references resolve
> against the whole document set (the parent's part table links every
> part).

---

## 4. The Boot-Verification Contract

Every package, regardless of which path in §3 produced it, must boot.
AGENTS.md §4.2 requires "build verification" and "run scenarios" for
every logical change. For packages that means a `run/<scenario>.run`
that:

1. Builds `lib/ld` (the dynamic linker; missing it from any boot
   image produces silent `ld.lib.so` ROM-denied failures at child
   load time) alongside `core init <component>`.
2. Stages the §2.3 `pkg_<name>.xml` plus `pkg_index.xml`.
3. Passes `[build_artifacts]` to `build_boot_image` and adds every
   staged extra (payload, lib.sos, content ROMs, font/plugin tars)
   by name to the explicit module list.
4. Asserts a behavioral marker, `<name>-probe: PASS`, bounded by
   `run_genode_until`. The run tool fails loud on timeout (fail-loud;
   no silent hangs).

### 4.1 The probe contract

Probes are plain Genode components (`repos/sponge/src/test/<name>_probe/`).
They drive `sponge_pkgd` over the `request`/`result`/`installed`
ROM/Report relay and assert whatever makes the package behaviorally
real. The minimum pattern, from `repos/sponge/src/test/calculator_probe/main.cc`:

1. **Install via `sponge_pkgd`.** Submit
   `<request op="install" pkg="<name>"/>`; poll the `result` ROM for
   a `<result op pkg status="..."/>` reply. Fail loud on missing
   reply or non-`ok` status (`calculator_probe/main.cc:381-388`).
2. **Verify the installed broadcast.** Read the `installed` ROM,
   find `<package name="<name>" ... running="..."/>`, assert the
   expected `running` value. For calculator (no `<autostart/>`) this
   is `"no"` after install and `"yes"` after launch
   (`calculator_probe/main.cc:392-436`).
3. **Launch via `sponge_pkgd`.** Submit
   `<request op="launch" pkg="<name>"/>`; poll; assert `status="ok"`.
4. **Assert the actual behavior.** The calculator polls
   nitpicker's `Capture` for a non-background fraction ≥ 0.30 AND
   ≥ 8 distinct 4-bit-per-channel color buckets
   (`calculator_probe/main.cc:131-168`, `341-365`); the PDF viewer
   samples for R+G+B > 200 white with non-bg fraction ≥ 0.50; the
   text editor, terminal, and Falkon follow the same pattern
   (Capture-side pixel verification distinct from the nitpicker
   background). Bare exit 0 must NOT pass; that is the
   `misleading_success_output` adversarial class.
5. **Exercise the error paths.** Submit
   `<request op="launch" pkg="nosuchpkg-13"/>` and assert
   `status="not-installed"`. Submit a second `launch calculator`
   and assert `status="already-running"`. Both must be returned by
   `sponge_pkgd` as clean statuses, never as crashes
   (`calculator_probe/main.cc:457-479`).

The probe logs `<name>-probe: PASS` on success; the run scenario
gates its `run_genode_until` on that regex; any timeout or FAIL line
becomes a non-zero exit. Missing-binary failure channels are
verified separately (e.g.
`run/sponge-textedit-fail.run` for the textedit case).

Probes in observe mode can also gate on
`run_genode_until` emitting QMP-TARGET markers consumed by the
`run/qmp.inc` helper. The pattern is documented in
[`docs/08-development.md`](../08-development.md) §4.4.

### 4.2 What "PASS" actually proves

The marker proves the package's behavioral contract end-to-end:
metadata parsed, dependency graph resolved (if any), `<start>` node
materialized in `pkg_runtime`, binary loaded, runtime reached a
visible/computable state, and pkgd's error paths return clear
statuses without crashing. It does NOT prove that the application
is correct or complete; that is the upstream component's own test
surface. The marker proves the *package wiring* is right.

---

## 5. Pitfalls

Hard-won lessons from Phase 7 through Phase 13. Each one has
bitten at least one package shipped in this repository.

### 5.1 `<env>` Attribute Format

`genode/repos/libports/include/libc/args.h:64-68` accepts the
**content form** (`<env name="K">V</env>`) and the **legacy form**
(`<env key="K" value="V"/>`). The two branches are
disambiguated by `has_attribute("key")`:

```cpp
auto with_env = [] (Node const &node, auto const &fn)
{
    if (node.has_type("env") && node.has_attribute("name") &&
        !node.has_attribute("key"))
        fn(node);
};

auto with_legacy_env = [] (Node const &node, auto const &fn)
{
    if (node.has_type("env") && node.has_attribute("key") &&
        node.has_attribute("value"))
        fn(node);
};
```

The plausible-looking hybrid `<env name="K" value="V"/>` matches
neither branch:

- `has_attribute("name")` is true, `has_attribute("key")` is true,
  so `with_env`'s guard fails (the `!has_attribute("key")` clause is
  false).
- `has_attribute("key")` is true, `has_attribute("value")` is true,
  so `with_legacy_env`'s guard could pass, but the legacy reader
  looks for `key` and `value` attributes; here they're attributes
  on a `name`-bearing element, which the reader skips because its
  own presence check is `<has_type("env") && has_attribute("key")
  && has_attribute("value")>`, but only when the value is read
  from `key`/`value` (not `name`/`value`).

Either way, the `K=V` pair is not consumed. **Symptom:** every
variable reads back empty (`PATH=`); PATH-search commands fail with
"command not found" while absolute paths still work.

**Fix.** Use one of the two accepted forms and be consistent. The
terminal package uses the content form (`<env name="TERM">screen</env>`
and friends, `pkg/terminal/metadata.xml:210-213`), and carried this
latent bug from Phase 7 to Phase 13, the symptom was dormant
because bash was always launched with its binary's full path, which
doesn't touch `PATH`.

### 5.2 Config Serialization Budget

`sponge_pkgd` serializes each package's metadata `<config>` into a
3072-byte buffer
(`repos/sponge/src/sponge_pkgd/main.cc:641`):

```cpp
char cfg_buf[3072] { };
Genode::Xml_generator::Result const cfg_res =
    Genode::Xml_generator::generate(
        Genode::Byte_range_ptr(cfg_buf, sizeof(cfg_buf)),
        Genode::Xml_generator::Tag_name("config"),
        [&](Genode::Xml_generator &g) {
            (void)g.append_node_content(child, { .value = 16 });
        });
if (cfg_res.ok())
    out.config_xml = Genode::String<3072>(cfg_buf);
else
    Genode::warning("pkg: <config> for ", name,
                    " exceeded serialization buffer");
```

A package whose serialized `<config>` exceeds 3072 bytes trips the
"exceeded serialization buffer" warning and **silently drops the
config**. The generated `<start>` node then runs with no libc/vfs
wiring, and the package fails to open libc-required files at startup
with no upstream-visible cause.

**The terminal package currently serializes to ~2956 bytes.** Its
verbose header comments do NOT count (they're metadata-side
`<!-- ... -->` markup that `append_node_content` strips), but the
inline `<vfs>` with nine `<tar>` entries plus four `<env>` plus
libc plus the nested sub-init's `<start>` graph add up fast. **Keep
metadata configs lean; put long rationale in XML comments.** Any
future tool added to the terminal toolset pushes that budget closer
to the ceiling; if you reach it, the silent-drop behavior is the
failure mode you'll see, and trimming the `<config>` is the fix
(comments remain free).

### 5.3 Synthetic Input Injection

Probes that drive nitpicker's `Event` service to type strings into
a focused terminal must submit `Press_char` AND `Release` per
character. Nitpicker drops a repeated `Press_char` with the same
keycode unless a `Release` intervenes (logged as `suspicious double
press of KEY_UNKNOWN`).

The terminal probe's keystroke pattern
(`repos/sponge/src/test/terminal_probe/main.cc:499-501` and the
`/bin/ls` block at 596-609) submits `Press_char` + `Release`
together per character. Drop the `Release` and the second `Press_char
KEY_A` disappears; the bash prompt then receives only the first 'a',
and the probe's glyph-count assertion fails by one.

QMP-driven recipes via `run/qmp.inc` (`qmp_type`, `qmp_send_key`)
get paired key-down/key-up for free. Synthetic `Event`-session
probes must emit the pair explicitly.

### 5.4 Quota Budgeting

A package's `<quota ram>` must cover the SUM of its sub-init
children plus init overhead. Phase 7 packaged `terminal` at 160M
and squeezed bash to "configured RAM exceeds available RAM, proceed
with ..."; the < 224M ceiling was the practical floor. Phase 13 W2
raised it to 224M to fit:

| Child | Quota |
|---|---|
| `terminal` (gems server) | 48M |
| `vfs` (with nine tar mounts and `<terminal/>` plugin) | 48M |
| `vfs_rom` (cached_fs_rom, serves `/bin/*`) | 32M |
| `/bin/bash` (noux-pkg) | 64M |
| **Sum of children** | **192M** |
| Package quota (`pkg/terminal/metadata.xml:52`) | 224M |

The 32M headroom covers init's bookkeeping. Below that, init aborts
the child at start time with the "exceeds available RAM" warning,
and the probe's terminal window never renders. If you add another
sub-init child to the terminal (an `xinit` configd bridge, a
sound), raise `pkg/terminal/metadata.xml`'s `<quota ram>` by at
least that child's quota + 8M for init overhead, then re-run
`run/sponge-terminal.run` and verify the boot is still clean.

Caps budgets follow the same rule (Phase 9 falkon lesson,
`docs/09-roadmap.md` §11.1). The repository's falkon metadata
records the empirical floor for a Qt6-WebEngine class application;
no other app in the Phase-13 baseline approaches that ceiling.
