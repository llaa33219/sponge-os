/*
 * \brief  C++ initialization, session, and client handling
 * \author Sebastian Sumpf
 * \author Stefan Kalkowski
 * \date   2023-06-29
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/env.h>
#include <base/registry.h>

#include <lx_emul/init.h>
#include <lx_emul/task.h>
#include <lx_emul/input_leds.h>
#include <lx_user/io.h>
#include <lx_kit/env.h>

#include <genode_c_api/event.h>

/* C-interface */
#include <usb_hid.h>

#include <led_state.h>

using namespace Genode;

/* Sponge diagnostic gate for evdev.c batch counting (defined in evdev.c) */
extern "C" unsigned lx_emul_evdev_diag;

struct Main
{
	Env &env;

	Attached_rom_dataspace config_rom { env, "config" };

	Usb::Led_state capslock { env, "capslock" },
	               numlock  { env, "numlock"  },
	               scrlock  { env, "scrlock"  };

	Signal_handler<Main> signal_handler  { env.ep(), *this,
	                                       &Main::handle_signal  };
	Signal_handler<Main> usb_rom_handler { env.ep(), *this,
	                                       &Main::handle_usb_rom };
	Signal_handler<Main> config_handler  { env.ep(), *this,
	                                       &Main::handle_config  };

	bool     _diag_enabled  { false };
	bool     _diag_announced { false };
	unsigned _sig_count     { 0 };

	Main(Env &env)
	:
		env(env)
	{
		Lx_kit::initialize(env, signal_handler);

		Genode_c_api::initialize_usb_client(env, Lx_kit::env().heap,
		                                    signal_handler, usb_rom_handler);

		genode_event_init(genode_env_ptr(env),
		                  genode_allocator_ptr(Lx_kit::env().heap));

		config_rom.sigh(config_handler);
		handle_config();

		lx_emul_start_kernel(nullptr);
	}

	void handle_signal()
	{
		/*
		 * Sponge diagnostic (config "evdev_diag"): count wakeups of the
		 * component — the usb driver signals this handler on transfer
		 * completions, so on real hardware the count climbing while
		 * evdev-batch stays frozen pins the stall INSIDE the DDE/lx
		 * layer (URB completion -> HID report path), whereas a frozen
		 * count pins dead USB interrupt delivery (xHCI runtime).
		 */
		if (_diag_enabled) {
			++_sig_count;
			/*
			 * Granularity: first 5 individually, then every 25th —
			 * a healthy mouse (60-125 Hz) crosses 25 signals within
			 * a second of movement, so lines appear WHILE moving and
			 * the settled panel keeps them. (An earlier every-200th
			 * cut never printed even in QEMU's 150-move reference.)
			 */
			if (_sig_count <= 5)
				log("usb-sig #", _sig_count);
			else if ((_sig_count % 25) == 0)
				log("usb-sig count=", _sig_count);
		}

		lx_user_handle_io();
		Lx_kit::env().scheduler.execute();
	}

	void handle_usb_rom()
	{
		lx_emul_usb_client_rom_update();
		Lx_kit::env().scheduler.execute();
	}

	void handle_config()
	{
		config_rom.update();
		Genode::Node const &config = config_rom.node();

		_diag_enabled = config.attribute_value("evdev_diag", false);
		lx_emul_evdev_diag = _diag_enabled;
		if (_diag_enabled && !_diag_announced) {
			_diag_announced = true;
			log("usb_hid diag on");
		}

		capslock.update(config, config_handler);
		numlock .update(config, config_handler);
		scrlock .update(config, config_handler);
		lx_emul_input_leds_update(capslock.enabled(), numlock.enabled(),
		                          scrlock.enabled());
		Lx_kit::env().scheduler.execute();
	}
};


void Component::construct(Env &env) { static Main main(env); }
