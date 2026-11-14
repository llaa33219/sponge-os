/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd settings rules — provider-owned `<settings>` metadata
 * parse/validation (docs/12-package-format.md §14.2/§14.3) and the
 * singleton open decision (§14.7).
 *
 * This module is the deterministic half of the Criterion-18 pkgd
 * surface: everything here is a pure function over caller-owned values
 * and Genode::Xml_node input — no Env, no sessions, no I/O, no
 * logging, no mutable global state (the same purity contract as the
 * sponge_pkg_launch library). The logging half (the bounded warnings
 * and the English refusal text) stays with the caller (metadata.cc /
 * ops.cc), and the host-side self_test.cc exercises these rules
 * directly.
 *
 * Only pure Genode headers are used: <util/string.h>,
 * <util/xml_node.h>, <util/hid.h> (the §14.7 crash/relaunch scan over
 * a caller-owned sandbox-state-report byte range — the same purity
 * argument as sponge_pkg_admission's HID decode) and
 * <sponge/pkg_launch.h> — the latter for the Resource_verdict this
 * decision reuses, per the §14.5 rule that the settings path reuses
 * the existing launch machinery shapes.
 *
 * PARSE CONTRACT (docs/12-package-format.md §14.2/§14.3):
 *
 *   One `<settings>` element per provider entry, child of `<package>`
 *   alongside (never inside) `<launcher>`:
 *
 *     id        required, [a-z0-9_-]{1,32}, unique per package —
 *               half of the (package, settings-id) identity (§14.1)
 *     title     required, free-form display text
 *     category  optional, closed enum desktop|hardware|system|
 *               applications|other; absent or unknown falls back to
 *               "other" and sets 'category_defaulted' so the caller
 *               emits the bounded warning (the <kind> typo rule)
 *     icon      optional payload-image name (display metadata only)
 *     target    required, exactly one bare payload binary name
 *
 *   0..N `<keyword>` children (short free-form search terms).
 *
 *   HARD REJECTIONS (a rejected entry invalidates its package — the
 *   caller refuses with a clear English error naming the entry):
 *
 *     duplicate id in one package | missing or non-bare target |
 *     absolute paths (leading '/' or '\', drive forms) | payload
 *     escapes (".." or any path separator) | shell/interpreter/
 *     command strings (whitespace, quotes, ; | & $ ` and any
 *     character outside the bare-name set) | an exclusive attribute
 *     or <exclusive> claim carried by the entry itself (settings are
 *     never exclusive, §14.7).
 *
 *   Unknown attributes and unknown child elements warn and are
 *   ignored (additive evolution) — except `exclusive`, which is a
 *   KNOWN attribute and a hard rejection on this element family.
 *
 * OPEN DECISION (docs/12-package-format.md §14.7):
 *
 *   Gate order: identity -> singleton -> binary -> resource -> ok.
 *
 *     not-installed        : unknown package, unknown settings-id, or
 *                            a stale entry after removal
 *     focused              : the (package, settings-id) claim is held
 *                            — a second open is a SUCCESS, never
 *                            "already-running" and never a duplicate
 *     binary-missing       : the entry's target is not staged (same
 *                            pre-flight as §9.2.1)
 *     insufficient-resources : the host package's declared quota does
 *                            not fit the hosting runtime's budget
 *                            (same pre-flight as §9.2.1)
 *     started              : no live instance — the caller claims the
 *                            identity and emits the <start> node
 *
 *   Settings launches NEVER join the exclusive running-set check
 *   (there is no exclusive gate in this decision at all) and have a
 *   distinct lifecycle identity from the app launch: the emitted
 *   <start> name derives deterministically from (package,
 *   settings-id) via settings_start_name(), so the app's
 *   installed/running state and the provider's never alias.
 *
 * START-NAME IDENTITY:
 *
 *   settings_start_name() projects the pair onto "<pkg>.<id>",
 *   truncated to the sandbox's Child_policy::Name width (63 chars,
 *   base/child.h). '.' cannot appear in a package name ([a-z0-9_-]+,
 *   §4.1) or in a settings id ([a-z0-9_-]{1,32}), so the projection
 *   can never alias an app <start> name; two pairs whose projections
 *   collide under truncation need adversarial 60+ character names.
 *
 * CRASH/RELAUNCH LIVENESS (§14.7 "Removal and crash"):
 *
 *   A crashed provider is contained in its own address space, and
 *   because the singleton claim covers a LIVE instance only, the next
 *   open must start a fresh instance. The live-instance fact is the
 *   hosting sandbox's state report (the same report the admission
 *   gate reads): Genode's sandbox Generator exposes every exited
 *   child as `<child name="..." exited="N">` (HID) and leaves the
 *   node in the report until the <start> node is abandoned. The
 *   claim's <start> name is the child's name, so the report answers
 *   "is this provider still alive" directly.
 *
 *   settings_provider_state() is the pure half of that check. Only an
 *   EXPLICIT `exited` attribute on the named <child> node counts as
 *   an exit — never mere absence of the node: a report can predate
 *   the provider's first paint (or the child's creation), so a name
 *   that is not in the report is UNKNOWN, not dead. A clean exit
 *   (`exited="0"`) is still an exit.
 *
 * SECOND-OPEN SEMANTICS (why no dedup/replay on this path):
 *
 *   §14.7 requires every open to answer its TRUE singleton state
 *   ("started" vs "focused"), which the request de-duplication's
 *   byte-identical replay cannot express — a byte-identical second
 *   open must answer "focused", not replay "started". Settings opens
 *   are therefore processed fresh on every request (the singleton
 *   claim makes re-processing idempotent: the claim is add-once and
 *   the focused path emits nothing). The claim set is the only state
 *   an open mutates — never the plan/installed/running sets — so the
 *   duplicate-replay recipe's state guarantee for the other
 *   operations is preserved. See _dispatch_request.
 */

#pragma once

#include <sponge/pkg_launch.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace Sponge::Pkgd {

/* Per-entry `<keyword>` bound ("short free-form search terms"). */
enum { SETTINGS_MAX_KEYWORDS = 8 };

/*
 * One parsed `<settings>` entry (docs/12-package-format.md §14.2).
 * Field widths follow the other parse types in pkgd.h.
 */
struct Settings_entry
{
	Genode::String<33>  id;
	Genode::String<64>  title;
	Genode::String<16>  category;   /* closed enum; "other" fallback  */
	Genode::String<64>  icon;       /* optional payload-image name    */
	bool                has_icon { false };
	Genode::String<64>  target;    /* bare payload binary name       */
	Genode::String<32>  keywords[SETTINGS_MAX_KEYWORDS] { };
	unsigned            num_keywords { 0 };

	/* Parse bookkeeping: category was absent or unknown and fell
	 * back to "other" — the caller emits the bounded warning. */
	bool                category_defaulted { false };
};

/*
 * The bounded hard-rejection reasons (§14.3). Check order (the first
 * failing rule wins, so a multiply-broken entry reports one
 * deterministic reason): id (missing -> malformed -> duplicate),
 * title, the `exclusive` attribute, then the target (missing ->
 * absolute -> escape -> shell -> overlong), and finally an
 * <exclusive> claim child. 'malformed_target' is the umbrella of
 * §14.3 row 2 ("not one bare payload binary name") for forms the
 * three specific rows do not cover (overlong values).
 */
enum class Settings_reject {
	none,
	missing_id,
	malformed_id,
	duplicate_id,
	missing_title,
	missing_target,
	malformed_target,
	absolute_target,
	payload_escape,
	command_string,
	exclusive_claim,
};

/* Bounded English reason text for the caller's refusal message. */
char const *settings_reject_reason(Settings_reject reject);

/*
 * Parse and validate ONE `<settings>` element into 'out'. 'prior'
 * holds the entries already ACCEPTED from the same package (the
 * duplicate-id check, §14.3). Returns the outcome; on rejection the
 * partial entry still carries whatever identity was parsed, so the
 * caller can name the offending entry.
 *
 * 'unknown_attribute'/'unknown_child' flag the warn-and-ignore cases
 * (the caller emits the bounded warning).
 */
struct Settings_parse
{
	Settings_reject reject { Settings_reject::none };
	bool            unknown_attribute { false };
	bool            unknown_child     { false };
};

Settings_parse parse_settings_entry(Genode::Xml_node const &node,
                                    Settings_entry &out,
                                    Settings_entry const *prior,
                                    unsigned num_prior);

/*
 * The deterministic <start>-name derivation "<pkg>.<id>" (truncating
 * at the sandbox's Child_policy::Name width — see the START-NAME
 * IDENTITY note above).
 */
Genode::String<64> settings_start_name(char const *pkg, char const *id);

/* Reject identities whose projected start name would truncate or collide. */
bool settings_start_name_valid(char const *pkg, char const *id);

/*
 * The §14.7 singleton open decision. 'proceed' (pre_resource only)
 * means "every gate before the resource gate passed — the resource
 * gate is now decisive", mirroring Pkg_launch::decide_pre_resource's
 * laziness contract: decide() equals the pre_resource prefix followed
 * by the resource gate, and whenever the prefix does not answer
 * 'proceed', decide() answers the same outcome for EVERY resource
 * verdict (the self-test pins both properties).
 */
enum class Settings_outcome {
	not_installed,
	binary_missing,
	insufficient_resources,
	started,
	focused,
	proceed,
};

Settings_outcome settings_decide_pre_resource(bool entry_found,
                                              bool binary_ok,
                                              bool claimed);

Settings_outcome settings_decide(bool entry_found, bool binary_ok,
                                 Pkg_launch::Resource_verdict const &resource,
                                 bool claimed);

/*
 * Singleton-claim membership over caller-owned parallel (pkg, id)
 * arrays — the claim-set counterpart of Pkg_launch::name_in_set. The
 * claim is the §14.7 identity (package, settings-id); holding it is
 * what makes a second open answer "focused" and adds no second
 * <start> node.
 */
bool settings_claim_in_set(char const *const *pkgs, char const *const *ids,
                           unsigned count,
                           char const *pkg, char const *id);

/*
 * The §14.7 crash/relaunch scan (see CRASH/RELAUNCH LIVENESS above):
 * the liveness state of one settings provider inside a hosting
 * sandbox's state report. 'buf'/'len' is the report body (the state
 * ROM content, HID); 'start_name' is the claim's derived <start> name
 * (settings_start_name). 'exited' is true iff the report carries a
 * <child> node with that exact name AND an explicit `exited`
 * attribute — absence of the node (or of the attribute) is never an
 * exit. 'code' is the attribute's value (the provider's exit value).
 * The HID decode never throws and never treats a malformed or empty
 * buffer as an exit.
 */
struct Settings_provider_state
{
	bool exited { false };
	long code   { 0 };
};

Settings_provider_state settings_provider_state(char const *buf,
                                                Genode::size_t len,
                                                char const *start_name);

}  /* namespace Sponge::Pkgd */
