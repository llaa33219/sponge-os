/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Implementation of vct's config-ROM argument parser.
 *
 * Genode supplies the config ROM in HID (human-intelligible data) format at
 * runtime, but XML is also accepted for test fixtures and tooling.
 */

#include "args.h"

#include <base/log.h>
#include <util/hid.h>
#include <util/xml_node.h>

using namespace Sponge;
using namespace Sponge::Vct;


namespace {

Genode::String<8> language_from_equals_form(Genode::String<128> const &token)
{
	char const *const t = token.string();
	char const *eq = t;
	while (*eq != '\0' && *eq != '=')
		eq++;
	if (*eq == '=')
		return Genode::String<8>(eq + 1);
	return Genode::String<8>();
}


bool starts_with(char const *str, char const *prefix)
{
	while (*prefix != '\0') {
		if (*str != *prefix)
			return false;
		str++;
		prefix++;
	}
	return true;
}


/* Apply a single parsed token to the Args struct. */
void apply_token(Genode::String<128> const &token, Args &out)
{
	char const *const t = token.string();

	if (Genode::strcmp(t, "--explain") == 0 ||
	    Genode::strcmp(t, "-x") == 0) {
		out.explain = true;
		return;
	}
	if (Genode::strcmp(t, "--manual") == 0 ||
	    Genode::strcmp(t, "-m") == 0) {
		out.manual = true;
		return;
	}
	if (Genode::strcmp(t, "--json") == 0) {
		out.json = true;
		return;
	}
	if (Genode::strcmp(t, "--verbose") == 0 ||
	    Genode::strcmp(t, "-v") == 0) {
		out.verbose = true;
		return;
	}
	if (Genode::strcmp(t, "--help") == 0 ||
	    Genode::strcmp(t, "-h") == 0) {
		out.subcommand = Genode::String<32>("help");
		return;
	}
	if (Genode::strcmp(t, "--version") == 0 ||
	    Genode::strcmp(t, "-V") == 0) {
		out.subcommand = Genode::String<32>("version");
		return;
	}

	if (Genode::strcmp(t, "--lang") == 0) {
		Genode::warning("vct: --lang requires a language code");
		return;
	}

	if (starts_with(t, "--lang=" )) {
		out.lang = language_from_equals_form(token);
		return;
	}

	/* Token did not match a flag: treat as positional. */
	if (Genode::strcmp(out.subcommand.string(), "status") == 0) {
		out.subcommand = Genode::String<32>(token);
		return;
	}
	if (Genode::strcmp(out.positional.string(), "") == 0) {
		out.positional = Genode::String<128>(token);
		return;
	}
	if (Genode::strcmp(out.positional2.string(), "") == 0) {
		out.positional2 = Genode::String<128>(token);
		return;
	}

	Genode::warning("vct: ignoring extra positional token: ", token);
}


bool looks_like_xml(char const *data, Genode::size_t size)
{
	for (Genode::size_t i = 0; i < size; i++) {
		char const c = data[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;
		return c == '<';
	}
	return false;
}


void collect_arg_token(Genode::String<128> const &token,
                       Genode::String<128> *tokens,
                       unsigned &token_count,
                       unsigned &dropped)
{
	if (token_count >= MAX_ARGS) {
		dropped++;
		return;
	}
	tokens[token_count] = token;
	token_count++;
}

}  /* namespace */


Args Sponge::Vct::parse_args(char const *data, Genode::size_t size)
{
	Args out;

	Genode::String<128> tokens[MAX_ARGS];
	unsigned token_count = 0;

	if (looks_like_xml(data, size)) {
		try {
			Genode::Xml_node const config(data, size);
			if (config.has_attribute("enable_notifications")) {
				Genode::String<8> const v = config.attribute_value("enable_notifications", Genode::String<8>("yes"));
				out.enable_notifications = (v == Genode::String<8>("yes"))
				                        || (v == Genode::String<8>("true"))
				                        || (v == Genode::String<8>("on"))
				                        || (v == Genode::String<8>("1"));
			}
			/* The standard config ROM form is <config><args><arg>...</arg></args></config>.
			 * Descend into <args> before iterating <arg> children. A previous
			 * flat iteration found nothing and silently fell back to the
			 * default "status" subcommand for every invocation. */
			config.with_optional_sub_node("args",
				[&](Genode::Xml_node const &args) {
					args.for_each_sub_node("arg",
						[&](Genode::Xml_node const &arg) {
							if (arg.has_attribute("name"))
								collect_arg_token(arg.attribute_value("name", Genode::String<128>()),
								                  tokens, token_count, out.dropped);
							else
								collect_arg_token(arg.decoded_content<Genode::String<128>>(),
								                  tokens, token_count, out.dropped);
						});
				});
		} catch (Genode::Xml_node::Invalid_syntax) {
			Genode::warning("vct: config ROM looks like XML but is invalid");
		}
	} else {
		Genode::Hid_node const config(Genode::Const_byte_range_ptr(data, size));
		if (config.has_type("config")) {
			Genode::String<8> const v = config.attribute_value("enable_notifications", Genode::String<8>("yes"));
			out.enable_notifications = (v == Genode::String<8>("yes"))
			                        || (v == Genode::String<8>("true"))
			                        || (v == Genode::String<8>("on"))
			                        || (v == Genode::String<8>("1"));
			config.for_each_sub_node([&](Genode::Hid_node const &node) {
				if (!node.has_type("args"))
					return;
				node.for_each_sub_node([&](Genode::Hid_node const &arg) {
					if (!arg.has_type("arg"))
						return;
					collect_arg_token(arg.attribute_value("name", Genode::String<128>()),
					                  tokens, token_count, out.dropped);
				});
			});
		}
	}

	for (unsigned i = 0; i < token_count; i++) {
		Genode::String<128> const &token = tokens[i];
		char const *const t = token.string();

		if (Genode::strcmp(t, "--lang") == 0) {
			if (i + 1 < token_count) {
				out.lang = Genode::String<8>(tokens[i + 1]);
				i++;
			} else {
				Genode::warning("vct: --lang requires a language code");
			}
			continue;
		}

		apply_token(token, out);
	}

	if (out.dropped > 0) {
		Genode::warning("vct: dropped ", out.dropped,
		                " argument(s) beyond MAX_ARGS=", MAX_ARGS);
	}

	return out;
}
