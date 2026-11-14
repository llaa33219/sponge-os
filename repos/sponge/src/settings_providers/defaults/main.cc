/* SPDX-License-Identifier: Apache-2.0
 *
 * defaults_settings — the first-party Reset-to-Defaults provider
 * (Criterion 18).
 *
 * Owns exactly `bake.applied` on its `defaults_settings` configd
 * label (the key_domain_acl.cc domain is the single key). The
 * provider triggers configd's baked-defaults reset path the same way
 * vct's `bake reset` and the legacy Defaults tab do: one write of
 * bake.applied=no. configd reapplies ONLY the baked keys and
 * theme.active, preserves every other user-set key, then records
 * bake.applied=yes — the provider never writes any of that itself.
 *
 * Quick vocabulary (defaults_settings_quick_request /
 * defaults_settings_quick_result):
 *   <quick_request op="reset"/>  -> bake.applied=no
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

Value_spec trigger_spec()
{
	Value_spec s { };
	s.kind      = Value_kind::ENUM;
	s.choices   = (char const *const []){ "no" };
	s.n_choices = 1;
	return s;
}


struct Defaults_provider : Provider
{
	Defaults_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		Row &state = add_row(Row_kind::INFO, "bake state", "bake.applied",
		                     Value_spec { });
		(void)state;

		add_row(Row_kind::INFO, "resets: panel height, widgets,",
		        "", Value_spec { });
		add_row(Row_kind::INFO, "  launcher sort, theme",
		        "", Value_spec { });
		add_row(Row_kind::INFO, "keeps: position, background, shortcuts",
		        "", Value_spec { });

		Row &reset = add_row(Row_kind::ACTION, "reset to baked defaults",
		                     "", Value_spec { });
		reset.custom = true;

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "defaults_settings";
		d.settings_id = "defaults";
		d.package     = "sponge-de";
		d.title       = "Reset to Defaults";
		d.config      = Config_labels {
			"defaults_settings",
			"defaults_settings_config_request",
			"defaults_settings_config_result" };
		d.quick       = Quick_labels {
			"defaults_settings_quick_request",
			"defaults_settings_quick_result" };
		d.quick_ops   = nullptr;
		d.n_quick_ops = 0;
		return d;
	}

	bool _trigger_reset(char *why, unsigned cap)
	{
		return write_value("bake.applied", trigger_spec(), "no", why, cap);
	}

	bool on_apply(Row &r, char *why, unsigned cap) override
	{
		if (r.kind == Row_kind::ACTION)
			return _trigger_reset(why, cap);

		return Provider::on_apply(r, why, cap);
	}

	Quick_result on_quick(char const *op, char const *id,
	                      char const *value, char *why,
	                      unsigned cap) override
	{
		(void)value;

		if (id[0] != 0) {
			why_set(why, cap, "this operation does not take an id selector");
			return Quick_result::ERROR;
		}

		if (Genode::strcmp(op, "reset") == 0) {
			if (!_trigger_reset(why, cap))
				return Quick_result::ERROR;
			return Quick_result::OK;
		}

		return Quick_result::UNKNOWN;
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Defaults_provider provider { env };
}
