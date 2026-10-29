/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_binary_prefix — pure parse-time binary_prefix rewriting for
 * Sponge OS package metadata <config> blocks.
 *
 * This library rewrites the serialized metadata <config> element of a
 * nested sub-init package (binary <binary>init</binary>) so its nested
 * <start>/<binary> names resolve on disk-served media, where the ROM
 * route reaches rom_sys chrooted at /system and unprefixed names miss
 * bin/ (docs/14 §4.4/§4.6). It was extracted verbatim from
 * sponge_pkgd's _parse_package rewrite block (Phase 16 P1.2,
 * docs/plans/phase16-runtime-overhaul.md; pre-extraction source
 * repos/sponge/src/sponge_pkgd/metadata.cc:233-308, itself the
 * verbatim P1.3 move of the rewrite landed in commit 09da75ea43) so
 * the rewriting becomes testable and reusable without touching pkgd's
 * I/O. The pre-extraction behavior is preserved byte for byte; the
 * nested limitations listed under PRESERVED LIMITATIONS below are
 * DELIBERATELY preserved (P2.3 owns any semantic change).
 *
 * PURITY CONTRACT (hard):
 *
 *   No Genode::Env, no sessions, no File_system, no hardware access,
 *   no I/O, no exceptions, no static/global rewrite buffers. The only
 *   Genode header used is the pure <util/string.h>. The caller owns
 *   every input and output buffer and their sizes.
 *
 * REWRITE RULES (the pre-extraction algorithm, unchanged):
 *
 *   The input is scanned as raw text (no XML parsing). Two needles are
 *   matched at every scan position, '<binary name="' first and
 *   '<start name="' second — i.e. the name attribute must be the FIRST
 *   attribute of the tag (the needle is a literal). For each match the
 *   attribute value is the byte range up to the next '"'.
 *
 *   path-free := the value is non-empty and does not start with '/'.
 *
 *   - '<binary name="B">' (path-free B) becomes
 *     '<binary name="prefix/B">'.
 *   - '<start name="X">' (path-free X) whose first child element is
 *     NOT '<b'-prefixed gains '<binary name="prefix/X"/>' emitted
 *     right after its open tag. Path-like names (/bin/bash — served by
 *     the sub-init's vfs_rom child) are left untouched.
 *   - An empty prefix performs NO rewriting at all: the output is the
 *     input verbatim (empty-prefix identity — boot-module/flat
 *     topologies carry no prefix and skip the rewrite entirely).
 *
 *   The first-child test is a two-character heuristic: after the open
 *   tag's '>' the scanner skips ' ', '\n', '\t', '\r' and treats any
 *   next byte pair '<' 'b' (e.g. <binary>, but also <book>) as "has
 *   own binary", with a length guard of 8 bytes (next + 7 < cfg_len).
 *
 * PRESERVED LIMITATIONS (not defects of the extraction — P2.3):
 *
 *   - Raw-text scan: needle-shaped text inside comments or raw
 *     attribute values is rewritten too (no XML awareness).
 *   - Non-idempotent: running the rewrite twice re-prefixes the
 *     injected/prefixed names again ("bin/bin/...").
 *   - A self-closing '<start name="x"/>' gains the injected
 *     '<binary/>' AFTER its '/>' (the scanner only looks for '>').
 *   - A <binary> that is not the first child of its <start> triggers
 *     BOTH its own prefixing and the start's injection (two <binary>
 *     entries result).
 *   - A '<binary name="' with no closing '"' still emits a closing
 *     '"' in the output; a '<start name="' with no '>' in the
 *     remaining input emits the rest of the input without reading past
 *     it (the pre-extraction code read one stale scratch byte here —
 *     see below).
 *
 * OUTPUT BOUNDS:
 *
 *   The output is written byte-exactly like the pre-extraction code
 *   did into its 2*16384-byte scratch: 'out_len' bytes are available,
 *   one byte is reserved for the terminating NUL, and output beyond
 *   out_len - 1 bytes is silently dropped (the caller's own truncation
 *   policy — sponge_pkgd feeds Genode::String<16384>, which truncates
 *   again at 16383 characters). 'rewrite' never writes out of bounds,
 *   treats its input as opaque bytes of length 'cfg_len' (embedded
 *   NULs pass through), and returns 0 without touching 'out' when
 *   out_len is 0.
 *
 *   The one deliberate clamp: the pre-extraction code emitted
 *   '(tag_end + 1) - i' bytes for a <start> open tag, which reads one
 *   byte past the serialized length when the input has no '>' after
 *   the needle (unreachable with the well-formed <config>
 *   serialization the call site feeds; the extra byte was stale
 *   scratch content and therefore unspecified). The library clamps at
 *   'cfg_len'. Every input for which the pre-extraction code was
 *   deterministic is byte-identical.
 */

#pragma once

#include <util/string.h>

namespace Sponge::Binary_prefix {

/*
 * Rewrite the serialized <config> block 'cfg' ('cfg_len' bytes) into
 * the caller-owned buffer 'out' ('out_len' bytes) per the rules above.
 *
 * 'prefix' is the disk-served binary path prefix (e.g. "bin/"); empty
 * or nullptr yields the input verbatim. Returns the number of bytes
 * written to 'out', excluding the terminating NUL.
 */
Genode::size_t rewrite(char *out, Genode::size_t out_len,
                       char const *cfg, Genode::size_t cfg_len,
                       char const *prefix);

}  /* namespace Sponge::Binary_prefix */
