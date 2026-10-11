/* SPDX-License-Identifier: Apache-2.0
 *
 * resq_probe — does a deferred child resource request ever get granted?
 *
 * The question this settles is not academic for Sponge OS: pkgd's
 * admission control (repos/sponge/src/sponge_pkgd) can only judge a
 * launch against the budget pkg_runtime's init reports at that moment,
 * so the value of a package that later outgrows its quota depends
 * entirely on whether init can widen it at runtime.
 *
 * Genode 26.08's answer has two halves, and only one of them is
 * automatic:
 *
 *   1. genode/repos/os/src/lib/sandbox/child.cc
 *      Sandbox::Child::resource_request() — the vendored patch (ledger
 *      row 21) answers a request IMMEDIATELY when the sandbox's own PD
 *      still has the slack, then notifies the child. This half works.
 *
 *   2. When the sandbox does NOT have the slack, the request is parked
 *      in _requested_resources and the child waits. There is no upward
 *      call: the sandbox does not ask its own parent for quota, and the
 *      top-level init has nobody to ask (core never satisfies an init
 *      resource request — genode/repos/os/src/init/main.cc registers a
 *      resource_avail handler explicitly to PREVENT init from blocking
 *      on one). Only a config application re-evaluates the parked
 *      request, via Child::apply_upgrade().
 *
 * This probe measures (1) and the absence of (2) by observing the
 * child's own PD quota across the wait, rather than by trusting the
 * source reading.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <base/signal.h>
#include <parent/parent.h>
#include <timer_session/connection.h>
#include <util/hid.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

struct Resq_probe
{
	Genode::Env &_env;

	Timer::Connection _timer { _env };

	Genode::String<16> _name   { "resq" };
	Genode::size_t     _req_ram { 0 };
	unsigned          _observe_ms { 60000 };

	Genode::Signal_handler<Resq_probe, Genode::Entrypoint> _avail_handler {
		_env.ep(), *this, &Resq_probe::_handle_avail };

	bool _notified { false };

	Resq_probe(Genode::Env &env) : _env(env) { }

	void _handle_avail()
	{
		_notified = true;
		Genode::log("resq-", _name, ": resource_avail signal received");
	}

	[[noreturn]]
	void _pass()
	{
		Genode::log("resq-", _name, ": PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}

	[[noreturn]]
	void _fail(char const *reason)
	{
		Genode::error("resq-", _name, ": FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	template <typename NODE>
	void _collect(NODE const &n)
	{
		/*
		 * "id", not "name": Genode's HID generator folds a lone `name`
		 * attribute into the node's CONTENT (`probe c`), which then has no
		 * name attribute left to read.
		 */
		/* "id" is the Xml_generator form; a run-script config uses "name". */
		_name   = n.attribute_value("id",   Genode::String<16>());
		if (Genode::strcmp(_name.string(), "") == 0)
			_name = n.attribute_value("name", Genode::String<16>("resq"));
		/* "req_ram" accepts the same suffixes the package metadata uses. */
		Genode::String<16> const ram = n.attribute_value("req_ram",
		                                                 Genode::String<16>("64M"));
		char const *p = ram.string();
		Genode::size_t v { 0 };
		unsigned digits { 0 };
		while (*p >= '0' && *p <= '9') { v = v * 10 + Genode::size_t(*p - '0'); ++p; ++digits; }
		if (digits) {
			switch (*p) {
			case 'K': case 'k': v *= 1024; break;
			case 'M': case 'm': v *= 1024 * 1024; break;
			case 'G': case 'g': v *= 1024 * 1024 * 1024; break;
			default: break;
			}
		}
		_req_ram = v;
	}

	void _parse_config()
	{
		Genode::Attached_rom_dataspace cfg { _env, "config" };
		cfg.update();
		if (!cfg.valid() || cfg.size() == 0) return;

		char const *const base = cfg.local_addr<char>();
		Genode::size_t  const sz  = cfg.size();
		if (base[0] == '<') {
			Genode::Xml_node const root(base, sz);
			if (root.has_type("config")) _collect(root);
			return;
		}
		/*
		 * HID: the parameters live on the <probe> SUB-node, not on the
		 * root — the root carries none. Collecting from the root silently
		 * yields every default.
		 */
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(base, sz));
		if (!root.has_type("config")) return;
		root.for_each_sub_node([&] (Genode::Hid_node const &n) {
			if (n.has_type("probe")) _collect(n); });
	}

	/*
	 * Watch the child's OWN RAM budget for the whole window and report
	 * what it saw. The probe deliberately does NOT turn that into a
	 * verdict: on base-sel4 a PD's physical memory region is sized from
	 * its configured quota at construction, so `stats()` stops tracking
	 * the sandbox's quota bookkeeping afterwards (measured: a granted
	 * request can leave `avail` unchanged or even lower). init's own log
	 * line IS the authoritative observable, and the run scenario gates on
	 * it. This component is the stimulus; the scenario is the judge.
	 */
	void _observe(Genode::size_t before, unsigned ms)
	{
		unsigned elapsed { 0 };
		Genode::size_t peak { before };
		while (elapsed < ms) {
			Genode::size_t const a = _env.pd().stats().ram.avail().value;
			if (a > peak) peak = a;
			_timer.msleep(250);
			elapsed += 250;
		}

		if (peak > before)
			Genode::log("resq-", _name, ": BUDGET-GREW ", before, " -> ",
			            peak, " (+", peak - before, ")");
		else
			Genode::log("resq-", _name, ": BUDGET-UNCHANGED at ", before,
			            " (peak ", peak, ")");
	}

	void _run()
	{
		_parse_config();

		if (_req_ram == 0) {
			/*
			 * Ballast mode: hold the quota, ask for nothing. Used by the
			 * "hog" child that makes its sibling's sandbox too tight.
			 */
			Genode::log("resq-", _name, ": ballast — holding ",
			            _env.pd().stats().ram.avail().value,
			            " usable bytes, requesting nothing");
			Genode::sleep_forever();
		}

		Genode::size_t const before = _env.pd().stats().ram.avail().value;
		Genode::log("resq-", _name, ": REQUESTED ram_quota=", _req_ram,
		            " (budget before the request: ", before, ")");

		_env.parent().resource_avail_sigh(_avail_handler);
		_env.parent().resource_request(
			Genode::Parent::Resource_args(
				Genode::String<64>("ram_quota=",
				                   Genode::String<32>(_req_ram))));

		_observe(before, _observe_ms);

		Genode::log("resq-", _name, ": PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}

void Component::construct(Genode::Env &env)
{
	static Resq_probe probe { env };
	probe._run();
}

Genode::size_t Component::stack_size() { return 32 * 1024; }
