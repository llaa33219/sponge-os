/* SPDX-License-Identifier: Apache-2.0
 *
 * background_settings — the first-party Background settings provider
 * (Criterion 18).
 *
 * Owns the `background.*` key domain on its `background_settings`
 * configd label: background.color (#RRGGBB hex, W2 validator) and
 * background.image (closed allowlist — the F10 path-traversal
 * defense). This is the provider half of the legacy Background tab;
 * the single shipped image stays /system/background/default.png.
 *
 * Quick vocabulary (background_settings_quick_request /
 * background_settings_quick_result):
 *   <quick_request op="color" value="#1e1e2e"/>  -> background.color
 *   <quick_request op="image" value="/system/background/default.png"/>
 *                                                    -> background.image
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

char const *const IMAGES[] = { "/system/background/default.png" };
constexpr unsigned N_IMAGES = 1;

Value_spec color_spec()
{
	Value_spec s { };
	s.validator = Value_spec::V_HEX_COLOR;
	return s;
}

Value_spec image_spec()
{
	Value_spec s { };
	s.kind      = Value_kind::PATH;
	s.choices   = IMAGES;
	s.n_choices = N_IMAGES;
	return s;
}

struct Background_provider : Provider
{
	static Quick_op const QUICK_OPS[2];

	Background_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		add_row(Row_kind::TEXT, "color", "background.color", color_spec());
		add_row(Row_kind::ENUM, "image", "background.image", image_spec());

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "background_settings";
		d.settings_id = "background";
		d.package     = "sponge-de";
		d.title       = "Background";
		d.config      = Config_labels {
			"background_settings",
			"background_settings_config_request",
			"background_settings_config_result" };
		d.quick       = Quick_labels {
			"background_settings_quick_request",
			"background_settings_quick_result" };
		d.quick_ops   = QUICK_OPS;
		d.n_quick_ops = 2;
		return d;
	}
};

Quick_op const Background_provider::QUICK_OPS[2] = {
	{ "color", "background.color", { }, false },
	{ "image", "background.image", { }, false },
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Background_provider provider { env };
}
