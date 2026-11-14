/* SPDX-License-Identifier: Apache-2.0
 *
 * key_domain_acl — per-writer-label key-domain ACL implementation
 * (Criterion 18 / D16.1). See key_domain_acl.h for the contract.
 *
 * The writer table below is the single source of truth for the
 * Criterion 18 provider-label mapping:
 *
 *   wm.*        is reserved for wm_settings (D16W.11),
 *   display.*   maps to display_settings,
 *   terminal.*  maps to terminal_settings,
 *   panel.* / background.* / theme.* / shortcuts.* map to their
 *   same-named provider labels (the historical tab surfaces),
 *   defaults_settings owns exactly `bake.applied` (the reset
 *   trigger; the reset itself runs inside configd),
 *   and vct is the every-domain admin escape hatch.
 *
 * `clock.format`, `launcher.sort_by`, `leitzentale.enabled`, and the
 * read-only bake metadata keys are deliberately NOT in any provider
 * default domain: their key sets are a separate decision (a
 * deployment can hand a key to a label via a `<writer keys="..."/>`
 * declaration in configd's component config without a code change).
 */

#include "key_domain_acl.h"

namespace Sponge::Configd::Acl {

using Genode::strcmp;
using Genode::strlen;


/*
 * The writer table. Every entry carries exactly one dedicated
 * config_request ROM session (`request_rom`) and one result report
 * label (`result_label`); the vct row keeps the historical labels
 * byte-identical so existing scenarios and vct wiring are unchanged.
 */
static Writer const _writers[MAX_WRITERS] = {
	{ "vct",                 true,  "*",
	  "config_request",                   "config_result" },
	{ "panel_settings",      false, "panel.*",
	  "panel_settings_config_request",    "panel_settings_config_result" },
	{ "background_settings", false, "background.*",
	  "background_settings_config_request", "background_settings_config_result" },
	{ "theme_settings",      false, "theme.*",
	  "theme_settings_config_request",    "theme_settings_config_result" },
	{ "shortcuts_settings",  false, "shortcuts.*",
	  "shortcuts_settings_config_request", "shortcuts_settings_config_result" },
	{ "defaults_settings",   false, "bake.applied",
	  "defaults_settings_config_request", "defaults_settings_config_result" },
	{ "display_settings",    false, "display.*",
	  "display_settings_config_request",  "display_settings_config_result" },
	{ "wm_settings",         false, "wm.*",
	  "wm_settings_config_request",       "wm_settings_config_result" },
	{ "terminal_settings",   false, "terminal.*",
	  "terminal_settings_config_request", "terminal_settings_config_result" },
};


Writer const *writers()
{
	return _writers;
}


Writer const *find_writer(char const *label)
{
	for (unsigned i = 0; i < MAX_WRITERS; ++i)
		if (strcmp(_writers[i].label, label) == 0)
			return &_writers[i];
	return nullptr;
}


bool Key_domain::contains(char const *key) const
{
	if (all)
		return true;

	for (unsigned i = 0; i < num_entries; ++i) {
		char const *entry = entries[i];
		Genode::size_t const entry_len = strlen(entry);

		/* "*" inside the list would have been folded into `all`. */
		if (entry_len == 1 && entry[0] == '*')
			return true;

		/* "prefix.*": any key beginning with "prefix." */
		if (entry_len >= 2 &&
		    entry[entry_len - 2] == '.' && entry[entry_len - 1] == '*') {
			Genode::size_t const prefix_len = entry_len - 1;
			if (strlen(key) >= prefix_len &&
			    strcmp(key, entry, prefix_len) == 0)
				return true;
			continue;
		}

		/* exact key */
		if (strcmp(key, entry) == 0)
			return true;
	}
	return false;
}


bool parse_domain(char const *spec, Key_domain &out)
{
	out = Key_domain { };
	out.all = false;

	/* Preserve the declared text verbatim (truncated) for
	 * diagnostics and the key-outside-domain error message. */
	for (Genode::size_t i = 0; i + 1 < MAX_DOMAIN_TEXT && spec[i]; ++i)
		out.text[i] = spec[i];

	Genode::size_t pos { 0 };
	Genode::size_t const len = strlen(spec);

	while (pos < len) {
		/* skip separators */
		while (pos < len) {
			char const c = spec[pos];
			if (c == ',' || c == ' ' || c == '\t' ||
			    c == '\n' || c == '\r')
				++pos;
			else
				break;
		}
		if (pos == len)
			break;

		Genode::size_t const first = pos;
		while (pos < len) {
			char const c = spec[pos];
			if (c == ',' || c == ' ' || c == '\t' ||
			    c == '\n' || c == '\r')
				break;
			++pos;
		}
		Genode::size_t const entry_len = pos - first;

		if (entry_len == 0 || entry_len >= MAX_DOMAIN_ENTRY)
			return false;

		if (entry_len == 1 && spec[first] == '*') {
			out.all = true;
			continue;
		}

		if (out.num_entries >= MAX_DOMAIN_ENTRIES)
			return false;

		char *entry = out.entries[out.num_entries];
		for (Genode::size_t i = 0; i < entry_len; ++i)
			entry[i] = spec[first + i];
		entry[entry_len] = 0;
		++out.num_entries;
	}
	return true;
}


Set_outcome gate_set(bool admin, bool registry_known,
                     Key_domain const &domain, char const *key)
{
	/* The closed typed registry gate runs first: an unknown key is
	 * rejected on every label, admin included. */
	if (!registry_known)
		return Set_outcome::Unknown_key;

	/* The label's key-domain gate runs second. */
	if (admin)
		return Set_outcome::Apply;

	return domain.contains(key)
	     ? Set_outcome::Apply
	     : Set_outcome::Key_outside_domain;
}

}  /* namespace Sponge::Configd::Acl */
