/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side golden self-test for sponge_binary_prefix (Phase 16 P1.2).
 *
 * NOT part of the Genode build (deliberately absent from
 * lib/mk/sponge_binary_prefix.mk), so it lives outside the shared build
 * run. It proves the P1.2 byte-identity claim two ways:
 *
 *   1. Equivalence: the library's rewrite is compared byte for byte
 *      against `ref_rewrite` below — a verbatim copy of the
 *      pre-extraction parse-time rewrite block
 *      (repos/sponge/src/sponge_pkgd/metadata.cc:233-308, itself the
 *      verbatim P1.3 move of the block landed in commit 09da75ea43 as
 *      sponge_pkgd main.cc's _parse_package rewrite).
 *
 *   2. Golden snapshots: the same fixture outputs are compared against
 *      embedded snapshot strings captured from that reference
 *      implementation, so any future edit that changes the bytes fails
 *      loudly even if it changes both copies together.
 *
 * Fixtures cover: the terminal nested-start topology (terminal/vfs
 * injection, vfs_rom's own binary, /bin/bash untouched), the files
 * package's libc/vfs composition (no needles — passes through), the
 * empty-prefix identity, name-first-attribute matching, the
 * first-child '<b' heuristic (including its preserved quirks), raw
 * comment-text rewriting, non-idempotence, unterminated tails (with
 * the documented cfg_len clamp), and the silent output truncation.
 *
 * BUILD & RUN (from the repository root; host g++ only — this never
 * touches the Genode build tree):
 *
 *   g++ -std=c++20 -O1 \
 *       -I genode/repos/base/include \
 *       -I genode/repos/base/include/spec/64bit \
 *       -I genode/repos/base/include/spec/x86_64 \
 *       -I genode/repos/base/include/spec/x86 \
 *       -I repos/sponge/include \
 *       repos/sponge/lib/src/sponge_binary_prefix/self_test.cc \
 *       repos/sponge/lib/src/sponge_binary_prefix/binary_prefix.cc \
 *       -o /tmp/sponge_binary_prefix_self_test \
 *   && /tmp/sponge_binary_prefix_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every fixture
 * matched. "self-test: FAIL" prints the first divergence.
 *
 * `--dump` prints the library output for every fixture (used to
 * capture the snapshot goldens embedded below).
 */

#include <sponge/binary_prefix.h>

#include <stdio.h>
#include <string.h>


/* ===================== reference implementation ===================== */
/*
 * Verbatim transcription of the pre-extraction rewrite block
 * (repos/sponge/src/sponge_pkgd/metadata.cc:233-308 on the P1.3-split
 * tree; originally landed as sponge_pkgd main.cc's _parse_package
 * rewrite in commit 09da75ea43). The only adaptations are the ones
 * the function signature had to gain: the serialized config and its
 * length, the prefix and the output buffer arrive as arguments instead
 * of the Main::_parse_package scratch (static cfg_buf/out_buf) and
 * Main::_binary_prefix. The scan — needle literals, name-first
 * matching, path-free rule, first-child '<b' heuristic, injection
 * position, byte-at-a-time fallback and the silent truncation at
 * 'o + 1 < out_len' — is character-identical.
 *
 * Two behaviors of the surrounding block are folded into the
 * transcribed behavior so ref and library are directly comparable:
 *
 *   - the call site's `if (_binary_prefix != "" && cfg_len > 0)`
 *     guard: an empty prefix or zero length left config_xml at the
 *     un-rewritten serialization, i.e. the observable result is the
 *     input verbatim — reproduced here (and in the library) as the
 *     empty-prefix identity copy;
 *   - the open-tag emit clamp at cfg_len (see below).
 *
 * The one clamp, identical in both copies: the pre-extraction code
 * emitted '(tag_end + 1) - i' bytes for a <start> open tag, reading
 * one stale scratch byte past the serialized length when the input
 * had no '>' after the needle. That path is unreachable with the
 * well-formed <config> serialization the call site feeds, and the
 * extra byte was unspecified (stale static-buffer content), so both
 * copies clamp at cfg_len. Every input for which the pre-extraction
 * code was deterministic is byte-identical. If this copy and
 * binary_prefix.cc are ever edited together, the snapshot goldens
 * below still catch the change.
 */

namespace {

Genode::size_t ref_rewrite(char *out, Genode::size_t out_len,
                           char const *cfg, Genode::size_t cfg_len,
                           char const *prefix)
{
	if (out == nullptr || out_len == 0)
		return 0;

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


/* ===================== fixtures ===================== */

/*
 * FIX_TERMINAL: the serialized metadata <config> of pkg/terminal
 * (modeled on pkg/terminal/metadata.xml, in the Xml_generator form the
 * call site serializes: tab-indented, one node per line, content
 * nodes inline; the tars list trimmed but every <start> open tag, name
 * and first child kept verbatim). prefix "bin/":
 *
 *   - <start name="terminal">  (first child <provides>)  -> injected
 *     <binary name="bin/terminal"/> right after the open tag;
 *   - <start name="vfs">       (first child <provides>)  -> injected
 *     <binary name="bin/vfs"/>;
 *   - <start name="vfs_rom">   (first child <binary .../>) -> NOT
 *     injected, and its own <binary name="cached_fs_rom"/> becomes
 *     bin/cached_fs_rom;
 *   - <start name="/bin/bash"> (path-like)               -> untouched.
 */
char const *const FIX_TERMINAL =
	"<config>\n"
	"\t<parent-provides>\n"
	"\t\t<service name=\"ROM\"/>\n"
	"\t\t<service name=\"PD\"/>\n"
	"\t\t<service name=\"RM\"/>\n"
	"\t\t<service name=\"CPU\"/>\n"
	"\t\t<service name=\"LOG\"/>\n"
	"\t\t<service name=\"Timer\"/>\n"
	"\t\t<service name=\"Gui\"/>\n"
	"\t\t<service name=\"Terminal\"/>\n"
	"\t\t<service name=\"Report\"/>\n"
	"\t</parent-provides>\n"
	"\t<default caps=\"120\"/>\n"
	"\t<default-route>\n"
	"\t\t<any-service>\n"
	"\t\t\t<parent/>\n"
	"\t\t\t<any-child/>\n"
	"\t\t</any-service>\n"
	"\t</default-route>\n"
	"\t<start name=\"terminal\" caps=\"150\" ram=\"48M\">\n"
	"\t\t<provides>\n"
	"\t\t\t<service name=\"Terminal\"/>\n"
	"\t\t</provides>\n"
	"\t\t<config ld_check_ctors=\"false\">\n"
	"\t\t\t<initial width=\"800\" height=\"600\"/>\n"
	"\t\t\t<vfs>\n"
	"\t\t\t\t<rom name=\"VeraMono.ttf\"/>\n"
	"\t\t\t\t<dir name=\"font\">\n"
	"\t\t\t\t\t<dir name=\"monospace\">\n"
	"\t\t\t\t\t\t<ttf name=\"regular\" path=\"/VeraMono.ttf\" size_px=\"14\"/>\n"
	"\t\t\t\t\t</dir>\n"
	"\t\t\t\t</dir>\n"
	"\t\t\t</vfs>\n"
	"\t\t</config>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"Gui\">\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t\t<any-child/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"\t<start name=\"vfs\" caps=\"250\" ram=\"48M\">\n"
	"\t\t<provides>\n"
	"\t\t\t<service name=\"File_system\"/>\n"
	"\t\t</provides>\n"
	"\t\t<config>\n"
	"\t\t\t<vfs>\n"
	"\t\t\t\t<tar name=\"bash-minimal.tar\"/>\n"
	"\t\t\t\t<tar name=\"vim-minimal.tar\"/>\n"
	"\t\t\t\t<tar name=\"coreutils-minimal.tar\"/>\n"
	"\t\t\t\t<dir name=\"dev\">\n"
	"\t\t\t\t\t<zero/>\n"
	"\t\t\t\t\t<null/>\n"
	"\t\t\t\t\t<log/>\n"
	"\t\t\t\t\t<terminal/>\n"
	"\t\t\t\t\t<terminal name=\"vct\" label=\"vct\"/>\n"
	"\t\t\t\t\t<inline name=\"rtc\">2018-01-01 00:01</inline>\n"
	"\t\t\t\t</dir>\n"
	"\t\t\t</vfs>\n"
	"\t\t\t<policy label_prefix=\"vfs_rom\" root=\"/\"/>\n"
	"\t\t\t<default-policy root=\"/\" writeable=\"yes\"/>\n"
	"\t\t</config>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"Terminal\" label=\"vct\">\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"Terminal\">\n"
	"\t\t\t\t<child name=\"terminal\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"\t<start name=\"vfs_rom\" ram=\"32M\">\n"
	"\t\t<binary name=\"cached_fs_rom\"/>\n"
	"\t\t<provides>\n"
	"\t\t\t<service name=\"ROM\"/>\n"
	"\t\t</provides>\n"
	"\t\t<config/>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"File_system\">\n"
	"\t\t\t\t<child name=\"vfs\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"\t<start name=\"/bin/bash\" caps=\"450\" ram=\"64M\">\n"
	"\t\t<config>\n"
	"\t\t\t<libc stdin=\"/dev/terminal\" stdout=\"/dev/terminal\" stderr=\"/dev/terminal\" rtc=\"/dev/rtc\" pipe=\"/pipe\"/>\n"
	"\t\t\t<vfs>\n"
	"\t\t\t\t<fs/>\n"
	"\t\t\t</vfs>\n"
	"\t\t\t<arg value=\"bash\"/>\n"
	"\t\t\t<env name=\"TERM\">screen</env>\n"
	"\t\t\t<env name=\"PATH\">/bin</env>\n"
	"\t\t\t<env name=\"PS1\">sponge:$PWD&gt; </env>\n"
	"\t\t\t<env name=\"SHELL\">/bin/bash</env>\n"
	"\t\t</config>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"File_system\">\n"
	"\t\t\t\t<child name=\"vfs\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"ROM\" label_suffix=\".lib.so\">\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"ROM\" label_last=\"/bin/bash\">\n"
	"\t\t\t\t<child name=\"vfs_rom\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"ROM\" label_prefix=\"/bin\">\n"
	"\t\t\t\t<child name=\"vfs_rom\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t\t<any-child/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"</config>\n";

/*
 * FIX_FILES: the serialized metadata <config> of pkg/files (modeled on
 * pkg/files/metadata.xml): a pure libc/vfs payload composition with NO
 * <start>/<binary> needles. The rewrite must pass it through verbatim
 * even with a non-empty prefix — including the name-bearing
 * composition tags (<tar name=...>, <dir name=...>, <inline name=...>)
 * that the needle literals must not match.
 */
char const *const FIX_FILES =
	"<config>\n"
	"\t<libc stdout=\"/dev/log\" stderr=\"/dev/log\" pipe=\"/pipe\" rtc=\"/dev/rtc\"/>\n"
	"\t<vfs>\n"
	"\t\t<dir name=\"dev\">\n"
	"\t\t\t<log/>\n"
	"\t\t\t<inline name=\"rtc\">2018-01-01 00:01</inline>\n"
	"\t\t</dir>\n"
	"\t\t<dir name=\"pipe\">\n"
	"\t\t\t<pipe/>\n"
	"\t\t</dir>\n"
	"\t\t<tar name=\"qt6_dejavusans.tar\"/>\n"
	"\t\t<tar name=\"qt6_libqgenode.tar\"/>\n"
	"\t\t<tar name=\"files_demo.tar\"/>\n"
	"\t\t<dir name=\"writable\">\n"
	"\t\t\t<ram/>\n"
	"\t\t</dir>\n"
	"\t</vfs>\n"
	"</config>\n";

/*
 * FIX_ATTRS: name-first-attribute matching. The needle literals
 * require 'name="' immediately after '<start ' / '<binary ', so a
 * name that is not the FIRST attribute is not rewritten, and empty or
 * path-like values are left untouched.
 */
char const *const FIX_ATTRS =
	"<config>\n"
	"\t<start name=\"a\">\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<start x=\"1\" name=\"b\">\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<start  name=\"c\">\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<binary name=\"d\"/>\n"
	"\t<binary x=\"2\" name=\"e\"/>\n"
	"\t<binary name=\"/path\"/>\n"
	"\t<binary name=\"\"/>\n"
	"</config>\n";

/*
 * FIX_CHILDREN: the first-child '<b' heuristic and its preserved
 * quirks (with enough trailing bytes for the 8-byte lookahead):
 *
 *   s1 first child <book>  -> '<b' matches -> NOT injected (quirk)
 *   s2 first child <binary name="x"/> -> NOT injected; x -> pre/x
 *   s3 first child <config>          -> injected pre/s3
 *   s4 self-closing <start/> -> injected AFTER '/>' (quirk)
 *   s5 first child <b/>    -> NOT injected (quirk)
 *   s6 <binary> NOT first child -> injected pre/s6 AND y -> pre/y
 *      (both fire — two <binary> entries, quirk)
 *   s7 whitespace (\t \r \n) skipped before the first child ->
 *      injected pre/s7
 */
char const *const FIX_CHILDREN =
	"<config>\n"
	"\t<start name=\"s1\">\n"
	"\t\t<book/>\n"
	"\t</start>\n"
	"\t<start name=\"s2\"><binary name=\"x\"/></start>\n"
	"\t<start name=\"s3\">\n"
	"\t\t<config/>\n"
	"\t</start>\n"
	"\t<start name=\"s4\"/>\n"
	"\t<start name=\"s5\">\n"
	"\t\t<b/>\n"
	"\t</start>\n"
	"\t<start name=\"s6\"><provides><binary name=\"y\"/></provides></start>\n"
	"\t<start name=\"s7\"> \t\r\n <config/></start>\n"
	"</config>\n";

/*
 * FIX_COMMENTS: raw-text scan — needle-shaped text inside comments is
 * rewritten too (preserved; production-reachable via raw attribute
 * values, which the serializer copies verbatim).
 */
char const *const FIX_COMMENTS =
	"<config>\n"
	"\t<!-- <start name=\"c1\"> kept -->\n"
	"\t<!-- <binary name=\"b1\"/> -->\n"
	"\t<start name=\"c2\">\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<binary name=\"b2\"/>\n"
	"</config>\n";

/*
 * Snapshot goldens captured from the reference implementation
 * (`--dump`) and cross-checked against it on every run. The terminal
 * snapshot covers the three nested-start outcomes (terminal/vfs
 * injected right after the open tag, vfs_rom's own binary prefixed
 * without injection, /bin/bash untouched); the files snapshot covers
 * the needle-free composition passthrough; attrs/children/comments
 * cover the name-first matching, the first-child '<b' quirks and the
 * comment-text rewriting; the tail snapshots cover the unterminated
 * quote/tag behavior and the 8-byte first-child lookahead guard.
 */
char const *const GOLDEN_TERMINAL =
	"<config>\n"
	"\t<parent-provides>\n"
	"\t\t<service name=\"ROM\"/>\n"
	"\t\t<service name=\"PD\"/>\n"
	"\t\t<service name=\"RM\"/>\n"
	"\t\t<service name=\"CPU\"/>\n"
	"\t\t<service name=\"LOG\"/>\n"
	"\t\t<service name=\"Timer\"/>\n"
	"\t\t<service name=\"Gui\"/>\n"
	"\t\t<service name=\"Terminal\"/>\n"
	"\t\t<service name=\"Report\"/>\n"
	"\t</parent-provides>\n"
	"\t<default caps=\"120\"/>\n"
	"\t<default-route>\n"
	"\t\t<any-service>\n"
	"\t\t\t<parent/>\n"
	"\t\t\t<any-child/>\n"
	"\t\t</any-service>\n"
	"\t</default-route>\n"
	"\t<start name=\"terminal\" caps=\"150\" ram=\"48M\"><binary name=\"bin/terminal\"/>\n"
	"\t\t<provides>\n"
	"\t\t\t<service name=\"Terminal\"/>\n"
	"\t\t</provides>\n"
	"\t\t<config ld_check_ctors=\"false\">\n"
	"\t\t\t<initial width=\"800\" height=\"600\"/>\n"
	"\t\t\t<vfs>\n"
	"\t\t\t\t<rom name=\"VeraMono.ttf\"/>\n"
	"\t\t\t\t<dir name=\"font\">\n"
	"\t\t\t\t\t<dir name=\"monospace\">\n"
	"\t\t\t\t\t\t<ttf name=\"regular\" path=\"/VeraMono.ttf\" size_px=\"14\"/>\n"
	"\t\t\t\t\t</dir>\n"
	"\t\t\t\t</dir>\n"
	"\t\t\t</vfs>\n"
	"\t\t</config>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"Gui\">\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t\t<any-child/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"\t<start name=\"vfs\" caps=\"250\" ram=\"48M\"><binary name=\"bin/vfs\"/>\n"
	"\t\t<provides>\n"
	"\t\t\t<service name=\"File_system\"/>\n"
	"\t\t</provides>\n"
	"\t\t<config>\n"
	"\t\t\t<vfs>\n"
	"\t\t\t\t<tar name=\"bash-minimal.tar\"/>\n"
	"\t\t\t\t<tar name=\"vim-minimal.tar\"/>\n"
	"\t\t\t\t<tar name=\"coreutils-minimal.tar\"/>\n"
	"\t\t\t\t<dir name=\"dev\">\n"
	"\t\t\t\t\t<zero/>\n"
	"\t\t\t\t\t<null/>\n"
	"\t\t\t\t\t<log/>\n"
	"\t\t\t\t\t<terminal/>\n"
	"\t\t\t\t\t<terminal name=\"vct\" label=\"vct\"/>\n"
	"\t\t\t\t\t<inline name=\"rtc\">2018-01-01 00:01</inline>\n"
	"\t\t\t\t</dir>\n"
	"\t\t\t</vfs>\n"
	"\t\t\t<policy label_prefix=\"vfs_rom\" root=\"/\"/>\n"
	"\t\t\t<default-policy root=\"/\" writeable=\"yes\"/>\n"
	"\t\t</config>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"Terminal\" label=\"vct\">\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"Terminal\">\n"
	"\t\t\t\t<child name=\"terminal\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"\t<start name=\"vfs_rom\" ram=\"32M\">\n"
	"\t\t<binary name=\"bin/cached_fs_rom\"/>\n"
	"\t\t<provides>\n"
	"\t\t\t<service name=\"ROM\"/>\n"
	"\t\t</provides>\n"
	"\t\t<config/>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"File_system\">\n"
	"\t\t\t\t<child name=\"vfs\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"\t<start name=\"/bin/bash\" caps=\"450\" ram=\"64M\">\n"
	"\t\t<config>\n"
	"\t\t\t<libc stdin=\"/dev/terminal\" stdout=\"/dev/terminal\" stderr=\"/dev/terminal\" rtc=\"/dev/rtc\" pipe=\"/pipe\"/>\n"
	"\t\t\t<vfs>\n"
	"\t\t\t\t<fs/>\n"
	"\t\t\t</vfs>\n"
	"\t\t\t<arg value=\"bash\"/>\n"
	"\t\t\t<env name=\"TERM\">screen</env>\n"
	"\t\t\t<env name=\"PATH\">/bin</env>\n"
	"\t\t\t<env name=\"PS1\">sponge:$PWD&gt; </env>\n"
	"\t\t\t<env name=\"SHELL\">/bin/bash</env>\n"
	"\t\t</config>\n"
	"\t\t<route>\n"
	"\t\t\t<service name=\"File_system\">\n"
	"\t\t\t\t<child name=\"vfs\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"ROM\" label_suffix=\".lib.so\">\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"ROM\" label_last=\"/bin/bash\">\n"
	"\t\t\t\t<child name=\"vfs_rom\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<service name=\"ROM\" label_prefix=\"/bin\">\n"
	"\t\t\t\t<child name=\"vfs_rom\"/>\n"
	"\t\t\t</service>\n"
	"\t\t\t<any-service>\n"
	"\t\t\t\t<parent/>\n"
	"\t\t\t\t<any-child/>\n"
	"\t\t\t</any-service>\n"
	"\t\t</route>\n"
	"\t</start>\n"
	"</config>\n";

char const *const GOLDEN_FILES =
	"<config>\n"
	"\t<libc stdout=\"/dev/log\" stderr=\"/dev/log\" pipe=\"/pipe\" rtc=\"/dev/rtc\"/>\n"
	"\t<vfs>\n"
	"\t\t<dir name=\"dev\">\n"
	"\t\t\t<log/>\n"
	"\t\t\t<inline name=\"rtc\">2018-01-01 00:01</inline>\n"
	"\t\t</dir>\n"
	"\t\t<dir name=\"pipe\">\n"
	"\t\t\t<pipe/>\n"
	"\t\t</dir>\n"
	"\t\t<tar name=\"qt6_dejavusans.tar\"/>\n"
	"\t\t<tar name=\"qt6_libqgenode.tar\"/>\n"
	"\t\t<tar name=\"files_demo.tar\"/>\n"
	"\t\t<dir name=\"writable\">\n"
	"\t\t\t<ram/>\n"
	"\t\t</dir>\n"
	"\t</vfs>\n"
	"</config>\n";

char const *const GOLDEN_ATTRS =
	"<config>\n"
	"\t<start name=\"a\"><binary name=\"pre/a\"/>\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<start x=\"1\" name=\"b\">\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<start  name=\"c\">\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<binary name=\"pre/d\"/>\n"
	"\t<binary x=\"2\" name=\"e\"/>\n"
	"\t<binary name=\"/path\"/>\n"
	"\t<binary name=\"\"/>\n"
	"</config>\n";

char const *const GOLDEN_CHILDREN =
	"<config>\n"
	"\t<start name=\"s1\">\n"
	"\t\t<book/>\n"
	"\t</start>\n"
	"\t<start name=\"s2\"><binary name=\"pre/x\"/></start>\n"
	"\t<start name=\"s3\"><binary name=\"pre/s3\"/>\n"
	"\t\t<config/>\n"
	"\t</start>\n"
	"\t<start name=\"s4\"/><binary name=\"pre/s4\"/>\n"
	"\t<start name=\"s5\">\n"
	"\t\t<b/>\n"
	"\t</start>\n"
	"\t<start name=\"s6\"><binary name=\"pre/s6\"/><provides><binary name=\"pre/y\"/></provides></start>\n"
	"\t<start name=\"s7\"><binary name=\"pre/s7\"/> \t\r\n"
	" <config/></start>\n"
	"</config>\n";

char const *const GOLDEN_COMMENTS =
	"<config>\n"
	"\t<!-- <start name=\"c1\"><binary name=\"bin/c1\"/> kept -->\n"
	"\t<!-- <binary name=\"bin/b1\"/> -->\n"
	"\t<start name=\"c2\"><binary name=\"bin/c2\"/>\n"
	"\t\t<child/>\n"
	"\t</start>\n"
	"\t<binary name=\"bin/b2\"/>\n"
	"</config>\n";

char const *const GOLDEN_TAIL_BINARY_QUOTE =
	"<binary name=\"pre/foo\"";

char const *const GOLDEN_TAIL_START_TAG =
	"<start name=\"x\"<binary name=\"pre/x\"/>";

char const *const GOLDEN_TAIL_LOOKAHEAD =
	"<start name=\"s\"><binary name=\"pre/s\"/><b";

char const *const GOLDEN_TAIL_SHORT_FIRST =
	"<start name=\"s\"><binary name=\"pre/s\"/><b/>";


struct Fixture
{
	char const *name;
	char const *cfg;
	char const *prefix;
	char const *golden;   /* "" until captured from --dump */
};

Fixture const FIXTURES[] = {
	{ "terminal", FIX_TERMINAL, "bin/", GOLDEN_TERMINAL },
	{ "files",    FIX_FILES,    "bin/", GOLDEN_FILES },
	{ "attrs",    FIX_ATTRS,    "pre/", GOLDEN_ATTRS },
	{ "children", FIX_CHILDREN, "pre/", GOLDEN_CHILDREN },
	{ "comments", FIX_COMMENTS, "bin/", GOLDEN_COMMENTS },

	/* Unterminated <binary name=" quote: the output still closes it. */
	{ "tail-binary-quote", "<binary name=\"foo",     "pre/", GOLDEN_TAIL_BINARY_QUOTE },

	/* Unterminated <start name=" tag: everything up to cfg_len is
	 * emitted, the injection follows, no byte past cfg_len is read
	 * (the documented clamp). */
	{ "tail-start-tag",    "<start name=\"x\"",      "pre/", GOLDEN_TAIL_START_TAG },

	/* '<b' within the last 7 bytes: the 8-byte lookahead guard
	 * (next + 7 < cfg_len) fails and the start is injected anyway. */
	{ "tail-lookahead",    "<start name=\"s\"><b",   "pre/", GOLDEN_TAIL_LOOKAHEAD },
	{ "tail-short-first",  "<start name=\"s\"><b/>", "pre/", GOLDEN_TAIL_SHORT_FIRST },
};

constexpr unsigned NUM_FIXTURES = sizeof(FIXTURES) / sizeof(FIXTURES[0]);


/* ===================== comparison harness ===================== */

unsigned failures = 0;

void expect_bytes(char const *what,
                  char const *expected, Genode::size_t expected_len,
                  char const *actual,   Genode::size_t actual_len)
{
	if (expected_len == actual_len
	    && memcmp(expected, actual, (size_t)expected_len) == 0)
		return;

	++failures;
	printf("FAIL: %s\n", what);
	printf("--- expected (%zu bytes) ---\n", (size_t)expected_len);
	fwrite(expected, 1, (size_t)expected_len, stdout);
	printf("\n--- actual (%zu bytes) ---\n", (size_t)actual_len);
	fwrite(actual, 1, (size_t)actual_len, stdout);

	/* first diverging offset */
	Genode::size_t n = expected_len < actual_len ? expected_len : actual_len;
	Genode::size_t i = 0;
	while (i < n && expected[i] == actual[i]) ++i;
	printf("\nfirst divergence at offset %zu: expected 0x%02x vs actual 0x%02x\n",
	       (size_t)i,
	       (unsigned)(i < expected_len ? (unsigned char)expected[i] : 0),
	       (unsigned)(i < actual_len   ? (unsigned char)actual[i]   : 0));
}

char ref_buf[64 * 1024];
char lib_buf[64 * 1024];

}  /* anonymous namespace */


int main(int argc, char **argv)
{
	bool const dump = (argc > 1 && strcmp(argv[1], "--dump") == 0);

	/* ---- 1. equivalence + golden snapshots per fixture ---- */
	for (unsigned f = 0; f < NUM_FIXTURES; ++f) {
		Fixture const &fx = FIXTURES[f];
		Genode::size_t const cfg_len = Genode::strlen(fx.cfg);

		Genode::size_t const ref_len =
			ref_rewrite(ref_buf, sizeof(ref_buf), fx.cfg, cfg_len, fx.prefix);
		Genode::size_t const lib_len =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               fx.cfg, cfg_len, fx.prefix);

		char what[128];
		snprintf(what, sizeof(what), "equivalence: fixture %s", fx.name);
		expect_bytes(what, ref_buf, ref_len + 1, lib_buf, lib_len + 1);

		snprintf(what, sizeof(what), "golden: fixture %s", fx.name);
		expect_bytes(what, fx.golden, Genode::strlen(fx.golden),
		             lib_buf, lib_len);

		if (dump) {
			printf("=== %s ===\n", fx.name);
			fwrite(lib_buf, 1, (size_t)lib_len, stdout);
			printf("\n=== end %s ===\n", fx.name);
		}
	}

	/* ---- 2. empty-prefix identity (and nullptr prefix) ---- */
	{
		Genode::size_t const cfg_len = Genode::strlen(FIX_TERMINAL);

		Genode::size_t const lib_len =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               FIX_TERMINAL, cfg_len, "");
		expect_bytes("empty prefix: identity copy",
		             FIX_TERMINAL, cfg_len, lib_buf, lib_len);
		expect_bytes("empty prefix: identity copy (length)",
		             (char const *)&cfg_len, sizeof(cfg_len),
		             (char const *)&lib_len, sizeof(lib_len));

		Genode::size_t const ref_len =
			ref_rewrite(ref_buf, sizeof(ref_buf), FIX_TERMINAL, cfg_len, "");
		expect_bytes("empty prefix: ref equivalence",
		             ref_buf, ref_len + 1, lib_buf, lib_len + 1);

		Genode::size_t const nil_len =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               FIX_TERMINAL, cfg_len, nullptr);
		expect_bytes("nullptr prefix: identity copy",
		             FIX_TERMINAL, cfg_len, lib_buf, nil_len);
	}

	/* ---- 3. non-idempotence (preserved) ---- */
	{
		Genode::size_t const cfg_len = Genode::strlen(FIX_TERMINAL);

		Genode::size_t const pass1 =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               FIX_TERMINAL, cfg_len, "bin/");
		char out1[64 * 1024];
		memcpy(out1, lib_buf, (size_t)pass1 + 1);

		Genode::size_t const pass2 =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               out1, pass1, "bin/");
		if (pass2 == pass1 && memcmp(out1, lib_buf, (size_t)pass1) == 0) {
			printf("FAIL: non-idempotence: second pass changed nothing\n");
			++failures;
		}

		Genode::size_t const ref2 =
			ref_rewrite(ref_buf, sizeof(ref_buf), out1, pass1, "bin/");
		expect_bytes("non-idempotence: ref equivalence (pass 2)",
		             ref_buf, ref2 + 1, lib_buf, pass2 + 1);
	}

	/* ---- 4. byte-opaque input (embedded NUL passes through) ---- */
	{
		char const cfg[] =
			"<binary name=\"z\"/>\0tail";
		Genode::size_t const cfg_len = sizeof(cfg) - 1;
		char const expected[] =
			"<binary name=\"pre/z\"/>\0tail";
		Genode::size_t const expected_len = sizeof(expected) - 1;

		Genode::size_t const lib_len =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               cfg, cfg_len, "pre/");
		expect_bytes("embedded NUL: byte-opaque rewrite",
		             expected, expected_len, lib_buf, lib_len);

		Genode::size_t const ref_len =
			ref_rewrite(ref_buf, sizeof(ref_buf), cfg, cfg_len, "pre/");
		expect_bytes("embedded NUL: ref equivalence",
		             ref_buf, ref_len + 1, lib_buf, lib_len + 1);
	}

	/* ---- 5. output bounds (silent truncation at out_len - 1) ---- */
	{
		Genode::size_t const cfg_len = Genode::strlen(FIX_TERMINAL);
		Genode::size_t const full =
			Sponge::Binary_prefix::rewrite(lib_buf, sizeof(lib_buf),
			                               FIX_TERMINAL, cfg_len, "bin/");
		char out_full[64 * 1024];
		memcpy(out_full, lib_buf, (size_t)full + 1);

		/* exact fit: out_len = full + 1 (content + NUL) */
		char fit[64 * 1024];
		Genode::size_t const fit_len =
			Sponge::Binary_prefix::rewrite(fit, full + 1,
			                               FIX_TERMINAL, cfg_len, "bin/");
		expect_bytes("bounds: exact fit", out_full, full + 1, fit, fit_len + 1);
		expect_bytes("bounds: exact fit (length)",
		             (char const *)&full, sizeof(full),
		             (char const *)&fit_len, sizeof(fit_len));

		/* one byte short: truncated to out_len - 1 bytes + NUL */
		Genode::size_t const want_trunc = full - 1;
		char trunc[64 * 1024];
		memset(trunc, 0xA5, sizeof(trunc));
		Genode::size_t const trunc_len =
			Sponge::Binary_prefix::rewrite(trunc, full,
			                               FIX_TERMINAL, cfg_len, "bin/");
		expect_bytes("bounds: truncation (length)",
		             (char const *)&want_trunc, sizeof(want_trunc),
		             (char const *)&trunc_len, sizeof(trunc_len));
		expect_bytes("bounds: truncation (prefix of full output)",
		             out_full, trunc_len, trunc, trunc_len);
		if (trunc[trunc_len] != 0) {
			printf("FAIL: bounds: truncation is not NUL-terminated\n");
			++failures;
		}

		/* out_len == 1: empty output, single NUL */
		Genode::size_t const want_zero = 0;
		char one[1];
		memset(one, 0xA5, sizeof(one));
		Genode::size_t const one_len =
			Sponge::Binary_prefix::rewrite(one, 1, FIX_TERMINAL, cfg_len, "bin/");
		expect_bytes("bounds: out_len 1 (length)",
		             (char const *)&want_zero, sizeof(want_zero),
		             (char const *)&one_len, sizeof(one_len));
		if (one[0] != 0) {
			printf("FAIL: bounds: out_len 1 is not a lone NUL\n");
			++failures;
		}

		/* out_len == 0: no-op, buffer untouched */
		char zero[1];
		zero[0] = 0x5A;
		Genode::size_t const zero_len =
			Sponge::Binary_prefix::rewrite(zero, 0, FIX_TERMINAL, cfg_len, "bin/");
		expect_bytes("bounds: out_len 0 (length)",
		             (char const *)&want_zero, sizeof(want_zero),
		             (char const *)&zero_len, sizeof(zero_len));
		if (zero[0] != 0x5A) {
			printf("FAIL: bounds: out_len 0 wrote to the buffer\n");
			++failures;
		}
	}

	if (failures) {
		printf("self-test: FAIL (%u divergence(s))\n", failures);
		return 1;
	}
	printf("self-test: PASS (equivalence + golden snapshots)\n");
	return 0;
}
