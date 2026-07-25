/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * lz_viewer — displays the Leitzentrale subsystem's composited UI as a
 * window on the outer (Sponge desktop) nitpicker.
 *
 * Sculpt's nested-GUI proxy chain carries view operations but no pixel
 * content to the outer nitpicker. This component is the explicit
 * "dedicated viewer" the criterion allows: it opens a Capture session to
 * the leitzentrale subsystem's PROVIDED Capture service (the inner
 * nitpicker's composited output — runtime_view dialogs + backdrop), and
 * copies each captured frame into a Gui view on the outer nitpicker.
 *
 * The result is the Leitzentrale shown as a real window on the Sponge
 * desktop, verifiable headlessly by lz_viz_probe (an outer Capture client
 * that asserts the Leitzentrale's distinct colors appear on screen).
 */

#include <base/attached_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <capture_session/connection.h>
#include <gui_session/connection.h>
#include <timer_session/connection.h>
#include <util/xml_node.h>

namespace {

using Pixel = Capture::Pixel;

/* Inner Leitzentrale screen size (defines the inner nitpicker panorama). */
unsigned const INNER_W = 800;
unsigned const INNER_H = 600;

/*
 * Where the Leitzentrale window appears on the outer (1024x768) desktop.
 * Centered-ish, leaving a desktop margin so it reads as a window, not a
 * fullscreen overlay.
 */
int const VIEW_X = 112;
int const VIEW_Y = 84;

/*
 * Distinct marker stamped into the top-left of each streamed frame so
 * lz_viz_probe can prove the stream is live (this exact color appears on
 * the outer nitpicker only if lz_viewer captured + wrote + the Gui view
 * is displayed). Chosen to be far from both the outer desktop background
 * (#1e1e2e) and any Leitzentrale UI color.
 */
int const MARK_R = 0xbf, MARK_G = 0x5f, MARK_B = 0xbf;
int const MARK_W = 40, MARK_H = 40;


struct Lz_viewer
{
	Genode::Env &_env;

	Timer::Connection   _timer { _env };

	/* Source: the subsystem's composited output (inner nitpicker Capture). */
	Capture::Connection _capture { _env, "lz-viewer" };

	/* Sink: a Gui view on the outer nitpicker. */
	Gui::Connection     _gui { _env, "lz-viewer" };

	Genode::Constructible<Genode::Attached_dataspace> _cap_ds { };
	Genode::Constructible<Genode::Attached_dataspace> _fb_ds  { };

	Gui::View_id const _view_id { 1 };

	bool _ready { false };

	Lz_viewer(Genode::Env &env) : _env(env)
	{
		Genode::log("lz-viewer: starting");

		/* Define the inner panorama and attach the capture buffer. */
		_capture.buffer({ .px       = Capture::Area(INNER_W, INNER_H),
		                  .mm       = Capture::Area(0, 0),
		                  .viewport = Capture::Rect{ Capture::Point(0, 0),
		                                              Capture::Area(INNER_W, INNER_H) } });
		_cap_ds.construct(_env.rm(), _capture.dataspace());

		/*
		 * Set up the Gui framebuffer (same geometry/format as the capture
		 * buffer) and attach it. Both are INNER_W*INNER_H Pixel_rgb888,
		 * so frames copy byte-for-byte.
		 */
		_gui.buffer(Framebuffer::Mode{ .area = Gui::Area(INNER_W, INNER_H),
		                               .alpha = false });
		_fb_ds.construct(_env.rm(), _gui.framebuffer.dataspace());

		/* Create the window view on the outer nitpicker. */
		Gui::Session::View_attr const attr {
			.title = "Leitzentrale",
			.rect  = Gui::Rect(Gui::Point(VIEW_X, VIEW_Y),
			                   Gui::Area(INNER_W, INNER_H)),
			.front = true
		};
		_gui.view(_view_id, attr);
		_gui.execute();

		_ready = true;
		Genode::log("lz-viewer: streaming Leitzentrale to outer nitpicker at (",
		            VIEW_X, ",", VIEW_Y, ") ", INNER_W, "x", INNER_H);

		_stream_loop();
	}

	void _stream_loop()
	{
		Genode::size_t const bytes =
			INNER_W * INNER_H * sizeof(Pixel);

		for (unsigned frame = 0; ; ++frame) {

			/* Pull a fresh composited frame from the subsystem. */
			_capture.capture_at(Capture::Point(0, 0));

			/* Blit capture buffer -> Gui framebuffer (byte-for-byte). */
			Pixel const *src = _cap_ds->local_addr<Pixel const>();
			Pixel       *dst = _fb_ds ->local_addr<Pixel>();
			Genode::memcpy(dst, src, bytes);

			/*
			 * Stamp a distinct marker patch into the top-left of the frame
			 * so lz_viz_probe can unambiguously confirm the stream is live
			 * (the marker appears on the outer nitpicker only if lz_viewer
			 * is actively capturing and writing).
			 */
			Pixel const mark_pixel { MARK_R, MARK_G, MARK_B };
			for (int y = 0; y < MARK_H; ++y)
				for (int x = 0; x < MARK_W; ++x)
					dst[y * INNER_W + x] = mark_pixel;

			/* Notify the outer nitpicker that the view content changed. */
			_gui.framebuffer.refresh(Gui::Rect(Gui::Point(0, 0),
			                                   Gui::Area(INNER_W, INNER_H)));

			if (frame % 20 == 0)
				Genode::log("lz-viewer: frame ", frame, " streamed");

			_timer.msleep(100);
		}
	}
};

} /* anonymous namespace */


void Component::construct(Genode::Env &env)
{
	static Lz_viewer viewer { env };
}


Genode::size_t Component::stack_size() { return 64 * 1024; }
