/*
 * \brief  i.MX8 USB-card driver Linux port
 * \author Stefan Kalkowski
 * \date   2021-06-29
 */

/*
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include <base/component.h>
#include <base/env.h>
#include <base/attached_rom_dataspace.h>

#include <lx_emul/init.h>
#include <lx_emul/shared_dma_buffer.h>
#include <lx_emul/usb.h>
#include <lx_kit/env.h>
#include <lx_kit/init.h>
#include <lx_kit/initial_config.h>
#include <lx_user/io.h>

#include <genode_c_api/usb.h>

using namespace Genode;


static bool _bios_handoff;

/*
 * Sponge diagnostic (usb-driver config attribute "host_diag", default
 * off, read once at construction): count completions/IRQ wakeups of
 * the xHCI host driver — the lowest observable point of the USB input
 * chain ABOVE Genode core's interrupt delivery. On real hardware:
 * 'host-sig frozen while usb-sig frozen' means the xHCI interrupt
 * itself stopped arriving at the driver (core/kernel interrupt path,
 * e.g. MSI delivery); 'host-sig climbs while usb-sig frozen' pins the
 * stall to the driver->client (usb_hid) completion forwarding.
 */
static bool _host_diag = false;

/* Sponge diagnostic gate consumed by genode_c_api/usb.cc (defined there) */
extern bool usb_host_diag;
static unsigned _host_sig_count = 0;


extern "C" int inhibit_pci_fixup(char const *name)
{
	if (_bios_handoff)
		return 0;

	char const *handoff = "__pci_fixup_final_quirk_usb_early_handoff";

	size_t length = Genode::min(Genode::strlen(name),
	                            Genode::strlen(handoff));

	return Genode::strcmp(handoff, name, length) == 0;
}


struct Main
{
	Env                  &env;
	Signal_handler<Main>  signal_handler { env.ep(), *this,
	                                       &Main::handle_signal };

	void handle_signal()
	{
		if (_host_diag) {
			_host_sig_count++;
			if (_host_sig_count <= 5)
				log("host-sig #", _host_sig_count);
			else if ((_host_sig_count % 25) == 0)
				log("host-sig count=", _host_sig_count);
		}

		lx_user_handle_io();
		Lx_kit::env().scheduler.execute();

		genode_usb_notify_peers();
	}

	Main(Env &env) : env(env)
	{
		{
			Lx_kit::Initial_config config { env };

			_bios_handoff = config.rom.node().attribute_value("bios_handoff", true);

			_host_diag = config.rom.node().attribute_value("host_diag", false);
			usb_host_diag = _host_diag;
		}

		Lx_kit::initialize(env, signal_handler);

		Genode_c_api::initialize_usb_service(env, signal_handler,
		                                     lx_emul_shared_dma_buffer_allocate,
		                                     lx_emul_shared_dma_buffer_free,
		                                     lx_emul_usb_release_device);
		lx_emul_start_kernel(nullptr);
	}
};


void Component::construct(Env &env)
{
	static Main main(env);
}
