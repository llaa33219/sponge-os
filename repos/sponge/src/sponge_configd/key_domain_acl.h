/* SPDX-License-Identifier: Apache-2.0
 *
 * key_domain_acl — per-writer-label key-domain ACL for sponge_configd
 * (Criterion 18 / D16.1).
 *
 * Shared DE keys stay typed in sponge_configd's closed registry behind
 * per-provider request labels. Every writer label owns exactly one
 * config_request ROM session and a key-domain allowlist; every `set`
 * is validated against the closed typed registry FIRST and the
 * label's key-domain SECOND. An out-of-domain write returns the
 * structured `key-outside-domain` error and never touches the store
 * or the broadcast. `vct` keeps every-domain admin access (the
 * admin/manual escape hatch); third-party packages get NO shared
 * label at all (they use package-private stores).
 *
 * This module is the pure decision seam shared by the production set
 * path (main.cc `_do_set`) and the deterministic host self-test
 * (self_test.cc / acl_tests.cc) — the tested gate logic IS the
 * shipped gate logic. It has no session, timer, or VFS dependency:
 * plain strings and fixed tables only (Genode::strcmp/strlen from
 * util/string.h are header-inline, so it also links on the host
 * without any Genode runtime objects).
 *
 * Allowlist ("key domain") text syntax, used both for the built-in
 * per-label defaults and for the component-config declarations
 * (`<writer label="..." keys="..."/>` in configd's <config>):
 *
 *   *            every key (the vct admin domain)
 *   prefix.*     any key beginning with "prefix." (so `panel.*`
 *                covers panel.height, panel.ids, and the pattern
 *                keys panel.<id>.* in one prefix domain)
 *   exact.key    exactly that one key (e.g. `bake.applied`)
 *
 * Entries are separated by commas and/or whitespace. An empty spec
 * is a valid deny-all domain. A declaration for a label replaces that
 * label's built-in default domain; declarations for unknown labels,
 * or for the admin `vct` label, are ignored with a warning (vct's
 * every-domain access is unconditional).
 */

#pragma once

#include <util/string.h>

namespace Sponge::Configd::Acl {

enum {
	MAX_WRITERS        = 9,    /* vct + 8 first-party provider labels */
	MAX_DOMAIN_ENTRIES = 8,
	MAX_DOMAIN_TEXT    = 256,
	MAX_DOMAIN_ENTRY   = 128,
};

/*
 * The structured error code emitted when a writer label attempts a
 * set outside its key domain. It appears both as the `reason`
 * attribute of the <result> and as the prefix of the human-readable
 * error text.
 */
constexpr char const *KEY_OUTSIDE_DOMAIN = "key-outside-domain";

/*
 * One writer label: its ACL identity, its built-in default key
 * domain, and its dedicated config_request / result channel labels
 * (one request ROM session per writer label, D16.1). The vct entry
 * is the historical admin channel and keeps the historical
 * `config_request` / `config_result` labels byte-identical.
 */
struct Writer
{
	char const *label;           /* writer label (ACL identity) */
	bool        admin;           /* every-domain admin access (vct) */
	char const *default_domain;  /* built-in allowlist text */
	char const *request_rom;     /* request ROM session label in configd */
	char const *result_label;    /* result report label */
};

/* The writer table (declaration order = MAX_WRITERS). */
Writer const *writers();

/* Look up a writer label; nullptr when the label has no shared
 *  channel (third-party labels are deliberately absent). */
Writer const *find_writer(char const *label);

/*
 * A parsed key-domain allowlist. `text` preserves the declared
 * allowlist verbatim for error messages and diagnostics.
 */
struct Key_domain
{
	unsigned num_entries { 0 };
	bool     all { false };
	char     entries[MAX_DOMAIN_ENTRIES][MAX_DOMAIN_ENTRY] { };
	char     text[MAX_DOMAIN_TEXT] { };

	bool contains(char const *key) const;
};

/*
 * Parse `spec` (comma/whitespace-separated entries, see the header
 * comment) into `out`. Returns false — leaving `out` unspecified —
 * when an entry exceeds MAX_DOMAIN_ENTRY or the entry count exceeds
 * MAX_DOMAIN_ENTRIES. An empty spec parses to a valid deny-all
 * domain.
 */
bool parse_domain(char const *spec, Key_domain &out);

/*
 * The set gate, in the mandated order: the closed typed registry
 * first (`registry_known` is the caller's registry answer for the
 * key — flat entry, instantiated pattern entry, or valid
 * pattern-template shape), the label's key-domain second.
 *
 *   Unknown_key         registry first: an unknown key is rejected
 *                       on every label (vct included) with the
 *                       existing structured unknown-key error.
 *   Key_outside_domain  known key, but outside the label's declared
 *                       domain — the caller reports the structured
 *                       `key-outside-domain` error and leaves the
 *                       store and broadcast unchanged.
 *   Apply               the caller may validate the value and
 *                       mutate the store.
 */
enum class Set_outcome { Apply, Unknown_key, Key_outside_domain };

Set_outcome gate_set(bool admin, bool registry_known,
                     Key_domain const &domain, char const *key);

}  /* namespace Sponge::Configd::Acl */
