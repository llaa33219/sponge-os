/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_quick_pair — the per-provider quick-request / quick-result pair
 * (Criterion 18, the quick-menu change lifecycle of
 * docs/05-sponge-de.md §5.4).
 *
 * The panel and background quick menus change live settings through
 * their OWNING first-party provider, never through a configd write
 * channel of their own. The lifecycle is: the menu ensure-starts the
 * provider via sponge_pkgd (start-or-focus on the normal
 * `(package, settings-id)` path), then sends ONE closed domain
 * operation over this pair. The provider validates the operation
 * (sp_validate.h, the same gates its window uses) and performs the
 * configd write on its own ACL-limited label. The quick menu never
 * gets a configd write channel and this pair is NOT an arbitrary
 * key/value relay: the operation vocabulary is closed and owned by
 * each provider (a provider answers "unknown operation" to anything
 * outside it).
 *
 * Wire shape (labels mirror the Criterion 18 writer-table suffix
 * convention `<label>_config_request`, see key_domain_acl.cc):
 *
 *   request  (ROM, label "<label>_quick_request"):
 *     <quick_request op="height" value="40"/>          (flat key)
 *     <quick_request op="height" id="alpha" value="40"/> (per-id key)
 *     <quick_request op="reset"/>                       (no value)
 *
 *   result   (Report, label "<label>_quick_result"):
 *     <quick_result status="ok"    op="height" value="40"/>
 *     <quick_result status="error" op="height" error="..."/>
 *
 * `op` is a closed per-provider enum (never a config key). `id` is
 * the optional panel-instance selector for the Panel provider's
 * `panel.<id>.*` pattern keys; every other provider rejects it.
 *
 * Both ends are optional in a given scenario: when the quick ROM is
 * not routed (no quick menu in the sandbox) the provider logs once
 * and runs its window-only lifecycle unchanged. A provider is never
 * kept alive just to serve quick edits — the menu repeats
 * ensure-start after a provider exit (docs/05 §5.4 warm path).
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <os/reporter.h>
#include <util/reconstructible.h>
#include <util/string.h>

#include "sp_validate.h"

namespace Sponge::Settings {

/* Labels of one provider's quick pair. Static storage. */
struct Quick_labels
{
	char const *request_rom;   /* e.g. "panel_settings_quick_request" */
	char const *result_label;  /* e.g. "panel_settings_quick_result"  */
};


/*
 * One closed quick operation. `spec` is the value constraint (shared
 * with the window rows so both paths validate identically);
 * `needs_id` marks the Panel provider's per-instance operations.
 */
struct Quick_op
{
	char const *op;
	char const *key;      /* target configd key ("panel.height") */
	Value_spec spec;
	bool       needs_id;  /* accept an id= selector (panel.<id>.*) */
};


/*
 * Result disposition of one quick operation:
 *   OK       — validated and written (or already stored);
 *   ERROR    — recognized op, invalid value or refused write
 *              (`why` carries the bounded English text);
 *   UNKNOWN  — the op is outside this provider's closed vocabulary.
 */
enum class Quick_result { OK, ERROR, UNKNOWN };


/*
 * The pair itself. Parsing is deliberately trivial: one `<quick_request>`
 * document carrying one operation. Multi-operation batches do not
 * exist (a quick-menu action is one change).
 */
class Quick_pair
{
	private:

		Genode::Constructible<Genode::Attached_rom_dataspace> _req { };
		Genode::Constructible<Genode::Expanding_reporter>     _res { };

		char _op   [32]        { };
		char _id   [32]        { };
		char _value[VAL_MAX]   { };

		bool _live { false };

	public:

		static bool enabled(Genode::Env &env)
		{
			bool enabled = false;
			try {
				Genode::Attached_rom_dataspace const config(env, "config");
				config.node().with_optional_sub_node("settings",
					[&] (Genode::Node const &n) {
						enabled = n.attribute_value("quick",
						                  Genode::String<16>()) == "yes";
					});
			} catch (Genode::Xml_node::Invalid_syntax) {
				Genode::warning("settings provider: malformed config; "
				                "quick-menu edits disabled");
			}
			return enabled;
		}

		/*
		 * Open both ends only when the provider config opts in with
		 * <settings quick="yes"/>. Missing that opt-in leaves the
		 * provider window-only and opens no quick sessions.
		 */
		void open(Genode::Env &env, Quick_labels const &labels,
		          Genode::Signal_context_capability sigh)
		{
			if (!enabled(env))
				return;

			try {
				_req.construct(env, labels.request_rom);
				_req->sigh(sigh);
				_req->update();
			}
			catch (Genode::Rom_connection::Rom_connection_failed) {
				Genode::log("settings provider: quick-request ROM '",
				            labels.request_rom, "' not routed — "
				            "quick-menu edits unavailable");
			}
			catch (Genode::Service_denied) {
				Genode::log("settings provider: quick-request ROM '",
				            labels.request_rom, "' denied — "
				            "quick-menu edits unavailable");
			}

			try {
				_res.construct(env, "quick_result", labels.result_label);
			}
			catch (Genode::Service_denied) {
				Genode::warning("settings provider: quick-result label '",
				                labels.result_label,
				                "' denied — quick results dropped");
			}

			_live = _req.constructed() && _res.constructed();
		}

		bool live() const { return _live; }

		/*
		 * Feed the pending request (if any) to `handler` and answer
		 * on the quick-result label. The handler returns the
		 * disposition and fills `why` on ERROR. Invalid request
		 * documents answer with a bounded error, never a crash and
		 * never a silent drop.
		 */
		template <typename HANDLER>
		void serve(HANDLER &&handler)
		{
			if (!_req.constructed())
				return;

			_req->update();

			if (!_req->valid())
				return;

			_op[0] = 0;
			_id[0] = 0;
			_value[0] = 0;

			bool malformed = false;

			try {
				Genode::Xml_node const root = _req->xml();

				if (!root.has_type("quick_request")) {
					malformed = true;
				} else {
					Genode::String<32> const op =
						root.attribute_value("op", Genode::String<32>());
					Genode::String<32> const id =
						root.attribute_value("id", Genode::String<32>());
					Genode::String<VAL_MAX> const value =
						root.attribute_value("value", Genode::String<VAL_MAX>());

					copy(_op,    op.string(),    sizeof(_op));
					copy(_id,    id.string(),    sizeof(_id));
					copy(_value, value.string(), sizeof(_value));

					if (_op[0] == 0)
						malformed = true;
				}
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				malformed = true;
			}

			if (malformed) {
				_answer_error("", "malformed quick request "
				                  "(expected <quick_request op=... />)");
				return;
			}

			char why[WHY_MAX] { };
			Quick_result const result = handler(_op, _id, _value, why, WHY_MAX);

			switch (result) {
			case Quick_result::OK:
				_answer_ok(_op, _value);
				return;

			case Quick_result::ERROR:
				_answer_error(_op, why[0] ? why : "change refused");
				return;

			case Quick_result::UNKNOWN:
				_answer_error(_op, "unknown operation for this settings "
				                  "provider (its quick vocabulary is closed)");
				return;
			}
		}

	private:

		static void copy(char *dst, char const *src, unsigned cap)
		{
			unsigned i = 0;
			for (; src[i] && i + 1 < cap; ++i)
				dst[i] = src[i];
			dst[i] = 0;
		}

		void _answer_ok(char const *op, char const *value)
		{
			if (!_res.constructed())
				return;

			_res->generate_xml([&] (Genode::Xml_generator &g) {
				g.attribute("status", "ok");
				g.attribute("op",    op);
				g.attribute("value", value);
			});
		}

		void _answer_error(char const *op, char const *error)
		{
			if (!_res.constructed())
				return;

			_res->generate_xml([&] (Genode::Xml_generator &g) {
				g.attribute("status", "error");
				g.attribute("op",    op);
				g.attribute("error", error);
			});
		}
};

}  /* namespace Sponge::Settings */
