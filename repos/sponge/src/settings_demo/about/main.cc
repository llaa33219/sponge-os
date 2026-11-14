/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_demo_about — provider binary for the
 * (settings_demo, about) settings entry (docs/12-package-format.md
 * §14). Owns a "Note" string value (0..32 of [A-Za-z0-9 space
 * . , ! ? -]; empty is a valid persisted value), persisted as
 * about.xml in the package-private store. See ../common/provider.h
 * for the shared lifecycle.
 */

#include "../common/provider.h"

namespace {

Sponge::Settings_demo::Provider_config const about_config {
	.who          = "settings_demo_about",
	.window_title = "About - settings_demo",
	.header       = "ABOUT - SETTINGS_DEMO",
	.value_label  = "Note:",
	.entry_name   = "note",
	.store_path   = "/about.xml",
	.default_value = "Sponge OS",
	.hint         = "Edit the note. Enter saves. Backspace deletes.",
	.rules        = { 0, 32 },
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Sponge::Settings_demo::Provider_main main { env, about_config };
}
