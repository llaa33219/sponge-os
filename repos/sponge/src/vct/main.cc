/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * Entry point for the vct component.
 *
 * vct (Very Convenient Tool) is Sponge OS's system management single-entry
 * command-line tool. It is a short-lived Genode component: spawned when the
 * user invokes a command, terminated once the command completes.
 *
 * Per Genode convention, the actual work happens in `Vct::Main`. This file
 * only constructs that object on the component heap and lets the framework
 * drive the rest.
 */

#include <base/component.h>
#include <base/heap.h>

#include "vct_main.h"

using namespace Sponge;

void Component::construct(Genode::Env &env)
{
	/* The Main object lives for the entire component lifetime. Using a static
	 * local on the component heap avoids global construction order issues. */
	static Vct::Main main { env };
	main.run();
}


/**********************
 * Component quota
 **********************/

/* Allocate a modest quota for vct on startup. vct is a short-lived tool, so
 * this can stay small; backend daemons (sponge_pkgd, etc.) carry the heavy
 * state. */
Genode::size_t Component::stack_size()      { return 64 * 1024 * sizeof(Genode::addr_t); }
