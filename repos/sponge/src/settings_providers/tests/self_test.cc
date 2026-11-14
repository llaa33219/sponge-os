/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test harness for the settings-provider shared logic
 * (src/settings_providers/tests/sp_logic_tests.cc).
 *
 * NOT part of the Genode build (deliberately absent from every
 * target.mk's SRC_CC). It executes the case list against the REAL
 * production validators (common/sp_validate.h, common/sp_rows.h)
 * without a Genode runtime, which makes the Criterion 18 provider
 * logic runnable in a plain checkout — the tested logic IS the
 * shipped logic (sponge_configd/self_test.cc precedent).
 *
 * BUILD & RUN (from the repository root; host g++ only):
 *
 *   ./repos/sponge/src/settings_providers/tests/run.sh
 *
 * Exit code 0 and a final "self-test: PASS (N checks)" line mean
 * every check matched. "self-test: FAIL (M/N)" prints every
 * divergence and exits 1.
 */

#include "sp_logic_tests.h"

#include <stdio.h>

namespace {

unsigned checks = 0;
unsigned failed = 0;

void report(bool ok, char const *what)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL: %s\n", what);
	}
}

}  /* anonymous namespace */


int main()
{
	Sponge::Settings_tests::run_logic_tests(report);

	if (checks == 0 || failed > 0) {
		printf("self-test: FAIL (%u/%u)\n", failed, checks);
		return 1;
	}

	printf("self-test: PASS (%u checks)\n", checks);
	return 0;
}
