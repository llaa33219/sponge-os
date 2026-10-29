/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkg_admission — pure resource-admission math for Sponge OS
 * package sandboxes.
 *
 * This library holds the admission/quota arithmetic sponge_pkgd performs
 * before a package's <start> node is admitted into a runtime config. It
 * was extracted verbatim from sponge_pkgd's admission.cc (Phase 16 P1.2,
 * docs/plans/phase16-runtime-overhaul.md; target named by
 * docs/evidence/phase16-runtime-config-coupling.md §9 "Admission math")
 * so the math becomes testable and reusable without touching pkgd's I/O.
 * The pre-extraction behavior is preserved exactly; all side effects
 * (state-report ROM reads, armed/unarmed arming, fail-open logging,
 * state mutation, result reports) stay in sponge_pkgd's shell.
 *
 * PURITY CONTRACT (hard):
 *
 *   No Genode::Env, no Report/ROM sessions, no File_system, no
 *   hardware access, no I/O, no exceptions, no mutable global state.
 *   The only Genode headers used are the pure <util/string.h> and,
 *   in the implementation, <util/hid.h> (the HID decode over a
 *   caller-owned byte range) plus <util/xml_node.h> for the
 *   pre-extraction defensive catch TYPE (Hid_node does not throw).
 *   Every input is a caller-owned value or a non-owning view of
 *   caller-owned bytes; every output lands in caller-owned memory.
 *
 * BEHAVIOR MAP (pre-extraction sponge_pkgd surface -> this library):
 *
 *   Main::_quota_bytes                      -> quota_bytes
 *   Main::_read_budget (HID walk + checks)  -> decode_budget
 *     (the ROM update/valid/size/local_addr I/O stays in pkgd's
 *      _read_budget, which delegates its body here)
 *   Main::_admits (margin comparison)       -> admits
 *     (the armed checks, fail-open log lines and budget selection stay
 *      in pkgd's _admits, which delegates the comparison here)
 *   install-loop budget decrement pair      -> consume
 *     (ops.cc's two-budget walk keeps the loop; the arithmetic is here)
 *   Main::ADMISSION_MARGIN_RAM / _CAPS      -> ADMISSION_MARGIN_RAM /
 *     ADMISSION_MARGIN_CAPS (values and types unchanged)
 *
 * QUOTA PARSING (quota_bytes, unchanged):
 *
 *   A package metadata quota string ("48M", "1024M", "1G", "512K",
 *   lowercase suffixes, or a plain byte count) parses to bytes. The
 *   suffix set is closed; anything else yields "no answer", which
 *   callers treat as "cannot prove it fits" -> the launch/install is
 *   refused rather than silently clamped. The input is a
 *   Genode::String<16>, exactly the width sponge_pkgd stores package
 *   RAM quotas and report attribute values at, so over-long strings
 *   are truncated before parsing exactly as before. Digits after the
 *   one-letter suffix are accepted-and-ignored (preserved quirk);
 *   digits must be followed by END OF STRING or a K/k/M/m/G/g suffix,
 *   otherwise the parse fails. Overflow wraps (preserved); zero is a
 *   valid parse result.
 *
 * BUDGET DECODE (decode_budget, unchanged):
 *
 *   The sandbox state report is HID, not XML, and every round figure
 *   is suffixed ("64M", not 67108864), because the Generator prints
 *   through Number_of_bytes::print(). The top-level <ram> and <caps>
 *   nodes are the runtime's OWN remaining budget (quota minus used) —
 *   the pool a new <start> node comes out of. The decode walks those
 *   top-level nodes, reads each node's "avail" attribute as a
 *   Genode::String<16> (same truncation as before) and parses it with
 *   quota_bytes. The first node per type that parses successfully
 *   wins: a malformed first node does not mask a later good one, and
 *   a good first node is not overwritten by a later one. Caps are
 *   additionally validated to be in 1..0xffffff. Out-parameters are
 *   written ONLY when both dimensions are known; anything else is "no
 *   usable budget", which pkgd treats as fail-open (admit with a log
 *   line). The HID decode never throws.
 *
 * ADMISSION COMPARISON (admits, unchanged):
 *
 *   avail >= need + margin on BOTH dimensions admits. The margins
 *   (below) cover what a child costs on top of its declared quota —
 *   init's per-child bookkeeping plus one period of report staleness —
 *   so a razor-thin package the sandbox would clamp anyway is refused
 *   deterministically. Equality (avail == need + margin) ADMITS; one
 *   byte/cap below refuses. The arithmetic is unsigned and wraps
 *   exactly as the pre-extraction expressions did.
 *
 * BUDGET CONSUME (consume, unchanged):
 *
 *   This is the exact decrement arithmetic used by the install
 *   pre-flight. It only updates caller-provided values; pkgd controls
 *   whether those values participate in later decisions. The current
 *   _admits path re-reads the state report, so this bookkeeping does
 *   not alter its fresh-budget verdict (pre-existing behavior). RAM
 *   subtracts unconditionally (unsigned wrap preserved); caps
 *   saturate — a demand ABOVE the remaining caps subtracts NOTHING
 *   (preserved quirk of the pre-extraction loop).
 */

#pragma once

#include <util/string.h>

namespace Sponge::Pkg_admission {

/*
 * Admission safety margin, verbatim from sponge_pkgd's pkgd.h. A
 * refusal threshold of exactly `avail >= need` would admit a razor-thin
 * package that init then has to clamp — recreating the silent failure
 * the gate exists to prevent. 8M / 64 caps is ~8% of a typical package
 * quota and two orders of magnitude above the per-child overhead, while
 * staying far below the 11% product headroom documented in docs/17 §6.
 */
static constexpr Genode::size_t ADMISSION_MARGIN_RAM  = 8u * 1024 * 1024;
static constexpr unsigned       ADMISSION_MARGIN_CAPS = 64;

/*
 * Parse a quota string into bytes. Returns false ("no answer") for a
 * malformed quota; 'out' is written only on success. See QUOTA PARSING
 * above for the exact accepted shapes.
 */
bool quota_bytes(Genode::String<16> const &q, Genode::size_t &out);

/*
 * Decode the runtime budget from a sandbox state report's bytes.
 * 'buf'/'len' is the report body (the caller's ROM content); returns
 * false when no usable top-level <ram>/<caps> avail pair is present.
 * See BUDGET DECODE above.
 */
bool decode_budget(char const *buf, Genode::size_t len,
                   Genode::size_t &avail_ram, unsigned &avail_caps);

/*
 * Admission comparison of one package's need against the budget:
 * true iff 'avail_ram >= need_ram + ADMISSION_MARGIN_RAM' AND
 * 'avail_caps >= need_caps + ADMISSION_MARGIN_CAPS'. See ADMISSION
 * COMPARISON above.
 */
bool admits(Genode::size_t avail_ram, unsigned avail_caps,
            Genode::size_t need_ram, unsigned need_caps);

/*
 * Decrement a budget by one admitted package's demand, with the
 * pre-extraction saturation semantics. See BUDGET CONSUME above.
 */
void consume(Genode::size_t &avail_ram, unsigned &avail_caps,
             Genode::size_t need_ram, unsigned need_caps);

}  /* namespace Sponge::Pkg_admission */
