/* SPDX-License-Identifier: Apache-2.0
 *
 * panel_settings — the first-party Panel settings provider
 * (Criterion 18).
 *
 * Owns the `panel.*` key domain on its `panel_settings` configd
 * label (key_domain_acl.cc): panel.height, panel.position,
 * panel.visible_widgets, panel.ids, and the panel.<id>.* pattern
 * keys. This is the provider half of the legacy Panel tab
 * (sponge-de/settings/panel_tab.cc) — same validator semantics,
 * no universal writer.
 *
 * Window rows: height (16..128 px), position (top/bottom; left and
 * right stay deferred with an explicit bounded message per D16.2
 * honest disclosure), visible widgets (clock/launcher/tasklist
 * toggles), the panel.ids comma list, and per-instance rows
 * (panel.<id>.height/.position/.visible_widgets) rebuilt whenever
 * panel.ids changes.
 *
 * Quick vocabulary (panel_quick_request / panel_quick_result):
 *   <quick_request op="height"   value="40"/>          -> panel.height
 *   <quick_request op="position" value="top"/>         -> panel.position
 *   <quick_request op="widgets"  value="clock,launcher"/>
 *                                                        -> panel.visible_widgets
 *   ... the same three ops carry an optional id="alpha" selector
 *       and then target the panel.<id>.* pattern keys.
 */

#include "../common/sp_provider.h"

using namespace Sponge::Settings;

namespace {

	char const *const POSITIONS[] = { "top", "bottom" };
	char const *const WIDGETS[]   = { "clock", "launcher", "tasklist" };

	constexpr unsigned N_POSITIONS = 2;
	constexpr unsigned N_WIDGETS   = 3;

/* The legacy tab range (W2 panel.height validator). */
constexpr unsigned HEIGHT_MIN  = 16;
constexpr unsigned HEIGHT_MAX  = 128;
constexpr unsigned HEIGHT_STEP = 4;

enum { FLAT_ROWS = 4, MAX_IDS = 8 };

Value_spec height_spec()
{
	Value_spec s { };
	s.kind = Value_kind::UINT;
	s.min  = HEIGHT_MIN;
	s.max  = HEIGHT_MAX;
	return s;
}

Value_spec position_spec()
{
	Value_spec s { };
	s.kind     = Value_kind::ENUM;
	s.choices  = POSITIONS;
	s.n_choices = N_POSITIONS;
	return s;
}

Value_spec widgets_spec()
{
	Value_spec s { };
	s.kind     = Value_kind::LIST;
	s.choices  = WIDGETS;
	s.n_choices = N_WIDGETS;
	return s;
}

Value_spec ids_spec()
{
	Value_spec s { };
	s.validator = Value_spec::V_ID_CSV;
	return s;
}


struct Panel_provider : Provider
{
	Quick_op static const QUICK_OPS[3];

	Panel_provider(Genode::Env &env)
	:
		Provider(env, desc())
	{
		add_row(Row_kind::UINT,  "height",        "panel.height",
		        height_spec(), HEIGHT_STEP);
		add_row(Row_kind::ENUM,  "position",      "panel.position",
		        position_spec());
		add_row(Row_kind::TOGGLES, "widgets",     "panel.visible_widgets",
		        widgets_spec());
		add_row(Row_kind::TEXT,  "panel ids",     "panel.ids",
		        ids_spec());

		start();
	}

	static Provider_desc desc()
	{
		Provider_desc d { };
		d.name        = "panel_settings";
		d.settings_id = "panel";
		d.package     = "sponge-de";
		d.title       = "Panel";
		d.config      = Config_labels {
			"panel_settings",
			"panel_settings_config_request",
			"panel_settings_config_result" };
		d.quick       = Quick_labels {
			"panel_settings_quick_request",
			"panel_settings_quick_result" };
		d.quick_ops   = QUICK_OPS;
		d.n_quick_ops = 3;
		return d;
	}

	/* ---- panel.<id>.* rows (rebuilt whenever panel.ids changes) ---- */

	void _rebuild_per_id_rows()
	{
		/* drop the old per-id block */
		_n_rows = FLAT_ROWS;
		if (_selected >= _n_rows)
			_selected = _n_rows - 1;

		Row &ids_row = _rows[3];
		if (!comma_list_ids(ids_row.text, nullptr, 0))
			return;

		char const *list = ids_row.text;
		Genode::size_t const len = Genode::strlen(list);
		unsigned start = 0;
		unsigned n_ids = 0;

		while (start <= len && n_ids < MAX_IDS) {
			unsigned end = start;
			while (end < len && list[end] != ',')
				++end;

			char id[32] { };
			unsigned pos = 0;
			for (unsigned i = start; i < end && pos + 1 < sizeof(id); ++i)
				if (list[i] != ' ' && list[i] != '\t')
					id[pos++] = list[i];
			id[pos] = 0;

			if (id_token(id)) {
				char key[ROW_KEY_MAX] { };

				build_key(key, "panel.", id, ".height");
				Row &rh = add_row(Row_kind::UINT, "", key,
				                  height_spec(), HEIGHT_STEP);
				label_for(rh, id, " height");

				build_key(key, "panel.", id, ".position");
				Row &rp = add_row(Row_kind::ENUM, "", key, position_spec());
				label_for(rp, id, " position");

				build_key(key, "panel.", id, ".visible_widgets");
				Row &rw = add_row(Row_kind::TOGGLES, "", key, widgets_spec());
				label_for(rw, id, " widgets");

				++n_ids;
			}

			if (end == len)
				break;
			start = end + 1;
		}
	}

	static void build_key(char *out, char const *a, char const *b,
	                      char const *c)
	{
		unsigned pos = 0;
		pos = cat(out, pos, a, ROW_KEY_MAX);
		pos = cat(out, pos, b, ROW_KEY_MAX);
		pos = cat(out, pos, c, ROW_KEY_MAX);
	}

	static void label_for(Row &r, char const *id, char const *suffix)
	{
		char label[ROW_LABEL_MAX] { };
		unsigned pos = 0;
		pos = cat(label, pos, id, ROW_LABEL_MAX);
		pos = cat(label, pos, suffix, ROW_LABEL_MAX);
		row_set_label(r, label);
	}

	static unsigned cat(char *out, unsigned pos, char const *s, unsigned cap)
	{
		for (unsigned i = 0; s[i] && pos + 1 < cap; ++i)
			out[pos++] = s[i];
		out[pos] = 0;
		return pos;
	}

	/* two-phase broadcast sync: panel.ids first, values second */
	void on_broadcast_doc(Genode::Xml_node const &root) override
	{
		root.for_each_sub_node("key", [&] (Genode::Xml_node const &n) {
			Genode::String<ROW_KEY_MAX> const name =
				n.attribute_value("name", Genode::String<ROW_KEY_MAX>());
			if (Genode::strcmp(name.string(), "panel.ids") != 0)
				return;

			Genode::String<VAL_MAX> const value =
				n.attribute_value("value", Genode::String<VAL_MAX>());

			Row &ids_row = _rows[3];
			if (!ids_row.editing) {
				row_set_text(ids_row, value.string());
				row_sync_state_from_text(ids_row);
				row_set_applied(ids_row);
			}
			_rebuild_per_id_rows();
		});

		Provider::on_broadcast_doc(root);
	}

	/* ---- closed quick vocabulary ---- */

	Quick_result on_quick(char const *op, char const *id,
	                      char const *value, char *why,
	                      unsigned cap) override
	{
		char key[ROW_KEY_MAX] { };

		if (id[0] != 0 && !id_token(id)) {
			why_set(why, cap,
			        "id must use only a-z 0-9 _ - (1..16 characters)");
			return Quick_result::ERROR;
		}

		bool const per_id = id[0] != 0;

		if (Genode::strcmp(op, "height") == 0) {
			if (per_id)
				build_key(key, "panel.", id, ".height");
			else
				cat(key, 0, "panel.height", ROW_KEY_MAX);
			if (!write_value(key, height_spec(), value, why, cap))
				return Quick_result::ERROR;
			return Quick_result::OK;
		}

		if (Genode::strcmp(op, "position") == 0) {
			if (Genode::strcmp(value, "left") == 0 ||
			    Genode::strcmp(value, "right") == 0) {
				why_set(why, cap,
				        "left and right panel positions are not "
				        "available yet (deferred)");
				return Quick_result::ERROR;
			}
			if (per_id)
				build_key(key, "panel.", id, ".position");
			else
				cat(key, 0, "panel.position", ROW_KEY_MAX);
			if (!write_value(key, position_spec(), value, why, cap))
				return Quick_result::ERROR;
			return Quick_result::OK;
		}

		if (Genode::strcmp(op, "widgets") == 0) {
			if (per_id)
				build_key(key, "panel.", id, ".visible_widgets");
			else
				cat(key, 0, "panel.visible_widgets", ROW_KEY_MAX);
			if (!write_value(key, widgets_spec(), value, why, cap))
				return Quick_result::ERROR;
			return Quick_result::OK;
		}

		return Quick_result::UNKNOWN;
	}
};

Quick_op const Panel_provider::QUICK_OPS[3] = {
	{ "height",   "panel.height",          { }, true  },
	{ "position", "panel.position",        { }, true  },
	{ "widgets",  "panel.visible_widgets", { }, true  },
};

}  /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Panel_provider provider { env };
}
