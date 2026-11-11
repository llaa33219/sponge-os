/* SPDX-License-Identifier: Apache-2.0
 *
 * layout_model.h — sponge_layouter pure layout/state policy model.
 *
 * The policy half of the native layouter (Criterion 16, D16W.1 in
 * docs/plans/phase16-sponge-wm.md): window create/destroy tracking,
 * creation order, floating placement (static rules + cascade
 * fallback), title move, 8-zone resize, minimize parking, work-area
 * maximize/restore, Master-Stack tiling with reflow, rules
 * (assign) semantics, focus steering, and sticky close requests.
 * The state machine is the one of docs/plans/wm-state-table.md (the
 * baseline rows plus the "Native WM Extension" section).
 *
 * This model is PURE and deterministic — same contract as
 * <sponge/wm_protocol.h>:
 *
 *   No Genode::Env, no Report/ROM sessions, no File_system, no
 *   hardware access, no exceptions, no RTTI-dependent design, no
 *   mutable global state, no dynamic allocation (fixed-capacity
 *   tables only). The only Genode headers used are the public
 *   <base/node.h>, <util/string.h>, and <util/xml_generator.h>
 *   (transitively via wm_protocol.h). Every mutator is a plain call;
 *   every output is a query. The deterministic tests in
 *   model_tests.cc exercise this exact code host-side (self_test.cc).
 *
 * GEOMETRY (binding, docs/plans/phase16-sponge-wm.md §11 +
 * docs/05-sponge-de.md §4.6):
 *
 *   - 'inner' is the client-content geometry in SCREEN-ABSOLUTE
 *     coordinates; window_request carries INNER geometry and
 *     window_layout reports INNER geometry.
 *   - The OUTER frame is the inner geometry expanded by the
 *     published decorator margins (the title band is the top
 *     margin).
 *   - The WORK AREA is the window_layout <boundary> (binding: "the
 *     work area is reported as the window_layout boundary"). Its
 *     rect comes from the explicit <work_area x y width height/>
 *     config node; work-area maximize means OUTER == work area and
 *     INNER = work area inset by the margins (so the frozen 64 px
 *     tasklist maximize inference keeps working), and the tiling
 *     arrangement places OUTER rects inside the work area.
 *   - Parking: BOTH inner coordinates at or below -32000 read as
 *     parked/minimized (the established write value is exactly
 *     x=y=-32000); a single sentinel coordinate does not park.
 *
 * COORDINATE FRAMES OF RULES (see README.md "Coordinate frames"):
 *
 *   - The tasklist `rules` ROM assigns are TARGET-RELATIVE to the
 *     reported boundary origin (frozen v1: the TasklistController
 *     subtracts the boundary xpos/ypos, the model adds them back —
 *     the "boundary-origin conversion" that keeps 20 republish
 *     cycles drift-free).
 *   - The static inline <rules> of the component config are
 *     SCREEN-ABSOLUTE "configured content geometry" (the committed
 *     run/probe contract: the run headers name the assign values as
 *     the window placement and the native probes assert them as
 *     screen-absolute window_layout coordinates).
 *   Each rules document carries its frame ('Rule_frame'); the frame
 *   split is a documented compatibility decision (README.md).
 *
 * MODE (binding): one global mode, floating | tiling, taken from the
 * configd broadcast key wm.layout.mode (default floating when
 * absent, 'parse_layout_mode'). Floating is user-directed placement
 * and 8-zone resize. Tiling is Master-Stack over OUTER rects: the
 * first eligible window in creation order (resizeable, non-minimized,
 * non-maximized) takes the left half of the work area as master, the
 * remaining eligible windows split the right half into equal bands
 * top to bottom in creation order (integer band math identical to
 * test/native_wm_tiling_probe's acceptance contract); a lone master
 * stays left-half; non-resizeable windows always float; minimized
 * windows park and leave the arrangement; maximized windows fill the
 * work area and leave the arrangement. Leaving tiling keeps every
 * window's current geometry as its floating geometry (no hidden
 * pre-tile history).
 *
 * INPUT (component behavior, hit-testing here): the layouter
 * hit-tests chrome with the shared frozen helpers of wm_protocol
 * ('chrome_hit' and friends): title move, 8-zone resize, and the
 * close/fullscreen/minimize buttons (a click is press + release
 * within 5 px landing in the same button rect). A press on any
 * chrome element focuses+raises the window (focus steering). Move
 * and resize apply only to floating windows (tiled geometry is
 * layouter-owned in v1; tiled drag/resize is a no-op), a resize
 * never shrinks the inner geometry below 64x64, and non-resizeable
 * windows float but never resize.
 */

#pragma once

#include <sponge/wm_protocol.h>


namespace Sponge::Layout_model {


using Wm_protocol::Rect;
using Wm_protocol::Margins;
using Wm_protocol::Focus_request;
using Wm_protocol::Focus_target;
using Wm_protocol::Window_list_entry;

enum : unsigned {
	MAX_WINDOWS = 64,
	MAX_ASSIGNS = 64,

	/* a button click: press + release within this distance, both
	 * landing in the same button rect (binding behavior) */
	CLICK_SLOP_PX = 5,

	/* minimum inner geometry kept by the 8-zone resize gesture */
	MIN_INNER_W = 64,
	MIN_INNER_H = 64
};

enum class Mode { FLOATING, TILING };

/*
 * The work area: the rect reported as the window_layout boundary and
 * used for work-area maximize and the tiling arrangement.
 */
struct Work_area
{
	int  x { 0 }, y { 0 };
	long w { 0 }, h { 0 };
};


/* ========================= rules documents ====================== */

/*
 * Coordinate frame of a rules document's xpos/ypos values.
 *
 * ABSOLUTE — screen-absolute content geometry (the static inline
 *   <rules> of the component config; the committed run/probe
 *   contract).
 * BOUNDARY_RELATIVE — target-relative to the reported boundary
 *   origin (the tasklist `rules` ROM; frozen protocol v1, the
 *   tasklist subtracts the boundary xpos/ypos and the model adds
 *   them back).
 */
enum class Rule_frame { ABSOLUTE, BOUNDARY_RELATIVE };

struct Rule_assign
{
	Genode::String<256> label        { };
	Genode::String<256> label_prefix { };
	Genode::String<256> label_suffix { };
	bool has_position { false };
	bool xpos_any     { false };
	bool ypos_any     { false };
	int  xpos         { 0 };
	int  ypos         { 0 };
	bool has_size     { false };
	long width        { 0 };
	long height       { 0 };
	bool maximized    { false };
	bool visible      { true };
};

struct Rules_doc
{
	Rule_frame  frame { Rule_frame::ABSOLUTE };
	Rule_assign assigns[MAX_ASSIGNS] { };
	unsigned    num_assigns { 0 };
};

/* Append one parsed assign (no-op when the table is full). */
void add_assign(Rules_doc &, Wm_protocol::Assign const &);

/*
 * Build a rules document from a '<rules>' node (the static inline
 * config node or a tasklist rules-rom document) through
 * Wm_protocol::parse_rules. Returns false when the node is not a
 * '<rules>' document (the caller then treats the source as invalid,
 * upstream window_layouter semantics: "should the ROM module not
 * contain valid rules, the 'rules' sub node of the config comes into
 * effect").
 */
bool rules_doc_from_node(Genode::Node const &, Rule_frame, Rules_doc &);

/*
 * Document equality (field-wise). "Last-wins" is per DISTINCT
 * publication: re-feeding an identical document is not a new
 * publication and must not re-apply assigns (which would snap
 * user-moved windows back to the echoed geometry).
 */
bool same_rules_doc(Rules_doc const &, Rules_doc const &);


/* ====================== config / broadcast ====================== */

/*
 * The configd broadcast mode key (binding): '<config>' root with
 * '<key name="wm.layout.mode" value="floating|tiling"/>' children.
 * Absent key, absent broadcast node, or any unrecognized value reads
 * as FLOATING (the registered configd default).
 */
Mode parse_layout_mode(Genode::Node const &configd_broadcast);

/*
 * Optional explicit '<work_area x= y= width= height=/>' from the
 * component config. Returns false when absent or degenerate
 * (non-positive size) so the caller can apply the public-info
 * fallback.
 */
bool work_area_from_node(Genode::Node const &config, Work_area &);


/* ============================ windows =========================== */

struct Window
{
	unsigned long       wid { 0 };
	Genode::String<256> label { };
	Genode::String<256> title { };
	bool          resizeable { false };
	unsigned long create_seq { 0 };

	/* current INNER screen-absolute geometry; x=y=-32000 parks */
	Rect inner { };

	/* saved pre-minimize / pre-maximize geometry */
	Rect restore { };

	bool minimized     { false };
	bool maximized     { false };
	bool hidden        { false };
	bool close_pending { false };
};


/* ============================= model ============================ */

struct Model
{
	private:

		Work_area _work { };
		Margins   _margins { };
		Mode      _mode { Mode::FLOATING };

		/* static inline rules: initial placement (ABSOLUTE frame) */
		Rules_doc _static_doc { };

		/* tasklist rules ROM: last-wins updates (BOUNDARY_RELATIVE
		 * frame); when invalid the static doc governs */
		Rules_doc _rom_doc { };
		bool      _rom_valid { false };

		Window _windows[MAX_WINDOWS] { };
		unsigned _num_windows { 0 };
		unsigned long _next_create_seq { 1 };

		/* focus_request id dedup (one processed id remembered) */
		bool          _have_focus_id { false };
		unsigned long _focus_id { 0 };

		/* focus steering (the `focus` report content) and the
		 * focus_state truth (the only source of focused="yes") */
		bool          _have_desired_focus { false };
		unsigned long _desired_focus { 0 };
		bool          _have_focus_truth { false };
		unsigned long _focus_truth { 0 };

		/* stack order (wids), front first: newest in front at
		 * creation, raise-to-front on focus steering and chrome
		 * presses */
		unsigned long _stack[MAX_WINDOWS] { };
		unsigned      _stack_size { 0 };

		/* gesture state (hit-tested chrome grab) */
		enum class Grab_kind { NONE, MOVE, RESIZE, BUTTON };
		Grab_kind _grab_kind { Grab_kind::NONE };
		unsigned long _grab_wid { 0 };
		Wm_protocol::Hit _grab_hit { Wm_protocol::Hit::NONE };
		Wm_protocol::Sizer_zone _grab_zone { Wm_protocol::Sizer_zone::NONE };
		int  _grab_px { 0 }, _grab_py { 0 };
		Rect _grab_inner { };

		bool _dirty { false };

		/* ---- internals ---- */

		Window       *_by_wid(unsigned long);
		Window const *_by_wid(unsigned long) const;
		int           _stack_pos(unsigned long) const;   /* -1 absent */
		void          _raise(unsigned long);
		bool          _arrangement_eligible(Window const &) const;

		Rules_doc const &_operative_doc() const
		{
			return _rom_valid ? _rom_doc : _static_doc;
		}

		void _cascade_place(Window &);
		void _apply_assign(Window &, Rule_assign const &, Rule_frame);
		void _place_arrangement();
		void _park(Window &);
		void _minimize(Window &);
		void _maximize(Window &);
		void _unmaximize(Window &);
		void _restore(Window &);
		void _destroy_at(unsigned idx);

		bool _gesture_eligible(Window const &) const;
		bool _resize_eligible(Window const &) const;

		/*
		 * Hit test with button disambiguation: the frozen
		 * 'chrome_hit' corner squares (16x16) overlap the close
		 * button's center column (its center sits exactly on the
		 * corner boundary), so points inside a shared
		 * 'button_rect' are the button (the click contract's
		 * "same rect"), everything else falls through to
		 * 'chrome_hit' (corner > edge grip > title/move).
		 */
		Wm_protocol::Hit _hit_test(Window const &, int px, int py) const;

		Rect _maximized_inner() const;

	public:

		/*
		 * Configuration (called once before any input): work area
		 * (also the reported boundary), decorator margins, initial
		 * mode, and the static inline rules document for initial
		 * placement. Margins may be zero at startup and updated
		 * later ('margins').
		 */
		void configure(Work_area const &, Margins const &, Mode,
		               Rules_doc const &static_rules);

		/* ---- consumed channels (complete snapshots / events) ---- */

		/*
		 * window_list (identity source, complete last-state-wins
		 * snapshot): new wids create windows in creation order
		 * (initial placement from the static rules, or the tiling
		 * arrangement), vanished wids are destroyed. The id
		 * attribute is the internal wid.
		 */
		void window_list(Window_list_entry const *, Genode::size_t);

		/*
		 * focus_request (sole writer: TasklistController): dedup on
		 * 'id' — a repeated id is ignored. A request for a known
		 * label raises the window and steers focus to it; a
		 * minimized window is restored in the same step
		 * (restore-with-focus, U3).
		 */
		void focus_request(Focus_request const &);

		/*
		 * rules ROM update (sole writer: TasklistController,
		 * last-wins per publication). A document identical to the
		 * current one is not a new publication (no re-apply). The
		 * document's assigns update existing windows in the
		 * document's frame (BOUNDARY_RELATIVE for the tasklist
		 * ROM): geometry, parked (minimize), maximized, and
		 * visible semantics in floating mode; state hints plus
		 * stored floating preference only in tiling mode (tiled
		 * geometry stays layouter-owned).
		 */
		void rules(Rules_doc const &);

		/*
		 * rules ROM invalid/empty (upstream semantics): the static
		 * inline document comes back into effect (once per
		 * valid->invalid transition).
		 */
		void rules_invalid();

		/* focus_state (sole truth for window_layout focused="yes") */
		void focus_state(Focus_target const &);

		/* decorator_margins (may arrive late; zero is legal) */
		void margins(Margins const &);

		/* wm.layout.mode (default floating) */
		void mode(Mode);

		/* ---- input events (screen-absolute pointer coordinates) ---- */

		/*
		 * Pointer press: hit-tests the topmost window's chrome
		 * ('chrome_hit'); any chrome hit focuses+raises the
		 * window, buttons arm a click, title/sizer arms a
		 * move/resize grab when the window is gesture-eligible.
		 * Returns true when a chrome element was hit.
		 */
		bool press(int px, int py);

		/*
		 * Pointer motion: applies the active move/resize grab
		 * (exact pointer delta; resize clamps at 64x64 inner).
		 */
		void motion(int px, int py);

		/*
		 * Pointer release: completes a button click when the
		 * release lands within CLICK_SLOP_PX of the press point
		 * in the SAME button rect (a sloppy release fires no
		 * action); ends any grab. Returns true on a chrome
		 * release.
		 */
		bool release(int px, int py);

		/* true while a move/resize grab is active (exclusive_input) */
		bool drag_active() const;

		/* ---- produced channels ---- */

		/*
		 * window_layout report body (protocol v1): one <boundary>
		 * carrying the WORK AREA rect, windows in stack order
		 * (front first) with screen-absolute INNER geometry,
		 * parked windows at (-32000,-32000), focused="yes" only
		 * per the focus_state truth. Hidden windows are omitted.
		 */
		void gen_window_layout(Genode::Xml_generator &) const;

		/*
		 * window_request report body (v1.1, complete desired
		 * state): per window either the INNER screen-absolute
		 * geometry (x=y=-32000 parks) or the sticky
		 * action="close" node — kept in every snapshot until the
		 * wid disappears from window_list (the layouter never
		 * pretends a window closed early).
		 */
		void gen_window_request(Genode::Xml_generator &) const;

		/* focus report body (v1.1 steering snapshot) */
		void gen_focus(Genode::Xml_generator &) const;

		/* one dirty flag for all three reports (complete snapshots) */
		bool consume_dirty();

		/* ---- test/inspection accessors ---- */

		unsigned          num_windows() const { return _num_windows; }
		Window const     &window(unsigned i) const { return _windows[i]; }
		Window const     *find(unsigned long wid) const { return _by_wid(wid); }
		Mode              mode() const { return _mode; }
		Work_area const  &work_area() const { return _work; }
		Margins const    &margins() const { return _margins; }
		bool              close_pending(unsigned long wid) const;

		/*
		 * The Master-Stack slot geometry (OUTER rect) of the
		 * slot_index-th arrangement slot for eligible_total
		 * eligible windows — exposed for the deterministic tests
		 * (identical math to the tiling acceptance probe).
		 */
		Rect arrangement_slot_outer(unsigned slot_index,
		                            unsigned eligible_total) const;
};


}  /* namespace Sponge::Layout_model */
