/* SPDX-License-Identifier: Apache-2.0
 *
 * acl_tests — the Criterion 18 per-label key-domain ACL case list.
 * See acl_tests.h for the contract.
 */

#include "acl_tests.h"

#include "key_domain_acl.h"

#include <stdio.h>
#include <string.h>

namespace Sponge::Configd::Acl_tests {

using Sponge::Configd::Acl::gate_set;
using Sponge::Configd::Acl::Key_domain;
using Sponge::Configd::Acl::MAX_WRITERS;
using Sponge::Configd::Acl::parse_domain;
using Sponge::Configd::Acl::Set_outcome;
using Sponge::Configd::Acl::Writer;
using Sponge::Configd::Acl::find_writer;
using Sponge::Configd::Acl::writers;

namespace {

Check_fn _inner   { nullptr };
unsigned _failures { 0 };

void count(bool ok, char const *what)
{
	if (!ok)
		++_failures;
	_inner(ok, what);
}

/*
 * Minimal store proving the gate runs BEFORE any mutation. It
 * mirrors main.cc `_do_set`: a write lands only when gate_set
 * returns Apply, so a rejected request must leave `value` and
 * `mutations` byte-identical.
 */
struct Store
{
	unsigned mutations { 0 };
	char     value[64]  { };
};

Set_outcome try_set(Store &store, bool admin, bool registry_known,
                    Key_domain const &domain,
                    char const *key, char const *value)
{
	Set_outcome const outcome = gate_set(admin, registry_known, domain, key);
	if (outcome == Set_outcome::Apply) {
		++store.mutations;
		snprintf(store.value, sizeof(store.value), "%s", value);
	}
	return outcome;
}

Key_domain domain_of(char const *text)
{
	Key_domain d { };
	parse_domain(text, d);
	return d;
}

char const *outcome_name(Set_outcome o)
{
	switch (o) {
	case Set_outcome::Apply:               return "Apply";
	case Set_outcome::Unknown_key:         return "Unknown_key";
	case Set_outcome::Key_outside_domain:  return "Key_outside_domain";
	}
	return "?";
}

void check_outcome(Check_fn report, Set_outcome got, Set_outcome want,
                   char const *what)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s (expected %s, got %s)",
	         what, outcome_name(want), outcome_name(got));
	report(got == want, buf);
}

void check_str(Check_fn report, char const *got, char const *want,
               char const *what)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s (expected '%s', got '%s')",
	         what, want, got);
	report(strcmp(got, want) == 0, buf);
}

}  /* anonymous namespace */


unsigned run_acl_tests(Check_fn report)
{
	_inner = report;
	report = count;
	_failures = 0;

	/*
	 * 1. Writer table: one config_request ROM session per writer
	 * label, vct + eight providers, and no shared label for
	 * third-party writers.
	 */
	{
		unsigned count { 0 };
		bool labels_unique { true };
		bool roms_unique   { true };
		for (unsigned i = 0; i < MAX_WRITERS; ++i) {
			++count;
			for (unsigned j = 0; j < i; ++j) {
				if (strcmp(writers()[i].label, writers()[j].label) == 0)
					labels_unique = false;
				if (strcmp(writers()[i].request_rom,
				           writers()[j].request_rom) == 0)
					roms_unique = false;
			}
		}
		report(count == MAX_WRITERS, "writer table has MAX_WRITERS entries");
		report(labels_unique, "writer labels are unique");
		report(roms_unique, "one distinct config_request ROM session per writer label");
		report(find_writer("vct") != nullptr, "vct label is registered");
		report(find_writer("panel_settings") != nullptr, "panel_settings label is registered");
		report(find_writer("background_settings") != nullptr, "background_settings label is registered");
		report(find_writer("theme_settings") != nullptr, "theme_settings label is registered");
		report(find_writer("shortcuts_settings") != nullptr, "shortcuts_settings label is registered");
		report(find_writer("defaults_settings") != nullptr, "defaults_settings label is registered");
		report(find_writer("display_settings") != nullptr, "display_settings label is registered");
		report(find_writer("wm_settings") != nullptr, "wm_settings label is registered");
		report(find_writer("terminal_settings") != nullptr, "terminal_settings label is registered");
		report(find_writer("some_third_party") == nullptr, "third-party labels have no shared label");
		report(find_writer("de_config_request") == nullptr, "no writer label for the removed universal de channel");

		report(find_writer("vct")->admin, "vct is the every-domain admin");
		report(!find_writer("panel_settings")->admin, "panel_settings is not admin");
		report(!find_writer("wm_settings")->admin, "wm_settings is not admin");
		check_str(report, find_writer("vct")->request_rom, "config_request",
		          "vct keeps the historical config_request ROM label");
		check_str(report, find_writer("vct")->result_label, "config_result",
		          "vct keeps the historical config_result label");
	}

	/*
	 * 2. Built-in label→domain mapping (Criterion 18):
	 * wm.* reserved for wm_settings, display.* / terminal.* mapped
	 * to their labels, defaults_settings owns exactly bake.applied.
	 */
	{
		struct { char const *label; char const *key; bool expect; char const *what; } const map[] = {
			{ "panel_settings",      "panel.height",        true,  "panel domain owns panel.height" },
			{ "panel_settings",      "panel.ids",           true,  "panel domain owns panel.ids" },
			{ "panel_settings",      "panel.alpha.height",  true,  "panel domain owns pattern keys" },
			{ "panel_settings",      "theme.active",        false, "panel domain excludes theme.active" },
			{ "panel_settings",      "wm.layout.mode",      false, "panel domain excludes wm.*" },
			{ "background_settings", "background.color",    true,  "background domain owns background.color" },
			{ "background_settings", "background.image",    true,  "background domain owns background.image" },
			{ "background_settings", "panel.height",        false, "background domain excludes panel.*" },
			{ "theme_settings",      "theme.active",        true,  "theme domain owns theme.active" },
			{ "theme_settings",      "panel.height",        false, "theme domain excludes panel.*" },
			{ "shortcuts_settings",  "shortcuts.bindings",  true,  "shortcuts domain owns shortcuts.bindings" },
			{ "shortcuts_settings",  "clock.format",        false, "shortcuts domain excludes clock.format" },
			{ "defaults_settings",   "bake.applied",        true,  "defaults domain owns bake.applied" },
			{ "defaults_settings",   "bake.profile",        false, "defaults domain excludes bake.profile" },
			{ "defaults_settings",   "theme.active",        false, "defaults domain excludes theme.active" },
			{ "display_settings",    "display.future.key",  true,  "display.* reserved for display_settings" },
			{ "display_settings",    "panel.height",        false, "display domain excludes panel.*" },
			{ "wm_settings",         "wm.layout.mode",      true,  "wm.* reserved for wm_settings" },
			{ "wm_settings",         "panel.alpha.height",  false, "wm domain excludes panel pattern keys" },
			{ "wm_settings",         "theme.active",        false, "wm domain excludes theme.active" },
			{ "terminal_settings",   "terminal.future.key", true,  "terminal.* reserved for terminal_settings" },
			{ "terminal_settings",   "display.future.key",  false, "terminal domain excludes display.*" },
		};
		for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
			Writer const *w = find_writer(map[i].label);
			report(w != nullptr, map[i].what);
			if (!w) continue;
			Key_domain const d = domain_of(w->default_domain);
			report(d.contains(map[i].key) == map[i].expect, map[i].what);
		}
	}

	/*
	 * 3. Allowlist text syntax: "*", "prefix.*", exact keys,
	 * separators, empty spec = deny-all, overflow = rejected.
	 */
	{
		report(domain_of("*").contains("anything.at.all"), "'*' matches every key");

		Key_domain const panel = domain_of("panel.*");
		report(panel.contains("panel.height"), "'panel.*' matches panel.height");
		report(panel.contains("panel.alpha.height"), "'panel.*' matches pattern key panel.alpha.height");
		report(panel.contains("panel.ids"), "'panel.*' matches panel.ids");
		report(!panel.contains("panel"), "'panel.*' does not match the bare prefix");
		report(!panel.contains("panelx.height"), "'panel.*' does not match panelx.height");
		report(!panel.contains("theme.active"), "'panel.*' does not match theme.active");

		Key_domain const exact = domain_of("bake.applied");
		report(exact.contains("bake.applied"), "exact entry matches its key");
		report(!exact.contains("bake.appliedx"), "exact entry rejects a longer key");
		report(!exact.contains("bake."), "exact entry rejects a shorter key");

		Key_domain const multi = domain_of("a.*, b.c,d.*");
		report(multi.contains("a.x"), "multi-entry list keeps prefix entry a.*");
		report(multi.contains("b.c"), "multi-entry list keeps exact entry b.c");
		report(multi.contains("d.z"), "multi-entry list keeps prefix entry d.*");
		report(!multi.contains("b.d"), "multi-entry list rejects non-listed keys");

		Key_domain const empty = domain_of("");
		report(!empty.contains("theme.active"), "empty spec is a valid deny-all domain");
		report(!empty.contains(""), "empty spec rejects even the empty key");

		Key_domain bad { };
		char huge[512];
		memset(huge, 'x', sizeof(huge));
		huge[sizeof(huge) - 1] = 0;
		report(!parse_domain(huge, bad), "oversized entry is rejected");

		char many[256] { };
		snprintf(many, sizeof(many),
		         "k0,k1,k2,k3,k4,k5,k6,k7,k8");
		report(!parse_domain(many, bad), "too many entries are rejected");
	}

	/*
	 * 4. Required behavior: in-domain success mutates the store.
	 */
	{
		Key_domain const panel = domain_of("panel.*");
		Store store { };
		check_outcome(report,
			try_set(store, false, true, panel, "panel.height", "40"),
			Set_outcome::Apply,
			"in-domain set from panel_settings is applied");
		check_str(report, store.value, "40", "in-domain set stores the value");
		report(store.mutations == 1, "in-domain set mutates exactly once");

		Store wm_store { };
		check_outcome(report,
			try_set(wm_store, false, true, domain_of("wm.*"),
			        "wm.layout.mode", "tiling"),
			Set_outcome::Apply,
			"in-domain set from wm_settings is applied");
	}

	/*
	 * 5. Required behavior: out-of-domain rejection returns
	 * `key-outside-domain` and leaves the store unchanged.
	 */
	{
		Key_domain const theme = domain_of("theme.*");
		Store store { };
		snprintf(store.value, sizeof(store.value), "%s", "28");
		store.mutations = 1;

		Set_outcome const o =
			try_set(store, false, true, theme, "panel.height", "40");
		check_outcome(report, o, Set_outcome::Key_outside_domain,
		              "out-of-domain set from theme_settings is rejected");
		check_str(report, Acl::KEY_OUTSIDE_DOMAIN, "key-outside-domain",
		          "the structured code is key-outside-domain");
		check_str(report, store.value, "28",
		          "rejected out-of-domain set leaves the store unchanged");
		report(store.mutations == 1,
		       "rejected out-of-domain set performs no mutation");

		Store pattern_store { };
		check_outcome(report,
			try_set(pattern_store, false, true, domain_of("wm.*"),
			        "panel.alpha.height", "40"),
			Set_outcome::Key_outside_domain,
			"out-of-domain pattern key is rejected");
		report(pattern_store.mutations == 0,
		       "rejected out-of-domain pattern key never instantiates (no mutation)");
	}

	/*
	 * 6. Required behavior: vct keeps every-domain admin access.
	 */
	{
		Key_domain const unused { };
		char const *const keys[] = {
			"panel.height", "panel.alpha.height", "panel.ids",
			"background.color", "theme.active", "shortcuts.bindings",
			"bake.applied", "wm.layout.mode", "clock.format",
		};
		for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
			Store store { };
			check_outcome(report,
				try_set(store, true, true, unused, keys[i], "x"),
				Set_outcome::Apply,
				"vct admin set succeeds on every domain");
		}
	}

	/*
	 * 7. Required behavior: unknown keys are rejected by the
	 * registry-first path — on every label, admin included, and
	 * even when the key is also outside the label's domain.
	 */
	{
		Store store { };
		check_outcome(report,
			try_set(store, false, false, domain_of("panel.*"),
			        "nonexistent.key", "x"),
			Set_outcome::Unknown_key,
			"unknown key is rejected on a provider label");
		check_outcome(report,
			try_set(store, false, false, domain_of("theme.*"),
			        "nonexistent.key", "x"),
			Set_outcome::Unknown_key,
			"unknown key is rejected registry-first even out of domain");
		check_outcome(report,
			try_set(store, true, false, Key_domain { }, "nonexistent.key", "x"),
			Set_outcome::Unknown_key,
			"unknown key is rejected even for the vct admin");
		report(store.mutations == 0, "unknown-key rejections never mutate");
	}

	/*
	 * 8. Required behavior: pattern-key panel domain behavior.
	 * panel.<id>.* and panel.ids belong to the panel_settings
	 * prefix domain and to no other label.
	 */
	{
		Key_domain const panel = domain_of("panel.*");
		Store store { };
		check_outcome(report,
			try_set(store, false, true, panel, "panel.alpha.height", "40"),
			Set_outcome::Apply,
			"panel_settings may write panel.<id>.height");
		check_outcome(report,
			try_set(store, false, true, panel, "panel.a1_b-2.visible_widgets",
			        "clock,tasklist"),
			Set_outcome::Apply,
			"panel_settings may write panel.<id>.visible_widgets");
		check_outcome(report,
			try_set(store, false, true, panel, "panel.ids", "alpha,beta"),
			Set_outcome::Apply,
			"panel_settings may write panel.ids");
		report(store.mutations == 3, "three in-domain pattern writes mutate");

		Store other { };
		check_outcome(report,
			try_set(other, false, true, domain_of("theme.*"),
			        "panel.alpha.height", "40"),
			Set_outcome::Key_outside_domain,
			"theme_settings may not write panel.<id> keys");
		check_outcome(report,
			try_set(other, false, true, domain_of("shortcuts.*"),
			        "panel.ids", "alpha"),
			Set_outcome::Key_outside_domain,
			"shortcuts_settings may not write panel.ids");
		report(other.mutations == 0, "out-of-domain pattern writes never mutate");
	}

	/*
	 * 9. Per-label decisions are independent: the same key on two
	 * labels is allowed for one and rejected for the other.
	 */
	{
		Key_domain const defaults_d = domain_of("bake.applied");
		Store store { };
		check_outcome(report,
			try_set(store, false, true, defaults_d, "bake.applied", "no"),
			Set_outcome::Apply,
			"defaults_settings may trigger the bake reset");
		check_outcome(report,
			try_set(store, false, true, domain_of("background.*"),
			        "bake.applied", "no"),
			Set_outcome::Key_outside_domain,
			"background_settings may not trigger the bake reset");
	}

	return _failures;
}

}  /* namespace Sponge::Configd::Acl_tests */
