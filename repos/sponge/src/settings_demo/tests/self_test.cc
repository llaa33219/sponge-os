/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test for the settings_demo package-private store
 * logic (common/store.h — docs/12-package-format.md §13.2, D16.10
 * tier (b)).
 *
 * NOT part of the Genode build (deliberately absent from the
 * providers' target.mk). It executes the deterministic case list of
 * tests/test_cases.cc without a Genode runtime — the same pure code
 * the providers ship, linked against Genode's own public XML parser.
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
 *       repos/sponge/src/settings_demo/tests/self_test.cc \
 *       repos/sponge/src/settings_demo/tests/test_cases.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/settings_demo_self_test \
 *   && /tmp/settings_demo_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every check
 * matched. "self-test: FAIL" prints every divergence. Prefer
 * tests/check.sh, which runs this plus the structural and metadata
 * schema checks.
 *
 * The Genode::raise shims below mirror the sponge_pkgd self_test
 * precedent: the real implementation drags in the backtrace closure,
 * and the shim keeps the throw mapping minus the logging. Unlike the
 * pkgd test, THIS test feeds malformed XML on purpose (parse_store
 * must fail-soft), so the Invalid_syntax path through the shim is
 * exercised deliberately.
 */

#include "test_cases.h"

#include <base/error.h>
#include <base/exception.h>

#include <stdio.h>

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

int main()
{
	unsigned const failures = run_store_cases();

	if (failures == 0) {
		printf("self-test: PASS (store validation + store XML)\n");
		return 0;
	}

	printf("self-test: FAIL (%u failures)\n", failures);
	return 1;
}
