/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Optional_rom_dataspace. See the header for why a
 * denied session needs Env::try_session() instead of the plain
 * Attached_rom_dataspace.
 */

#include "optional_rom_dataspace.h"

#include <base/log.h>
#include <rom_session/rom_session.h>
#include <util/string.h>

using namespace Sponge::Sponge_DE;


bool Optional_rom_dataspace::open(char const *label)
{
	if (_rom.constructed())
		return valid();

	/*
	 * Session arguments identical to base's Connection<Rom_session>
	 * (see include/rom_session/connection.h), so the server sees the
	 * same quota request it would see for a regular ROM.
	 */
	char argbuf[Genode::Parent::Session_args::MAX_SIZE];
	Genode::copy_cstring(argbuf, "", sizeof(argbuf));
	Genode::Arg_string::set_arg_string(argbuf, sizeof(argbuf), "label", label);
	Genode::Arg_string::set_arg(argbuf, sizeof(argbuf), "ram_quota",
	                            Genode::String<32>(Genode::Ram_quota {
	                                Genode::Rom_session::RAM_QUOTA }).string());
	Genode::Arg_string::set_arg(argbuf, sizeof(argbuf), "cap_quota",
	                            Genode::String<32>(Genode::Cap_quota {
	                                Genode::Rom_session::CAP_QUOTA }).string());

	Genode::Env::Session_result const result =
		_env.try_session(Genode::Rom_session::service_name(),
		                 _id_space_element.id(),
		                 Genode::Parent::Session_args(argbuf),
		                 Genode::Affinity { });

	bool opened = false;

	result.with_result(
		[&] (Genode::Session_capability cap) {
			_rom.construct(Genode::static_cap_cast<Genode::Rom_session>(cap));
			opened = true;
		},
		[&] (Genode::Session_error e) {
			/* Log detail only — the caller owns the user-facing
			 * warning, which also covers the attach failure. */
			Genode::log("optional ROM \"", label,
			            "\" session not granted (error ", (unsigned)e, ")");
		});

	if (!opened)
		return false;

	_try_attach();
	return valid();
}


void Optional_rom_dataspace::update()
{
	if (!_rom.constructed())
		return;

	/* Content that still fits the existing mapping is refreshed in
	 * place; otherwise the module is re-fetched and re-attached. */
	if (_ds.constructed() && _rom->update() == true)
		return;

	_try_attach();
}