/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Sponge OS version header. Single source of truth for version strings
 * across all Sponge OS components.
 *
 * DO NOT edit individual version strings in component code; always read
 * from this header so `vct --version`, boot logs, and the DE stay in sync.
 */

#pragma once

#include <util/string.h>

namespace Sponge {

/* Semantic version components of the Sponge OS distribution. */
constexpr unsigned VERSION_MAJOR = 0;
constexpr unsigned VERSION_MINOR = 1;
constexpr unsigned VERSION_PATCH = 0;

/* Human-readable pre-release marker (empty for stable releases). */
constexpr char const *VERSION_SUFFIX = "-alpha";

/* Human-readable version string, e.g. "0.1.0-alpha". */
constexpr char const *VERSION_STRING = "0.1.0-alpha";

/* Codename for the current development cycle. Sponge OS uses cellular-biology
 * themed codenames. Phase 0/1 uses "Archaeocyte" (a totipotent sponge cell
 * that can become any other cell type — fitting for the foundational stage). */
constexpr char const *CODENAME = "Archaeocyte";

}  /* namespace Sponge */
