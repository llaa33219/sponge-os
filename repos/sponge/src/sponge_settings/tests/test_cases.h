/* SPDX-License-Identifier: Apache-2.0
 *
 * test_cases.h — deterministic pure test cases for sponge_settings.
 *
 * Covers discovery parsing of the §14.2 <settings> fields as carried
 * in the installed broadcast (identity bounds, duplicate/overflow
 * drops, keyword cap, category closed-enum fallback), both settings
 * launch channel wire shapes (request generation, result parsing and
 * identity matching), and the pure presentation layout (closed-enum
 * grouping order, content height, scroll clamp, hit-testing). Pure
 * and deterministic: no sessions, no timers, no input, no pixels.
 *
 * One case list, one harness: tests/self_test.cc (host-side; build
 * and run command in its header comment).
 */

#pragma once

namespace Sponge::Settings {

using Check_fn = void (*)(bool ok, char const *what);

unsigned run_test_cases(Check_fn report);

}  /* namespace Sponge::Settings */
