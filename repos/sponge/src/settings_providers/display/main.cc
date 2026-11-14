/* SPDX-License-Identifier: Apache-2.0
 *
 * display_settings — the first-party Display settings provider
 * (Criterion 18), typed-key registration SEAM.
 *
 * The `display.*` key domain is ACL-owned by the `display_settings`
 * writer label (sponge_configd/key_domain_acl.cc), but sponge_configd's
 * closed typed registry has NO display.* keys yet. This provider
 * therefore ships its full lifecycle — window, error reporting,
 * config channel on `display_settings_config_request`, closed quick
 * pair — with an empty write surface, and says so honestly in its
 * window instead of inventing settings that could not be stored.
 *
 * === Typed-key registration seam (configd follow-up) ===
 *
 * TODO(configd follow-up): register the exact display.* typed keys.
 * When the display surface is decided, the additions are exactly:
 *
 *   1. sponge_configd/main.cc `_registry[]`: one Key_def entry per
 *      decided display.* key (name-sorted; bump _num_keys / MAX_KEYS
 *      as needed). Each entry lands with its typed validator, in the
 *      same closed-registry style as wm.layout.mode.
 *   2. This file: fill `DISPLAY_OPS` / the row table below from the
 *      same key list and flip `SEAM_READY` to true. The shared
 *      Provider runtime then maps rows and quick ops onto the keys
 *      with no other change.
 *
 * Nothing else is invented here: no guessed brightness/resolution
 * policy, no placeholder keys, no writes outside the seam.
 *
 * Quick pair (display_settings_quick_request /
 * display_settings_quick_result) exists with a deliberately EMPTY
 * vocabulary until the seam lands — every operation answers the
 * bounded "not registered yet" error below.
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

constexpr bool SEAM_READY = false;

constexpr char const *const SEAM_MESSAGE =
	"display settings are not registered yet (pending the configd "
	"typed-key follow-up, see README.md)";

struct Display_provider : Provider
{
	Display_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		add_row(Row_kind::INFO, "display.* domain: display_settings",
		        "", Value_spec { });
		add_row(Row_kind::INFO, "typed keys: pending configd registration",
		        "", Value_spec { });
		add_row(Row_kind::INFO, "see settings_providers/display/README.md",
		        "", Value_spec { });

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "display_settings";
		d.settings_id = "display";
		d.package     = "sponge-de";
		d.title       = "Display";
		d.config      = Config_labels {
			"display_settings",
			"display_settings_config_request",
			"display_settings_config_result" };
		d.quick       = Quick_labels {
			"display_settings_quick_request",
			"display_settings_quick_result" };
		d.quick_ops   = nullptr;
		d.n_quick_ops = 0;
		return d;
	}

	Quick_result on_quick(char const *op, char const *id,
	                      char const *value, char *why,
	                      unsigned cap) override
	{
		(void)op;
		(void)id;
		(void)value;

		if (SEAM_READY)
			return Quick_result::UNKNOWN;

		why_set(why, cap, SEAM_MESSAGE);
		return Quick_result::ERROR;
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Display_provider provider { env };
}
