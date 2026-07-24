/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_hello — minimal installable package payload (Phase 4b).
 *
 * The smallest possible long-lived Genode component: it logs a fixed
 * marker and sleeps forever. The marker is what
 * run/sponge-pkg-install.run matches to prove that `vct install hello`
 * started the component inside the nested pkg_runtime init.
 *
 * It requests only parent-provided sessions (LOG/ROM/PD/CPU); no Gui,
 * no File_system, no libc.
 */

#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>

void Component::construct(Genode::Env &env)
{
	(void) env;

	Genode::log("hello from package: running");

	Genode::sleep_forever();
}


Genode::size_t Component::stack_size() { return 16 * 1024; }
