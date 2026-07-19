/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Sponge OS platform identification.
 *
 * Sponge OS targets seL4 as its sole microkernel base. Rationale:
 *   - formally verified microkernel -> matches our control philosophy
 *   - capability-native -> maps cleanly onto Genode's capability model
 *   - upstream Genode `base-sel4` is actively maintained
 *
 * Components must NOT branch on `SPONGE_PLATFORM` to add quirks. If you
 * find yourself writing `#if SPONGE_PLATFORM_IS_SEL4`, instead express
 * the dependency through Genode's standard `SPEC` macros (e.g. `SPEC_sel4`)
 * or through a runtime capability check. This header exists for
 * diagnostic strings only.
 */

#pragma once

namespace Sponge {

constexpr char const *PLATFORM_NAME    = "seL4";
constexpr char const *PLATFORM_BASE    = "base-sel4";
constexpr char const *PLATFORM_VENDOR  = "seL4 Foundation";

}  /* namespace Sponge */
