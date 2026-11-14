/* SPDX-License-Identifier: Apache-2.0
 *
 * theme_settings — the first-party Theme settings provider
 * (Criterion 18).
 *
 * Owns the `theme.*` key domain on its `theme_settings` configd
 * label: theme.active (open string in the closed registry — the four
 * shipped themes are default/light/dark/compact). The window offers
 * the shipped set as a cycling choice (the legacy Theme tab UX); an
 * externally-set theme name stays visible as-is (escape hatch intact,
 * configd accepts any non-empty name).
 *
 * Quick vocabulary (theme_settings_quick_request /
 * theme_settings_quick_result):
 *   <quick_request op="active" value="dark"/>  -> theme.active
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

char const *const THEMES[] = { "default", "light", "dark", "compact" };
constexpr unsigned N_THEMES = 4;

Value_spec active_spec()
{
	Value_spec s { };
	s.kind      = Value_kind::ENUM;
	s.choices   = THEMES;
	s.n_choices = N_THEMES;
	return s;
}

struct Theme_provider : Provider
{
	static Quick_op const QUICK_OPS[1];

	Theme_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		add_row(Row_kind::ENUM, "active theme", "theme.active", active_spec());

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "theme_settings";
		d.settings_id = "theme";
		d.package     = "sponge-de";
		d.title       = "Theme";
		d.config      = Config_labels {
			"theme_settings",
			"theme_settings_config_request",
			"theme_settings_config_result" };
		d.quick       = Quick_labels {
			"theme_settings_quick_request",
			"theme_settings_quick_result" };
		d.quick_ops   = QUICK_OPS;
		d.n_quick_ops = 1;
		return d;
	}
};

Quick_op const Theme_provider::QUICK_OPS[1] = {
	{ "active", "theme.active", { }, false },
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Theme_provider provider { env };
}
