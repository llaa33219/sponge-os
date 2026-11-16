# 06 - vct Design

> vct (Very Convenient Tool) is the single entry point for managing
> Sponge OS. This document defines vct's design philosophy, command
> structure, and consistency rules.

---

## 1. Name and Philosophy

**vct** stands for **V**ery **C**onvenient **T**ool. The name is the
goal — a "very convenient tool". Every vct design decision starts from
this question:

> "How many steps does it take the user to finish this task? Can it
> be one step?"

At the same time, control must not be lost: every automated task has an
escape hatch (see `AGENTS.md` §3.3).

---

## 2. Command Structure

vct follows a subcommand structure. General form:

```
vct <command> [subcommand] [options] [args]
```

Examples:

```
vct install firefox           # install a package
vct install firefox --explain # preview what automation would do
vct status                    # summary of system state
vct config theme dark         # change a configuration value (positional)
vct leitzentrale              # open the Leitzentrale window
```

Common flags:

| Flag | Purpose |
|---|---|
| `--help`, `-h` | Concise English summary followed by detailed English help |
| `--lang ko` | Render user-facing messages in Korean when the runtime supports it |
| `--json` | Machine-readable output (for script integration) |
| `--version`, `-V` | vct version |
| `--verbose` | Detailed log (show automation steps) |
| `--explain` | Report what automation would do and exit (do not execute) |
| `--manual` | Turn automation off and run step by step |

`--help` starts with a concise English summary. Example:

```
$ vct install --help
Install a Sponge OS package.

Usage: vct install <package> [--explain] [--manual] [--json]

Resolves dependencies and configures the component tree automatically
by default. Use --explain to preview the steps, or --manual to run
them one by one.

Options:
  --explain     Preview the planned steps and exit (no execution).
  --manual      Turn automation off and run step by step.
  --json        Print the result as JSON.
  --no-deps     Skip automatic dependency installation (not yet implemented).
  ...
```

The `--lang ko` flag switches the user-facing summary and step
messages to Korean when a Korean translation is available.

---

## 3. Argument Delivery (Design, Locked)

vct receives its arguments through a config ROM provided by `init`.
When vct starts, `init` supplies a `<config>` element in Genode's HID
(human-intelligible data) format:

```hid
+ config
| + arg status
| + arg --json
```

vct parses the `arg` children (each carries its value in the `name`
attribute) into its internal `Args` struct. The legacy XML form is still
accepted for existing test fixtures and tooling:

```xml
<config>
  <args>
    <arg>status</arg>
    <arg>--json</arg>
  </args>
</config>
```

The user-facing shell (when one exists) constructs this config and spawns
vct on the user's behalf. This is the standard Genode pattern.

The parser supports the common flags in `AGENTS.md` §3.3, including
`--lang ko` and `--json`.

---

## 4. Proposed Subcommand List

Moved to [`vct/subcommands.md`](vct/subcommands.md) (§4.1-§4.8). A citation of `docs/06-vct.md §4.8` resolves to §4.8 of that file.

---

## 5. Automation vs Control: An Example

Moved to [`vct/automation-example.md`](vct/automation-example.md) (§5.1-§5.3).

---

## 6. Output Formats

Moved to [`vct/output-backend-status.md`](vct/output-backend-status.md) (§6.1-§6.2) with §7 and §8.

---

## 7. Backend Call Structure

Moved to [`vct/output-backend-status.md`](vct/output-backend-status.md) with §6 and §8.

---

## 8. Implementation Status

Moved to [`vct/output-backend-status.md`](vct/output-backend-status.md) with §6 and §7.
