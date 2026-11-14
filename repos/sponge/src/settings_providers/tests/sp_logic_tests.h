/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_logic_tests — deterministic cases for the settings-provider
 * shared logic (common/sp_validate.h, common/sp_rows.h).
 *
 * One case list, one harness (self_test.cc): the cases run host-side
 * without a Genode runtime against the REAL production validators —
 * the same validate_value / bindings_value / row helpers the eight
 * providers call — so the tested logic is the shipped logic (the
 * sponge_configd key_domain_acl self-test precedent).
 *
 * Covered contracts:
 *   - uint range parity (digits only, [min..max]) for panel.height;
 *   - enum parity (panel.position top/bottom; left/right stays
 *     provider-refused per D16.2 while configd's registry accepts it);
 *   - enum-list parity (panel.visible_widgets tokens);
 *   - hex color parity (#RRGGBB) for background.color;
 *   - allowlist parity (background.image F10 path defense);
 *   - comma-list id parity (panel.ids, [a-z0-9_-]{1,16});
 *   - theme.active non-empty + transport bounds;
 *   - shortcuts.bindings structured value parity (one-TAB rule,
 *     closed action enum, key aliases, reject-the-whole-write);
 *   - the shared bounds (length, XML attribute safety);
 *   - TOGGLES/ENUM row state derivation (mask/index round-trips).
 *
 * Nothing here claims an integration result: real Report/ROM session
 * wiring lives in the Genode components and is proven by the build
 * and scenario probes, not this file.
 */

#pragma once

namespace Sponge::Settings_tests {

using Report_fn = void (*)(bool ok, char const *what);

unsigned run_logic_tests(Report_fn report);

}  /* namespace Sponge::Settings_tests */
