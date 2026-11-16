> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §5-§10 (Testing through Debugging)

## 5. Testing

### 5.1 Automated tests

The verification logic embedded in Genode's run scripts is the default
testing tool. A `run/*.run` file uses `run_script` and pattern matching
(`expect`) to verify the expected output. The four vct command
scenarios (`sponge-vct-help.run`, `sponge-vct-version.run`,
`sponge-vct-status.run`, `sponge-vct-component-list.run`) all rely on
this.

### 5.2 Component unit tests

Logic that is unit-testable (for example, vct's argument parser) is
extracted into separate headers so it can be tested outside Genode. A
unit-test framework has not been chosen yet (see `docs/09-roadmap.md`).

### 5.3 Manual verification

- Boot success: the log shows the scenario's success regex (for
  `sponge-minimal`, the `Sponge OS codename: Archaeocyte` line)
  followed by `Run script execution successful.`
- `vct --help` output verification.
- (Sponge DE) check that the window appears on the SDL framebuffer
  output.

---

## 6. Coding Workflow

Summary of `AGENTS.md` §4:

1. **Before changing anything**: read the relevant `docs/` and identify
   which philosophy or constraint is touched.
2. **Make the change**: update code and (if needed) documentation
   together.
3. **Verify**: build, run scenarios, and unit tests pass.
4. **Commit**: one logical change equals one commit. Conventional
   commits format.
5. **PR**: include in the PR body (a) which philosophy is satisfied,
   (b) the number of user steps, and (c) the test results.

---

## 7. Coding Rules Summary

Mojo is the preferred and default language for Sponge-owned code,
host tools and Genode/seL4 components alike
(`docs/plans/mojo-language-policy.md` DLP.1). New components start in
Mojo within the proven subset of §1.5; existing C++ migrates
opportunistically per unit, with run scenarios green at every step.
The rules below are the C++ rules: they govern thin adapters (entry
glue and `SRC_O` seams), existing C++ components not yet migrated,
and anything under `genode/`.

(Excerpt from `AGENTS.md` §3, applying to that C++ code)

- **C++ standard**: follow the version Genode supports (C++17
  recommended).
- **Naming**: classes in `PascalCase`, functions in `snake_case`,
  members in `snake_case_`.
- **Header guards**: `#pragma once`.
- **Smart pointers**: prefer `Genode::Constructible<T>` and
  `Genode::Allocator_avl`.
- **Exceptions**: none (Genode builds with exceptions disabled).
  Constructor failures are propagated through explicit initialization
  patterns.
- **Genode namespace**: `size_t`, `addr_t`, `uint32_t` etc. are NOT in
  the global namespace on real Genode. Always qualify as
  `Genode::size_t`, `Genode::addr_t`. A standalone test harness that
  `using`'s them into global scope will produce code that fails on
  the real framework.
- **Component entry points**: `Component::construct` and (if
  overridden) `Component::stack_size` are declared exactly as the
  framework expects. `stack_size` returns `Genode::size_t`, not bare
  `size_t`.
- **Prohibited**: type bypass, empty catch, monolithic components,
  capability bypass.

---

## 8. Contribution Workflow

### 8.1 Filing Issues

- (Use a separate channel until the repository becomes public.)
- An issue should include: symptoms, reproduction steps, expected
  behavior, actual behavior, environment information.

### 8.2 Submitting a PR

- Prefer small PRs.
- PR body template:
  ```
  ## Summary of changes
  ...
  ## Philosophies satisfied
  - [ ] Convenience: ...
  - [ ] Control: ...
  - [ ] Automation: ...
  ## Number of user steps
  This task asks the user for N interactions.
  ## Tests
  ...
  ```

### 8.3 Review Criteria

- Does the code follow the hard rules in `AGENTS.md`?
- Does the documentation match the code?
- Are tests included?
- Did the number of user steps grow (a convenience regression)?
- For patches against the vendored `genode/` tree, is the patch
  recorded in [`docs/11-environment.md`](../11-environment.md) §4 with a
  reason and a "drop when" note?

---

## 9. Documentation Rules

- All user-facing documentation is in English (see `AGENTS.md` §1.3).
- Code comments are in English (consistent with identifiers).
- New components are added to the inventory in
  `docs/04-components.md`.
- Design changes update the relevant `docs/` first
  (see `AGENTS.md` §4.1).
- Changes to the vendored tree, the third-party port set, or the
  environment contract are documented in
  [`docs/11-environment.md`](../11-environment.md).

---

## 10. Debugging

- Log output through Genode's `LOG` session (`Genode::log`,
  `Genode::warning`, `Genode::error`).
- When a run script executes, the LOG output is shown on the console.
- (Planned) `vct --verbose` will print backend communication in
  detail.
- (Planned) Leitzentrale will show component state in real time.
- For `base-sel4` boot failures, see the `-m 1G` lesson in §3.6 and
  the SKIM-window explanation in
  [`docs/11-environment.md`](../11-environment.md) §7.2.
- For Qt6 build failures, see the patch-ledger entry for patch #4 in
  [`docs/11-environment.md`](../11-environment.md) §4.

For difficult debugging issues, first re-read the principles in
`AGENTS.md` §5 "AI Agent-Specific Guidelines" and
`docs/02-philosophy.md`.

---

