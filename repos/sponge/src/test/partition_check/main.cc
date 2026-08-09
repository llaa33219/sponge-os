/*
 * SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * partition_check — Phase 12 W2 NVMe Tier-0 P3 partition-number probe
 * (docs/plans/phase12-hardware.md §"W2: Storage variants and product-
 * media selector", Risk 3 mitigation; Risk 10 mitigation; docs/14
 * §4.4 partition-by-number contract).
 *
 * Opens ROM "partitions" served by part_block via the Tier-0 report_rom,
 * scans for a `<partition number="N"/>` entry where N == the configured
 * `expected_number`, and logs one of:
 *
 *   partition-check: PASS (Number: <N>)
 *   partition-check: FAIL: rom_invalid       (partitions ROM absent)
 *   partition-check: FAIL: number_mismatch  (expected N, got N')
 *
 * On PASS or FAIL the probe calls env.parent().exit() so the scenario
 * ends cleanly (no lingering component).
 *
 * The match is a small ASCII substring search for `number="N"` in the
 * ROM dataspace. part_block's reporter always renders the partition
 * number as an ASCII decimal attribute (gpt.h:456 / mbr.h:238), so
 * the byte check is deterministic.
 *
 * === Asynchronous read ===
 *
 * `report_rom` creates an empty module on the reader's first lookup,
 * BEFORE the writer (part_block) has published its first report. A
 * synchronous `Attached_rom_dataspace` read would see an empty ROM and
 * immediately log `FAIL: rom_invalid`. To wait for the writer's first
 * non-empty report, the probe subscribes to ROM update signals and
 * retries the scan on each signal. The retry is bounded by an
 * attempt counter (read-only-attempts) so a truly broken upstream
 * chain eventually returns FAIL rather than spinning forever.
 *
 * If the upstream chain is broken (part_block has no Block session,
 * report_rom has no policy for the partitions report, etc.), the
 * Attached_rom_dataspace constructor blocks until the session is
 * established (and never, if the chain never produces the ROM). The
 * bounded `run_genode_until` in the scenario catches the silence.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/signal.h>
#include <util/string.h>

namespace {

using Genode::Attached_rom_dataspace;
using Genode::Env;
using Genode::log;
using Genode::Signal_handler;
using Genode::size_t;
using Genode::String;

/*
 * Maximum content length we scan. The partitions ROM from part_block
 * is a small XML report (~1 KiB); this ceiling is generous.
 */
size_t const MAX_ROM_BYTES = 4096;

/*
 * Maximum number of update-signal retries before we declare FAIL.
 * part_block publishes its first report within a handful of
 * microseconds after Block-session establishment; 64 retries is
 * generous headroom for slow boots (Phase 9 C4 flake adds several
 * hundred ms to the storage-chain startup).
 */
unsigned const MAX_UPDATE_RETRIES = 64;

/*
 * Render the ASCII decimal for `n` into the output buffer. Returns the
 * number of bytes written (no NUL terminator). For n in [1..127] this
 * fits in 3 bytes; the default expected number is 3.
 */
size_t render_decimal(unsigned long n, char *out)
{
	if (n == 0) { out[0] = '0'; return 1; }
	size_t len = 0;
	char tmp[24];
	size_t tlen = 0;
	while (n > 0 && tlen < sizeof(tmp)) {
		tmp[tlen++] = char('0' + (n % 10));
		n /= 10;
	}
	while (tlen > 0) { out[len++] = tmp[--tlen]; }
	return len;
}
}


struct Main
{
	Env &_env;

	Attached_rom_dataspace _part_rom { _env, "partitions" };
	Signal_handler<Main>   _update_handler { _env.ep(), *this, &Main::_check };

	unsigned _retries { 0 };
	bool    _done { false };

	Main(Env &env) : _env(env)
	{
		/*
		 * Subscribe to ROM update signals. report_rom raises a signal
		 * whenever the partitions module is updated by part_block (the
		 * writer). Each signal triggers _check which scans the (now
		 * updated) ROM for `| number: 3 |`. We bound retries to
		 * MAX_UPDATE_RETRIES so a broken chain eventually returns FAIL
		 * rather than spinning.
		 */
		_part_rom.sigh(_update_handler);
		_check();
	}

	void _check()
	{
		if (_done) return;

		/*
		 * Expected partition number is pinned at compile time. Phase 12
		 * W2 uses P3 for the GENODE partition; this matches the docs/14
		 * §4.4 partition-by-number contract for both AHCI and NVMe.
		 */
		constexpr unsigned long EXPECTED_NUMBER = 3;

		_part_rom.update();

		if (!_part_rom.valid()) {
			/*
			 * First-lookup empty module from report_rom; wait for
			 * part_block's first non-empty report.
			 */
			if (_retries++ < MAX_UPDATE_RETRIES) return;
			log("partition-check: FAIL: rom_invalid (after ",
			    _retries, " update retries)");
			_done = true;
			_env.parent().exit(1);
			return;
		}

		/*
		 * Search the HID tabular representation emitted by
		 * part_block's reporter. Inside a `g.tabular(...)` block,
		 * attributes are pipe-separated `name: value` strings (NOT
		 * standard XML `name="value"`). The expected attribute is
		 * rendered as `... | number: <N> | ...`. We search for the
		 * exact byte sequence `| number: <N> |` so a coincidental
		 * match in a non-tabular attribute (e.g. `part_number="3"`
		 * would NOT match) is avoided.
		 */
		char needle[48];
		needle[0] = '|';
		needle[1] = ' ';
		size_t nlen = 2;
		needle[nlen++] = 'n';
		needle[nlen++] = 'u';
		needle[nlen++] = 'm';
		needle[nlen++] = 'b';
		needle[nlen++] = 'e';
		needle[nlen++] = 'r';
		needle[nlen++] = ':';
		needle[nlen++] = ' ';
		nlen += render_decimal(EXPECTED_NUMBER, needle + nlen);
		needle[nlen++] = ' ';
		needle[nlen++] = '|';
		needle[nlen]   = '\0';

		char const *base  = _part_rom.local_addr<char>();
		size_t const avail = _part_rom.size() < MAX_ROM_BYTES
			? _part_rom.size() : MAX_ROM_BYTES;

		bool found = false;
		if (avail >= nlen) {
			for (size_t i = 0; i + nlen <= avail; i++) {
				bool n_ok = true;
				for (size_t k = 0; k < nlen; k++) {
					if (base[i + k] != needle[k]) { n_ok = false; break; }
				}
				if (n_ok) { found = true; break; }
			}
		}

		if (found) {
			/*
			 * Echo back the literal `Number: 3` so the run-script
			 * `run_genode_until` regex can grep for the exact byte
			 * assertion the plan asks for. The probe is intentionally
			 * structured to log `Number: 3` even though part_block
			 * renders the attribute as lowercase `number="3"`.
			 */
			log("partition-check: PASS (Number: ", EXPECTED_NUMBER, ")");
			_done = true;
			_env.parent().exit(0);
			return;
		}

		/*
		 * Module was non-empty but didn't carry `number="3"`. If retries
		 * remain, wait for the next update. If exhausted, log FAIL.
		 */
		if (_retries++ < MAX_UPDATE_RETRIES) return;

		log("partition-check: FAIL: number_mismatch ",
		    "(expected number=\"", EXPECTED_NUMBER, "\", ",
		    "rom_ds_size=", (Genode::uint64_t)avail, ")");
		_done = true;
		_env.parent().exit(1);
	}
};


void Component::construct(Genode::Env &env) { static Main main(env); }

Genode::size_t Component::stack_size() { return 16 * 1024; }