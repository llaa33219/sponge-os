/* SPDX-License-Identifier: Apache-2.0
 *
 * seam_check.cc — adapter-seam check: parse
 * pkg/settings_demo/metadata.xml with the IN-FLIGHT sponge_pkgd
 * settings rules (repos/sponge/src/sponge_pkgd/settings_rules.cc) and
 * assert both entries come out accepted with the documented identity.
 *
 * WHY THIS EXISTS (the explicit adapter seam): the pkgd settings
 * launch is developed in parallel. This package is authored against
 * the docs/12-package-format.md §14.2/§14.3 schema, and this binary
 * proves the package's metadata is accepted by the real pkgd-side
 * parser, entry by entry. If the pkgd rules API moves, this check
 * fails loudly right here and nowhere else needs to change.
 *
 * BUILD & RUN (from the repository root; host g++ only):
 *
 *   g++ -std=c++20 -O1 \
 *       -I genode/repos/base/include \
 *       -I genode/repos/base/include/spec/64bit \
 *       -I genode/repos/base/include/spec/x86_64 \
 *       -I genode/repos/base/include/spec/x86 \
 *       -I genode/repos/base/src/include \
 *       -I repos/sponge/include \
 *       -I repos/sponge/src/sponge_pkgd \
 *       repos/sponge/src/settings_demo/tests/seam_check.cc \
 *       repos/sponge/src/sponge_pkgd/settings_rules.cc \
 *       repos/sponge/lib/src/sponge_pkg_launch/pkg_launch.cc \
 *       genode/repos/base/src/lib/base/hid.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/settings_demo_seam_check \
 *   && /tmp/settings_demo_seam_check pkg/settings_demo/metadata.xml
 *
 * Prefer tests/check.sh, which runs this with the right paths.
 */

#include "settings_rules.h"

#include <base/error.h>
#include <base/exception.h>
#include <base/lock.h>
#include <base/log.h>
#include <base/output.h>

#include <stdio.h>
#include <string.h>

void Genode::raise(Genode::Unexpected_error e)
{
	switch (e) {
	case Genode::Unexpected_error::INDEX_OUT_OF_BOUNDS:
		throw Genode::Index_out_of_bounds();
	case Genode::Unexpected_error::NONEXISTENT_SUB_NODE:
		throw Genode::Nonexistent_sub_node();
	case Genode::Unexpected_error::ACCESS_UNCONSTRUCTED_OBJ:
		throw Genode::Access_unconstructed_obj();
	case Genode::Unexpected_error::IPC_BUFFER_EXCEEDED:
		break;
	}
	throw Genode::Ipc_error();
}

void Genode::raise(Genode::Alloc_error e)
{
	switch (e) {
	case Genode::Alloc_error::OUT_OF_RAM:  throw Genode::Out_of_ram();
	case Genode::Alloc_error::OUT_OF_CAPS: throw Genode::Out_of_caps();
	case Genode::Alloc_error::DENIED:      break;
	}
	throw Genode::Denied();
}

namespace {

struct Null_output : Genode::Output
{
	void out_char(char) override { }
	void out_string(char const *, Genode::size_t) override { }
};

Null_output null_output { };

}

Genode::Log &Genode::Log::log()
{
	static Genode::Log instance { null_output };
	return instance;
}

void Genode::Log::_acquire(Type) { }
void Genode::Log::_release() { }

Genode::Lock::Lock(Genode::Lock::State initial)
:
	_spinlock_state(0), _state(initial), _last_applicant(0), _owner(nullptr)
{ }

using Sponge::Pkgd::Settings_entry;
using Sponge::Pkgd::Settings_parse;

unsigned failures = 0;

void check(bool ok, char const *what)
{
	if (ok)
		return;
	++failures;
	printf("FAIL: %s\n", what);
}

void check_eq_str(char const *got, char const *want, char const *what)
{
	if (strcmp(got, want) == 0)
		return;
	++failures;
	printf("FAIL: %s (got \"%s\" want \"%s\")\n", what, got, want);
}

enum { MAX_ENTRIES = 8, FILE_MAX = 16 * 1024 };

int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("usage: %s <metadata.xml>\n", argv[0]);
		return 2;
	}

	static char buf[FILE_MAX];
	FILE *f = fopen(argv[1], "rb");
	if (f == nullptr) {
		printf("FAIL: cannot open %s\n", argv[1]);
		return 2;
	}
	size_t const len = fread(buf, 1, sizeof(buf), f);
	fclose(f);

	Settings_entry entries[MAX_ENTRIES] { };
	unsigned num = 0;

	try {
		Genode::Xml_node const root(buf, len);

		check(root.has_type("package"), "metadata root is <package>");

		root.for_each_sub_node("settings", [&] (Genode::Xml_node const &node) {
			if (num >= MAX_ENTRIES) {
				++failures;
				printf("FAIL: more than %u settings entries\n",
				       (unsigned)MAX_ENTRIES);
				return;
			}

			Settings_parse const parsed = Sponge::Pkgd::parse_settings_entry(
				node, entries[num], entries, num);

			if (parsed.reject != Sponge::Pkgd::Settings_reject::none) {
				++failures;
				printf("FAIL: entry %u rejected by pkgd rules: %s\n",
				       num, Sponge::Pkgd::settings_reject_reason(
				                parsed.reject));
				return;
			}

			check(!parsed.unknown_attribute,
			      "no unknown attributes on <settings>");
			check(!parsed.unknown_child,
			      "no unknown children in <settings>");
			++num;
		});

	} catch (Genode::Xml_node::Invalid_syntax) {
		printf("FAIL: metadata.xml is not valid XML\n");
		return 1;
	}

	if (num != 2) {
		++failures;
		printf("FAIL: expected exactly 2 settings entries (got %u)\n", num);
		return 1;
	}

	check_eq_str(entries[0].id.string(), "greeting", "first entry id");
	check_eq_str(entries[0].target.string(), "settings_demo_greeting",
	             "first entry target");
	check_eq_str(entries[0].title.string(), "Greeting", "first entry title");
	check(!entries[0].category_defaulted,
	      "first entry category is a closed-enum value");
	check(entries[0].has_icon, "first entry declares an icon");
	check(entries[0].num_keywords > 0, "first entry carries keywords");

	check_eq_str(entries[1].id.string(), "about", "second entry id");
	check_eq_str(entries[1].target.string(), "settings_demo_about",
	             "second entry target");
	check_eq_str(entries[1].title.string(), "About", "second entry title");
	check(!entries[1].category_defaulted,
	      "second entry category is a closed-enum value");
	check(entries[1].has_icon, "second entry declares an icon");
	check(entries[1].num_keywords > 0, "second entry carries keywords");

	if (strcmp(entries[0].id.string(), entries[1].id.string()) == 0) {
		++failures;
		printf("FAIL: the two settings ids are not distinct\n");
	}

	Genode::String<64> const start0 =
		Sponge::Pkgd::settings_start_name("settings_demo",
		                                  entries[0].id.string());
	Genode::String<64> const start1 =
		Sponge::Pkgd::settings_start_name("settings_demo",
		                                  entries[1].id.string());

	check_eq_str(start0.string(), "settings_demo.greeting",
	             "first entry start name");
	check_eq_str(start1.string(), "settings_demo.about",
	             "second entry start name (distinct lifecycle identity)");

	if (failures == 0) {
		printf("seam-check: PASS (%u entries accepted by pkgd rules)\n",
		       num);
		return 0;
	}

	printf("seam-check: FAIL (%u failures)\n", failures);
	return 1;
}
