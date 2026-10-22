/*
 * upgrade_wedge — the container-wedge reproduction.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE FIELD FAILURE THIS ATTACKS
 *
 *   2026-10-19, LG gram 17ZD90N: a fast scrollbar drag in the SPONGE
 *   DEBUG window freezes the whole system. The mouse dies, the panel
 *   health label reads "sponge_pkgd unresponsive(N)", and the freeze
 *   NEVER heals. A freeze that never heals is not saturation: a thread
 *   is blocked.
 *
 *   The blocked thread is the sandbox init's ENTRYPOINT, and the reason
 *   is the quota-upgrade protocol (docs/evidence/sponge-upgrade-wedge.md):
 *
 *     victim  --Parent::upgrade-->  sandbox init  --nested upgrade-->
 *              top init  --PENDING-->  provider (never answers)
 *
 *   The sandbox init forwards the upgrade to its own parent and then
 *   WAITS for the answer. 'Parent::Upgrade_result::PENDING' is not a
 *   promise - it means "the request has been taken, the session's
 *   provider has not acted on it yet" - and a provider that is stuck
 *   (the field's file-system server on a stalled USB stick) never
 *   answers. The waiting thread is the sandbox's single entrypoint:
 *   it serves every child's session plumbing AND the containment alarm,
 *   so a parked entrypoint takes the whole container down silently and
 *   forever - the alarm that would have noticed is parked with it.
 *
 * THE THREE ROLES (one binary, selected by the config attribute 'role')
 *
 *   provider  Announces the service "Test_service" and NEVER answers a
 *             quota-upgrade request for it. This is the field's stuck
 *             file-system server, made deterministic.
 *
 *   victim    Opens a session to "Test_service" and asks for a quota
 *             upgrade on that session. This is the storm's consequence:
 *             a component whose RAM/caps quota runs out asks its parent
 *             for more.
 *
 *   witness   Opens a session to "Test_service" once per second and
 *             logs each successful round. A session request goes
 *             through the sandbox's entrypoint, so the witness stops
 *             talking exactly when the container is wedged - and keeps
 *             talking when it is not. The witness is the oracle: it
 *             distinguishes "the container is stuck" from "the container
 *             is quiet".
 *
 * The RED/GREEN pair is measured by run/sponge-upgrade-wedge.run: with
 * the unpatched base, the sandbox's entrypoint parks and the witness
 * stops; with the patch, the upgrade is refused truthfully, the
 * victim's request fails fast, and the witness keeps its rounds.
 */

/* Genode includes */
#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/connection.h>
#include <base/env.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/sleep.h>
#include <root/component.h>
#include <base/rpc.h>
#include <session/session.h>
#include <base/rpc_server.h>
#include <base/node.h>
#include <timer_session/connection.h>
#include <util/string.h>

namespace {

	struct Test_session_interface : Genode::Session
	{
		static char const * service_name() { return "Test_service"; }
		static constexpr unsigned CAP_QUOTA = 8;

		virtual void ping() = 0;

		GENODE_RPC(Rpc_ping, void, ping);
		GENODE_RPC_INTERFACE(Rpc_ping);
	};

	struct Test_session : Genode::Rpc_object<Test_session_interface>
	{
		void ping() override { }
	};

	/*
	 * The provider's root. It creates sessions normally and swallows
	 * quota-upgrade requests - the field's stuck file-system server.
	 */
	struct Test_root : Genode::Root_component<Test_session>
	{
		Test_root(Genode::Env &env, Genode::Allocator &md_alloc)
		: Genode::Root_component<Test_session>(env.ep(), md_alloc) { }

		Create_result _create_session(char const *) override
		{
			return *new (md_alloc()) Test_session();
		}

		void _upgrade_session(Test_session &, char const *) override
		{
			Genode::log("provider: quota-upgrade request received - "
			            "not answering (the stuck file-system server)");
			Genode::sleep_forever();
		}
	};

	/*
	 * A connection that exposes the session's ID at the parent, so the
	 * victim can ask for a quota upgrade on exactly that session.
	 */
	struct Test_connection : Genode::Connection<Test_session_interface>
	{
		using Connection::Connection;

		Genode::Parent::Client::Id parent_id() const
		{
			return _id_space_element.id();
		}
	};

	enum class Role { Provider, Victim, Witness };

	/* the config attribute 'role' is numeric so that the HID and the XML
	 * delivery of the same document both parse it (a string attribute
	 * would need a string-typed parse, which 'Node' does not offer) */
	enum { ROLE_PROVIDER = 0, ROLE_VICTIM = 1, ROLE_WITNESS = 2 };

	Role _role(Genode::Env &env)
	{
		Genode::Attached_rom_dataspace config { env, "config" };
		config.update();

		if (!config.valid() || config.size() == 0)
			return Role::Victim;

		Genode::Node const cfg(Genode::Const_byte_range_ptr(
			config.local_addr<char const>(), config.size()));

		unsigned const role = cfg.attribute_value("role", unsigned(ROLE_VICTIM));

		switch (role) {
		case ROLE_PROVIDER: return Role::Provider;
		case ROLE_WITNESS:  return Role::Witness;
		default:            return Role::Victim;
		}
	}

	Genode::Ram_quota const session_ram_quota { 64*1024 };
}

void Component::construct(Genode::Env &env)
{
	switch (_role(env)) {

	case Role::Provider:
		{
			static Genode::Sliced_heap heap { env.ram(), env.rm() };
			static Test_root root { env, heap };

			env.parent().announce(env.ep().manage(root));

			Genode::log("provider: announced ", Test_session_interface::service_name());
		}
		break;

	case Role::Victim:
		{
			Test_connection conn(env, "victim", session_ram_quota,
			                     Genode::Connection<Test_session_interface>::Args());

			Genode::log("victim: session to ", Test_session_interface::service_name(),
			            " established - requesting a quota upgrade");

			env.upgrade(conn.parent_id(), "ram_quota=4096, cap_quota=1");

			Genode::log("victim: quota upgrade returned");
		}
		break;

	case Role::Witness:
		{
			Timer::Connection timer { env };
			unsigned round { 0 };

			for (;;) {
				round++;

				/*
				 * The markers localise a stall: the last marker seen in
				 * the log tells whether the witness was sleeping, waiting
				 * for the probe, or already done.
				 */
				Genode::log("witness: round ", round, " sleep begin");
				timer.msleep(3000);
				Genode::log("witness: round ", round, " sleep end");

				/*
				 * A fresh session request per round. It must not use
				 * 'Test_service': the provider is stuck in the victim's
				 * upgrade request, so a request for that service would
				 * block on the stuck provider and not on the container.
				 * A Timer session request goes through the sandbox's
				 * entrypoint and is answered by a healthy server, so a
				 * silent witness means the CONTAINER is wedged.
				 */
				Genode::log("witness: round ", round, " probe begin");
				Timer::Connection probe { env };
				Genode::log("witness: round ", round, " probe end");

				Genode::log("witness: round ", round, " ok");
			}
		}
		break;
	}
}
