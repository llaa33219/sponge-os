/* SPDX-License-Identifier: Apache-2.0
 *
 * Host-side self-test for sponge_pkg_admission (Phase 16 P1.2).
 *
 * NOT part of the Genode build (deliberately absent from
 * lib/mk/sponge_pkg_admission.mk), so it lives outside the shared build
 * run. It proves the extraction claim two ways:
 *
 *   1. Equivalence: every fixture runs through the library AND through
 *      `ref_*` below — verbatim transcriptions of the pre-extraction
 *      admission arithmetic (sponge_pkgd admission.cc's _quota_bytes /
 *      _read_budget HID walk / _admits margin comparison, and ops.cc's
 *      install-loop decrement pair) — and the answers must match on
 *      every case, including the preserved quirks.
 *
 *   2. Targeted assertions: the quota strings/units accepted today
 *      (with their exact values, including the suffix-then-junk quirk
 *      and the String<16> truncation contract), the budget decode over
 *      real HID state-report shapes (generated with Genode's own
 *      Hid_generator AND hand-written literals), unavailable/invalid
 *      budgets (missing nodes, unparsable avail, caps 0 / caps above
 *      0xffffff, missing end marker, empty body), the exact-fit vs
 *      one-byte/one-cap refusal boundary around the ADMISSION_MARGIN_*
 *      margins, and the budget decrement arithmetic (including the
 *      preserved caps saturation and unsigned wrap semantics).
 *
 * BUILD & RUN (from the repository root; host g++ only — this never
 * touches the Genode build tree):
 *
 *   g++ -std=c++20 -O1 \
 *       -I genode/repos/base/include \
 *       -I genode/repos/base/include/spec/64bit \
 *       -I genode/repos/base/include/spec/x86_64 \
 *       -I genode/repos/base/include/spec/x86 \
 *       -I genode/repos/base/src/include \
 *       -I repos/sponge/include \
 *       repos/sponge/lib/src/sponge_pkg_admission/self_test.cc \
 *       repos/sponge/lib/src/sponge_pkg_admission/pkg_admission.cc \
 *       genode/repos/base/src/lib/base/hid.cc \
 *       genode/repos/base/src/lib/base/output.cc \
 *       -o /tmp/sponge_pkg_admission_self_test \
 *   && /tmp/sponge_pkg_admission_self_test
 *
 * Exit code 0 and a final "self-test: PASS" line mean every check
 * matched. "self-test: FAIL" prints every divergence.
 *
 * The three host stubs below (Genode::Log, Genode::Lock) exist only to
 * satisfy the linker for symbols hid.cc references from its generator
 * side (the invalid-attribute warning); the decode path never logs and
 * the test never triggers them.
 */

#include <sponge/pkg_admission.h>

#include <base/lock.h>
#include <util/hid.h>
#include <util/xml_node.h>

#include <stdio.h>
#include <string.h>


/* ===================== host stubs (link only) ===================== */

namespace {

struct Null_output : Genode::Output
{
	void out_char(char) override { }
	void out_string(char const *, Genode::size_t) override { }
};

Null_output null_output { };

}  /* anonymous namespace */

Genode::Log &Genode::Log::log()
{
	static Genode::Log instance { null_output };
	return instance;
}

void Genode::Log::_acquire(Type) { }
void Genode::Log::_release() { }

Genode::Lock::Lock(Genode::Lock::State initial)
:
	_spinlock_state(0), _state(initial), _last_applicant(0), _owner(nullptr)
{ }


namespace {

using Sponge::Pkg_admission::ADMISSION_MARGIN_CAPS;
using Sponge::Pkg_admission::ADMISSION_MARGIN_RAM;

/* ===================== comparison harness ===================== */

unsigned long checks    { 0 };
unsigned long failures  { 0 };

void fail(char const *what)
{
	printf("FAIL: %s\n", what);
	++failures;
}

void check(bool cond, char const *what)
{
	++checks;
	if (!cond) fail(what);
}


/* ===================== reference implementation ===================== */
/*
 * Verbatim transcriptions of the pre-extraction arithmetic, in the
 * original expression shapes. The margin constants are literal copies
 * of the pre-extraction pkgd.h declarations (a drift of the library
 * constants is caught by comparing against these, plus the explicit
 * constant-equality checks below).
 */

namespace Ref {

static constexpr Genode::size_t ADMISSION_MARGIN_RAM  = 8u * 1024 * 1024;
static constexpr unsigned       ADMISSION_MARGIN_CAPS = 64;

/* sponge_pkgd admission.cc, Main::_quota_bytes */
bool quota_bytes(Genode::String<16> const &q, Genode::size_t &out)
{
	char const *p = q.string();
	Genode::size_t v { 0 };
	unsigned digits { 0 };
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + Genode::size_t(*p - '0');
		++p; ++digits;
	}
	if (!digits) return false;

	switch (*p) {
	case 'K': case 'k': v *= 1024; break;
	case 'M': case 'm': v *= 1024 * 1024; break;
	case 'G': case 'g': v *= 1024 * 1024 * 1024; break;
	case '\0':            break;
	default:               return false;
	}
	out = v;
	return true;
}

/* sponge_pkgd admission.cc, Main::_read_budget's HID walk (the ROM
 * I/O around it stayed in the shell; this is the extracted body) */
bool decode_budget(char const *buf, Genode::size_t len,
                   Genode::size_t &avail_ram, unsigned &avail_caps)
{
	bool ram_ok { false }, caps_ok { false };
	Genode::size_t ram_bytes { 0 }, caps_value { 0 };

	try {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(buf, len));

		Genode::String<16> ram_avail  { }, caps_avail { };
		root.for_each_sub_node([&] (Genode::Hid_node const &n) {
			if (n.has_type("ram") && !ram_ok) {
				ram_avail = n.attribute_value("avail", Genode::String<16>());
				ram_ok = Ref::quota_bytes(ram_avail, ram_bytes);
			}
			if (n.has_type("caps") && !caps_ok) {
				caps_avail = n.attribute_value("avail", Genode::String<16>());
				caps_ok = Ref::quota_bytes(caps_avail, caps_value) &&
				          caps_value > 0 && caps_value <= 0xffffffu;
			}
		});
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		return false;
	}

	if (!ram_ok || !caps_ok)
		return false;

	avail_ram  = ram_bytes;
	avail_caps = (unsigned)caps_value;
	return true;
}

/* sponge_pkgd admission.cc, Main::_admits margin comparison */
bool admits(Genode::size_t avail_ram, unsigned avail_caps,
            Genode::size_t need_ram, unsigned need_caps)
{
	if (avail_ram < need_ram + ADMISSION_MARGIN_RAM)
		return false;
	if (avail_caps < need_caps + ADMISSION_MARGIN_CAPS)
		return false;
	return true;
}

/* sponge_pkgd ops.cc, install-loop decrement pair */
void consume(Genode::size_t &avail_ram, unsigned &avail_caps,
             Genode::size_t need_ram, unsigned need_caps)
{
	avail_ram  -= need_ram;
	avail_caps -= (need_caps > avail_caps ? 0u : need_caps);
}

}  /* namespace Ref */


/* ===================== quota parsing ===================== */

void quota_case(char const *input, bool expect_ok, Genode::size_t expect_val,
                char const *label)
{
	Genode::String<16> const q { input };
	Genode::size_t out { ~0UL }, ref_out { ~0UL };

	bool const ok     = Sponge::Pkg_admission::quota_bytes(q, out);
	bool const ref_ok = Ref::quota_bytes(q, ref_out);

	++checks;
	if (ok != ref_ok || (ok && out != ref_out)) {
		printf("FAIL: quota equivalence diverged for '%s'\n", label);
		++failures;
		return;
	}
	++checks;
	if (ok != expect_ok) {
		printf("FAIL: quota '%s': ok=%d expected %d\n", label, ok, expect_ok);
		++failures;
		return;
	}
	if (expect_ok) {
		++checks;
		if (out != expect_val) {
			printf("FAIL: quota '%s': got %llu expected %llu\n", label,
			       (unsigned long long)out,
			       (unsigned long long)expect_val);
			++failures;
		}
	}
}

void test_quota_parsing()
{
	/* units and shapes accepted today */
	quota_case("0",       true, 0,                          "zero");
	quota_case("1",       true, 1,                          "one byte");
	quota_case("12345",   true, 12345,                      "plain count");
	quota_case("512K",    true, 512ull * 1024,              "512K");
	quota_case("512k",    true, 512ull * 1024,              "512k lower");
	quota_case("48M",     true, 48ull * 1024 * 1024,        "48M");
	quota_case("48m",     true, 48ull * 1024 * 1024,        "48m lower");
	quota_case("64M",     true, 64ull * 1024 * 1024,        "64M report figure");
	quota_case("96M",     true, 96ull * 1024 * 1024,        "96M");
	quota_case("1024M",   true, 1024ull * 1024 * 1024,      "1024M");
	quota_case("1G",      true, 1024ull * 1024 * 1024,      "1G");
	quota_case("1g",      true, 1024ull * 1024 * 1024,      "1g lower");
	quota_case("2G",      true, 2048ull * 1024 * 1024,      "2G");

	/* preserved quirk: trailing junk AFTER the suffix is ignored */
	quota_case("12M3",    true, 12ull * 1024 * 1024,        "12M3 suffix-junk");
	quota_case("12mM",    true, 12ull * 1024 * 1024,        "12mM suffix-junk");

	/* malformed quotas refuse ("no answer") */
	quota_case("",        false, 0,                         "empty");
	quota_case("M",       false, 0,                         "suffix only");
	quota_case("X",       false, 0,                         "letter only");
	quota_case("12X",     false, 0,                         "bad suffix char");
	quota_case("12 ",     false, 0,                         "trailing space");
	quota_case("12 3",    false, 0,                         "inner space");
	quota_case("12.5M",   false, 0,                         "decimal point");
	quota_case("-12M",    false, 0,                         "minus sign");
	quota_case("+12M",    false, 0,                         "plus sign");
	quota_case(" 12M",    false, 0,                         "leading space");
	quota_case("0x10",    false, 0,                         "hex shape");

	/* String<16> truncation contract: over-long input is cut to 15
	 * chars BEFORE parsing, exactly as the pre-extraction call sites
	 * (Package::ram, report attribute_value) delivered it */
	{
		Genode::String<16> const long_q { "12345678901234567890" };
		Genode::String<16> const cut_q  { "123456789012345" };
		Genode::size_t a { 0 }, b { 0 };
		bool const ok_a = Sponge::Pkg_admission::quota_bytes(long_q, a);
		bool const ok_b = Sponge::Pkg_admission::quota_bytes(cut_q, b);
		check(ok_a && ok_b && a == b,
		      "quota: String<16> truncation matches the 15-char parse");
		check(Genode::strcmp(long_q.string(), cut_q.string()) == 0,
		      "quota: String<16> really truncated the input");
	}
}


/* ===================== budget decode ===================== */

void budget_case(char const *buf, Genode::size_t len, bool expect_ok,
                 Genode::size_t expect_ram, unsigned expect_caps,
                 char const *label)
{
	Genode::size_t ram { ~0UL }, ref_ram { ~0UL };
	unsigned       caps { ~0u }, ref_caps { ~0u };

	bool const ok     = Sponge::Pkg_admission::decode_budget(
		buf, len, ram, caps);
	bool const ref_ok = Ref::decode_budget(buf, len, ref_ram, ref_caps);

	++checks;
	if (ok != ref_ok || (ok && (ram != ref_ram || caps != ref_caps))) {
		printf("FAIL: budget equivalence diverged for '%s'\n", label);
		++failures;
		return;
	}
	++checks;
	if (ok != expect_ok) {
		printf("FAIL: budget '%s': ok=%d expected %d\n", label, ok, expect_ok);
		++failures;
		return;
	}
	if (expect_ok) {
		++checks;
		if (ram != expect_ram || caps != expect_caps) {
			printf("FAIL: budget '%s': got ram=%llu caps=%u "
			       "expected ram=%llu caps=%u\n", label,
			       (unsigned long long)ram, caps,
			       (unsigned long long)expect_ram, expect_caps);
			++failures;
		}
	}
}

void test_budget_decode()
{
	/* Hand-written HID literal in the exact state-report shape the
	 * sandbox Generator emits (root type line, "+ " sub-nodes with
	 * " | tag: value" attributes, "-" end marker). */
	static char const literal[] =
		"state\n"
		"+ ram  | quota: 160M | used: 20M | avail: 140M\n"
		"+ caps | quota: 3200 | used: 100 | avail: 3100\n"
		"-\n";
	budget_case(literal, sizeof(literal) - 1, true,
	            140ull * 1024 * 1024, 3100, "literal report");

	/* Same content produced by Genode's own Hid_generator — the
	 * serializer the 26.08 sandbox uses (round-trip pin). */
	{
		char buf[256];
		Genode::Hid_generator::Result const r =
			Genode::Hid_generator::generate(
				Genode::Byte_range_ptr(buf, sizeof(buf)), "state",
				[] (Genode::Hid_generator &g) {
					g.tabular([&] {
						g.node("ram", [&] {
							g.attribute("quota", "160M");
							g.attribute("used", "20M");
							g.attribute("avail", "140M"); });
						g.node("caps", [&] {
							g.attribute("quota", "3200");
							g.attribute("used", "100");
							g.attribute("avail", "3100"); });
					});
				});
		Genode::size_t len = 0;
		bool const ok = r.convert<bool>(
			[&] (Genode::size_t n) { len = n; return true; },
			[&] (Genode::Buffer_error) { return false; });
		check(ok, "budget: Hid_generator fixture rendered");
		if (ok)
			budget_case(buf, len, true,
			            140ull * 1024 * 1024, 3100,
			            "generated report");
	}

	/* Plain byte counts (no suffix) — the report may print exact
	 * figures; must NOT be misread as suffixed. */
	static char const plain[] =
		"state\n"
		"+ ram  | avail: 61776536\n"
		"+ caps | avail: 1034\n"
		"-\n";
	budget_case(plain, sizeof(plain) - 1, true, 61776536, 1034,
	            "plain byte counts");

	/* RAM has no range check: avail 0 is a usable budget. */
	static char const ram_zero[] =
		"state\n"
		"+ ram  | avail: 0\n"
		"+ caps | avail: 1\n"
		"-\n";
	budget_case(ram_zero, sizeof(ram_zero) - 1, true, 0, 1,
	            "ram avail 0 admitted");

	/* Unavailable / invalid budgets -> "no usable budget" (false). */
	static char const no_ram[] =
		"state\n"
		"+ caps | avail: 100\n"
		"-\n";
	budget_case(no_ram, sizeof(no_ram) - 1, false, 0, 0, "missing ram node");

	static char const no_caps[] =
		"state\n"
		"+ ram | avail: 1M\n"
		"-\n";
	budget_case(no_caps, sizeof(no_caps) - 1, false, 0, 0, "missing caps node");

	static char const only_root[] = "state\n-\n";
	budget_case(only_root, sizeof(only_root) - 1, false, 0, 0, "no sub nodes");

	budget_case("", 0, false, 0, 0, "empty buffer");

	static char const no_end_marker[] =
		"state\n"
		"+ ram  | avail: 1M\n"
		"+ caps | avail: 100\n";
	budget_case(no_end_marker, sizeof(no_end_marker) - 1, false, 0, 0,
	            "missing HID end marker");

	static char const bad_ram[] =
		"state\n"
		"+ ram  | avail: 12X\n"
		"+ caps | avail: 100\n"
		"-\n";
	budget_case(bad_ram, sizeof(bad_ram) - 1, false, 0, 0, "unparsable ram");

	static char const bad_caps[] =
		"state\n"
		"+ ram  | avail: 1M\n"
		"+ caps | avail: 12X\n"
		"-\n";
	budget_case(bad_caps, sizeof(bad_caps) - 1, false, 0, 0, "unparsable caps");

	static char const caps_zero[] =
		"state\n"
		"+ ram  | avail: 1M\n"
		"+ caps | avail: 0\n"
		"-\n";
	budget_case(caps_zero, sizeof(caps_zero) - 1, false, 0, 0, "caps avail 0");

	static char const caps_over[] =
		"state\n"
		"+ ram  | avail: 1M\n"
		"+ caps | avail: 16777216\n"
		"-\n";
	budget_case(caps_over, sizeof(caps_over) - 1, false, 0, 0,
	            "caps above 0xffffff");

	/* caps boundary: 0xffffff itself is the last accepted value */
	static char const caps_max[] =
		"state\n"
		"+ ram  | avail: 1M\n"
		"+ caps | avail: 16777215\n"
		"-\n";
	budget_case(caps_max, sizeof(caps_max) - 1, true, 1024u * 1024, 0xffffffu,
	            "caps 0xffffff boundary");

	/* First-node fallthrough: a malformed first node does not mask a
	 * later good one, and a good first node is not overwritten. */
	static char const fallthrough[] =
		"state\n"
		"+ ram  | avail: 12X\n"
		"+ ram  | avail: 2M\n"
		"+ caps | avail: 5\n"
		"+ caps | avail: 7\n"
		"-\n";
	budget_case(fallthrough, sizeof(fallthrough) - 1, true,
	            2ull * 1024 * 1024, 5, "bad first node falls through");

	static char const first_wins[] =
		"state\n"
		"+ ram  | avail: 2M\n"
		"+ ram  | avail: 9M\n"
		"+ caps | avail: 7\n"
		"+ caps | avail: 8\n"
		"-\n";
	budget_case(first_wins, sizeof(first_wins) - 1, true,
	            2ull * 1024 * 1024, 7, "first good node wins");

	/* Unrelated sub-nodes are ignored; per-child <ram>/<caps> deeper
	 * in <child> blocks must not be read (top-level walk only). */
	static char const decoys[] =
		"state\n"
		"+ foo  | avail: 3M\n"
		"+ child\n"
		"  + ram  | avail: 5M\n"
		"  + caps | avail: 6\n"
		"+ ram  | avail: 1M\n"
		"+ caps | avail: 2\n"
		"-\n";
	budget_case(decoys, sizeof(decoys) - 1, true,
	            1024u * 1024, 2, "decoy and nested nodes ignored");
}


/* ===================== admission comparison ===================== */

void admits_case(Genode::size_t avail_ram, unsigned avail_caps,
                 Genode::size_t need_ram, unsigned need_caps,
                 bool expect, char const *label)
{
	bool const ok     = Sponge::Pkg_admission::admits(
		avail_ram, avail_caps, need_ram, need_caps);
	bool const ref_ok = Ref::admits(avail_ram, avail_caps, need_ram, need_caps);

	++checks;
	if (ok != ref_ok) {
		printf("FAIL: admits equivalence diverged for '%s'\n", label);
		++failures;
		return;
	}
	++checks;
	if (ok != expect) {
		printf("FAIL: admits '%s': got %d expected %d\n", label, ok, expect);
		++failures;
	}
}

void test_admits()
{
	Genode::size_t const M  = ADMISSION_MARGIN_RAM;
	unsigned       const MC = ADMISSION_MARGIN_CAPS;

	check(ADMISSION_MARGIN_RAM  == Ref::ADMISSION_MARGIN_RAM,
	      "margin RAM constant unchanged");
	check(ADMISSION_MARGIN_CAPS == Ref::ADMISSION_MARGIN_CAPS,
	      "margin caps constant unchanged");

	/* Exact fit = need + margin exactly: ADMITS (boundary). */
	admits_case(96ull * 1024 * 1024 + M, 2000 + MC, 96ull * 1024 * 1024, 2000,
	            true, "exact fit (need + margin)");

	/* One byte / one cap below: refuses. */
	admits_case(96ull * 1024 * 1024 + M - 1, 2000 + MC,
	            96ull * 1024 * 1024, 2000, false, "one byte below");
	admits_case(96ull * 1024 * 1024 + M, 2000 + MC - 1,
	            96ull * 1024 * 1024, 2000, false, "one cap below");

	/* Bare need without margin room: refuses (the margin exists so a
	 * razor-thin package the sandbox would clamp never admits). */
	admits_case(96ull * 1024 * 1024, 2000, 96ull * 1024 * 1024, 2000,
	            false, "no margin room");
	admits_case(1, 1, 1, 1, false, "bare one");

	/* Zero-demand boundary around the margin itself. */
	admits_case(M - 1, MC, 0, 0, false, "zero need below margin");
	admits_case(M, MC, 0, 0, true, "zero need exactly at margin");

	/* Generous fit. */
	admits_case(512ull * 1024 * 1024, 10000, 48ull * 1024 * 1024, 512,
	            true, "generous fit");

	/* The scenario numbers from run/sponge-pkg-admission: fat_b
	 * (96M / 2000 caps) vs the ~58M left of a 160M pkg_runtime. */
	admits_case(58ull * 1024 * 1024, 1034, 96ull * 1024 * 1024, 2000,
	            false, "fat_b refusal (both dimensions)");

	/* Real-world figure from the README result example. */
	admits_case(61776536, 1034, 1073741824u, 2000, false,
	            "falkon vs small budget");

	/* One dimension at a time: ram blocks / caps blocks. */
	admits_case(96ull * 1024 * 1024 + M, 100000,
	            96ull * 1024 * 1024, 2000, true, "caps generous");
	admits_case(512ull * 1024 * 1024, 2000 + MC,
	            96ull * 1024 * 1024, 2000, true, "ram generous");
	admits_case(96ull * 1024 * 1024 + M, 2000 + MC - 1,
	            96ull * 1024 * 1024, 2000, false, "caps only blocker");
	admits_case(96ull * 1024 * 1024 + M - 1, 2000 + MC,
	            96ull * 1024 * 1024, 2000, false, "ram only blocker");

	/* Preserved unsigned arithmetic: a need so large that need+margin
	 * wraps compares against the wrapped sum (same as the
	 * pre-extraction expression — equivalence is the contract). */
	admits_case(100, 100, ~0UL - 8, 100, false, "wrapped ram need (match ref)");
}


/* ===================== budget consume ===================== */

void consume_case(Genode::size_t avail_ram, unsigned avail_caps,
                  Genode::size_t need_ram, unsigned need_caps,
                  Genode::size_t expect_ram, unsigned expect_caps,
                  char const *label)
{
	Genode::size_t ram { avail_ram }, ref_ram { avail_ram };
	unsigned       caps { avail_caps }, ref_caps { avail_caps };

	Sponge::Pkg_admission::consume(ram, caps, need_ram, need_caps);
	Ref::consume(ref_ram, ref_caps, need_ram, need_caps);

	++checks;
	if (ram != ref_ram || caps != ref_caps) {
		printf("FAIL: consume equivalence diverged for '%s'\n", label);
		++failures;
		return;
	}
	++checks;
	if (ram != expect_ram || caps != expect_caps) {
		printf("FAIL: consume '%s': got ram=%llu caps=%u "
		       "expected ram=%llu caps=%u\n", label,
		       (unsigned long long)ram, caps,
		       (unsigned long long)expect_ram, expect_caps);
		++failures;
	}
}

void test_consume()
{
	/* Plain decrement. */
	consume_case(100ull * 1024 * 1024, 1000,
	             40ull * 1024 * 1024, 300,
	             60ull * 1024 * 1024, 700, "plain decrement");

	/* Caps equal to the remaining budget empties it exactly. */
	consume_case(10, 300, 5, 300, 5, 0, "caps emptied exactly");

	/* Preserved saturation: a caps demand ABOVE the remainder
	 * subtracts NOTHING (the pre-extraction `> ? 0 :` shape). */
	consume_case(10, 100, 5, 200, 5, 100, "caps demand above remainder");

	/* Preserved unsigned wrap on RAM: demand above the remainder
	 * wraps (same as 'free_ram[k] -= need_ram' did). */
	consume_case(10, 100, 20, 50,
	             10ul - 20ul, 50, "ram demand wraps unsigned");

	/* Two-budget install walk, as the loop applies it: two admitted
	 * packages in sequence against the same per-sandbox budget. */
	{
		Genode::size_t ram { 60ull * 1024 * 1024 };
		unsigned       caps { 1000 };
		Sponge::Pkg_admission::consume(ram, caps, 48ull * 1024 * 1024, 512);
		Sponge::Pkg_admission::consume(ram, caps, 4ull * 1024 * 1024, 100);
		check(ram == 8ull * 1024 * 1024 && caps == 388,
		      "consume: chained install-walk decrement");
	}
}


}  /* anonymous namespace */


int main()
{
	test_quota_parsing();
	test_budget_decode();
	test_admits();
	test_consume();

	if (failures) {
		printf("self-test: FAIL (%lu of %lu checks)\n", failures, checks);
		return 1;
	}
	printf("self-test: PASS (%lu checks)\n", checks);
	return 0;
}
