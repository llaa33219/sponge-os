/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_protocol_probe — deterministic WM protocol v1 contract tests for
 * sponge_wm_protocol (docs/05-sponge-de.md §4.6, frozen 2026-11-12).
 *
 * Plain Genode component (Component::construct; no Qt, no libc, no
 * sessions — AGENTS.md §3.1). The capability surface is EMPTY: every
 * case is a pure library call against in-memory buffers (generation
 * through Genode::Xml_generator/Hid_generator, parsing through
 * Genode::Node), so the probe needs no ROM, no Report, no Timer and
 * cannot depend on scenario timing. This is the runtime-side half of
 * the protocol contract gate for the native WM components
 * (D16W.6/M0): the produced shapes must keep the delivered
 * TasklistController working unchanged.
 *
 * Output: one line per check ("wm-protocol-probe: ok <desc>" /
 * "wm-protocol-probe: FAIL <desc>"), a summary line, and the final
 * gate marker:
 *
 *   wm-protocol-probe: PASS
 *
 * on success, or
 *
 *   wm-protocol-probe: FAIL (<n> failed checks)
 *
 * with exit code 1 on any failure. The scenario gate greps for the
 * marker line.
 *
 * The same case list runs host-side without a Genode runtime via the
 * self-test in this directory (see self_test.cc for the build/run
 * command).
 */

#include <base/component.h>
#include <base/log.h>

#include "contract_tests.h"

namespace {

struct Probe
{
	Genode::Env &_env;

	static void report(bool ok, char const *what)
	{
		if (ok)
			Genode::log("wm-protocol-probe: ok ", what);
		else
			Genode::log("wm-protocol-probe: FAIL ", what);
	}

	explicit Probe(Genode::Env &env) : _env(env)
	{
		unsigned const failures = Sponge::Wm_protocol::run_contract_tests(report);

		if (failures == 0) {
			Genode::log("wm-protocol-probe: PASS");
			_env.parent().exit(0);
			return;
		}

		Genode::log("wm-protocol-probe: FAIL (", failures, " failed checks)");
		_env.parent().exit(1);
	}
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe(env);
}
