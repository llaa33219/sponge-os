/*
 * \brief   Core's CSpace layout definition
 * \author  Norman Feske
 * \date    2015-05-06
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _CORE__INCLUDE__CORE_CSPACE_H_
#define _CORE__INCLUDE__CORE_CSPACE_H_

#include <sel4_boot_info.h>

namespace Core { class Core_cspace; }


class Core::Core_cspace
{
	public:

		/* CNode dimensions */
		enum {
			NUM_TOP_SEL_LOG2  = 11UL,
			/* CONFIG_ROOT_CNODE_SIZE_BITS from seL4 autoconf.h */
			NUM_CORE_SEL_LOG2 = CONFIG_ROOT_CNODE_SIZE_BITS,
			NUM_PHYS_SEL_LOG2 = 21UL,

			/*
			 * Sponge Phase 15 W4 (row 14 patch — complements row 13
			 * in the seL4 kernel). The flat phys CNode above caps
			 * frame caps at 2^21 slots = 8 GiB of physical address
			 * space at 4 KiB granularity. Above 8 GiB (the GOP
			 * framebuffer on UEFI/Insyde systems with >= 8 GiB of
			 * RAM, the xHCI BAR at 32 GiB on q35/OVMF, 64-bit PCI
			 * BARs in q35 pci-hole64), the row-13 kernel creates
			 * device untypeds but Genode core's flat indexing
			 * (node_offset = phys_addr >> 12) overruns the 2^21
			 * cap of the low CNode — see initial_untyped_pool.h
			 * line ~220, the "limited untyped cnode range" warning.
			 *
			 * NUM_HIGH_PHYS_SEL_LOG2 = 23 widens the addressing
			 * window to 2^23 = 32 GiB at 4 KiB granularity, sized
			 * to cover [8 GiB, 8 GiB + 2^23 * 4 KiB) = [8 GiB,
			 * 40 GiB). This window includes the qemu-xhci BAR at
			 * 32 GiB (the failure case), the Insyde H2O GOP on the
			 * LG gram 17ZD90N at ~32 GiB, and all practical 64-bit
			 * PCI BARs on q35/OVMF (the q35 pci-hole64 default
			 * tops out near 64 GiB). The window is created lazily
			 * on first IO_MEM request that needs it; the 256 MiB
			 * of CNode backing is only consumed when high-phys
			 * device memory actually exists. See docs/11-environment.md
			 * row 14.
			 */
			NUM_HIGH_PHYS_SEL_LOG2 = 23UL,

			NUM_CORE_PAD_SEL_LOG2 = 32UL - NUM_TOP_SEL_LOG2 - NUM_CORE_SEL_LOG2,
		};

		/*
		 * Sponge (row 14): the high-phys CNode uses a different
		 * cap-selector bit layout than the low phys CNode (the
		 * low CNode uses 11-bit top + 21-bit slot via
		 * NUM_PHYS_SEL_LOG2; the high CNode uses 9-bit top +
		 * 23-bit slot via NUM_HIGH_PHYS_SEL_LOG2 — 32 bits total).
		 * The constant below anchors the phys_addr remap: a frame
		 * at phys_addr lives at slot (phys_addr - HIGH_PHYS_BASE)
		 * >> 12 in the high-phys CNode, where HIGH_PHYS_BASE is
		 * the lower bound of the high-phys address window
		 * (currently 8 GiB).
		 */
		static addr_t constexpr HIGH_PHYS_BASE = 1ULL << 33; /* 8 GiB */

		/* selectors for initially created CNodes during core bootup */
		static unsigned top_cnode_sel()       { return (unsigned)sel4_boot_info().empty.start; }
		static unsigned core_pad_cnode_sel()  { return top_cnode_sel()      + 1; }
		static unsigned core_cnode_sel()      { return core_pad_cnode_sel() + 1; }
		static unsigned phys_cnode_sel()      { return core_cnode_sel()     + 1; }
		static unsigned untyped_cnode_4k()    { return phys_cnode_sel()     + 1; }
		static unsigned untyped_cnode_8k()    { return untyped_cnode_4k()   + 1; }
		static unsigned untyped_cnode_16k()   { return untyped_cnode_8k()   + 1; }
		static unsigned io_port_sel()         { return untyped_cnode_16k()  + 1; }

		/*
		 * Sponge (row 14): selector reserved for the high-phys CNode
		 * cap in the initial thread CNode. The slot is allocated at
		 * boot (no kernel allocation needed) but the CNode itself is
		 * constructed lazily by Platform::construct_high_phys_cnode()
		 * on first IO_MEM request that needs it. Reserving the slot
		 * upfront keeps the cap-sel arithmetic in
		 * untyped_memory.h::high_phys_cnode_sel() simple (it's a
		 * static offset relative to the top CNode just like the
		 * other CNode selectors).
		 */
		static unsigned high_phys_cnode_sel() { return io_port_sel()        + 1; }

		static unsigned core_static_sel_end() { return high_phys_cnode_sel() + 1; }

		/* indices within top-level CNode */
		enum Top_cnode_idx {
			TOP_CNODE_CORE_IDX       = 0,

			TOP_CNODE_UNTYPED_16K    = 0x7fc, /* untyped objects 16K  */
			TOP_CNODE_UNTYPED_8K     = 0x7fd, /* untyped objects  8K  */
			TOP_CNODE_UNTYPED_4K     = 0x7fe, /* untyped objects  4K  */
			TOP_CNODE_PHYS_IDX       = 0x7ff, /* physical page frames */

			/*
			 * Sponge row 14: dedicated CNode for page-frame caps of
			 * physical addresses above 8 GiB. See NUM_HIGH_PHYS_SEL_LOG2
			 * for the window size rationale. The slot 0x7e0 sits
			 * well below the four reserved slots (0x7fc-0x7ff) and
			 * is far from the core PD root-cnode entries (0-0x7fb),
			 * so it never collides with the existing layout.
			 */
			TOP_CNODE_HIGH_PHYS_IDX  = 0x7e0  /* physical page frames >= 8 GiB */
		};

		enum { CORE_VM_ID = 1 };
};

#endif /* _CORE__INCLUDE__CORE_CSPACE_H_ */
