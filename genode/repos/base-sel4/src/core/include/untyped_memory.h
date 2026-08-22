/*
 * \brief   Utilities for dealing with untyped memory
 * \author  Norman Feske
 * \author  Alexander Boettcher
 * \date    2015-05-06
 */

/*
 * Copyright (C) 2015-2025 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__UNTYPED_MEMORY_H_
#define _CORE__INCLUDE__UNTYPED_MEMORY_H_

/* Genode includes */
#include <base/allocator.h>

/* core includes */
#include <util.h>
#include <cap_sel_alloc.h>
#include <core_cspace.h>

/* seL4 includes */
#include <sel4/sel4.h>

namespace Core { struct Untyped_memory; }


struct Core::Untyped_memory
{
	/*
	 * Object size of the large-CNode-backing untyped pool. On x86_64 this
	 * is the 16 KiB pool that also backs VCPUs (Vcpu_kobj::SIZE_LOG2 == 14).
	 * The large-backing branch is only reached when a child-PD CNode exceeds
	 * one page, which the configured CSPACE sizes permit only on x86_64;
	 * the value is inert on architectures where CNodes stay page-sized.
	 */
	enum { LARGE_BACKING_UNTYPED_LOG2 = 14 };

	static inline Allocator::Alloc_result alloc_pages(Range_allocator &phys,
	                                                  size_t const num_pages)
	{
		return phys.alloc_aligned(num_pages*PAGE_SIZE, AT_PAGE);
	}


	static inline Allocator::Alloc_result alloc_page(Range_allocator &phys)
	{
		return alloc_pages(phys, 1);
	}


	static inline void free_page(Range_allocator &phys_alloc, addr_t addr)
	{
		phys_alloc.free(reinterpret_cast<void *>(addr));
	}


	static inline void free_pages(Range_allocator &phys_alloc, addr_t addr,
	                              size_t const num_pages)
	{
		phys_alloc.free(reinterpret_cast<void *>(addr), num_pages * PAGE_SIZE);
	}


	/**
	 * Local utility solely used by 'untyped_sel' and 'frame_sel'
	 */
	static inline Cap_sel _core_local_sel(Core_cspace::Top_cnode_idx top_idx,
	                                      addr_t phys_addr,
	                                      addr_t size_log2 = PAGE_SIZE_LOG2)
	{
		unsigned const upper_bits = top_idx << Core_cspace::NUM_PHYS_SEL_LOG2;
		unsigned const mask       = (1ul << Core_cspace::NUM_PHYS_SEL_LOG2) - 1;
		unsigned const lower_bits = unsigned(phys_addr >> size_log2) & mask;

		return Cap_sel(upper_bits | lower_bits);
	}


	/*
	 * Sponge (row 14): high-phys variant of the core-local selector.
	 *
	 * The low phys CNode (top_idx = 0x7ff, NUM_PHYS_SEL_LOG2 = 21) covers
	 * 8 GiB at 4 KiB granularity. For phys_addr >= HIGH_PHYS_BASE the
	 * slot index (phys_addr >> 12) overruns 2^21, so a wider selector
	 * format is used: top_idx is shifted by NUM_HIGH_PHYS_SEL_LOG2
	 * (= 23), and the slot is the (phys_addr - HIGH_PHYS_BASE) >> 12
	 * remap. See core_cspace.h NUM_HIGH_PHYS_SEL_LOG2 + HIGH_PHYS_BASE
	 * + docs/11-environment.md row 14.
	 */
	static inline Cap_sel _high_phys_sel(addr_t phys_addr,
	                                     addr_t size_log2 = PAGE_SIZE_LOG2)
	{
		unsigned const upper_bits = Core_cspace::TOP_CNODE_HIGH_PHYS_IDX
		                            << Core_cspace::NUM_HIGH_PHYS_SEL_LOG2;
		unsigned const mask       = (1ul << Core_cspace::NUM_HIGH_PHYS_SEL_LOG2) - 1;
		unsigned const lower_bits = unsigned((phys_addr - Core_cspace::HIGH_PHYS_BASE)
		                                    >> size_log2) & mask;

		return Cap_sel(upper_bits | lower_bits);
	}


	/**
	 * Return core-local selector for untyped page at given physical address
	 *
	 * Sponge (row 14): only valid for phys_addr < HIGH_PHYS_BASE — the
	 * 4 KiB-untyped CNode only covers the lower 8 GiB. High-phys
	 * device untypeds are looked up by bootinfo scan in
	 * _high_phys_untyped_sel() and passed directly to seL4_Untyped_Retype
	 * as the 'service' argument; the slot-indexed CNode encoding has
	 * no meaning for them.
	 */
	static inline Cap_sel untyped_sel(addr_t phys_addr)
	{
		return _core_local_sel(Core_cspace::TOP_CNODE_UNTYPED_4K, phys_addr);
	}


	/**
	 * Return core-local selector for the untyped object backing a CNode of
	 * the given backing size at 'phys_addr'.
	 *
	 * Backings up to one page (4 KiB) are served by the 4 KiB untyped pool.
	 * Larger backings are served by the 16 KiB untyped pool, whose objects
	 * are addressed by phys_addr >> LARGE_BACKING_UNTYPED_LOG2.
	 */
	static inline Cap_sel untyped_sel(addr_t phys_addr, addr_t backing_log2)
	{
		if (backing_log2 > PAGE_SIZE_LOG2)
			return _core_local_sel(Core_cspace::TOP_CNODE_UNTYPED_16K,
			                       phys_addr, LARGE_BACKING_UNTYPED_LOG2);

		return untyped_sel(phys_addr);
	}


	/**
	 * Per-arch physical-memory allocator used for CNode backing.
	 *
	 * On x86_64, backings larger than one page are drawn from the 16 KiB
	 * untyped pool (the same pool that backs VCPUs). On other architectures
	 * CNodes stay page-sized, so the default allocator is returned unchanged
	 * and the large-backing path is never taken.
	 */
	static Range_allocator &cnode_backing_alloc(uint8_t  cnode_size_log2,
	                                            Range_allocator &default_alloc);


	/**
	 * Return core-local selector for 4K page frame at given physical address
	 *
	 * Sponge (row 14): for phys_addr >= HIGH_PHYS_BASE (8 GiB), the
	 * frame cap lives in the dedicated high-phys CNode
	 * (TOP_CNODE_HIGH_PHYS_IDX). The slot is (phys_addr - HIGH_PHYS_BASE)
	 * >> 12, NOT phys_addr >> 12 (which would overrun the low phys
	 * CNode's 2^21 cap).
	 */
	static inline Cap_sel frame_sel(addr_t phys_addr)
	{
		if (phys_addr >= Core_cspace::HIGH_PHYS_BASE)
			return _high_phys_sel(phys_addr);

		return _core_local_sel(Core_cspace::TOP_CNODE_PHYS_IDX, phys_addr);
	}


	static seL4_Word smallest_page_type();


	/*
	 * Sponge (row 14): for phys_addr above HIGH_PHYS_BASE (8 GiB), the
	 * page-frame cap is retype'd into the dedicated high-phys CNode at
	 * top slot TOP_CNODE_HIGH_PHYS_IDX (0x7e0), at the slot computed by
	 * (phys_addr - HIGH_PHYS_BASE) >> 12. The CNode must exist BEFORE
	 * this function runs for a high-phys range — callers
	 * (io_mem_session_support.cc::_acquire) trigger the lazy creation
	 * via platform_specific().construct_high_phys_cnode() at the top of
	 * their path, so by the time the retype fires the CNode is in the
	 * top CNode. See docs/11-environment.md row 14.
	 */
	static inline bool _high_phys_untyped_sel(addr_t phys_addr)
	{
		seL4_BootInfo const &bi = sel4_boot_info();
		unsigned const count = (unsigned)(bi.untyped.end - bi.untyped.start);

		for (unsigned i = 0; i < count; i++) {
			auto const &desc = bi.untypedList[i];
			if (!desc.isDevice)
				continue;
			addr_t const base = desc.paddr;
			addr_t const size = 1UL << desc.sizeBits;
			if (phys_addr >= base && phys_addr < base + size)
				return (seL4_Untyped)(bi.untyped.start + i);
		}
		return 0;
	}


	/**
	 * Create page frames from untyped memory
	 */
	static inline bool convert_to_page_frames(addr_t phys_addr,
	                                          size_t num_pages)
	{
		auto const phys_addr_base = phys_addr;

		for (size_t i = 0; i < num_pages; i++, phys_addr += PAGE_SIZE) {

			seL4_Untyped const service     = untyped_sel(phys_addr).value();
			seL4_Word    const type        = smallest_page_type();
			seL4_Word    const size_bits   = 0;
			seL4_CNode   const root        = Core_cspace::top_cnode_sel();
			seL4_Word    const node_index  = Core_cspace::TOP_CNODE_PHYS_IDX;
			seL4_Word    const node_depth  = Core_cspace::NUM_TOP_SEL_LOG2;
			seL4_Word    const node_offset = phys_addr >> PAGE_SIZE_LOG2;
			seL4_Word    const num_objects = 1;

			long const ret = seL4_Untyped_Retype(service,
			                                     type,
			                                     size_bits,
			                                     root,
			                                     node_index,
			                                     node_depth,
			                                     node_offset,
			                                     num_objects);

			if (ret == seL4_NoError)
				continue;

			error(__FUNCTION__, ": seL4_Untyped_RetypeAtOffset "
			      "returned ", ret, " - physical_range=",
			      Hex_range(node_offset << PAGE_SIZE_LOG2,
			                (num_pages - i) * PAGE_SIZE));

			/* revert already converted memory */
			convert_to_untyped_frames(phys_addr_base, PAGE_SIZE * i);

			return false;
		}

		return true;
	}


	/**
	 * Free up page frames and turn it so into untyped memory
	 */
	static inline void convert_to_untyped_frames(addr_t const phys_addr,
	                                             addr_t const phys_size)
	{
		seL4_Untyped const service = Core_cspace::phys_cnode_sel();
		int const space_size = Core_cspace::NUM_PHYS_SEL_LOG2;

		for (addr_t phys = phys_addr; phys < phys_addr + phys_size; phys += PAGE_SIZE) {

			unsigned const index = (unsigned)(phys >> PAGE_SIZE_LOG2);

			/**
			 * Without the revoke, one gets sporadically
			 *  Untyped Retype: Insufficient memory ( xx bytes needed, x bytes
			 *                                        available)
			 * for the phys_addr when it gets reused.
			 */
			int ret = seL4_CNode_Revoke(service, index, space_size);
			if (ret != seL4_NoError)
				error(__FUNCTION__, ": seL4_CNode_Revoke returned ", ret);

			/**
			 * Without the delete, one:
			 *  Untyped Retype: Slot #xxxx in destination window non-empty
			 */
			ret = seL4_CNode_Delete(service, index, space_size);
			if (ret != seL4_NoError)
				error(__FUNCTION__, ": seL4_CNode_Delete returned ", ret);
		}
	}
	};

#endif /* _CORE__INCLUDE__UNTYPED_MEMORY_H_ */
