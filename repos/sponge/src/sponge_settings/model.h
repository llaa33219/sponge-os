/* SPDX-License-Identifier: Apache-2.0
 *
 * model.h — pure discovery model, settings-launch wire shapes, and
 * presentation layout for sponge_settings (Criterion 18).
 *
 * Discovery contract: docs/12-package-format.md §14.4 — sponge_pkgd
 * publishes each installed package's <settings> entries as part of its
 * installed metadata (the "installed" broadcast), and watchers refresh
 * on that ROM's update signal. This model re-parses the whole broadcast
 * on every update (the document is small and last-state-wins).
 *
 * Launch contract: docs/12-package-format.md §14.5 — requests and
 * results reuse the §9.2.1 launch machinery plus a target="settings"
 * discriminator and the settings-id. The two wire-shape seams
 * ('gen_settings_request' and 'parse_settings_result') are the ONLY
 * places that know the channel encoding: if the pkgd-side settings
 * implementation (in flight) settles on different attribute names,
 * adapt them here and nowhere else.
 *
 * ADAPTER SEAM (discovery shape): this parser consumes
 *
 *   <installed ...>
 *     <packages>
 *       <package name="..." ...>
 *         <settings id="..." title="..." category="..."
 *                   icon="..." target="...">
 *           <keyword>...</keyword>
 *         </settings>
 *       </package>
 *     </packages>
 *   </installed>
 *
 * i.e. the §14.2 <settings> fields "carried per entry" inside the
 * §9.2.1 installed broadcast. If pkgd publishes the entries under a
 * different node path, only 'Model::parse' changes.
 *
 * PURITY CONTRACT (same as <sponge/pkg_config.h> and
 * <sponge/wm_protocol.h>): no Genode::Env, no Report/ROM/File_system
 * sessions, no hardware access, no exceptions, no RTTI-dependent
 * design, no mutable global state, no dynamic allocation. The only
 * Genode headers used are the public <base/node.h>, <base/output.h>,
 * <util/string.h>, and <util/xml_generator.h>. The deterministic cases
 * in tests/ execute this code on the host.
 */

#pragma once

#include <base/node.h>
#include <base/output.h>
#include <util/string.h>
#include <util/xml_generator.h>

namespace Sponge::Settings {


/*
 * Closed category enum (docs/12 §14.2). An absent or unknown
 * attribute value falls back to OTHER (the <kind> typo rule, §4.5)
 * and is counted in 'Model::category_fallbacks'.
 */
enum class Category { DESKTOP, HARDWARE, SYSTEM, APPLICATIONS, OTHER };

static constexpr unsigned CATEGORY_COUNT = 5;

/* Closed-enum display labels (English, user-facing). */
char const *category_label(Category category);

/*
 * Parse the closed enum from a wire value. Never fails: anything
 * unknown or absent maps to 'Category::OTHER'. '*recognized' (optional)
 * reports whether the wire value named a known category.
 */
Category category_from_attr(char const *value, bool *recognized = nullptr);


/*
 * One <settings> entry: the §14.2 fields minus what the shell must
 * never act on. 'target' is retained for display/diagnostics ONLY —
 * the launch path is identity-only ('(package, settings-id)'), and the
 * shell never resolves, opens, or interprets the target binary.
 */
struct Entry
{
	static constexpr unsigned MAX_KEYWORDS  = 4;
	static constexpr unsigned KEYWORD_CHARS = 24;

	Genode::String<64> pkg { };       /* owning package name         */
	Genode::String<33> id  { };       /* [a-z0-9_-]{1,32}, §14.2     */
	Genode::String<64> title { };
	Category           category { Category::OTHER };
	Genode::String<64> icon { };      /* empty: no icon declared     */
	Genode::String<64> target { };    /* informational only          */

	Genode::String<KEYWORD_CHARS> keywords[MAX_KEYWORDS] { };
	unsigned num_keywords { 0 };

	bool has_icon() const { return icon.string()[0] != '\0'; }
};


/*
 * Installed-set discovery model. Fixed capacity with bounded,
 * counted drops — the listing surface never grows without limit and
 * every dropped entry is observable through the counters.
 */
struct Model
{
	static constexpr unsigned MAX_ENTRIES = 64;

	Entry    entries[MAX_ENTRIES] { };
	unsigned num_entries { 0 };

	/* bounded diagnostics (surfaced via logs, never unbounded state) */
	unsigned dropped_invalid   { 0 }; /* missing/ malformed id|title|target */
	unsigned dropped_duplicate { 0 }; /* repeated (package, settings-id)     */
	unsigned dropped_overflow  { 0 }; /* beyond MAX_ENTRIES                  */
	unsigned category_fallbacks { 0 }; /* absent/unknown category on kept entries */

	void clear();

	/*
	 * Full reparse of the installed broadcast ('installed' ROM root).
	 * Resetting and rebuilding is the refresh path: no diffing, no
	 * incremental state that a removal could leave behind.
	 */
	void parse(Genode::Node const &installed_root);

	/* Look up by the stable identity pair; nullptr when absent. */
	Entry const *find(char const *pkg, char const *id) const;

	private:

		/* Parse one <settings> element into the set (display-side
		 * validation only; §14.3 hard rejections belong to pkgd). */
		void _parse_entry(Genode::String<64> const &pkg,
		                  Genode::Node const &settings);
};


/* ===================== settings launch channel =====================
 *
 * Wire shape (docs/12 §14.5 — "the existing launch machinery plus a
 * target="settings" discriminator and the settings-id"), carried on
 * the shell's OWN single-writer labels:
 *
 *   request  (Report, label "settings_request"):
 *     <request op="launch" target="settings"
 *              pkg="sponge-de" id="panel"/>
 *
 *   result   (ROM, label "settings_result"):
 *     <result status="ok" op="launch" target="settings"
 *             pkg="sponge-de" id="panel" action="started"/>
 *
 * 'id' carries the settings-id — the primary identity attribute (the
 * metadata vocabulary); the docs-prose 'settings-id' is accepted as
 * an alias on results. 'action' is "started" (no live instance) or
 * "focused" (singleton raised); refusal statuses reuse the §9.2.1
 * lexemes verbatim: "not-installed", "binary-missing",
 * "insufficient-resources". "package-manager-unavailable" (§14.7)
 * never arrives on the wire — the shell derives it when the channel
 * cannot be opened or the bounded result wait expires.
 *
 * The request carries NO command string and NO seq (matching the
 * sponge-de launcher precedent); pkgd answers duplicates from its
 * request cache (§9.2.1).
 */

enum class Launch_status {
	OK_STARTED,
	OK_FOCUSED,
	NOT_INSTALLED,
	BINARY_MISSING,
	INSUFFICIENT_RESOURCES,
	PACKAGE_MANAGER_UNAVAILABLE,
	UNKNOWN
};

struct Launch_result
{
	Launch_status       status { Launch_status::UNKNOWN };
	Genode::String<32>  raw_status { };
	Genode::String<16>  raw_action { };
};

/* Bounded English surface text for one status (user-facing). */
char const *status_text(Launch_status status);

/* Emit the request attributes on the reporter's <request> root node. */
void gen_settings_request(Genode::Xml_generator &g,
                          char const *pkg, char const *settings_id);

/*
 * Parse one <result> document of the settings channel. Returns true
 * when the document answers a pending open of (pkg, settings_id) —
 * an identity mismatch or a foreign document returns false and must
 * be ignored by the caller. Attribute presence is tolerated: a result
 * without 'pkg'/'id' is accepted as the answer to the single pending
 * open (there is at most one in flight). The identity attribute is
 * 'id' (primary) or 'settings-id' (alias).
 */
bool parse_settings_result(Genode::Node const &root,
                           char const *pkg, char const *settings_id,
                           Launch_result &out);


/* ===================== presentation layout (pure) =====================
 *
 * Row geometry of the listing. Pure functions over the model so the
 * host self-test can pin the grouping order (closed enum order),
 * content height, scrolling clamp, and click hit-testing without a
 * pixel in sight.
 */

namespace Layout {

enum {
	MARGIN    = 14,
	HEADER_H  = 30,
	ENTRY_H   = 42,
	STATUS_H  = 38,
	ICON_BOX  = 14,
	SCROLL_STEP = 48
};

struct Row
{
	enum class Kind { CATEGORY, ENTRY };

	Kind     kind { Kind::CATEGORY };
	Category category { Category::OTHER }; /* Kind::CATEGORY       */
	unsigned entry { 0 };                  /* Kind::ENTRY -> index */
	int      y { 0 };
	int      h { 0 };
};

static constexpr unsigned MAX_ROWS = Model::MAX_ENTRIES + CATEGORY_COUNT;

/*
 * Flatten the grouped listing: one CATEGORY header per non-empty
 * category followed by its entries in discovery order, categories in
 * closed-enum order. Returns the number of rows written (truncated to
 * 'max_rows'; the model is bounded so MAX_ROWS always suffices).
 */
unsigned build_rows(Model const &model, Row *rows, unsigned max_rows);

/* Total scrollable content height in pixels. */
int content_height(Model const &model);

/* Clamp a scroll offset to the content. */
int clamp_scroll(Model const &model, int scroll, int list_h);

/*
 * Hit-test a click at 'y' (list-area coordinates, 0..list_h) under
 * the given scroll offset. Returns the entry index, or -1 for empty
 * space, category headers, and the status area.
 */
int hit_entry(Model const &model, int scroll, int list_h, int y);

} /* namespace Layout */

} /* namespace Sponge::Settings */
