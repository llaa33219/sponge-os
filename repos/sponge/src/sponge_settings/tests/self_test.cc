/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test harness for the sponge_settings pure model
 * (discovery parsing, category fallback, identity bounds, keyword cap,
 * overflow drops, settings launch wire shapes, row layout/hit-testing).
 *
 * NOT part of the Genode build (deliberately absent from the
 * component's target.mk). It executes the deterministic case list of
 * tests/test_cases.cc without a Genode runtime — the same pure code
 * the component ships (model.cc), linked against Genode's own public
 * string/XML/node sources.
 *
 * BUILD & RUN (from the repository root; host g++ only — this never
 * touches the Genode build tree):
 *
 *   g++ -std=c++20 -O1 \
 *       -I genode/repos/base/include \
 *       -I genode/repos/base/include/spec/64bit \
 *       -I genode/repos/base/include/spec/x86_64 \
 *       -I genode/repos/base/include/spec/x86 \
 *       -I genode/repos/base/src/include \
 *       -I genode/repos/os/include \
 *       -I repos/sponge/include \
 *       repos/sponge/src/sponge_settings/tests/self_test.cc \
 *       repos/sponge/src/sponge_settings/tests/test_cases.cc \
 *       repos/sponge/src/sponge_settings/model.cc \
 *       genode/repos/base/src/lib/base/node.cc \
 *       genode/repos/base/src/lib/base/hid.cc \
 *       genode/repos/base/src/lib/base/xml_generator.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_settings_self_test \
 *   && /tmp/sponge_settings_self_test
 *
 * Exit code 0 and a final "self-test: PASS (N checks)" line mean every
 * check matched. "self-test: FAIL" prints every divergence (the FAIL
 * lines carry expected/got detail).
 *
 * The host stubs below (Genode::Log, Genode::Lock, Genode::raise)
 * exist only to satisfy the linker for symbols the tested code never
 * reaches; the 'raise' stub aborts loudly if that assumption is ever
 * wrong. Same pattern as src/sponge_decorator/tests/self_test.cc.
 *
 * Note on -std: the Genode public headers use C++20 abbreviated
 * templates, so the host build compiles with -std=c++20; the tested
 * sources stay in the C++17 subset per AGENTS.md §3.1.
 */

#include "test_cases.h"

#include <base/error.h>
#include <base/lock.h>
#include <base/log.h>
#include <util/string.h>

#include <stdio.h>
#include <stdlib.h>


/* ===================== host stubs (link only) ===================== */

namespace {

struct Null_output : Genode::Output
{
	void out_char(char) override { }
	void out_string(char const *, Genode::size_t) override { }
};

Null_output null_output { };

}  /* anonymous namespace */

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

void Genode::raise(Genode::Unexpected_error)
{
	fprintf(stderr, "self-test stub: Genode::raise(Unexpected_error) reached\n");
	abort();
}


/* ========================== harness ============================== */

namespace {

unsigned checks = 0;

void report(bool ok, char const *what)
{
	checks++;
	if (!ok)
		printf("FAIL: %s\n", what);
}

}  /* anonymous namespace */


int main()
{
	unsigned const failures = Sponge::Settings::run_test_cases(report);

	if (failures != 0) {
		printf("self-test: FAIL (%u failed checks)\n", failures);
		return 1;
	}

	printf("self-test: PASS (%u checks)\n", checks);
	return 0;
}
