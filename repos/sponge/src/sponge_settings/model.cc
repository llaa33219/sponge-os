/* SPDX-License-Identifier: Apache-2.0
 *
 * model.cc — pure discovery model, settings-launch wire shapes, and
 * presentation layout for sponge_settings. See model.h for the
 * contracts (docs/12-package-format.md §14) and the purity rules.
 */

#include "model.h"

namespace Sponge::Settings {

using Genode::Cstring;
using Genode::Node;
using Genode::String;
using Genode::strcmp;


/* ===================== category enum ===================== */

char const *category_label(Category category)
{
	switch (category) {
	case Category::DESKTOP:      return "Desktop";
	case Category::HARDWARE:     return "Hardware";
	case Category::SYSTEM:       return "System";
	case Category::APPLICATIONS: return "Applications";
	case Category::OTHER:        return "Other";
	}
	return "Other";
}


Category category_from_attr(char const *value, bool *recognized)
{
	Category result = Category::OTHER;
	bool     ok     = true;

	     if (strcmp(value, "desktop") == 0)      result = Category::DESKTOP;
	else if (strcmp(value, "hardware") == 0)     result = Category::HARDWARE;
	else if (strcmp(value, "system") == 0)       result = Category::SYSTEM;
	else if (strcmp(value, "applications") == 0) result = Category::APPLICATIONS;
	else if (strcmp(value, "other") == 0)        result = Category::OTHER;
	else ok = false;

	if (recognized) *recognized = ok;
	return result;
}


/* ===================== helpers (anonymous) ===================== */

namespace {

bool str_empty(char const *s) { return s[0] == '\0'; }

bool valid_id_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
	    || c == '_' || c == '-';
}

/*
 * Small Output sink for reading <keyword> text content through
 * 'Node::Quoted_content' (entity-decoded, bounded).
 */
struct Capture : Genode::Output
{
	char     buf[64] { };
	unsigned len { 0 };

	void out_char(char c) override
	{
		if (len + 1 < sizeof(buf))
			buf[len++] = c;
	}

	char const *cstr()
	{
		buf[len] = '\0';
		return buf;
	}
};

} /* anonymous namespace */


/* ===================== discovery model ===================== */

void Model::clear()
{
	num_entries         = 0;
	dropped_invalid     = 0;
	dropped_duplicate   = 0;
	dropped_overflow    = 0;
	category_fallbacks  = 0;
}


Entry const *Model::find(char const *pkg, char const *id) const
{
	for (unsigned i = 0; i < num_entries; ++i)
		if (strcmp(entries[i].pkg.string(), pkg) == 0 &&
		    strcmp(entries[i].id.string(),  id)  == 0)
			return &entries[i];
	return nullptr;
}


void Model::parse(Node const &installed_root)
{
	clear();

	installed_root.with_optional_sub_node("packages", [&] (Node const &packages) {
		packages.for_each_sub_node("package", [&] (Node const &pkg_node) {

			String<64> const pkg =
				pkg_node.attribute_value("name", String<64>());

			if (str_empty(pkg.string()))
				return;

			pkg_node.for_each_sub_node("settings", [&] (Node const &settings) {
				_parse_entry(pkg, settings);
			});
		});
	});
}


/*
 * ADAPTER SEAM (discovery shape): consumes one <settings> element of
 * the §14.2 schema exactly as carried inside the installed broadcast.
 * Validation here is display-side defense in depth — sponge_pkgd owns
 * the §14.3 hard rejections; this shell only refuses to render what it
 * could not address.
 */
void Model::_parse_entry(String<64> const &pkg, Node const &node)
{
	/* bounded first: nothing past the cap is stored, ever */
	if (num_entries >= MAX_ENTRIES) {
		dropped_overflow++;
		return;
	}

	/*
	 * The identity is '(package, settings-id)' (§14.4), so the id is
	 * read over the raw attribute bytes and validated to the exact
	 * §14.2 bound '[a-z0-9_-]{1,32}' — a silently truncated id would
	 * corrupt the identity sent on the launch channel.
	 */
	char     id_buf[33] { };
	unsigned id_len = 0;
	bool     id_ok  = false;

	node.for_each_attribute([&] (Node::Attribute const &attr) {
		if (strcmp(attr.name.string(), "id") != 0)
			return;
		if (attr.value.num_bytes == 0 || attr.value.num_bytes > 32)
			return;
		for (unsigned i = 0; i < attr.value.num_bytes; ++i)
			if (!valid_id_char(attr.value.start[i]))
				return;
		for (unsigned i = 0; i < attr.value.num_bytes; ++i)
			id_buf[i] = attr.value.start[i];
		id_len = (unsigned)attr.value.num_bytes;
		id_ok  = true;
	});

	if (!id_ok) {
		dropped_invalid++;
		return;
	}
	id_buf[id_len] = '\0';

	String<64> const title  = node.attribute_value("title",  String<64>());
	String<64> const target = node.attribute_value("target", String<64>());

	/* §14.2: title and target are required; the shell cannot open an
	 * entry it cannot name or that pkgd could not have validated. */
	if (str_empty(title.string()) || str_empty(target.string())) {
		dropped_invalid++;
		return;
	}

	/* the (package, settings-id) pair must be unique (§14.1) */
	if (find(pkg.string(), id_buf)) {
		dropped_duplicate++;
		return;
	}

	Entry &e = entries[num_entries];
	e.pkg    = pkg;
	e.id     = String<33>(Cstring(id_buf, id_len));
	e.title  = title;
	e.target = target;

	/* absent or unknown category warns and falls back to 'other'
	 * (§14.2, the <kind> typo rule of §4.5) */
	String<16> const cat_attr =
		node.attribute_value("category", String<16>());
	bool recognized = false;
	e.category = category_from_attr(cat_attr.string(), &recognized);
	if (!recognized)
		category_fallbacks++;

	e.icon = node.attribute_value("icon", String<64>());

	node.for_each_sub_node("keyword", [&] (Node const &keyword) {
		if (e.num_keywords >= Entry::MAX_KEYWORDS)
			return;
		Capture capture;
		Node::Quoted_content { keyword }.print(capture);
		String<Entry::KEYWORD_CHARS> const term(Cstring(capture.buf, capture.len));
		if (str_empty(term.string()))
			return;
		e.keywords[e.num_keywords] = term;
		e.num_keywords++;
	});

	num_entries++;
}


/* ===================== launch channel wire shapes ===================== */

char const *status_text(Launch_status status)
{
	switch (status) {
	case Launch_status::OK_STARTED:
		return "Provider opened";
	case Launch_status::OK_FOCUSED:
		return "Provider focused (already open)";
	case Launch_status::NOT_INSTALLED:
		return "Not installed (package or entry was removed)";
	case Launch_status::BINARY_MISSING:
		return "Provider binary missing from the image";
	case Launch_status::INSUFFICIENT_RESOURCES:
		return "Not enough resources to start the provider";
	case Launch_status::PACKAGE_MANAGER_UNAVAILABLE:
		return "Package manager unavailable";
	case Launch_status::UNKNOWN:
		break;
	}
	return "Unexpected result from the package manager";
}


/*
 * ADAPTER SEAM (request shape, docs/12 §14.5). Emits the attributes
 * of the <request> root node generated on the shell's
 * "settings_request" report label. Identity only: no command string,
 * no target binary, no seq. 'id' is the primary identity attribute
 * (the metadata vocabulary); the docs prose form 'settings-id' is an
 * accepted alias on the pkgd side.
 */
void gen_settings_request(Genode::Xml_generator &g,
                          char const *pkg, char const *settings_id)
{
	g.attribute("op",  "launch");
	g.attribute("target", "settings");
	g.attribute("pkg", pkg);
	g.attribute("id", settings_id);
}


/*
 * ADAPTER SEAM (result shape, docs/12 §14.7). The §14.7 statuses are
 * §9.2.1 statuses reused verbatim behind the target="settings"
 * discriminator; 'ok' carries action="started" or action="focused".
 */
bool parse_settings_result(Node const &root,
                           char const *pkg, char const *settings_id,
                           Launch_result &out)
{
	if (!root.has_type("result"))
		return false;

	String<32> const op = root.attribute_value("op", String<32>());
	if (!str_empty(op.string()) && strcmp(op.string(), "launch") != 0)
		return false;

	String<16> const target = root.attribute_value("target", String<16>());
	if (!str_empty(target.string()) && strcmp(target.string(), "settings") != 0)
		return false;

	String<64> const res_pkg = root.attribute_value("pkg", String<64>());
	if (!str_empty(res_pkg.string()) && strcmp(res_pkg.string(), pkg) != 0)
		return false;

	/*
	 * Identity attribute: 'id' is primary (the metadata vocabulary and
	 * the pkgd result shape), 'settings-id' the docs-prose alias. A
	 * present-and-different identity is not our answer; absent means
	 * the document answers the single pending open.
	 */
	String<33> const res_id = root.attribute_value("id", String<33>());
	String<33> const res_id_alias =
		root.attribute_value("settings-id", String<33>());

	char const *const res_id_str = !str_empty(res_id.string())
	                             ? res_id.string()
	                             : res_id_alias.string();

	if (!str_empty(res_id_str) && strcmp(res_id_str, settings_id) != 0)
		return false;

	out.raw_status = root.attribute_value("status", String<32>());
	out.raw_action = root.attribute_value("action", String<16>());

	char const *const status = out.raw_status.string();

	if (strcmp(status, "ok") == 0) {
		/* 'action="focused"' is the singleton raise (§14.7); every
		 * other 'ok' answer counts as a fresh start */
		out.status = (strcmp(out.raw_action.string(), "focused") == 0)
		           ? Launch_status::OK_FOCUSED
		           : Launch_status::OK_STARTED;
	} else if (strcmp(status, "not-installed") == 0) {
		out.status = Launch_status::NOT_INSTALLED;
	} else if (strcmp(status, "binary-missing") == 0) {
		out.status = Launch_status::BINARY_MISSING;
	} else if (strcmp(status, "insufficient-resources") == 0) {
		out.status = Launch_status::INSUFFICIENT_RESOURCES;
	} else {
		out.status = Launch_status::UNKNOWN;
	}

	return true;
}


/* ===================== presentation layout ===================== */

unsigned Layout::build_rows(Model const &model, Row *rows, unsigned max_rows)
{
	unsigned n = 0;
	int      y = 0;

	for (unsigned c = 0; c < CATEGORY_COUNT; ++c) {

		Category const category = (Category)c;

		bool header = false;

		for (unsigned i = 0; i < model.num_entries; ++i) {

			if (model.entries[i].category != category)
				continue;

			if (!header) {
				if (n < max_rows)
					rows[n] = Row { Row::Kind::CATEGORY, category, 0, y, HEADER_H };
				n++;
				y += HEADER_H;
				header = true;
			}

			if (n < max_rows)
				rows[n] = Row { Row::Kind::ENTRY, category, i, y, ENTRY_H };
			n++;
			y += ENTRY_H;
		}
	}

	return n <= max_rows ? n : max_rows;
}


int Layout::content_height(Model const &model)
{
	int h = 0;

	for (unsigned c = 0; c < CATEGORY_COUNT; ++c) {

		bool header = false;

		for (unsigned i = 0; i < model.num_entries; ++i) {

			if (model.entries[i].category != (Category)c)
				continue;

			if (!header) {
				h += HEADER_H;
				header = true;
			}
			h += ENTRY_H;
		}
	}
	return h;
}


int Layout::clamp_scroll(Model const &model, int scroll, int list_h)
{
	int const max_scroll = content_height(model) - list_h;

	if (max_scroll <= 0 || scroll < 0)
		return 0;
	if (scroll > max_scroll)
		return max_scroll;
	return scroll;
}


int Layout::hit_entry(Model const &model, int scroll, int list_h, int y)
{
	if (y < 0 || y >= list_h)
		return -1;

	int const content_y = y + scroll;

	Row rows[MAX_ROWS];
	unsigned const n = build_rows(model, rows, MAX_ROWS);

	for (unsigned r = 0; r < n; ++r) {
		Row const &row = rows[r];
		if (content_y >= row.y && content_y < row.y + row.h)
			return row.kind == Row::Kind::ENTRY ? (int)row.entry : -1;
	}
	return -1;
}

} /* namespace Sponge::Settings */
