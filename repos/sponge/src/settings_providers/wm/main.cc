/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_settings — the first-party Window Management settings provider
 * (Criterion 18).
 *
 * Owns the `wm.*` key domain on its `wm_settings` configd label. The
 * domain holds exactly one registered key today — wm.layout.mode
 * (closed enum floating|tiling, consumed by sponge_layouter via the
 * configd broadcast; D16W.11) — and this provider presents exactly
 * that, no invented surface. Further wm.* keys join through the same
 * row/quick pattern when they are registered in configd's closed
 * typed registry.
 *
 * Quick vocabulary (wm_settings_quick_request /
 * wm_settings_quick_result):
 *   <quick_request op="layout" value="tiling"/>  -> wm.layout.mode
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

char const *const MODES[] = { "floating", "tiling" };
constexpr unsigned N_MODES = 2;

Value_spec mode_spec()
{
	Value_spec s { };
	s.kind      = Value_kind::ENUM;
	s.choices   = MODES;
	s.n_choices = N_MODES;
	return s;
}

struct Wm_provider : Provider
{
	static Quick_op const QUICK_OPS[1];

	Wm_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		add_row(Row_kind::ENUM, "layout mode", "wm.layout.mode", mode_spec());

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "wm_settings";
		d.settings_id = "wm";
		d.package     = "sponge-de";
		d.title       = "Window Management";
		d.config      = Config_labels {
			"wm_settings",
			"wm_settings_config_request",
			"wm_settings_config_result" };
		d.quick       = Quick_labels {
			"wm_settings_quick_request",
			"wm_settings_quick_result" };
		d.quick_ops   = QUICK_OPS;
		d.n_quick_ops = 1;
		return d;
	}
};

Quick_op const Wm_provider::QUICK_OPS[1] = {
	{ "layout", "wm.layout.mode", { }, false },
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Wm_provider provider { env };
}
