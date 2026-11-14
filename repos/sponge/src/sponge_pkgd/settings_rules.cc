/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd settings rules — implementation. See settings_rules.h
 * for the contract. Pure decision/parse code only: no Env, no
 * sessions, no I/O, no logging (the bounded warnings and the English
 * refusal text are emitted by the caller, metadata.cc / ops.cc).
 *
 * Attribute values are validated over their RAW ranges
 * (Attribute::with_raw_value), never through a bounded String copy,
 * so an overlong value can never truncate into a valid-looking one.
 */

#include "settings_rules.h"

#include <util/hid.h>
#include <util/xml_node.h>

namespace {

bool lower_id_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
	    || c == '_' || c == '-';
}


bool bare_target_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
	    || (c >= '0' && c <= '9')
	    || c == '_' || c == '-' || c == '.' || c == '+';
}


bool ascii_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}


/*
 * §14.2: the closed category enum. Returns "" for an unknown token.
 */
char const *known_category(char const *s)
{
	static char const *const table[] = {
		"desktop", "hardware", "system", "applications", "other" };
	for (unsigned i = 0; i < sizeof(table)/sizeof(table[0]); ++i)
		if (Genode::strcmp(table[i], s) == 0)
			return table[i];
	return "";
}

}  /* anonymous namespace */


char const *Sponge::Pkgd::settings_reject_reason(Settings_reject reject)
{
	switch (reject) {
	case Settings_reject::none:            return "";
	case Settings_reject::missing_id:      return "missing required attribute id";
	case Settings_reject::malformed_id:    return "id must match [a-z0-9_-]{1,32}";
	case Settings_reject::duplicate_id:    return "duplicate settings id in one package";
	case Settings_reject::missing_title:   return "missing required attribute title";
	case Settings_reject::missing_target:  return "missing required attribute target";
	case Settings_reject::malformed_target:
		return "target is not one bare payload binary name";
	case Settings_reject::absolute_target:
		return "target must not be an absolute path";
	case Settings_reject::payload_escape:
		return "target must stay inside the package payload (no '..' or path separators)";
	case Settings_reject::command_string:
		return "target must be one binary name, not a command string";
	case Settings_reject::exclusive_claim:
		return "a settings entry must not carry an exclusive claim";
	}
	return "invalid settings entry";
}


Sponge::Pkgd::Settings_parse
Sponge::Pkgd::parse_settings_entry(Genode::Xml_node const &node,
                                   Settings_entry &out,
                                   Settings_entry const *prior,
                                   unsigned num_prior)
{
	Settings_parse res { };
	out = Settings_entry { };

	/*
	 * Raw-attribute access: validation runs over the full value range
	 * so a value longer than the storage width cannot truncate into a
	 * valid-looking one (see the file header). Xml_attribute values
	 * are not NUL-terminated windows into the XML buffer, hence the
	 * Cstring(start, length) bounded copies below.
	 */
	struct Raw { char const *s { "" }; Genode::size_t n { 0 }; };

	auto raw = [&] (char const *name) {
		Raw r { };
		node.for_each_attribute([&] (Genode::Xml_attribute const &a) {
			if (r.n == 0 && a.has_type(name))
				a.with_raw_value(
					[&] (char const *start, Genode::size_t length) {
						r.s = start;
						r.n = length;
					});
		});
		return r;
	};

	/* ---- id: identity first (§14.1/§14.2) ---- */
	Raw const id_raw = raw("id");
	if (id_raw.n == 0) {
		res.reject = Settings_reject::missing_id;
		return res;
	}
	out.id = Genode::String<33>(Genode::Cstring(id_raw.s, id_raw.n));
	if (id_raw.n > 32) {
		res.reject = Settings_reject::malformed_id;
		return res;
	}
	for (Genode::size_t i = 0; i < id_raw.n; ++i)
		if (!lower_id_char(id_raw.s[i])) {
			res.reject = Settings_reject::malformed_id;
			return res;
		}

	/* Duplicate id in one package (§14.3): the (package, settings-id)
	 * identity must be unique per package. */
	for (unsigned i = 0; i < num_prior; ++i)
		if (Genode::strcmp(prior[i].id.string(), out.id.string()) == 0) {
			res.reject = Settings_reject::duplicate_id;
			return res;
		}

	/* ---- title: required ---- */
	Raw const title_raw = raw("title");
	if (title_raw.n == 0) {
		res.reject = Settings_reject::missing_title;
		return res;
	}
	out.title = node.attribute_value("title", Genode::String<64>());

	/*
	 * ---- exclusive claim (§14.3): "an exclusive attribute or claim
	 * carried by the <settings> entry itself" is a hard rejection —
	 * a settings entry point must always be openable (§14.7). The
	 * `exclusive` attribute is KNOWN vocabulary (§4.6) and is refused
	 * here rather than warn-and-ignored; an <exclusive> child is the
	 * claim form and is refused with the same reason (checked in the
	 * child walk below).
	 */
	if (node.has_attribute("exclusive")) {
		res.reject = Settings_reject::exclusive_claim;
		return res;
	}

	/* ---- target: one bare payload binary name (§14.3) ---- */
	Raw const target_raw = raw("target");
	if (target_raw.n == 0) {
		res.reject = Settings_reject::missing_target;
		return res;
	}
	{
		char const *s = target_raw.s;
		Genode::size_t const n = target_raw.n;

		/* Absolute paths: leading '/' or '\', drive forms ("C:..."). */
		if (s[0] == '/' || s[0] == '\\') {
			res.reject = Settings_reject::absolute_target;
			return res;
		}
		if (n >= 2 && ascii_alpha(s[0]) && s[1] == ':') {
			res.reject = Settings_reject::absolute_target;
			return res;
		}

		/* Payload escapes: ".." or any path separator. */
		bool escape { false };
		for (Genode::size_t i = 0; i < n; ++i) {
			if (s[i] == '/' || s[i] == '\\')
				escape = true;
			if (s[i] == '.' && i + 1 < n && s[i + 1] == '.')
				escape = true;
		}
		if (escape) {
			res.reject = Settings_reject::payload_escape;
			return res;
		}

		/* Shell/interpreter/command strings: anything outside the
		 * bare-name set covers whitespace, quotes, ; | & $ ` and any
		 * other command-line shape. */
		for (Genode::size_t i = 0; i < n; ++i)
			if (!bare_target_char(s[i])) {
				res.reject = Settings_reject::command_string;
				return res;
			}

		/* The umbrella of §14.3 row 2: still not representable as a
		 * bare payload binary name (overlong). */
		if (n > 63) {
			res.reject = Settings_reject::malformed_target;
			return res;
		}

		out.target = Genode::String<64>(Genode::Cstring(s, n));
	}

	/* ---- category: closed enum, absent/unknown -> "other" ---- */
	Raw const cat_raw = raw("category");
	if (cat_raw.n == 0) {
		out.category = Genode::String<16>("other");
		out.category_defaulted = true;
	} else {
		Genode::String<16> const cat(
			Genode::Cstring(cat_raw.s, cat_raw.n));
		char const *const known = known_category(cat.string());
		if (known[0] != 0)
			out.category = Genode::String<16>(known);
		else {
			out.category = Genode::String<16>("other");
			out.category_defaulted = true;
		}
	}

	/* ---- icon: optional display metadata ---- */
	if (node.has_attribute("icon")) {
		out.icon     = node.attribute_value("icon", Genode::String<64>());
		out.has_icon = true;
	}

	/* ---- children: <keyword> terms, <exclusive> claim, unknown
	 *      elements warn-and-ignored (the caller warns) ---- */
	node.for_each_sub_node([&] (Genode::Xml_node const &child) {
		if (child.has_type("keyword")) {
			if (out.num_keywords < SETTINGS_MAX_KEYWORDS)
				out.keywords[out.num_keywords++] =
					child.decoded_content<Genode::String<32>>();
		} else if (child.has_type("exclusive")) {
			res.reject = Settings_reject::exclusive_claim;
		} else {
			res.unknown_child = true;
		}
	});
	if (res.reject != Settings_reject::none)
		return res;

	/* Unknown attributes warn-and-ignored (additive evolution);
	 * everything named above is known, `exclusive` is known-and-
	 * rejected. */
	node.for_each_attribute([&] (Genode::Xml_attribute const &a) {
		if (!a.has_type("id") && !a.has_type("title") &&
		    !a.has_type("category") && !a.has_type("icon") &&
		    !a.has_type("target") && !a.has_type("exclusive"))
			res.unknown_attribute = true;
	});

	return res;
}


Genode::String<64> Sponge::Pkgd::settings_start_name(char const *pkg,
                                                     char const *id)
{
	/* "<pkg>.<id>", truncated at the sandbox's Child_policy::Name
	 * width by String<64> (see the header's START-NAME IDENTITY). */
	return Genode::String<64>(pkg, ".", id);
}


bool Sponge::Pkgd::settings_start_name_valid(char const *pkg, char const *id)
{
	Genode::size_t const pkg_len = Genode::strlen(pkg);
	Genode::size_t const id_len  = Genode::strlen(id);
	return pkg_len + 1 + id_len <= 63;
}


Sponge::Pkgd::Settings_outcome
Sponge::Pkgd::settings_decide_pre_resource(bool entry_found, bool binary_ok,
                                           bool claimed)
{
	/* Identity -> singleton -> binary (§14.7). The singleton gate
	 * precedes the binary gate exactly like the app model's
	 * already-running gate (pkg_launch.h DECISION ORDER): a live
	 * instance is reachable and the open is a focus, not a spawn. */
	if (!entry_found)
		return Settings_outcome::not_installed;
	if (claimed)
		return Settings_outcome::focused;
	if (!binary_ok)
		return Settings_outcome::binary_missing;
	return Settings_outcome::proceed;
}


Sponge::Pkgd::Settings_outcome
Sponge::Pkgd::settings_decide(bool entry_found, bool binary_ok,
                              Pkg_launch::Resource_verdict const &resource,
                              bool claimed)
{
	Settings_outcome const pre =
		settings_decide_pre_resource(entry_found, binary_ok, claimed);
	if (pre != Settings_outcome::proceed)
		return pre;

	if (resource.kind == Pkg_launch::Resource_verdict::Kind::admitted)
		return Settings_outcome::started;
	return Settings_outcome::insufficient_resources;
}


bool Sponge::Pkgd::settings_claim_in_set(char const *const *pkgs,
                                         char const *const *ids,
                                         unsigned count,
                                         char const *pkg, char const *id)
{
	for (unsigned i = 0; i < count; ++i)
		if (Genode::strcmp(pkgs[i], pkg) == 0 &&
		    Genode::strcmp(ids[i], id) == 0)
			return true;
	return false;
}


Sponge::Pkgd::Settings_provider_state
Sponge::Pkgd::settings_provider_state(char const *buf, Genode::size_t len,
                                      char const *start_name)
{
	Settings_provider_state result { };

	/* Hid_node marks malformed input invalid instead of throwing. */
	Genode::Hid_node const root(Genode::Const_byte_range_ptr(buf, len));

	root.for_each_sub_node([&] (Genode::Hid_node const &n) {
		if (result.exited || !n.has_type("child"))
			return;

		Genode::String<64> const name =
			n.attribute_value("name", Genode::String<64>());
		if (Genode::strcmp(name.string(), start_name) != 0)
			return;

		/*
		 * Absence of `exited` is NOT an exit (the report can
		 * predate the child's first paint); an explicit
		 * `exited="0"` still is one.
		 */
		if (!n.has_attribute("exited"))
			return;

		result.exited = true;
		result.code   = n.attribute_value("exited", 0L);
	});

	return result;
}
