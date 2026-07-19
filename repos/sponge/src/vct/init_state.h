/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * InitStateReader — reads the live state report produced by a Genode init
 * component.
 *
 * The report is consumed as a ROM module labelled "state", usually provided
 * by a sibling report_rom component. Genode's sandbox state reporter emits
 * the report in a tabular text format, which this reader parses into
 * structured data for vct's status and component-list commands.
 */

#pragma once

#include <base/attached_rom_dataspace.h>
#include <timer_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>

namespace Sponge::Vct {

class InitStateReader
{
	public:

		struct Child
		{
			Genode::String<64> name;
			Genode::String<64> binary;
			Genode::String<16> state;

			unsigned long ram_assigned { 0 };
			unsigned long ram_quota    { 0 };
			unsigned long ram_used     { 0 };
			unsigned long ram_avail    { 0 };

			unsigned long cap_assigned { 0 };
			unsigned long cap_quota    { 0 };
			unsigned long cap_used     { 0 };
			unsigned long cap_avail    { 0 };

			Child() : name(), binary(), state("ok") { }
		};

		explicit InitStateReader(Genode::Env &env);

		bool available() const { return _available; }

		bool has_error() const { return false; }
		Genode::String<64> error_string() const { return Genode::String<64>(); }

		unsigned child_count() const { return _num_children; }

		unsigned long total_ram_quota() const { return _ram_quota; }
		unsigned long total_ram_used()  const { return _ram_used; }
		unsigned long total_ram_avail() const { return _ram_avail; }

		unsigned long total_caps_quota() const { return _cap_quota; }
		unsigned long total_caps_used()  const { return _cap_used; }
		unsigned long total_caps_avail() const { return _cap_avail; }

		template <typename FN>
		void for_each_child(FN const &fn) const;

	private:

		static constexpr unsigned MAX_CHILDREN = 16;

		Genode::Constructible<Genode::Attached_rom_dataspace> _state_rom { };
		Genode::Constructible<Timer::Connection>              _timer    { };

		bool _available = false;

		unsigned long _ram_quota { 0 };
		unsigned long _ram_used  { 0 };
		unsigned long _ram_avail { 0 };

		unsigned long _cap_quota { 0 };
		unsigned long _cap_used  { 0 };
		unsigned long _cap_avail { 0 };

		Child _children[MAX_CHILDREN] { };
		unsigned _num_children = 0;

		void _parse_content();
};


/* ===================== template implementation ===================== */

template <typename FN>
void InitStateReader::for_each_child(FN const &fn) const
{
	for (unsigned i = 0; i < _num_children; ++i)
		fn(_children[i]);
}

}  /* namespace Sponge::Vct */
