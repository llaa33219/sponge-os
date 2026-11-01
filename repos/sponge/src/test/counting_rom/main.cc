/* SPDX-License-Identifier: Apache-2.0
 *
 * counting_rom — ROM proxy that logs every session create.
 *
 * Phase 16 P3.3 instrumentation (run/sponge-pkg-dispatch-liveness.run):
 * serves the parent's ROM modules verbatim and logs one
 *
 *     counting-rom: create <module>
 *
 * line per ROM session creation. The scenario asserts that NO create
 * happens inside a request's dispatch window — the P3.3 claim is that
 * package dispatch decides from the bootstrap metadata cache and opens
 * no ROM sessions at all (a session create is I/O on the request path;
 * a denied or stalled create parks the entrypoint, docs/evidence/
 * forwarded-create-starvation.md).
 *
 * Content is the PARENT's dataspace passed through (boot modules are
 * image-immutable — nothing to copy or diff).
 */

#include <base/component.h>
#include <base/log.h>
#include <base/session_label.h>
#include <base/signal.h>
#include <base/heap.h>
#include <rom_session/connection.h>
#include <root/component.h>

namespace {

using namespace Genode;

struct Counting_session : Rpc_object<Genode::Rom_session>
{
	Rom_connection _parent;

	Signal_context_capability _sigh { };

	Counting_session(Env &env, Session_label const &module)
	: _parent(env, module) { }

	Rom_dataspace_capability dataspace() override
	{
		return _parent.dataspace();
	}

	void sigh(Signal_context_capability sigh) override
	{
		_sigh = sigh;
	}

	bool update() override
	{
		if (_sigh.valid())
			Signal_transmitter(_sigh).submit();
		return true;
	}
};


struct Counting_root : Root_component<Counting_session>
{
	Env &_env;

	Counting_root(Env &env, Allocator &md_alloc)
	:
		Root_component<Counting_session>(&env.ep().rpc_ep(), &md_alloc),
		_env(env)
	{ }

	Create_result _create_session(const char *args) override
	{
		Session_label const label  = label_from_args(args);
		Session_label const module = label.last_element();

		Genode::log("counting-rom: create ", module);

		try {
			return *new (md_alloc()) Counting_session(_env, module);
		}
		catch (Rom_connection::Rom_connection_failed) {
			Genode::error("counting-rom: parent has no module ", module);
			return Create_error::DENIED;
		}
	}
};


struct Main
{
	Env &_env;

	Sliced_heap _heap { _env.ram(), _env.rm() };

	Counting_root _root { _env, _heap };

	Main(Env &env) : _env(env)
	{
		_env.parent().announce(_env.ep().manage(_root));
		Genode::log("counting-rom: ready");
	}
};

} /* namespace */


void Component::construct(Genode::Env &env)
{
	static Main main { env };
}
