/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_demo_greeting — provider binary for the
 * (settings_demo, greeting) settings entry (docs/12-package-format.md
 * §14). Owns a "Greeting" string value (1..24 of [A-Za-z0-9 space
 * . , ! ? -]), persisted as greeting.xml in the package-private
 * store. See ../common/provider.h for the shared lifecycle.
 */

#include "../common/provider.h"

namespace {

Sponge::Settings_demo::Provider_config const greeting_config {
	.who          = "settings_demo_greeting",
	.window_title = "Greeting - settings_demo",
	.header       = "GREETING - SETTINGS_DEMO",
	.value_label  = "Greeting:",
	.entry_name   = "greeting",
	.store_path   = "/greeting.xml",
	.default_value = "Hello",
	.hint         = "Type to edit. Enter saves. Backspace deletes.",
	.rules        = { 1, 24 },
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Sponge::Settings_demo::Provider_main main { env, greeting_config };
}
