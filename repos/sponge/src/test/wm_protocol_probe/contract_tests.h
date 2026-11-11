/* SPDX-License-Identifier: Apache-2.0
 *
 * Deterministic protocol-v1 contract cases for sponge_wm_protocol
 * (docs/05-sponge-de.md §4.6, frozen 2026-11-12).
 *
 * One case list, two harnesses:
 *
 *   - src/test/wm_protocol_probe/main.cc — the Genode test target
 *     ('wm_protocol_probe', printed marker "wm-protocol-probe: PASS")
 *     for the runtime scenarios;
 *   - src/test/wm_protocol_probe/self_test.cc — the host-side
 *     self-test (build/run command in its header comment) that proves
 *     the cases pass without a Genode runtime.
 *
 * The cases are pure and deterministic: no ROM/Report sessions, no
 * timers, no input, no clock. They generate documents into
 * caller-owned buffers, parse them back through Genode::Node, and
 * compare against golden fixtures transcribed from the delivered
 * producers/consumers (upstream gems wm/window_layouter emission
 * order and the TasklistController emission/parsing format).
 */

#pragma once

namespace Sponge::Wm_protocol {

/*
 * Report callback: invoked once per check with the outcome and a
 * short description (with expected/got detail inline on failure).
 */
using Check_fn = void (*)(bool ok, char const *what);

/*
 * Run every contract case. Returns the number of failed checks (0 =
 * all pass). 'report' must be non-null.
 */
unsigned run_contract_tests(Check_fn report);

}  /* namespace Sponge::Wm_protocol */
