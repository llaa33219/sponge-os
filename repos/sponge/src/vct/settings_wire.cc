/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_wire.cc — vct's Settings wire adapter. See settings_wire.h
 * for the contract (docs/12-package-format.md §14) and the purity
 * rules. Host-tested by tests/self_test.cc.
 */

#include "settings_wire.h"

namespace Sponge::Vct::Settings_wire {

using Genode::strcmp;
using Genode::String;


/* ===================== category enum ===================== */

char const *category_label(Category category)
{
	switch (category) {
	case Category::DESKTOP:      return "desktop";
	case Category::HARDWARE:     return "hardware";
	case Category::SYSTEM:       return "system";
	case Category::APPLICATIONS: return "applications";
	case Category::OTHER:        return "other";
	}
	return "other";
}


Category category_from_attr(char const *value, bool *recognized)
{
	Category result = Category::OTHER;
	bool     ok     = true;

	     if (strcmp(value, "desktop") == 0)      result = Category::DESKTOP;
	else if (strcmp(value, "hardware") == 0)     result = Category::HARDWARE;
	else if (strcmp(value, "system") == 0)       result = Category::SYSTEM;
	else if (strcmp(value, "applications") == 0) result = Category::APPLICATIONS;
	else if (strcmp(value, "other") == 0)        result = Category::OTHER;
	else ok = false;

	if (recognized) *recognized = ok;
	return result;
}


/* ===================== discovery (installed metadata) ===================== */

unsigned collect_entries(Genode::Xml_node const &root,
                         Entry *out, unsigned max)
{
	unsigned n = 0;

	root.with_optional_sub_node("packages",
		[&](Genode::Xml_node const &pkgs) {
			pkgs.for_each_sub_node("package",
				[&](Genode::Xml_node const &p) {
					String<64> const pkg =
						p.attribute_value("name", String<64>());
					if (strcmp(pkg.string(), "") == 0)
						return;

					p.for_each_sub_node("settings",
						[&](Genode::Xml_node const &s) {
							if (n >= max)
								return;

							String<33> const id =
								s.attribute_value("id", String<33>());
							String<64> const title =
								s.attribute_value("title", String<64>());

							/* Display-side defense in depth: an entry
							 * without identity or name cannot be
							 * addressed by `settings open` or shown
							 * to the user. */
							if (strcmp(id.string(), "") == 0 ||
							    strcmp(title.string(), "") == 0)
								return;

							Entry &e = out[n];
							e.pkg   = pkg;
							e.id    = id;
							e.title = title;

							/* Absent or unknown category falls back
							 * to 'other' (§14.2, the §4.5 typo rule). */
							e.category = category_from_attr(
								s.attribute_value("category", String<16>())
									.string());

							++n;
						});
				});
		});

	return n;
}


unsigned group_order(Entry const *entries, unsigned n,
                     unsigned *order, unsigned max)
{
	unsigned m = 0;

	for (unsigned c = 0; c < CATEGORY_COUNT; ++c)
		for (unsigned i = 0; i < n && m < max; ++i)
			if ((unsigned)entries[i].category == c)
				order[m++] = i;

	return m;
}


/* ===================== settings open channel ===================== */

void gen_settings_request(Genode::Xml_generator &g,
                          char const *pkg, char const *settings_id)
{
	g.attribute("op",     "launch");
	g.attribute("target", "settings");
	g.attribute("pkg",    pkg);
	g.attribute("id",     settings_id);
}


bool parse_settings_result(Genode::Xml_node const &root,
                           char const *pkg, char const *settings_id,
                           Launch_result &out)
{
	if (!root.has_type("result"))
		return false;

	String<32> const op = root.attribute_value("op", String<32>());
	if (strcmp(op.string(), "launch") != 0)
		return false;

	/*
	 * Shared "result" label: the discriminator must be present and
	 * exact — a plain launch result never answers a settings open.
	 */
	if (!root.has_attribute("target"))
		return false;
	String<16> const target = root.attribute_value("target", String<16>());
	if (strcmp(target.string(), "settings") != 0)
		return false;

	String<64> const res_pkg = root.attribute_value("pkg", String<64>());
	if (strcmp(res_pkg.string(), pkg) != 0)
		return false;

	/*
	 * Identity attribute: 'id' is primary (the metadata vocabulary
	 * and the pkgd result shape), 'settings-id' the docs-prose alias.
	 * On the shared channel the identity must be present and match.
	 */
	String<33> res_id = root.attribute_value("id", String<33>());
	if (strcmp(res_id.string(), "") == 0)
		res_id = root.attribute_value("settings-id", String<33>());

	if (strcmp(res_id.string(), "") == 0 ||
	    strcmp(res_id.string(), settings_id) != 0)
		return false;

	out.raw_status = root.attribute_value("status", String<32>());
	out.raw_action = root.attribute_value("action", String<16>());
	out.error      = root.attribute_value("error", String<256>());

	char const *const status = out.raw_status.string();

	if (strcmp(status, "ok") == 0) {
		/* 'action="focused"' is the singleton raise (§14.7); every
		 * other 'ok' answer counts as a fresh start. */
		out.status = (strcmp(out.raw_action.string(), "focused") == 0)
		           ? Launch_status::OK_FOCUSED
		           : Launch_status::OK_STARTED;
	}
	else if (strcmp(status, "not-installed") == 0) {
		out.status = Launch_status::NOT_INSTALLED;
	}
	else if (strcmp(status, "binary-missing") == 0) {
		out.status = Launch_status::BINARY_MISSING;
	}
	else if (strcmp(status, "insufficient-resources") == 0) {
		out.status   = Launch_status::INSUFFICIENT_RESOURCES;
		out.need_ram  = root.attribute_value("need_ram",  String<32>("?"));
		out.avail_ram = root.attribute_value("avail_ram", String<32>("?"));
		out.need_caps = root.attribute_value("need_caps", String<32>("?"));
		out.avail_caps = root.attribute_value("avail_caps", String<32>("?"));
	}
	else {
		/* status="error" (structured pkgd error; read 'error') and
		 * any unforeseen lexeme. */
		out.status = Launch_status::UNKNOWN;
	}

	return true;
}

}  /* namespace Sponge::Vct::Settings_wire */
