/* SPDX-License-Identifier: Apache-2.0
 *
 * shortcuts_settings — the first-party Keyboard Shortcuts provider
 * (Criterion 18).
 *
 * Owns the `shortcuts.*` key domain on its `shortcuts_settings`
 * configd label: shortcuts.bindings, the structured multi-line value
 * ("action<TAB>key_sequence" per line; closed action enum
 * launcher/focus_next/dismiss). This is the provider half of the
 * legacy Shortcuts tab, keeping its reject-the-whole-write rule:
 * every change re-serializes the full table and one bad line refuses
 * the write with a bounded English error, leaving the store as it was.
 *
 * Window rows: one action row + one key-sequence row per binding,
 * plus add/remove actions. Quick vocabulary
 * (shortcuts_settings_quick_request / _quick_result):
 *   <quick_request op="bindings" value="launcher&#9;Super&#10;..."/>
 *                                          -> shortcuts.bindings
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

char const *const ACTIONS[] = { "launcher", "focus_next", "dismiss" };
constexpr unsigned N_ACTIONS = 3;

enum { FLAT_ROWS = 2, MAX_BINDINGS = 16 };

Value_spec action_spec()
{
	Value_spec s { };
	s.kind      = Value_kind::ENUM;
	s.choices   = ACTIONS;
	s.n_choices = N_ACTIONS;
	return s;
}

Value_spec sequence_spec()
{
	Value_spec s { };
	s.validator = Value_spec::V_KEYSEQ;
	return s;
}

Value_spec table_spec()
{
	Value_spec s { };
	s.validator = Value_spec::V_BINDINGS;
	return s;
}


struct Shortcuts_provider : Provider
{
	static Quick_op const QUICK_OPS[1];

	Shortcuts_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		_rebuild_rows(0);

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "shortcuts_settings";
		d.settings_id = "shortcuts";
		d.package     = "sponge-de";
		d.title       = "Keyboard Shortcuts";
		d.config      = Config_labels {
			"shortcuts_settings",
			"shortcuts_settings_config_request",
			"shortcuts_settings_config_result" };
		d.quick       = Quick_labels {
			"shortcuts_settings_quick_request",
			"shortcuts_settings_quick_result" };
		d.quick_ops   = QUICK_OPS;
		d.n_quick_ops = 1;
		return d;
	}

	unsigned _n_bindings { 0 };

	void _rebuild_rows(unsigned n_bindings)
	{
		clear_rows();

		char label[ROW_LABEL_MAX] { };

		for (unsigned i = 0; i < n_bindings; ++i) {
			num_label(label, i + 1, " action");
			Row &a = add_row(Row_kind::ENUM, label, "", action_spec());
			a.custom = true;

			num_label(label, i + 1, " key sequence");
			Row &k = add_row(Row_kind::TEXT, label, "", sequence_spec());
			k.custom = true;
		}

		add_row(Row_kind::ACTION, "add binding", "", Value_spec { }, 1)
			.custom = true;
		add_row(Row_kind::ACTION, "remove selected binding", "",
		        Value_spec { }, 1).custom = true;

		_n_bindings = n_bindings;
	}

	static void num_label(char *out, unsigned n, char const *suffix)
	{
		unsigned pos = 0;
		out[pos++] = char('0' + (n / 10) % 10);
		out[pos++] = char('0' + n % 10);
		for (unsigned i = 0; suffix[i] && pos + 1 < ROW_LABEL_MAX; ++i)
			out[pos++] = suffix[i];
		out[pos] = 0;
	}

	/* serialize the row pairs into the shortcuts.bindings wire value */
	bool _serialize(char *out, unsigned cap, char *why, unsigned wcap)
	{
		unsigned pos = 0;

		for (unsigned i = 0; i < _n_bindings; ++i) {
			Row const &a = _rows[2 * i];
			Row const &k = _rows[2 * i + 1];

			if (pos > 0 && pos + 1 < cap)
				out[pos++] = '\n';

			for (unsigned j = 0; a.text[j] && pos + 1 < cap; ++j)
				out[pos++] = a.text[j];
			if (pos + 1 < cap)
				out[pos++] = '\t';
			for (unsigned j = 0; k.text[j] && pos + 1 < cap; ++j)
				out[pos++] = k.text[j];
		}
		out[pos] = 0;

		return bindings_value(out, why, wcap);
	}

	bool on_apply(Row &r, char *why, unsigned cap) override
	{
		if (r.kind == Row_kind::ACTION) {
			if (Genode::strcmp(r.label, "add binding") == 0) {
				if (_n_bindings >= MAX_BINDINGS) {
					why_set(why, cap,
					        "binding limit reached (16)");
					return false;
				}
				unsigned const n = _n_bindings;
				_rebuild_rows(_n_bindings + 1);
				row_set_text(_rows[2 * n],     "launcher");
				row_sync_state_from_text(_rows[2 * n]);
				row_set_text(_rows[2 * n + 1], "Super");
				why_set(why, cap,
				        "binding added — edit it, then press Enter "
				        "to save the table");
				return false;   /* not yet stored; message is the status */
			}

			/* remove selected binding */
			unsigned pair = _selected / 2;
			if (_selected >= 2 * _n_bindings || _n_bindings == 0) {
				why_set(why, cap,
				        "select a binding first (Up/Down), then remove");
				return false;
			}

			char table[VAL_MAX] { };
			unsigned pos = 0;
			for (unsigned i = 0; i < _n_bindings; ++i) {
				if (i == pair)
					continue;
				Row const &a = _rows[2 * i];
				Row const &k = _rows[2 * i + 1];
				if (pos > 0 && pos + 1 < sizeof(table))
					table[pos++] = '\n';
				for (unsigned j = 0; a.text[j] && pos + 1 < sizeof(table); ++j)
					table[pos++] = a.text[j];
				if (pos + 1 < sizeof(table))
					table[pos++] = '\t';
				for (unsigned j = 0; k.text[j] && pos + 1 < sizeof(table); ++j)
					table[pos++] = k.text[j];
			}
			table[pos] = 0;

			if (_n_bindings == 1) {
				/* removing the last binding empties the table, which
				 * the registry rejects — say so instead of failing */
				why_set(why, cap,
				        "cannot remove the last binding (the table "
				        "must keep at least one)");
				return false;
			}

			if (!bindings_value(table, why, cap))
				return false;

			if (!write_value("shortcuts.bindings", table_spec(),
			                 table, why, cap))
				return false;

			_rebuild_rows(_n_bindings - 1);
			return true;
		}

		/* a row edit: re-serialize and write the whole table */
		char table[VAL_MAX] { };
		if (!_serialize(table, sizeof(table), why, cap))
			return false;

		return write_value("shortcuts.bindings", table_spec(),
		                   table, why, cap);
	}

	/* the table lives in ONE key: rebuild rows from its value */
	void on_broadcast_key(char const *key, char const *value) override
	{
		if (Genode::strcmp(key, "shortcuts.bindings") != 0) {
			Provider::on_broadcast_key(key, value);
			return;
		}

		for (unsigned i = 0; i < 2 * _n_bindings + FLAT_ROWS; ++i)
			if (i < _n_rows && _rows[i].editing)
				return;   /* an edit is in progress — keep the buffer */

		/* parse "action\tsequence" lines into row pairs */
		char actions[MAX_BINDINGS][32]  { };
		char sequences[MAX_BINDINGS][VAL_MAX] { };
		unsigned n = 0;

		Genode::size_t const length = Genode::strlen(value);
		unsigned line_start = 0;
		while (line_start < length && n < MAX_BINDINGS) {
			unsigned line_end = line_start;
			while (line_end < length && value[line_end] != '\n')
				++line_end;

			unsigned tab = line_start;
			while (tab < line_end && value[tab] != '\t')
				++tab;

			unsigned pos = 0;
			for (unsigned i = line_start; i < tab && pos + 1 < 32; ++i)
				actions[n][pos++] = value[i];
			actions[n][pos] = 0;

			pos = 0;
			for (unsigned i = tab + 1; i < line_end && pos + 1 < VAL_MAX; ++i)
				sequences[n][pos++] = value[i];
			sequences[n][pos] = 0;

			++n;
			line_start = line_end + 1;
		}

		_rebuild_rows(n);
		for (unsigned i = 0; i < n; ++i) {
			row_set_text(_rows[2 * i],     actions[i]);
			row_sync_state_from_text(_rows[2 * i]);
			row_set_applied(_rows[2 * i]);
			row_set_text(_rows[2 * i + 1], sequences[i]);
			row_sync_state_from_text(_rows[2 * i + 1]);
			row_set_applied(_rows[2 * i + 1]);
		}
	}
};

Quick_op const Shortcuts_provider::QUICK_OPS[1] = {
	{ "bindings", "shortcuts.bindings", { }, false },
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Shortcuts_provider provider { env };
}
