/*
 * \brief  Input-interrupt handler
 * \author Norman Feske
 * \date   2007-10-08
 */

/*
 * Copyright (C) 2007-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#ifndef _DRIVERS__INPUT__SPEC__PS2__IRQ_HANDLER_H_
#define _DRIVERS__INPUT__SPEC__PS2__IRQ_HANDLER_H_

/* Genode includes */
#include <base/entrypoint.h>
#include <platform_session/device.h>

/* local includes */
#include "input_driver.h"

/*
 * Sponge diagnostic (ps2 config attribute "ps2_diag", default off, set
 * by the component's config handler): count PS/2 interrupts per device
 * and log the first 3 plus every 100th. Separates the laptop's PS/2
 * touchpad/EC input path from the USB mouse path — both feed the same
 * event_filter -> nitpicker chain, so "ps2-irq climbs while evdev-batch
 * frozen" pins the failure to USB/xHCI interrupt delivery alone.
 */
inline bool ps2_diag = false;

class Irq_handler
{
	private:

		Platform::Device::Irq               _irq;
		Genode::Signal_handler<Irq_handler> _handler;
		Input_driver                       &_input_driver;
		Event::Session_client              &_event_session;

		unsigned _diag_count { 0 };

		void _handle()
		{
			_irq.ack();

			if (ps2_diag) {
				_diag_count++;
				if (_diag_count <= 3)
					Genode::log("ps2-irq #", _diag_count,
					            " dev=", _irq_idx ? "mouse" : "kbd");
				else if ((_diag_count % 100) == 0)
					Genode::log("ps2-irq count=", _diag_count,
					            " dev=", _irq_idx ? "mouse" : "kbd");
			}

			_event_session.with_batch([&] (Event::Session_client::Batch &batch) {
				while (_input_driver.event_pending())
					_input_driver.handle_event(batch);
			});
		}

	public:

		Irq_handler(Genode::Entrypoint    &ep,
		            Input_driver          &input_driver,
		            Event::Session_client &event_session,
		            Platform::Device      &device,
		            unsigned               idx)
		:
		_irq(device, {idx}),
			_handler(ep, *this, &Irq_handler::_handle),
			_input_driver(input_driver),
			_event_session(event_session),
			_irq_idx(idx)
		{
			_irq.sigh(_handler);
		}

	private:

		unsigned _irq_idx { 0 };
};

#endif /* _DRIVERS__INPUT__SPEC__PS2__IRQ_HANDLER_H_ */
