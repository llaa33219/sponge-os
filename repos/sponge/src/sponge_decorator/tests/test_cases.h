/* SPDX-License-Identifier: Apache-2.0
 *
 * test_cases.h — deterministic pure test cases for sponge_decorator.
 *
 * Covers theme parsing (Sponge INI semantics), focus-state style
 * selection (window_chrome.focused -> chrome_style), margin
 * validation, the published decorator_margins shape, chrome view
 * geometry (frozen button/zone placement), glyph bitmap/rect
 * calculations, and title fit/origin calculations. Pure and
 * deterministic: no sessions, no timers, no input, no pixels — the
 * renderer's draw placement is checked at the geometry level only.
 *
 * One case list, one harness: tests/self_test.cc (host-side; build
 * and run command in its header comment).
 */

#pragma once

namespace Sponge::Decorator {

using Check_fn = void (*)(bool ok, char const *what);

unsigned run_test_cases(Check_fn report);

}  /* namespace Sponge::Decorator */
