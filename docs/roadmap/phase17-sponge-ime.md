> Part of [09 - Roadmap](../09-roadmap.md) §10, Phase 17.

### Phase 17: Sponge IME — Multi-Language / CJK Input

**Entry condition:** Phase 16 Criterion 27 (final Genode/seL4 patch
cleanup) must be complete before this phase starts.

#### Goal

A Sponge-native input method editor, implemented as a proper Genode
component, supporting multiple languages with CJK input (Korean
first-class, given the project's audience).

#### Completion Criteria

- [ ] `sponge_ime` component implemented: sits in the input chain
  (event_filter-level integration), capability-minimal, and
  toggleable at runtime.
- [ ] Korean Hangul composition working (2-set at minimum); at least
  one additional CJK or multi-language layout supported.
- [ ] IME state configurable through `sponge_configd`; automation
  default (sensible layout per locale) with manual override.
- [ ] Scenario-verified: composed characters reach a focused text
  field in the text editor and terminal.
