/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * sponge_configd — configuration backend daemon (Phase 5a).
 *
 * A long-lived, signal-driven Genode component. It watches a
 * "config_request" ROM (relayed by report_rom from vct's request
 * report), validates the requested key/value against a known-key
 * registry, applies it to an in-memory flat dotted key-value store,
 * and answers vct through a "config_result" report that report_rom
 * relays back as a ROM. This Report/ROM channel is the settled
 * vct<->backend design (docs/04-components.md §5); there is no RPC
 * stub or IDL.
 *
 * A second Expanding_reporter ("config") broadcasts the whole store as
 * a ROM, regenerated on every successful set (and once at startup with
 * the defaults). Future watchers (sponge_themed, sponge-de) read this
 * ROM to react to config changes without issuing requests.
 *
 * Operations:
 *   - get:  return one key's value (error if the key is unknown).
 *   - set:  validate + store one key/value, regenerate the broadcast
 *           (error if the key is unknown or the value is invalid).
 *   - list: enumerate every known key/value, name-sorted.
 *
 * The store is flat and dotted (theme.active, panel.position). Keys are
 * a closed registry: an unknown key is a structured error, never a
 * silent write. The registry carries each key's type so set can reject
 * an out-of-range enum before touching the store.
 *
 * Determinism: list and the broadcast emit keys in name-sorted order
 * with a fixed attribute order and no volatile fields, so a watcher's
 * config-diff is stable across unrelated sets.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Configd {

class Main;

namespace { }

}  /* namespace Sponge::Configd */


/* ===================== key registry ===================== */

/*
 * The known-key registry. A configuration key is valid only if it
 * appears here; everything else is rejected with a structured error.
 * This keeps the store closed and inspectable (AGENTS.md §1.2 — no
 * hidden global state, parent config is explicit).
 *
 * Phase 5a keys:
 *   - theme.active    : free-form string (a theme name).
 *   - panel.position  : enum {top, bottom, left, right}.
 *
 * The registry is declared in name-sorted order; the output generators
 * also sort defensively (selection sort, like sponge_pkgd) so adding a
 * key out of order cannot perturb a watcher's config-diff.
 */
class Sponge::Configd::Main
{
	public:

		explicit Main(Genode::Env &env);

	private:

		static constexpr unsigned MAX_KEYS  = 16;
		static constexpr unsigned MAX_ENUMS = 8;

		struct Key_def
		{
			char const *name;
			bool        is_enum;
			char const *enum_values[MAX_ENUMS];
			unsigned    num_enums;
			char const *default_value;
		};

		/* Known keys, name-sorted. */
		static Key_def const _registry[MAX_KEYS];
		static unsigned const _num_keys;

		Genode::Env &_env;

		/* Request ROM: report_rom relays vct's "config_request" report. */
		Genode::Attached_rom_dataspace _request_rom { _env, "config_request" };

		/* Result report: report_rom relays this to vct's "config_result" ROM. */
		Genode::Expanding_reporter _result_reporter { _env, "result", "config_result" };

		/*
		 * Broadcast report: report_rom relays this as a "config" ROM so
		 * watchers (sponge_themed, sponge-de) read the full store. It is
		 * regenerated on every successful set; an initial report with the
		 * defaults is emitted in the constructor before any watcher can
		 * request it.
		 */
		Genode::Expanding_reporter _broadcast_reporter { _env, "config", "config" };

		Genode::Signal_handler<Main> _request_handler {
			_env.ep(), *this, &Main::_handle_request };

		/*
		 * De-duplication of the request ROM. ROM signals can fire more
		 * than once for the same content, and a repeated set with an
		 * identical signature is a no-op (the value is already stored).
		 * Skipping it avoids regenerating an identical broadcast and
		 * lets vct observe the already-correct result. The signature is
		 * op|key|value.
		 */
		Genode::String<320> _last_request_sig { };

		/* Current value per registry index. Defaults are applied in the
		 * constructor so the store is never empty. */
		Genode::String<128> _values[MAX_KEYS] { };

		/* ---- request handling ---- */
		void _handle_request();
		void _do_get(char const *key);
		void _do_set(char const *key, char const *value);
		void _do_list();

		/* ---- registry helpers ---- */
		bool _find_key(char const *key, unsigned &idx) const;
		bool _value_valid(unsigned idx, char const *value,
		                  Genode::String<256> &why) const;

		/* ---- deterministic name-sorted index order ---- */
		void _sorted_order(unsigned *order) const;

		/* ---- output ---- */
		void _generate_broadcast();
		void _report_get_ok(char const *key, char const *value);
		void _report_set_ok(char const *key, char const *value);
		void _report_list_ok();
		void _report_error(char const *op, char const *key, char const *message);
};


Sponge::Configd::Main::Key_def const Sponge::Configd::Main::_registry[MAX_KEYS] = {
	{ "leitzentrale.enabled", true,
	  { "true", "false" }, 2, "false" },
	{ "panel.position", true,
	  { "top", "bottom", "left", "right" }, 4, "bottom" },
	{ "theme.active",   false,
	  { }, 0, "light" },
};

unsigned const Sponge::Configd::Main::_num_keys = 3;


/* ===================== registry helpers ===================== */

bool Sponge::Configd::Main::_find_key(char const *key, unsigned &idx) const
{
	for (unsigned i = 0; i < _num_keys; ++i)
		if (Genode::strcmp(_registry[i].name, key) == 0) {
			idx = i;
			return true;
		}
	return false;
}


/*
 * Validate a candidate value for registry index `idx`. Returns true when
 * the value is acceptable; otherwise false and `why` carries a precise
 * reason for the structured error.
 *
 *   - No key accepts an empty value (a config value must be meaningful).
 *   - Enum keys reject anything outside their declared set.
 *   - Free-form (string) keys accept any non-empty value.
 */
bool Sponge::Configd::Main::_value_valid(unsigned idx, char const *value,
                                         Genode::String<256> &why) const
{
	if (Genode::strcmp(value, "") == 0) {
		why = Genode::String<256>("empty value for key '", _registry[idx].name, "'");
		return false;
	}

	Key_def const &d = _registry[idx];

	if (!d.is_enum)
		return true;

	for (unsigned i = 0; i < d.num_enums; ++i)
		if (Genode::strcmp(value, d.enum_values[i]) == 0)
			return true;

	/* Build "expected: a, b, c" for the error message. */
	char buf[128] { "expected: " };
	Genode::size_t pos = Genode::strlen(buf);
	for (unsigned i = 0; i < d.num_enums; ++i) {
		if (i > 0 && pos + 2 < sizeof(buf)) {
			buf[pos++] = ',';
			buf[pos++] = ' ';
		}
		char const *s = d.enum_values[i];
		while (*s && pos + 1 < sizeof(buf))
			buf[pos++] = *s++;
	}
	buf[pos] = 0;

	why = Genode::String<256>("invalid value '", Genode::String<128>(value),
	                          "' for key '", Genode::String<64>(d.name),
	                          "' (", Genode::String<128>(buf), ")");
	return false;
}


void Sponge::Configd::Main::_sorted_order(unsigned *order) const
{
	for (unsigned i = 0; i < _num_keys; ++i) order[i] = i;
	for (unsigned i = 0; i < _num_keys; ++i) {
		unsigned best { i };
		for (unsigned j = i + 1; j < _num_keys; ++j)
			if (Genode::strcmp(_registry[order[j]].name,
			                   _registry[order[best]].name) < 0)
				best = j;
		if (best != i) {
			unsigned tmp = order[i];
			order[i] = order[best];
			order[best] = tmp;
		}
	}
}


/* ===================== request handling ===================== */

void Sponge::Configd::Main::_handle_request()
{
	_request_rom.update();

	if (!_request_rom.valid())
		return;

	try {
		Genode::Xml_node const req = _request_rom.xml();

		if (!req.has_type("request")) {
			_report_error("get", "", "request root is not <request>");
			return;
		}

		Genode::String<32>  const op    = req.attribute_value("op",
		                                              Genode::String<32>());
		Genode::String<128> const key   = req.attribute_value("key",
		                                              Genode::String<128>());
		Genode::String<128> const value = req.attribute_value("value",
		                                              Genode::String<128>());

		Genode::String<320> const sig(op, "|", key, "|", value);
		if (sig == _last_request_sig)
			return;
		_last_request_sig = sig;

		if (Genode::strcmp(op.string(), "list") == 0) {
			_do_list();
			return;
		}

		if (Genode::strcmp(key.string(), "") == 0) {
			_report_error(op.string(), "", "no key specified in request");
			return;
		}

		if (Genode::strcmp(op.string(), "get") == 0) {
			_do_get(key.string());
			return;
		}
		if (Genode::strcmp(op.string(), "set") == 0) {
			_do_set(key.string(), value.string());
			return;
		}

		_report_error(op.string(), key.string(), "unknown operation");
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("get", "", "malformed request ROM");
	}
}


void Sponge::Configd::Main::_do_get(char const *key)
{
	unsigned idx { 0 };
	if (!_find_key(key, idx)) {
		_report_error("get", key,
		              Genode::String<128>("unknown key: ", key).string());
		return;
	}

	_report_get_ok(key, _values[idx].string());
}


void Sponge::Configd::Main::_do_set(char const *key, char const *value)
{
	unsigned idx { 0 };
	if (!_find_key(key, idx)) {
		_report_error("set", key,
		              Genode::String<128>("unknown key: ", key).string());
		return;
	}

	Genode::String<256> why { };
	if (!_value_valid(idx, value, why)) {
		_report_error("set", key, why.string());
		return;
	}

	_values[idx] = Genode::String<128>(value);

	/* Regenerate the broadcast so every watcher sees the new store. */
	_generate_broadcast();

	_report_set_ok(key, value);
}


void Sponge::Configd::Main::_do_list()
{
	_report_list_ok();
}


/* ===================== output generation ===================== */

/*
 * Emit the full store as <config><key name="..." value="..."/></config>,
 * name-sorted. The root is "config" (the reporter's node type) so a
 * watcher reads it as a standard config ROM. Deterministic: fixed
 * attribute order, no volatile fields.
 */
void Sponge::Configd::Main::_generate_broadcast()
{
	unsigned order[MAX_KEYS] { };
	_sorted_order(order);

	_broadcast_reporter.generate_xml([&](Genode::Xml_generator &g) {
		for (unsigned n = 0; n < _num_keys; ++n) {
			unsigned const i = order[n];
			g.node("key", [&] {
				g.attribute("name",  Genode::String<64>(_registry[i].name));
				g.attribute("value", _values[i]);
			});
		}
	});
}


void Sponge::Configd::Main::_report_get_ok(char const *key, char const *value)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "get");
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("value",  Genode::String<128>(value));
	});
}


void Sponge::Configd::Main::_report_set_ok(char const *key, char const *value)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "set");
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("value",  Genode::String<128>(value));
	});
}


void Sponge::Configd::Main::_report_list_ok()
{
	unsigned order[MAX_KEYS] { };
	_sorted_order(order);

	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "list");
		g.attribute("count",  _num_keys);

		g.node("keys", [&] {
			for (unsigned n = 0; n < _num_keys; ++n) {
				unsigned const i = order[n];
				g.node("key", [&] {
					g.attribute("name",  Genode::String<64>(_registry[i].name));
					g.attribute("value", _values[i]);
				});
			}
		});
	});
}


void Sponge::Configd::Main::_report_error(char const *op, char const *key,
                                          char const *message)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "error");
		g.attribute("op",     op);
		g.attribute("key",    Genode::String<128>(key));
		g.attribute("error",  message);
	});
}


/* ===================== component wiring ===================== */

Sponge::Configd::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_configd: ready");

	/* Apply registry defaults so the store is never empty and the
	 * initial broadcast reflects a usable configuration. */
	for (unsigned i = 0; i < _num_keys; ++i)
		_values[i] = Genode::String<128>(_registry[i].default_value);

	/* Publish the default store before any watcher can request it
	 * (init starts children in config order). */
	_generate_broadcast();

	_request_rom.sigh(_request_handler);
	_request_rom.update();

	/* Process a request that arrived before the signal handler was wired. */
	_handle_request();
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Configd::Main main { env };
}


/* Carries request-handling state on the stack; keep it comfortable. */
Genode::size_t Component::stack_size() { return 32 * 1024 * sizeof(Genode::addr_t); }
