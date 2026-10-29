/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Sponge::Binary_prefix — pure parse-time
 * binary_prefix rewriting for Sponge OS package metadata <config>
 * blocks.
 *
 * The scanner is a verbatim extraction of sponge_pkgd's parse-time
 * rewrite block (Phase 16 P1.2,
 * docs/plans/phase16-runtime-overhaul.md; pre-extraction source
 * repos/sponge/src/sponge_pkgd/metadata.cc:233-308, itself the
 * verbatim P1.3 move of the rewrite landed in commit 09da75ea43).
 * The rewrite rules, the needle literals, the first-attribute
 * matching, the first-child '<b' heuristic, the injection position,
 * the raw-text (comment) rewriting, the non-idempotence and the
 * silent output truncation are preserved exactly — see
 * <sponge/binary_prefix.h> for the contracts and
 * lib/src/sponge_binary_prefix/self_test.cc for the byte-identity
 * proof against a verbatim reference copy of the old block.
 *
 * Sole clamp (unobservable at the call site): the pre-extraction
 * '(tag_end + 1) - i' open-tag emit read one stale scratch byte past
 * the serialized length on a '<start name="' tail with no '>'. The
 * emit is clamped at cfg_len here (and in the test's reference copy);
 * every input for which the pre-extraction code was deterministic is
 * byte-identical.
 *
 * Purity: no Env, no sessions, no I/O, no exceptions, no mutable
 * global state. Every input and output buffer is owned by the caller.
 */

#include <sponge/binary_prefix.h>

Genode::size_t Sponge::Binary_prefix::rewrite(char *out, Genode::size_t out_len,
                                              char const *cfg,
                                              Genode::size_t cfg_len,
                                              char const *prefix)
{
	if (out == nullptr || out_len == 0)
		return 0;

	/*
	 * Empty-prefix identity: the pre-extraction call site skipped the
	 * rewrite entirely when binary_prefix was empty and kept the
	 * un-rewritten serialization. The verbatim copy below reproduces
	 * that observable result for library callers.
	 */
	if (prefix == nullptr || prefix[0] == '\0') {
		Genode::size_t o = 0;
		while (o + 1 < out_len && o < cfg_len) {
			out[o] = cfg[o];
			++o;
		}
		out[o] = 0;
		return o;
	}

	Genode::size_t o = 0;
	auto emit = [&](char const *s, Genode::size_t len) {
		while (len > 0 && o + 1 < out_len) {
			out[o++] = *s++;
			--len;
		}
	};
	auto emit_cstr = [&](char const *s) {
		emit(s, Genode::strlen(s)); };

	char const *needle_start = "<start name=\"";
	char const *needle_bin   = "<binary name=\"";
	for (Genode::size_t i = 0; i < cfg_len; ) {
		Genode::size_t remain = cfg_len - i;
		auto starts_with = [&](char const *n) {
			Genode::size_t l = Genode::strlen(n);
			return remain >= l
			    && Genode::memcmp(cfg + i, n, l) == 0; };
		if (starts_with(needle_bin)) {
			Genode::size_t v = i + Genode::strlen(needle_bin);
			Genode::size_t e = v;
			while (e < cfg_len && cfg[e] != '"') e++;
			bool const path_free =
				(e > v) && cfg[v] != '/';
			emit_cstr(needle_bin);
			if (path_free)
				emit_cstr(prefix);
			emit(cfg + v, e - v);
			emit_cstr("\"");
			i = e + 1;
			continue;
		}
		if (starts_with(needle_start)) {
			Genode::size_t v = i + Genode::strlen(needle_start);
			Genode::size_t e = v;
			while (e < cfg_len && cfg[e] != '"') e++;
			bool const path_free =
				(e > v) && cfg[v] != '/';
			Genode::size_t tag_end = e;
			while (tag_end < cfg_len && cfg[tag_end] != '>') tag_end++;
			Genode::size_t next = tag_end + 1;
			while (next < cfg_len
			       && (cfg[next] == ' ' || cfg[next] == '\n'
			           || cfg[next] == '\t' || cfg[next] == '\r'))
				next++;
			bool const has_own_binary =
				next + 7 < cfg_len
				&& cfg[next] == '<'
				&& cfg[next+1] == 'b';
			/* pre-extraction: (tag_end + 1) - i (one-byte over-read on
			 * a '>'-less tail; clamped at cfg_len — see file header) */
			Genode::size_t const tag_len =
				(tag_end < cfg_len ? tag_end + 1 : cfg_len) - i;
			emit(cfg + i, tag_len);
			if (path_free && !has_own_binary) {
				emit_cstr("<binary name=\"");
				emit_cstr(prefix);
				emit(cfg + v, e - v);
				emit_cstr("\"/>");
			}
			i = tag_end + 1;
			continue;
		}
		emit(cfg + i, 1);
		i++;
	}
	out[o] = 0;
	return o;
}
