/* SPDX-License-Identifier: Apache-2.0
 *
 * acl_tests — deterministic per-label key-domain ACL cases for
 * sponge_configd's Criterion 18 gate (key_domain_acl.{h,cc}).
 *
 * One case list, one harness (self_test.cc): the cases run host-side
 * without a Genode runtime against the REAL production gate — the
 * same Acl::gate_set / Acl::Key_domain / writer table that
 * main.cc `_do_set` consults — so the tested gate logic is the
 * shipped gate logic. The local `Store` harness applies a write only
 * when the gate returns Apply, mirroring main.cc's structure where
 * the gate runs before the first mutation; the no-mutation cases
 * assert the store is byte-unchanged after a rejection.
 *
 * Covered contracts (Criterion 18 / D16.1):
 *   - in-domain success per label,
 *   - out-of-domain rejection with `key-outside-domain` and NO
 *     store mutation (pattern keys included — no instantiation),
 *   - vct every-domain admin success,
 *   - unknown-key rejection on the registry-first path (even for
 *     admin and even when the key is also out of domain),
 *   - pattern-key panel domain behavior (panel.<id>.* and panel.ids
 *     belong to the panel_settings prefix domain),
 *   - the built-in label→domain mapping (wm.* reserved for
 *     wm_settings, display.* / terminal.* mapped to their labels,
 *     defaults_settings owns exactly bake.applied),
 *   - allowlist text parsing and override semantics.
 *
 * Nothing here claims an integration result: real Report/ROM
 * session wiring and the closed typed registry live in the Genode
 * component and are proven by the scenario probes, not this file.
 */

#pragma once

namespace Sponge::Configd::Acl_tests {

/*
 * Report callback: invoked once per check with the outcome and a
 * short description (with expected/got detail inline on failure).
 */
using Check_fn = void (*)(bool ok, char const *what);

/*
 * Run every ACL case. Returns the number of failed checks
 * (0 = all pass). `report` must be non-null.
 */
unsigned run_acl_tests(Check_fn report);

}  /* namespace Sponge::Configd::Acl_tests */
