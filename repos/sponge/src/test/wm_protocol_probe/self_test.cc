/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test harness for the WM protocol v1 contract cases
 * (src/test/wm_protocol_probe/contract_tests.cc, docs/05-sponge-de.md
 * §4.6, frozen 2026-11-12).
 *
 * NOT part of the Genode build (deliberately absent from
 * lib/mk/sponge_wm_protocol.mk and from the probe's target.mk), so it
 * lives outside the shared build run. It executes the exact case list
 * of the Genode test target 'wm_protocol_probe' without a Genode
 * runtime, which is what makes the contract gate runnable in a plain
 * checkout: generation through Genode's own Xml_generator/
 * Hid_generator sources, parsing through Genode::Node — the same
 * reader the delivered TasklistController uses.
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
 *       -I repos/sponge/include \
 *       repos/sponge/src/test/wm_protocol_probe/self_test.cc \
 *       repos/sponge/src/test/wm_protocol_probe/contract_tests.cc \
 *       repos/sponge/lib/src/sponge_wm_protocol/wm_protocol.cc \
 *       genode/repos/base/src/lib/base/node.cc \
 *       genode/repos/base/src/lib/base/hid.cc \
 *       genode/repos/base/src/lib/base/xml_generator.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_wm_protocol_self_test \
 *   && /tmp/sponge_wm_protocol_self_test
 *
 * Exit code 0 and a final "self-test: PASS (N checks)" line mean every
 * check matched. "self-test: FAIL" prints every divergence (the FAIL
 * lines carry expected/got detail).
 *
 * The host stubs below (Genode::Log, Genode::Lock, Genode::raise)
 * exist only to satisfy the linker for symbols hid.cc and node.cc
 * reference from paths the tested code never reaches (the
 * invalid-attribute warning and the Xml_node error raising); the
 * tested paths never log and never raise. The 'raise' stub aborts
 * loudly if that assumption is ever wrong.
 *
 * Note on -std: the Genode public headers (util/string.h,
 * base/node.h) use C++20 abbreviated templates, so the host build
 * compiles with -std=c++20 exactly like the other Sponge self-tests;
 * the library sources themselves stay in the C++17 subset per
 * AGENTS.md §3.1.
 */

#include "contract_tests.h"

#include <base/lock.h>
#include <base/log.h>
#include <base/error.h>
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
	unsigned const failures = Sponge::Wm_protocol::run_contract_tests(report);

	if (failures != 0) {
		printf("self-test: FAIL (%u of %u checks)\n", failures, checks);
		return 1;
	}

	printf("self-test: PASS (%u checks)\n", checks);
	return 0;
}
