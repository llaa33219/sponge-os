/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_root — the Gui service root of the sponge_wm core.
 *
 * The run-side session policy (docs/plans/phase16-sponge-wm.md §11
 * run-side routing; the run stanza's <policy label_prefix="..."
 * role="decorator|layouter"/> + <default-policy/>) decides the role of
 * every incoming Gui session. Policy matching uses the public
 * 'Genode::with_matching_policy' on the agnostic 'Genode::Node' config
 * (the config ROM may arrive in either wire encoding).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <os/session_policy.h>
#include <root/component.h>

#include "wm_session.h"

namespace Sponge::Wm_core {


struct Wm_root : Genode::Root_component<Wm_session>
{
	Genode::Env &_env;
	Wm_ops     &_ops;
	Genode::Attached_rom_dataspace const &_config;

	Wm_root(Genode::Env &env, Wm_ops &ops, Genode::Allocator &md_alloc,
	        Genode::Attached_rom_dataspace const &config)
	:
		Genode::Root_component<Wm_session>(env.ep(), md_alloc),
		_env(env), _ops(ops), _config(config)
	{ }

	Create_result _create_session(char const *args,
	                              Genode::Affinity const &) override;
	void _upgrade_session(Wm_session &, char const *) override;
	void _destroy_session(Wm_session &) override;
};


}  /* namespace Sponge::Wm_core */
