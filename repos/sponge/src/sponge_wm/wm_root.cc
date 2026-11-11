/* SPDX-License-Identifier: Apache-2.0
 *
 * wm_root — Gui service root of the sponge_wm core. See wm_root.h.
 */

#include "wm_root.h"

using namespace Sponge::Wm_core;
using Session = Gui::Session;


namespace {

Role role_of_policy(Genode::Node const &policy)
{
	Genode::String<16> const role =
		policy.attribute_value("role", Genode::String<16>());

	if (Genode::strcmp(role.string(), "decorator") == 0)
		return Role::DECORATOR;
	if (Genode::strcmp(role.string(), "layouter") == 0)
		return Role::LAYOUTER;
	return Role::CONTENT;
}

}  /* anonymous namespace */


Wm_root::Create_result Wm_root::_create_session(char const *args,
                                                Genode::Affinity const &)
{
	Session::Label const label    = Genode::label_from_args(args);
	Session::Resources resources = Genode::session_resources_from_args(args);

	Role role = Role::CONTENT;

	/*
	 * Encoding-agnostic policy matching ('Genode::Node' reads both
	 * wire encodings; 'Session_policy' is Xml_node-only).
	 */
	bool const matched = Genode::with_matching_policy(label, _config.node(),
		[&] (Genode::Node const &policy) {
			role = role_of_policy(policy);
			return true;
		},
		[&] { return false; });

	if (!matched) {
		warning("no policy defined for label '", label, "'");
		return Genode::Session_error::DENIED;
	}

	/*
	 * Floor budget for the per-session input buffer
	 * (Input::Event_queue::QUEUE_SIZE events), the command buffer, and
	 * the info ROM. A smaller donation cannot serve the session.
	 */
	enum : Genode::size_t {
		INPUT_DS = Input::Event_queue::QUEUE_SIZE*sizeof(Input::Event),
		FLOOR    = INPUT_DS + 32*1024
	};

	if (resources.ram_quota.value < FLOOR)
		return Genode::Session_error::INSUFFICIENT_RAM;

	if (resources.cap_quota.value < Session::CAP_QUOTA)
		return Genode::Session_error::INSUFFICIENT_CAPS;

	unsigned long wid = 0;
	if (role == Role::CONTENT) {
		wid = _ops.new_window(label);
		if (wid == 0) {
			warning("window table full, refusing session '", label, "'");
			return Genode::Session_error::DENIED;
		}
	}

	return Genode::Root_component<Wm_session>::_alloc_obj(
		_env, _ops, role, wid, resources, label);
}


void Wm_root::_upgrade_session(Wm_session &session, char const *args)
{
	session.upgrade(Genode::ram_quota_from_args(args));
	session.upgrade(Genode::cap_quota_from_args(args));
}


void Wm_root::_destroy_session(Wm_session &session)
{
	Genode::destroy(md_alloc(), &session);
}
