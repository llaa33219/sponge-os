/* SPDX-License-Identifier: Apache-2.0
 *
 * settings_wire — vct's adapter for the provider-owned Settings wire
 * contract (docs/12-package-format.md §14, docs/06-vct.md §4.8).
 *
 * WHY THIS MODULE EXISTS (adapter seam): the two wire-shape seams
 * below mirror sponge_settings' model.h seams ('gen_settings_request'
 * / 'parse_settings_result') one-for-one, because vct cannot link
 * that component's source and the shared sponge_backend_client
 * 'ReportRomClient::request()' cannot express the settings request
 * (it emits only op+pkg; backend_client.h is shared and outside vct's
 * edit scope). This is the smallest explicit adapter: pure parsing
 * and generation only — no Env, no sessions, no I/O — so the host
 * self-test in tests/ can pin the exact contract.
 *
 * WIRE CONTRACT (docs/12 §14.5/§14.7, mirrored from model.h):
 *
 *   request  (Report, vct's single-writer "request" label):
 *     <request op="launch" target="settings"
 *              pkg="sponge-de" id="panel"/>
 *
 *   result   (ROM, vct's "result" label):
 *     <result status="ok" op="launch" target="settings"
 *             pkg="sponge-de" id="panel" action="started"/>
 *
 * 'id' is the primary identity attribute (the metadata vocabulary);
 * 'settings-id' is accepted as an alias on results (sponge_pkgd
 * accepts it on requests too). 'action' is "started" (fresh singleton
 * claim) or "focused" (already-open singleton raised); the refusal
 * statuses reuse the §9.2.1 lexemes verbatim: "not-installed",
 * "binary-missing", "insufficient-resources" (with need_ram /
 * avail_ram / need_caps / avail_caps figures). A structured pkgd
 * error carries status="error" plus an 'error' message.
 * "package-manager-unavailable" (§14.7) never arrives on the wire —
 * vct derives it when the channel cannot be opened or the bounded
 * result wait expires (commands.cc).
 *
 * STRICTNESS DIFFERENCE vs sponge_settings' parse (deliberate):
 * sponge_settings owns a DEDICATED result label, so its parser may
 * treat an absent pkg/id as the answer to the single pending open.
 * vct shares the "result" label with ordinary `vct launch` results,
 * so 'parse_settings_result' here REQUIRES the settings discriminator
 * (target="settings") AND a present, matching identity (via the 'id'
 * or 'settings-id' alias). A stale plain-launch result can therefore
 * never satisfy a settings open.
 *
 * PURITY (same contract as <sponge/pkg_config.h>): no Genode::Env, no
 * sessions, no allocation, no mutable global state. Only
 * <util/string.h>, <util/xml_node.h>, <util/xml_generator.h>. The
 * cases in tests/ execute this code on the host.
 */

#pragma once

#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Vct::Settings_wire {


/*
 * Closed category enum (docs/12 §14.2). An absent or unknown wire
 * value falls back to OTHER (the §4.5 <kind> typo rule).
 */
enum class Category { DESKTOP, HARDWARE, SYSTEM, APPLICATIONS, OTHER };

static constexpr unsigned CATEGORY_COUNT = 5;

/* Closed-enum display labels: the metadata wire tokens, so the CLI
 * vocabulary matches the package metadata vocabulary (English,
 * user-facing, lowercase). */
char const *category_label(Category category);

/*
 * Parse the closed enum from a wire value. Never fails: anything
 * unknown or absent maps to 'Category::OTHER'. '*recognized'
 * (optional) reports whether the wire value named a known category.
 */
Category category_from_attr(char const *value, bool *recognized = nullptr);


/*
 * One discovered <settings> entry: the fields `vct settings list`
 * displays. The addressable identity is the (pkg, id) pair
 * (docs/12 §14.1).
 */
struct Entry
{
	Genode::String<64> pkg { };
	Genode::String<33> id { };   /* [a-z0-9_-]{1,32}, §14.2 */
	Genode::String<64> title { };
	Category           category { Category::OTHER };
};

/* Bounded listing surface (the Model::MAX_ENTRIES convention). */
static constexpr unsigned MAX_ENTRIES = 64;

/*
 * Collect the settings entries from one installed-metadata document:
 * the §14.4 shape sponge_pkgd carries in its `installed` broadcast
 * (and in the `list` result) —
 *
 *   <installed ...>  (or <result ...>)
 *     <packages>
 *       <package name="..." ...>
 *         <settings id="..." title="..." category="..."
 *                   [icon="..."] target="...">
 *           <keyword>...</keyword>
 *         </settings>
 *       </package>
 *     </packages>
 *
 * Display-side defense in depth (pkgd owns the §14.3 hard
 * rejections): entries without a non-empty id or title cannot be
 * named or addressed and are skipped. Bounded at 'max' entries.
 * Returns the number collected.
 */
unsigned collect_entries(Genode::Xml_node const &root,
                         Entry *out, unsigned max);

/*
 * Stable group order: the indices of 'entries[0..n)' sorted by
 * closed-enum category order (desktop, hardware, system,
 * applications, other), keeping discovery order within one category.
 * Writes at most 'max' indices; returns the number written.
 */
unsigned group_order(Entry const *entries, unsigned n,
                     unsigned *order, unsigned max);


/* ===================== settings open channel ===================== */

/*
 * Status lexemes are the §14.7 set (the §9.2.1 launch statuses behind
 * the target="settings" discriminator). 'PACKAGE_MANAGER_UNAVAILABLE'
 * is derived locally by the caller (see the wire contract above);
 * 'UNKNOWN' covers status="error" (structured pkgd error, read
 * 'error') and any unforeseen lexeme.
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

	/* Structured pkgd error message (status="error", §14.7). */
	Genode::String<256> error { };

	/* insufficient-resources budget figures (§9.2.1); "?" when the
	 * wire carried none. */
	Genode::String<32>  need_ram  { "?" };
	Genode::String<32>  avail_ram { "?" };
	Genode::String<32>  need_caps { "?" };
	Genode::String<32>  avail_caps { "?" };
};

/*
 * Emit the request attributes on the <request> root node (§14.5).
 * Identity only: no command string, no target binary, no seq —
 * matching the sponge_settings seam byte for byte.
 */
void gen_settings_request(Genode::Xml_generator &g,
                          char const *pkg, char const *settings_id);

/*
 * Parse one <result> document of the settings channel (§14.7).
 * Returns true when the document answers a pending open of
 * (pkg, settings_id) on vct's SHARED result label — a foreign,
 * plain-launch, or identity-mismatched document returns false and
 * must be ignored. The identity attribute is 'id' (primary) or
 * 'settings-id' (alias); on the shared channel the identity must be
 * present and match (see the strictness note in the file header).
 */
bool parse_settings_result(Genode::Xml_node const &root,
                           char const *pkg, char const *settings_id,
                           Launch_result &out);

}  /* namespace Sponge::Vct::Settings_wire */
