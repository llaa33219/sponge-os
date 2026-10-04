/*
 * vct_tty — Terminal front-end for the in-terminal `vct` command.
 *
 * The terminal package's vfs exposes /dev/vct (a <terminal label="vct"/>
 * plugin node). The /bin/vct shell script writes one command line there
 * ("install hello") and reads the rendered answer back. This component
 * owns the other end of that Terminal session: it parses the line,
 * forwards it to sponge_pkgd through the tty_request/tty_result
 * Report/ROM pair (the same ReportRomClient protocol vct uses, on a
 * dedicated label pair because report_rom is single-writer per label),
 * and writes the result plus a sentinel line back through the session.
 *
 * One client, one session, one thread: the vfs plugin connects once at
 * package start; every write RPC is answered synchronously (a pending
 * request blocks the RPC for up to the client's poll budget, which the
 * waiting shell script is doing anyway).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Genode includes */
#include <base/component.h>
#include <base/heap.h>
#include <root/component.h>
#include <base/attached_ram_dataspace.h>
#include <terminal_session/terminal_session.h>
#include <util/xml_node.h>

/* Sponge includes */
#include <sponge/backend_client.h>

namespace Vct_tty {

	class Session_component;
	class Root_component;
	class Main;

	using namespace Genode;
}

class Vct_tty::Session_component
:
	public Rpc_object<Terminal::Session, Session_component>
{
	private:

		enum { IO_BUF_SIZE = 4096, LINE_LEN = 128, OUT_SIZE = 4096 };

		Attached_ram_dataspace _io_buffer;

		Sponge::Backend::ReportRomClient &_client;

		char     _line[LINE_LEN] { };
		unsigned _line_len       { 0 };

		char     _out[OUT_SIZE]  { };
		Genode::size_t _out_len   { 0 };
		Genode::size_t _out_read  { 0 };

		Signal_context_capability _read_avail_sigh { };

		void _emit(char const c)
		{
			if (_out_len < OUT_SIZE)
				_out[_out_len++] = c;
		}

		void _emit(char const *s)
		{
			while (*s)
				_emit(*s++);
		}

		/*
		 * Render the structured <result/> the same way vct does for
		 * its terminal output: a one-line headline, then one line per
		 * sub-node (the list op), then the sentinel the shell script
		 * breaks on.
		 */
		void _emit_result(Xml_node const &result)
		{
			typedef Genode::String<64> Str;

			Str const status = result.attribute_value("status", Str());
			Str const op     = result.attribute_value("op",     Str());
			Str const pkg    = result.attribute_value("pkg",    Str());

			_emit("vct: ");
			_emit(op.string());
			if (pkg != "") {
				_emit(" ");
				_emit(pkg.string());
			}
			_emit(" -> ");
			_emit(status.string());
			_emit("\n");

			result.for_each_sub_node([&](Xml_node const &n) {
				Str const name    = n.attribute_value("name", Str());
				Str const running = n.attribute_value("running", Str());
				if (name == "")
					return;
				_emit("  ");
				_emit(name.string());
				if (running != "") {
					_emit("  (running=");
					_emit(running.string());
					_emit(")");
				}
				_emit("\n");
			});
		}

		void _finish_response()
		{
			_emit("=== vct: done ===\n");

			Genode::Signal_transmitter(_read_avail_sigh).submit();
		}

		void _process_line()
		{
			_line[_line_len] = 0;

			/* strip trailing CR/LF */
			while (_line_len > 0 &&
			       (_line[_line_len - 1] == '\n' ||
			        _line[_line_len - 1] == '\r'))
				_line[--_line_len] = 0;

			_line_len = 0;

			if (_line[0] == 0) {
				_emit("usage: vct <explain|install|remove|list> [package]\n");
				_finish_response();
				return;
			}

			char op[LINE_LEN]  { };
			char pkg[LINE_LEN] { };
			{
				unsigned i = 0, j = 0;
				while (_line[i] && _line[i] != ' ' && j < LINE_LEN - 1)
					op[j++] = _line[i++];
				while (_line[i] == ' ')
					++i;
				j = 0;
				while (_line[i] && j < LINE_LEN - 1)
					pkg[j++] = _line[i++];
			}

			bool const ok = (pkg[0] == 0)
			              ? _client.request(op)
			              : _client.request(op, pkg);

			if (ok) {
				_emit_result(_client.result_xml());
			} else {
				_emit("vct: no answer from the package backend (timeout)\n");
			}
			_finish_response();
		}

	public:

		Session_component(Ram_allocator &ram, Env::Local_rm &rm,
		                   Sponge::Backend::ReportRomClient &client)
		:
			_io_buffer(ram, rm, IO_BUF_SIZE), _client(client)
		{ }


		/********************************
		 ** Terminal session interface **
		 ********************************/

		Size size() override { return Size(80, 25); }

		bool avail() override { return _out_read < _out_len; }

		size_t _read(size_t num_bytes)
		{
			size_t const pending = _out_len - _out_read;
			size_t const n = Genode::min(num_bytes, pending);

			char *dst = _io_buffer.local_addr<char>();
			for (size_t i = 0; i < n; ++i)
				dst[i] = _out[_out_read + i];

			_out_read += n;

			if (_out_read == _out_len) {
				_out_len  = 0;
				_out_read = 0;
			}
			return n;
		}

		size_t _write(size_t num_bytes)
		{
			num_bytes = Genode::min(num_bytes, _io_buffer.size());

			char const *src = _io_buffer.local_addr<char>();

			for (size_t i = 0; i < num_bytes; ++i) {
				char const c = src[i];
				if (c == '\n' || _line_len == LINE_LEN - 1) {
					_process_line();
				} else if (c != '\r') {
					_line[_line_len++] = c;
				}
			}
			return num_bytes;
		}

		Dataspace_capability _dataspace() { return _io_buffer.cap(); }

		void read_avail_sigh(Signal_context_capability sigh) override
		{
			_read_avail_sigh = sigh;
		}

		void size_changed_sigh(Signal_context_capability) override { }

		void connected_sigh(Signal_context_capability sigh) override
		{
			Genode::Signal_transmitter(sigh).submit();
		}

		size_t read(void *, size_t) override { return 0; }
		size_t write(void const *, size_t) override { return 0; }
};


class Vct_tty::Root_component : public Genode::Root_component<Session_component>
{
	private:

		Ram_allocator &_ram;
		Env::Local_rm &_rm;
		Sponge::Backend::ReportRomClient &_client;

	protected:

		Create_result _create_session(const char *) override
		{
			return *new (md_alloc()) Session_component(_ram, _rm, _client);
		}

	public:

		Root_component(Entrypoint    &ep,
		               Allocator     &md_alloc,
		               Ram_allocator &ram,
		               Env::Local_rm &rm,
		               Sponge::Backend::ReportRomClient &client)
		:
			Genode::Root_component<Session_component>(&ep.rpc_ep(), &md_alloc),
			_ram(ram), _rm(rm), _client(client)
		{ }
};


struct Vct_tty::Main
{
	Env &_env;

	Sliced_heap sliced_heap { _env.ram(), _env.rm() };

	/*
	 * The pkgd channel: request label "tty_request", result label
	 * "tty_result" (report_rom policies wire both ends; pkgd enables
	 * the channel via its <tty_request/> config node).
	 */
	Sponge::Backend::ReportRomClient client { _env, "tty_request", "tty_result" };

	Root_component root { _env.ep(), sliced_heap, _env.ram(), _env.rm(), client };

	Main(Env &env) : _env(env)
	{
		_env.parent().announce(_env.ep().manage(root));
		Genode::log("vct_tty: ready (tty_request channel)");
	}
};

void Component::construct(Genode::Env &env)
{
	static Vct_tty::Main main(env);
}
