# 02 - Core Philosophy

> Every design decision starts here. When this document conflicts with
> another, this one wins. (However, the "Hard Rules" in `AGENTS.md` outrank
> this document.)

---

## 1. The Three Philosophies

Sponge OS pursues three philosophies **simultaneously**. They are not in
a trade-off relationship.

```
   ┌─────────────┐       ┌─────────────┐       ┌─────────────┐
   │ Convenience │       │   Control   │       │  Automation │
   └──────┬──────┘       └──────┬──────┘       └──────┬──────┘
          │                     │                     │
          └─────────────────────┼─────────────────────┘
                                │
                        ┌───────┴───────┐
                        │  Sponge OS    │
                        │  User         │
                        │  Experience   │
                        └───────────────┘
```

Design so that **none of the three is lost**. Frames such as "sacrifice
some control for convenience" are rejected. Instead we design like this:

> **Automation is the default; control is a door that opens for whoever
> needs it, as far as they need it.**

---

## 2. Convenience

### 2.1 Definition

Everyday users must be able to use the system without specialist
knowledge. Genode's internal concepts (sessions, capabilities, PD, RM,
...) must **not be exposed** in the default UX.

### 2.2 Evaluation Criteria

A claim of convenience has to be backed by code. Every convenience
feature has to answer these questions:

- How many user steps does it take to complete the task?
- Does the user need to know Genode terminology?
- Can users of common OSes (Windows, macOS) understand it intuitively?
- Do error messages point toward recovery?

These numbers must be stated in PR descriptions and documentation.

### 2.3 Examples

✅ **Good example**: package install
```
$ vct install firefox
```
The user only expresses "what to install". Dependency resolution,
component configuration, session wiring, and launcher registration are
all handled automatically.

❌ **Bad example**: the user has to manually wire the component tree.
Forcing the user to create an `init/fs` child under `init`, assign
capabilities, and write the routing violates the convenience principle.
That work can still **exist** as an escape hatch for control, however.

---

## 3. Control

### 3.1 Definition

Users who want it must be able to selectively take direct control over
**every part** of the system. Even the parts hidden by automation must
be inspectable and modifiable when the user asks.

### 3.2 Evaluation Criteria

- Can every automated task be **split into manual steps**?
- Are automation artifacts (generated configuration, component tree, routing) inspectable?
- Can the automation be disabled or bypassed?
- Will the user's changes be silently overwritten by automation?

### 3.3 The Escape Hatch Principle

Every automation in Sponge OS must have an escape hatch, in one of three
forms:

1. **`--manual` flag**: turn automation off and run step by step.
2. **Subcommands**: finer-grained commands that split the same task.
3. **Leitzentrale window**: a GUI for manipulating components directly.

Example (`vct install`):
```
vct install firefox              # automation (convenience)
vct install firefox --manual     # step by step (control)
vct install firefox --explain    # preview what automation would do
vct leitzentrale                 # manipulate components directly (advanced control)
```

### 3.4 Protecting User Changes

It is a control violation for automation to **silently overwrite** an
explicit user change. Config files, component trees, and routings that
the user has edited manually must be warned about or preserved before
automation touches them.

---

## 4. Automation

### 4.1 Definition

Repetitive and complex work is handled by the system. The user expresses
only **intent**; the system takes responsibility for the details.

### 4.2 Scope of Automation

Sponge OS automates the following:

- Package dependency resolution and component configuration
- Session wiring between components (capability routing)
- Hardware detection and driver configuration
- User environment defaults (themes, shortcuts, panel)
- Updates and migrations
- Common error recovery

### 4.3 Transparency of Automation

Automation must be **transparent**. When the user wants to know what
automation did and why, they should be able to. That meets the control
principle.

- Every automation action is logged.
- `vct explain <task>` simulates the automation before it runs.
- The reasoning behind automation decisions is documented or traceable
  in the code.

### 4.4 Behavior on Automation Failure

When automation fails:
1. Print a clear English error message.
2. Suggest a recovery path when possible.
3. Roll back partially applied changes, or explicitly tell the user
   that rollback is not possible.
4. When automatic recovery is hard, guide the user toward an escape
   hatch for manual intervention.

---

## 5. Resolving Philosophy Conflicts

When the three philosophies appear to clash, follow these rules:

| Situation | Resolution |
|---|---|
| Automation appears to obscure control | Add an escape hatch. Do not remove automation. |
| Control appears to sacrifice convenience | Keep control optional, not default. Do not remove convenience. |
| Convenience appears to require hiding automation | Keep automation transparent. Do not hide automation. |

The key point: all three philosophies must be **satisfied**. Trade-off
frames that sacrifice one to gain another are rejected.

---

## 6. Genode Philosophy (Foundational Principles)

Sponge OS accepts the following Genode principles as **foundational**.
They are immutable and outrank the user-facing philosophies (convenience,
control, automation).

1. **Component isolation**: each component runs only inside its own
   address space.
2. **Capability-based communication**: inter-component communication
   happens only through capability-based IPC.
3. **Least privilege**: a component requests only the minimum sessions
   it needs.
4. **Explicit configuration**: parent-to-child configuration is
   inspectable and explicit.

These principles cannot be violated in code, and any attempt to bypass
them in order to implement the user-facing philosophies is rejected.
Instead, the question is how to deliver those philosophies **on top of**
these principles.

See [`03-architecture.md`](03-architecture.md) for the details.