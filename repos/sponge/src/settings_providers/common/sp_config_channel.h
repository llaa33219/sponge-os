/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_config_channel — per-provider configd write channel
 * (Criterion 18 / D16.1).
 *
 * Every first-party provider writes its shared DE keys through ITS
 * OWN config request label, and sponge_configd enforces the
 * label-to-key-domain ACL on every set (sponge_configd/key_domain_acl.{h,cc};
 * `key-outside-domain` is refused there and never touches the store).
 * This class is the writer side of exactly one such label pair:
 *
 *   provider --[Report  <label>_config_request ]--> report_rom --> configd
 *   provider <--[ROM     <label>_config_result ]-- report_rom <-- configd
 *
 * The label names come from the Criterion 18 writer table in
 * sponge_configd/key_domain_acl.cc and MUST stay byte-identical to
 * it (the structural test pins them). The request/result XML shape
 * is configd's shared wire contract:
 *
 *   request: <request op="set" key="..." value="..."/>
 *   result:  <result status="ok"  op="set" key="..." value="..."/>
 *            <result status="error" op="set" key="..." error="..."
 *                    [reason="key-outside-domain"]/>
 *
 * Poll model: like Sponge::Backend::ReportRomClient (bounded ~6s,
 * 200ms settle + 60 x 100ms polls), synchronous on the caller's
 * thread. A provider applies one user action per call, so the
 * bounded block is the established write latency contract.
 *
 * Duplicate-request recovery: configd de-duplicates a `op|key|value`
 * request that is byte-identical to the channel's previous request
 * and answers such a duplicate silently (`_handle_request_impl`
 * signature check). A user can legitimately re-apply the same value,
 * so this class recovers instead of surfacing a phantom timeout:
 *   1. send `set`, poll for its result;
 *   2. on timeout send `get` and poll — if the stored value already
 *      equals the requested one, the write is a no-op success;
 *   3. otherwise re-send the `set` (the intervening `get` cleared
 *      configd's duplicate signature) and poll again;
 *   4. only a second timeout is reported as the bounded English
 *      "no result from configd" error.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <base/log.h>
#include <os/reporter.h>
#include <timer_session/connection.h>
#include <util/string.h>

#include "sp_validate.h"

namespace Sponge::Settings {

/*
 * Identity of one provider's configd channel. The strings are the
 * Criterion 18 writer-table labels (key_domain_acl.cc) — static
 * storage, referenced directly by the reporter/ROM constructors.
 */
struct Config_labels
{
	char const *writer;          /* ACL writer label, e.g. "panel_settings" */
	char const *request_rom;     /* e.g. "panel_settings_config_request" */
	char const *result_rom;      /* e.g. "panel_settings_config_result" */
};


class Config_channel
{
	private:

		Genode::Env &_env;

		Config_labels const _labels;

		Timer::Connection              _timer { _env };
		Genode::Expanding_reporter     _req   { _env, "request", _labels.request_rom };
		Genode::Attached_rom_dataspace _res   { _env, _labels.result_rom };

		static constexpr unsigned POLL_ITERS = 60;   /* x 100 ms */
		static constexpr unsigned SETTLE_US  = 200 * 1000;

		bool _result_matches(Genode::Xml_node const &r, char const *op,
		                     char const *key) const
		{
			try {
				if (!r.has_type("result"))
					return false;

				Genode::String<8> const rop =
					r.attribute_value("op", Genode::String<8>());
				Genode::String<VAL_MAX> const rkey =
					r.attribute_value("key", Genode::String<VAL_MAX>());

				return Genode::strcmp(rop.string(),  op)  == 0
				    && Genode::strcmp(rkey.string(), key) == 0;
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}

		/* Parse the current result document into the caller's fields. */
		static void _parse_result(Genode::Xml_node const &r,
		                         bool &ok, char *error, unsigned cap)
		{
			ok = true;
			why_clear(error, cap);

			try {
				Genode::String<8> const status =
					r.attribute_value("status", Genode::String<8>());

				if (Genode::strcmp(status.string(), "ok") == 0)
					return;

				ok = false;

				Genode::String<WHY_MAX> const err =
					r.attribute_value("error", Genode::String<WHY_MAX>());

				why_set(error, cap, err.string());
				if (error[0] == 0)
					why_set(error, cap, "configd refused the change");
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				ok = false;
				why_set(error, cap, "malformed result from configd");
			}
		}

		/*
		 * Poll until a result answers (op, key). Returns true when
		 * one arrived within the budget.
		 */
		bool _poll_result(char const *op, char const *key)
		{
			_timer.msleep(0);
			for (unsigned i = 0; i < POLL_ITERS; ++i) {
				_res.update();
				if (_res.valid() && _result_matches(_res.xml(), op, key))
					return true;
				_timer.msleep(100);
			}
			return false;
		}

		void _send(char const *op, char const *key, char const *value)
		{
			_req.generate_xml([&] (Genode::Xml_generator &g) {
				g.attribute("op",    op);
				g.attribute("key",   key);
				g.attribute("value", value);
			});
		}

		bool _current_value_is(char const *value)
		{
			try {
				Genode::String<VAL_MAX> const current =
					_res.xml().attribute_value("value",
					                           Genode::String<VAL_MAX>());
				return Genode::strcmp(current.string(), value) == 0;
			}
			catch (Genode::Xml_node::Invalid_syntax) {
				return false;
			}
		}

	public:

		Config_channel(Genode::Env &env, Config_labels const &labels)
		:
			_env(env), _labels(labels)
		{
			Genode::log("settings provider: config channel live on label '",
			            _labels.writer, "' -> ", _labels.request_rom,
			            " / ", _labels.result_rom);
		}

		char const *writer_label() const { return _labels.writer; }

		/*
		 * Apply one key/value write on this provider's label. On
		 * success the value is recorded; on failure `error` carries
		 * the bounded English reason (configd's structured error
		 * text, or a local transport diagnosis). Never throws.
		 */
		bool set(char const *key, char const *value,
		         char *error, unsigned cap)
		{
			why_clear(error, cap);

			_send("set", key, value);

			if (_poll_result("set", key)) {
				bool ok = false;
				_parse_result(_res.xml(), ok, error, cap);
				return ok;
			}

			/*
			 * No answer within the budget. Either configd silently
			 * de-duplicated the request (same value already stored)
			 * or the channel is not answering.
			 */
			_send("get", key, "");

			if (_poll_result("get", key)) {
				bool get_ok = false;
				char get_err[WHY_MAX] { };
				_parse_result(_res.xml(), get_ok, get_err, WHY_MAX);

				if (get_ok && _current_value_is(value)) {
					why_clear(error, cap);
					return true;   /* the requested value is stored */
				}
			}

			/*
			 * The value is not what we asked for: re-send the set.
			 * The intervening `get` cleared configd's duplicate
			 * signature, so this attempt is processed normally.
			 */
			_send("set", key, value);

			if (_poll_result("set", key)) {
				bool ok = false;
				_parse_result(_res.xml(), ok, error, cap);
				return ok;
			}

			why_set(error, cap,
			        "no result from configd (write may not have been applied)");
			return false;
		}
};

}  /* namespace Sponge::Settings */
