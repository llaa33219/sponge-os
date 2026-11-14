/* SPDX-License-Identifier: Apache-2.0
 *
 * terminal_settings — the Terminal settings provider (Criterion 18),
 * typed-key registration SEAM. Ships in the terminal package payload
 * (pkg/terminal/metadata.xml declares the <settings> entry; Terminal
 * is not core DE).
 *
 * The `terminal.*` key domain is ACL-owned by the `terminal_settings`
 * writer label (sponge_configd/key_domain_acl.cc), but sponge_configd's
 * closed typed registry has NO terminal.* keys yet. This provider
 * ships its full lifecycle — window, error reporting, config channel
 * on `terminal_settings_config_request`, closed quick pair — with an
 * empty write surface, and says so honestly in its window.
 *
 * === Typed-key registration seam (configd follow-up) ===
 *
 * TODO(configd follow-up): register the exact terminal.* typed keys.
 * When the terminal surface is decided, the additions are exactly:
 *
 *   1. sponge_configd/main.cc `_registry[]`: one Key_def entry per
 *      decided terminal.* key (name-sorted; bump _num_keys / MAX_KEYS
 *      as needed), each with its typed validator in the
 *      wm.layout.mode style.
 *   2. This file: fill the row table and `TERMINAL_OPS` from the same
 *      key list and flip `SEAM_READY` to true. The shared Provider
 *      runtime then maps rows and quick ops onto the keys with no
 *      other change.
 *
 * No settings surface is invented here (font, shell, colors, ... are
 * product decisions for the follow-up, not placeholders).
 *
 * Quick pair (terminal_settings_quick_request /
 * terminal_settings_quick_result) exists with a deliberately EMPTY
 * vocabulary until the seam lands — every operation answers the
 * bounded "not registered yet" error below.
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

constexpr bool SEAM_READY = false;

constexpr char const *const SEAM_MESSAGE =
	"terminal settings are not registered yet (pending the configd "
	"typed-key follow-up, see README.md)";

struct Terminal_provider : Provider
{
	Terminal_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		add_row(Row_kind::INFO, "terminal.* domain: terminal_settings",
		        "", Value_spec { });
		add_row(Row_kind::INFO, "typed keys: pending configd registration",
		        "", Value_spec { });
		add_row(Row_kind::INFO, "see settings_providers/terminal/README.md",
		        "", Value_spec { });

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "terminal_settings";
		d.settings_id = "terminal";
		d.package     = "terminal";
		d.title       = "Terminal";
		d.config      = Config_labels {
			"terminal_settings",
			"terminal_settings_config_request",
			"terminal_settings_config_result" };
		d.quick       = Quick_labels {
			"terminal_settings_quick_request",
			"terminal_settings_quick_result" };
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
	static Terminal_provider provider { env };
}
