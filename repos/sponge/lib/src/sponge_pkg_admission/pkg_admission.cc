/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Sponge::Pkg_admission — pure resource-admission
 * math for Sponge OS package sandboxes.
 *
 * Every function body is a verbatim extraction of sponge_pkgd's
 * pre-extraction admission arithmetic (Phase 16 P1.2,
 * docs/plans/phase16-runtime-overhaul.md; pre-extraction source
 * repos/sponge/src/sponge_pkgd/admission.cc and the install-loop
 * decrement pair in ops.cc). The quota parser, the HID budget walk,
 * the margin comparison and the saturation decrement are preserved
 * exactly — see <sponge/pkg_admission.h> for the contracts and
 * lib/src/sponge_pkg_admission/self_test.cc for the equivalence proof
 * against verbatim reference copies of the old code.
 *
 * Purity: no Env, no sessions, no I/O, no exceptions, no mutable
 * global state. Every input and output is owned by the caller. The
 * defensive try/catch around the HID walk is the pre-extraction one
 * (Hid_node does not throw; kept so the shape stays identical).
 */

#include <sponge/pkg_admission.h>

#include <util/hid.h>
#include <util/xml_node.h>   /* Xml_node::Invalid_syntax, the defensive catch type */

bool Sponge::Pkg_admission::quota_bytes(Genode::String<16> const &q,
                                        Genode::size_t &out)
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


bool Sponge::Pkg_admission::decode_budget(char const *buf, Genode::size_t len,
                                          Genode::size_t &avail_ram,
                                          unsigned &avail_caps)
{
	bool ram_ok { false }, caps_ok { false };
	Genode::size_t ram_bytes { 0 }, caps_value { 0 };

	try {
		Genode::Hid_node const root(Genode::Const_byte_range_ptr(buf, len));

		Genode::String<16> ram_avail  { }, caps_avail { };
		root.for_each_sub_node([&] (Genode::Hid_node const &n) {
			if (n.has_type("ram") && !ram_ok) {
				ram_avail = n.attribute_value("avail", Genode::String<16>());
				ram_ok = quota_bytes(ram_avail, ram_bytes);
			}
			if (n.has_type("caps") && !caps_ok) {
				caps_avail = n.attribute_value("avail", Genode::String<16>());
				caps_ok = quota_bytes(caps_avail, caps_value) &&
				          caps_value > 0 && caps_value <= 0xffffffu;
			}
		});
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		/* Hid_node does not throw; defensive only. */
		return false;
	}

	if (!ram_ok || !caps_ok)
		return false;

	avail_ram  = ram_bytes;
	avail_caps = (unsigned)caps_value;
	return true;
}


bool Sponge::Pkg_admission::admits(Genode::size_t avail_ram, unsigned avail_caps,
                                   Genode::size_t need_ram, unsigned need_caps)
{
	if (avail_ram < need_ram + ADMISSION_MARGIN_RAM)
		return false;
	if (avail_caps < need_caps + ADMISSION_MARGIN_CAPS)
		return false;
	return true;
}


void Sponge::Pkg_admission::consume(Genode::size_t &avail_ram,
                                    unsigned &avail_caps,
                                    Genode::size_t need_ram, unsigned need_caps)
{
	avail_ram  -= need_ram;
	avail_caps -= (need_caps > avail_caps ? 0u : need_caps);
}
