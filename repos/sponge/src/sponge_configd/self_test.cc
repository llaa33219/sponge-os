/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test harness for sponge_configd's per-label
 * key-domain ACL (src/sponge_configd/acl_tests.cc).
 *
 * NOT part of the Genode build (deliberately absent from target.mk's
 * SRC_CC), so it lives outside the shared build run. It executes the
 * exact case list against the REAL production gate (key_domain_acl.cc)
 * without a Genode runtime, which is what makes the Criterion 18 ACL
 * gate runnable in a plain checkout. The production set path
 * (main.cc `_do_set`) consults the same Acl::gate_set decision, so
 * the tested gate logic is the shipped gate logic.
 *
 * BUILD & RUN (from the repository root; host g++ only — this never
 * touches the Genode build tree):
 *
 *   g++ -std=c++20 -O1 \
 *       -I genode/repos/base/include \
 *       -I genode/repos/base/include/spec/64bit \
 *       -I genode/repos/base/include/spec/x86_64 \
 *       -I genode/repos/base/include/spec/x86 \
 *       repos/sponge/src/sponge_configd/self_test.cc \
 *       repos/sponge/src/sponge_configd/acl_tests.cc \
 *       repos/sponge/src/sponge_configd/key_domain_acl.cc \
 *       -o /tmp/sponge_configd_acl_self_test \
 *   && /tmp/sponge_configd_acl_self_test
 *
 * Exit code 0 and a final "self-test: PASS (N checks)" line mean every
 * check matched. "self-test: FAIL" prints every divergence.
 *
 * Note on -std: the Genode public headers use C++20 abbreviated
 * templates, so the host build compiles with -std=c++20 exactly like
 * the other Sponge self-tests; the production sources stay in the
 * C++17 subset per AGENTS.md §3.1. key_domain_acl.cc needs no host
 * stubs: Genode::strcmp/strlen are header-inline in util/string.h.
 */

#include "acl_tests.h"

#include <stdio.h>


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
	unsigned const failures = Sponge::Configd::Acl_tests::run_acl_tests(report);

	if (failures != 0) {
		printf("self-test: FAIL (%u of %u checks failed)\n", failures, checks);
		return 1;
	}

	printf("self-test: PASS (%u checks)\n", checks);
	return 0;
}
