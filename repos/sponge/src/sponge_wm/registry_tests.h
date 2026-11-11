/* SPDX-License-Identifier: Apache-2.0
 *
 * registry_tests — deterministic window-registry / protocol-state
 * cases for the sponge_wm core model (wm_registry.h).
 *
 * One case list, one harness (src/sponge_wm/self_test.cc): the cases
 * run host-side without a Genode runtime against the REAL production
 * model (wm_registry.cc) and the REAL production protocol library
 * (sponge_wm_protocol), so the tested logic is the shipped logic.
 *
 * The cases are pure and deterministic: no ROM/Report sessions, no
 * timers, no input, no clock. They mutate a local 'Registry' and
 * round-trip every published snapshot through the production
 * generators and parsers (Sponge::Wm_protocol), asserting the binding
 * contracts of docs/plans/phase16-sponge-wm.md §11 and the frozen
 * protocol v1 (docs/05-sponge-de.md §4.6).
 *
 * Native end-to-end behavior (real Gui sessions, real input, view
 * placement) is proven by the scenario probes
 * (run/sponge-wm-native*.run + test/native_wm_*), NOT here; nothing in
 * this file claims an integration result.
 */

#pragma once

namespace Sponge::Wm_registry {

/*
 * Report callback: invoked once per check with the outcome and a
 * short description (with expected/got detail inline on failure).
 */
using Check_fn = void (*)(bool ok, char const *what);

/*
 * Run every registry/protocol-state case. Returns the number of
 * failed checks (0 = all pass). 'report' must be non-null.
 */
unsigned run_registry_tests(Check_fn report);

}  /* namespace Sponge::Wm_registry */
