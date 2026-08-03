/*
 * SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * boot_probe — Phase 8 P1 storage-chain smoke probe (docs/14 §3, §4.4).
 *
 * Opens ROM "marker.txt" served by cached_fs_rom (which reads it from
 * the GENODE ext2 partition via the full Tier-0 chain: ahci/nvme →
 * part_block → vfs_rump → cached_fs_rom), validates its exact content,
 * and logs:
 *
 *   boot-probe: PASS (<N> bytes: "<actual content>")
 *   boot-probe: FAIL: rom_invalid
 *   boot-probe: FAIL: content_mismatch (expected "<E>", got "<A>")
 *
 * See target.mk for the full rationale (including why an upstream chain
 * break manifests as probe silence + a bounded run_genode_until timeout,
 * not a silent hang).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <util/string.h>

namespace {

using Genode::Attached_rom_dataspace;
using Genode::Cstring;
using Genode::Env;
using Genode::log;
using Genode::size_t;
using Genode::String;

/*
 * Maximum content length we validate. The marker file is a short
 * known string; this ceiling keeps the stack buffer bounded.
 */
size_t const MAX_CONTENT = 256;
}

struct Main
{
	Env &_env;

	/*
	 * The marker ROM. Served by cached_fs_rom. The constructor blocks
	 * until the ROM session is established — i.e., until the entire
	 * Tier-0 chain (ahci → part_block → vfs → cached_fs_rom) is up and
	 * the file "marker.txt" is readable inside the chroot. If any
	 * upstream stage is broken, this blocks and the bounded
	 * run_genode_until timeout in the run script catches it.
	 */
	Attached_rom_dataspace _marker { _env, "marker.txt" };

	Main(Env &env) : _env(env)
	{
		/*
		 * The expected marker content is pinned at compile time.
		 * It MUST match the marker_content string in run/sponge-boot.run
		 * exactly (no trailing newline). P1 hardcodes this; a later
		 * phase can make it config-driven once the config-ROM delivery
		 * path is verified for this component shape.
		 */
		String<MAX_CONTENT> expected { "sponge-boot-marker-v1" };

		/*
		 * Validate the ROM module. If the chain produced an
		 * invalid dataspace (e.g., cached_fs_rom could not read
		 * the file), fail with a clear reason.
		 */
		if (!_marker.valid()) {
			log("boot-probe: FAIL: rom_invalid");
			_env.parent().exit(1);
			return;
		}

		/*
		 * Read the content. cached_fs_rom serves the file as a
		 * page-aligned ROM dataspace — the actual file content
		 * is in the leading bytes, and trailing bytes up to the
		 * page boundary are zero padding. We compare only the
		 * leading expected-length bytes (not the full dataspace).
		 */
		char  const *base  = _marker.local_addr<char>();
		size_t const avail = _marker.size();

		/*
		 * Displayable copy of the actual leading content.
		 */
		size_t const expected_len = expected.length();
		size_t const show = expected_len < MAX_CONTENT
			? expected_len : MAX_CONTENT - 1;
		char actual_buf[MAX_CONTENT];
		for (size_t i = 0; i < show; i++) {
			char c = (i < avail) ? base[i] : '\0';
			actual_buf[i] = (c >= 0x20 && c < 0x7f) ? c : '.';
		}
		actual_buf[show] = '\0';

		/*
		 * Exact comparison: the marker file's leading bytes must
		 * equal the expected string. The ROM dataspace is page-
		 * padded by cached_fs_rom, so we compare only the
		 * expected-length prefix.
		 */
		bool const bytes_ok = (avail >= expected_len) &&
			(Genode::strcmp(base, expected.string(), expected_len) == 0);

		if (bytes_ok) {
			log("boot-probe: PASS (", (Genode::uint64_t)expected_len,
			    " bytes: \"", Cstring(actual_buf, show), "\")");
			_env.parent().exit(0);
		} else {
			log("boot-probe: FAIL: content_mismatch ",
			    "(expected \"", expected, "\", got \"",
			    Cstring(actual_buf, show), "\" rom_ds_size=",
			    (Genode::uint64_t)avail, ")");
			_env.parent().exit(1);
		}
	}
};

void Component::construct(Env &env) { static Main main(env); }

Genode::size_t Component::stack_size() { return 16 * 1024; }
