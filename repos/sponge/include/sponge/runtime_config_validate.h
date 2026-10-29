/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_runtime_config_validate — pure validation of the runtime
 * <config> documents that sponge_pkgd publishes for the package
 * sandboxes (Phase 16 P1.5 publication seam).
 *
 * The validator answers one question: "is this document exactly a
 * well-formed instance of the runtime-config subset that
 * sponge_pkg_config renders?" It is the render -> validate -> publish
 * gate for the named runtime targets (app/driver today): a rendering
 * regression, a malformed raw <config> append, or a hostile metadata
 * fragment is rejected with a structured reason BEFORE anything is
 * published to a report session.
 *
 * PURITY CONTRACT (hard, same as <sponge/pkg_config.h>):
 *
 *   No Genode::Env, no Report/ROM sessions, no File_system, no
 *   hardware access, no exceptions, no mutable global state, no
 *   dynamic allocation, no standard-library dependency. The caller
 *   owns every buffer: the input document, the Workspace scratch, and
 *   the returned Result (a value with its own detail text). The
 *   implementation is deliberately self-contained (its own small
 *   syntax-agnostic XML reader) so it links both into Genode
 *   components (where util/xml_node.h requires C++ exceptions) and
 *   into the host-side self-test.
 *
 * WHAT IS VALIDATED — THE PKGD-EMITTED SUBSET (NOT generic XSD):
 *
 *   This is NOT a full init config.xsd / init.hsd implementation. It
 *   validates the structural subset that Sponge::Pkg_config::emit_target
 *   can produce, with the types/lexemes init itself enforces
 *   (genode/repos/os/src/init/config.xsd, init.hsd, base_types.xsd):
 *
 *     <config>                                  (no attributes)
 *       <report delay_ms init_ram init_caps     (exactly these four
 *                child_ram/>                     attributes, all
 *                                               required, positive /
 *                                               Boolean lexemes)
 *       <repro_padding>                         (TEST-ONLY, see below)
 *         <p v="..."/>...
 *       </repro_padding>
 *       <parent-provides>                       (>= 1, <= 64 unique)
 *         <service name="..."/>...              (name required)
 *       </parent-provides>
 *       <default-route>                         (exactly one)
 *         <any-service><parent/><any-child/></any-service>
 *       </default-route>
 *       <default caps="..."/>                   (caps required,
 *                                               positive; no ram)
 *       <start name="..." caps="...">           (0..32, names unique)
 *         <binary name="..."/>?                 (<= 1, name required)
 *         <resource name="RAM" quantum="..."/>  (exactly 1)
 *         <config>...</config>?                 (<= 1, opaque — see
 *                                               below)
 *         <route>                               (exactly 1)
 *           <service name="..." [label] [label_last]>
 *             <parent/> | <child name="..."/>   (exactly one target)
 *           </service>...
 *           <any-service><parent/></any-service>  (exactly 1)
 *         </route>
 *       </start>
 *
 *   Anything else — even constructs that generic init accepts
 *   (<alias>, <heartbeat>, <priority>, <ram> on <default>, service
 *   policies, per-service <default-route> entries, ...) — is rejected
 *   as outside the emitted subset. That strictness is the point of a
 *   publication gate: only what the renderer is allowed to produce
 *   may pass.
 *
 *   Required by the emission contract (missing => MISSING_REQUIRED):
 *   the root <config>, exactly one <report>, <parent-provides>,
 *   <default-route> and <default>, per <start> the name/caps
 *   attributes and the <resource>/<route> nodes, per route at least
 *   one <service>. Uniqueness: <start name> and every
 *   <parent-provides> service name (=> DUPLICATE_NAME).
 *
 * LEXEMES (from init.hsd / base_types.xsd and docs/12-package-format.md):
 *
 *   - positive     \d*[1-9]\d*   (at most 10 digits)  — delay_ms,
 *                                                <default caps>
 *   - natural      \d+           (at most 10 digits)  — <start caps>
 *   - num_bytes    \d+[GMK]?     (digits > 0, <= 15 chars) — quantum
 *   - Boolean      true|yes|on|false|no|off            — report flags
 *   - package name [a-z0-9_]{1,63}   (docs/12 §4.1)    — start name
 *   - service name [A-Za-z0-9_]{1,31}                  — service name
 *   - binary path  [A-Za-z0-9_.+/-]{1,95}              — binary name
 *   - child name   [A-Za-z0-9_.+-]{1,63}               — child target
 *   - label        printable ASCII {1,95}              — label attrs
 *
 * DOCUMENTED EXCEPTIONS AND LIMITATIONS (subset honesty):
 *
 *   1. <repro_padding> is the ONLY test-only exception: the repro
 *      harness knobs of sponge_pkgd (repro_pad / repro_pad_grow,
 *      pkgd.h REPRO_PAD_BYTES) emit it so a write overflows the
 *      report buffer on purpose (run/sponge-config-regen-survival.run
 *      family). Production emission never carries it (pad_bytes = 0);
 *      the validator permits it for compatibility with the current
 *      repro scenarios and validates its shape conservatively: no
 *      attributes, children only <p v="..."/>, each v exactly 64
 *      characters.
 *   2. The inline <config> block inside <start> is OPAQUE production
 *      content: it is package metadata emitted verbatim (raw append),
 *      and init's own schema treats it as "anything"
 *      (config.xsd processContents="skip"). The validator therefore
 *      checks only that the whole document is well-formed XML; the
 *      block's attributes and content are not interpreted.
 *   3. Sibling ORDER is not enforced (init's config-diff does not
 *      depend on it; emission order stays deterministic regardless).
 *   4. Entity references are checked syntactically (&name;, &#NN;,
 *      &#xHH;) but not resolved; lexeme lengths are measured on the
 *      raw (escaped) value text.
 *   5. No XML prolog, DOCTYPE, processing instructions, or trailing
 *      content beyond whitespace/comments — pkgd never emits them.
 *   6. The ld.lib.so route triple (ROM service with
 *      label_last="ld.lib.so" routed to <parent/>) is shape-validated
 *      like any other route entry, not pinned: its presence and exact
 *      values are an emission invariant, not a schema constraint.
 *
 * WORKSPACE:
 *
 *   'validate' needs bounded scratch for the uniqueness checks. The
 *   caller owns it (a Workspace value, ~6 KiB) and passes it per
 *   call; 'validate' resets its counters on entry. Keep it off small
 *   component stacks — sponge_pkgd keeps exactly this kind of scratch
 *   in members for that reason.
 */

#pragma once

namespace Sponge::Runtime_config_validate {

/*
 * Structured validation outcome. 'ok' is true only when 'code' is OK;
 * 'detail' then stays empty. On failure 'detail' carries a bounded,
 * NUL-terminated English reason with the offending line and node —
 * never a silent bool.
 */
enum class Code {
	OK                = 0,
	XML_SYNTAX        = 1,  /* malformed / unsupported XML syntax   */
	UNKNOWN_NODE      = 2,  /* structural node outside the subset   */
	UNKNOWN_ATTRIBUTE = 3,  /* attribute outside the subset         */
	INVALID_VALUE     = 4,  /* value violates its lexeme / shape    */
	MISSING_REQUIRED  = 5,  /* required node/attribute absent       */
	DUPLICATE_NAME    = 6,  /* repeated start / service name        */
};

struct Result
{
	enum { DETAIL_CAPACITY = 192 };

	bool ok   { true };
	Code code { Code::OK };
	char detail[DETAIL_CAPACITY] { };
};

/*
 * Caller-owned scratch for the name-uniqueness checks. Bounds mirror
 * sponge_pkg_config's emission bounds (MAX_PACKAGES / MAX_PROVIDES).
 */
struct Workspace
{
	enum {
		MAX_START_NAMES   = 32,
		MAX_SERVICE_NAMES = 64,
		NAME_CAPACITY     = 64,   /* covers the 63-char name bound */
	};

	char     start_names[MAX_START_NAMES][NAME_CAPACITY] { };
	char     service_names[MAX_SERVICE_NAMES][NAME_CAPACITY] { };
	unsigned num_start_names   { 0 };
	unsigned num_service_names { 0 };
};

/*
 * Validate one complete "<config>...</config>" document (the report
 * node type sponge_pkgd publishes; see
 * Sponge::Pkg_config::emit_config). 'xml' is the caller's buffer of
 * 'xml_len' bytes (need not be NUL-terminated). Returns the first
 * violation found, or ok.
 */
Result validate(char const *xml, unsigned xml_len, Workspace &workspace);

}  /* namespace Sponge::Runtime_config_validate */
