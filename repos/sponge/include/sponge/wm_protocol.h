/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_wm_protocol — Sponge-owned WM protocol v1 report/ROM shapes
 * and the binding internal protocol v1.1 (see the internal section
 * below; registered in docs/plans/phase16-sponge-wm.md §11).
 *
 * Frozen contract: docs/05-sponge-de.md §4.6 "Protocol v1 report/ROM
 * contract (frozen 2026-11-12)" (Criterion 16, D16W.6). This library
 * holds the protocol-side types, parsing, and generation helpers shared
 * by the native WM components:
 *
 *   generation (producer side):
 *     'window_list'   — sponge_wm (wm core -> TasklistController)
 *     'window_layout' — sponge_layouter (layouter -> TasklistController)
 *
 *   parsing (consumer side):
 *     'window_list'   — sponge_layouter (identity source)
 *     'focus_request' — sponge_layouter (sole writer: TasklistController)
 *     'rules'         — sponge_layouter (sole writer: TasklistController)
 *
 *   'window_layout' parsing exists for contract verification (tests and
 *   probes). The delivered TasklistController is NOT a user of this
 *   library and is never modified by it.
 *
 * SINGLE-WRITER DISCIPLINE (AGENTS.md §1.2, D16W.6): 'focus_request'
 * and 'rules' are written ONLY by the TasklistController. This library
 * deliberately provides NO generation helpers for those two labels so
 * no native component can accidentally become a second writer. The
 * contract tests pin their shapes by parsing golden documents that
 * byte-match the controller's emission format
 * (src/sponge-de/panel/tasklist_controller.cc).
 *
 * WIRE ENCODING (contract clarification): the frozen §4.6 shapes are
 * the Node-level model. Genode 26.08 serializes report/ROM payloads
 * through two interchangeable encodings — XML (Xml_generator) and HID
 * (Hid_generator, the sandbox default for the `Generator` facade, see
 * base/src/lib/ldso/main.cc '_generate_xml'). Genode::Node reads both
 * transparently, which is exactly how the delivered TasklistController
 * consumes the upstream reports. Therefore:
 *
 *   - all parse entry points take 'Genode::Node const &' and work on
 *     either encoding (and on either of the two syntaxes);
 *   - all generation helpers write through 'Genode::Xml_generator' —
 *     the Sponge report convention ('Expanding_reporter::generate_xml'
 *     as used by every Sponge-owned report writer) — whose output is
 *     the XML rendering of the frozen shape, with the upstream
 *     attribute order preserved for byte-stable reports.
 *
 * PURITY CONTRACT (hard, same as <sponge/pkg_config.h>):
 *
 *   No Genode::Env, no Report/ROM sessions, no File_system, no
 *   hardware access, no exceptions, no RTTI-dependent design, no
 *   mutable global state, no dynamic allocation. The caller owns every
 *   buffer. The only Genode headers used are the public <base/node.h>,
 *   <util/string.h>, and <util/xml_generator.h>.
 *
 * SHAPE RULES (docs/05-sponge-de.md §4.6, mirrored from the delivered
 * TasklistController consumption):
 *
 *   - window_list entries without a label identity (empty/missing
 *     'label') are ignored — by the generator (never emitted) and by
 *     the parser (never visited).
 *   - window_list entries without a defined size (width/height not
 *     both > 0) are omitted from the report ("omitted by the upstream
 *     wm until first sized").
 *   - window_layout windows without a title identity (empty/missing
 *     'title') are ignored the same way. The 'title' attribute is the
 *     concatenation 'label + " " + client title', or the bare 'label'
 *     when the client title is empty — see 'layout_title'. Consumers
 *     match identity pairs by that concatenation, never by splitting
 *     on spaces.
 *   - boolean attributes use the 'yes' lexeme when true and are
 *     omitted when false ('has_alpha'/'hidden'/'resizeable' in
 *     window_list, 'focused' in window_layout), matching the upstream
 *     producers. 'rules' assigns always carry 'maximized="yes|no"'.
 *   - pass-through fields of the frozen contract ('id'/'width'/
 *     'height' of window_list; window_layout 'id', 'has_alpha',
 *     'maximizer', 'closer', <highlight>) are ignored by the parsers
 *     and the window_layout generator omits them (the contract allows
 *     native producers to omit pass-through fields). The window_list
 *     generator emits them in the upstream attribute order so the
 *     native report is byte-identical to the upstream XML shape.
 *   - 'focus_request' parsing is strict: the root must be
 *     <focus_request> with BOTH a non-empty 'label' attribute and an
 *     'id' attribute (the layouter deduplicates on 'id', so the exact
 *     value must be preserved). Anything else is rejected as malformed.
 *   - STRING ATTRIBUTE ENCODE/DECODE (H1 hardening): window titles and
 *     labels are untrusted (client-controlled, up to 63/255 bytes,
 *     may contain '"', '&', '<', '>'). Genode::Xml_generator writes
 *     attribute values verbatim, so every generation helper escapes
 *     the attribute-injection set ('&', '"', '<') through the one
 *     shared 'escape_attr' helper before writing it, and every parse
 *     helper reverses it with 'unescape_attr'. The wire therefore
 *     carries escaped text while the library speaks decoded
 *     (semantic) text: 'parse(gen(x)) == x' and
 *     'gen(parse(gen(x))) == gen(x)' (no double escaping), and the
 *     identity composition 'layout_title' plus the TasklistController's
 *     'label + " " + title' raw-wire match stay aligned because
 *     escaping is per-character and the space separator is inert.
 *     '>' is left raw on purpose (XML 1.0 reserves only '<', '&', and
 *     the quote character inside attribute values, so a raw '>' is
 *     inert and cannot forge nodes) and because the canonical "->"
 *     Genode session-label separator must stay byte-identical on the
 *     wire for the frozen label-matching consumers; see 'escape_attr'.
 *     Valid attribute text (no '&', '"', '<') is byte-identical on the
 *     wire and round-trips unchanged.
 *
 * GEOMETRY SEMANTICS (docs/05-sponge-de.md §4.6):
 *
 *   - parking sentinel: a reported window whose 'xpos' AND 'ypos' are
 *     both <= -32000 reads as parked/minimized (the established write
 *     value is xpos=ypos=-32000). A single sentinel coordinate does
 *     not park.
 *   - window_layout coordinates are screen-absolute; rules <assign>
 *     xpos/ypos are target-relative — subtract the boundary xpos/ypos
 *     (see 'assign_position'). Feeding absolute values back drifts the
 *     placement by the boundary origin on every republish.
 *   - maximized is NOT reported (v1 adds no maximized attribute to
 *     window_layout); consumers infer it from geometry and publish it
 *     as <assign maximized="yes|no">.
 */

#pragma once

#include <base/node.h>
#include <util/string.h>
#include <util/xml_generator.h>


namespace Sponge::Wm_protocol {


/* ====================== geometry semantics ====================== */

/* Parking sentinel (docs/05-sponge-de.md §4.6). */
enum : int { PARKED_COORD = -32000 };

/*
 * True when the reported position reads as parked/minimized: BOTH
 * coordinates at or below the sentinel. A single sentinel coordinate
 * does not park.
 */
bool parked(int xpos, int ypos);

/* The established parking write value: xpos=ypos=-32000. */
struct Position { int xpos, ypos; };

Position parked_position();

/*
 * window_layout screen-absolute -> <assign> target-relative
 * conversion: subtract the boundary origin. Returns
 * { abs_xpos - boundary_xpos, abs_ypos - boundary_ypos }.
 */
Position assign_position(int abs_xpos, int abs_ypos,
                         int boundary_xpos, int boundary_ypos);

/*
 * The window_layout 'title' identity: 'label + " " + client title',
 * or the bare 'label' when the client title is empty (never a
 * trailing or double space). Consumers match window_layout titles
 * against this concatenation (or the bare label), never by splitting
 * on spaces. Truncates at 255 characters like every Genode::String.
 */
Genode::String<256> layout_title(char const *label, char const *title);


/* ========================= shape types ========================== */

/* One <window> entry of the window_list report. */
struct Window_list_entry
{
	unsigned long       id         { 0 };   /* pass-through, emitted */
	Genode::String<256> label      { };    /* required identity */
	Genode::String<256> title      { };    /* client title, may be empty */
	long                width      { 0 };  /* pass-through, emitted */
	long                height     { 0 };
	bool                has_alpha  { false };
	bool                hidden     { false };
	bool                resizeable { false };
};

/* One <boundary> of the window_layout report (v1: one target, screen). */
struct Boundary
{
	Genode::String<64> name   { };   /* pass-through target name */
	int                xpos   { 0 }; /* consumed: origin + area */
	int                ypos   { 0 };
	long               width  { 0 };
	long               height { 0 };
};

/* One <window> of the window_layout report. */
struct Layout_window
{
	Genode::String<256> title    { };      /* concatenated identity */
	int                 xpos     { 0 };    /* screen-absolute origin */
	int                 ypos     { 0 };
	long                width    { 0 };    /* inner-geometry size */
	long                height   { 0 };
	bool                focused  { false };
};

/* The focus_request report root (sole writer: TasklistController). */
struct Focus_request
{
	unsigned long       id    { 0 };   /* monotonic counter, preserved */
	Genode::String<256> label { };    /* the full window_list label */
};

/*
 * One <assign> of the rules document (sole writer: TasklistController
 * for the tracked assignments; static fragments may carry the wider
 * upstream vocabulary: label_prefix/label_suffix, visible,
 * xpos/ypos="any"). Mirrors the upstream window_layouter assign
 * semantics (assign.h): 'has_position' requires BOTH xpos and ypos
 * attributes, 'has_size' requires BOTH width and height; 'visible'
 * defaults to true.
 */
struct Assign
{
	Genode::String<256> label        { };
	Genode::String<256> label_prefix { };
	Genode::String<256> label_suffix { };
	Genode::String<64>  target       { };
	bool  has_position { false };
	bool  xpos_any     { false };   /* xpos="any" */
	bool  ypos_any     { false };   /* ypos="any" */
	int   xpos         { 0 };
	int   ypos         { 0 };
	bool  has_size     { false };
	long  width        { 0 };
	long  height       { 0 };
	bool  maximized    { false };
	bool  visible      { true };
};

/* One <press>/<release> key-sequence node of the rules document. */
struct Key_action
{
	Genode::String<24> key    { };
	Genode::String<32> action { };
};

/*
 * The rules document meta data (sole writer: TasklistController).
 * Assigns are visited separately, in document order (frontmost
 * first — child order is semantic). 'press'/'release' hold the FIRST
 * node of each kind; the counts expose repetitions.
 */
struct Rules
{
	Genode::String<64> screen_name { };      /* first <screen name> */
	unsigned num_screens  { 0 };
	unsigned num_assigns  { 0 };
	unsigned num_press    { 0 };
	unsigned num_release  { 0 };
	bool     has_press    { false };
	bool     has_release  { false };
	Key_action press   { };
	Key_action release { };
};


/* ======================= generation (XML) ======================= */

/*
 * Escape one string for use as an XML attribute value (the single
 * shared encoder; its exact inverse is 'unescape_attr' below):
 *
 *   '&' -> "&amp;"   '"' -> "&quot;"   '<' -> "&lt;"
 *
 * This is the full set XML 1.0 requires (and C14N escapes) inside a
 * quoted attribute value: '&', the quote character, and '<'. '>'
 * stays raw on purpose. Re-evaluated against the security
 * requirement and the actual constraints: a raw '>' is explicitly
 * allowed in an attribute value (XML 1.0 reserves only '<', '&', and
 * the quote there) and cannot close the attribute or forge nodes, so
 * escaping it buys no safety; but the canonical "->" separator of
 * Genode session labels ("parent -> child") must stay byte-identical
 * on the wire for the frozen byte-shape goldens and the label-prefix
 * consumers (native_wm_common + native_wm_qmp_probe match raw '->'
 * text and are out of scope to change). Escaping '>' would therefore
 * be a cosmetic wire change that breaks legitimate label matching —
 * the opposite of the stated goal. So the escaped set is exactly the
 * injection-capable characters, and '>' remains raw.
 *
 * All other bytes pass through unchanged, so valid attribute text is
 * byte-identical to its input. The capacity holds the worst case of
 * the protocol's largest strings (Window_list_entry label/title,
 * Genode::String<256>: 255 bytes * 6 = 1530 escaped bytes); the
 * helper never truncates in the middle of an entity (oversized input
 * is dropped at an entity boundary).
 *
 * Why this exists: titles/labels are untrusted window state. Written
 * verbatim into an attribute, a quote-bearing title closes the
 * attribute early and forges sibling nodes (or breaks the document so
 * a parse failure reads as an empty snapshot). Escaping at generation
 * keeps the document shape intact so the value cannot inject nodes.
 */
Genode::String<1600> escape_attr(char const *src);

/*
 * Exact inverse of 'escape_attr': decode one escaped attribute value
 * back to its original text. Every parse helper below funnels string
 * attribute reads through this so the library speaks decoded
 * (semantic) text while the wire carries escaped text. The inverse
 * covers the three entities 'escape_attr' emits plus '&gt;'/'&apos;'
 * for foreign producers. A bare '&' or an incomplete entity passes
 * through unchanged, so a raw/HID value decodes to itself and only
 * well-formed entity text is translated — 'unescape_attr(escape_attr(x))
 * == x' for every input, which is what keeps 'gen(parse(gen(x)))' from
 * double-escaping and keeps the TasklistController's raw-wire
 * 'label + " " + title' match aligned with the composed window_layout
 * title. 'CAP' is the destination field width (decode only shrinks,
 * so the escaped input may be read at the same width).
 */
template <Genode::size_t CAP>
Genode::String<CAP> unescape_attr(char const *src)
{
	char buf[CAP];
	Genode::size_t n = 0;

	for (char const *p = (src ? src : ""); *p != '\0' && n + 1 < CAP; ) {

		char const *suffix = nullptr;
		char        plain  = '\0';

		if      (Genode::strcmp(p, "&amp;",  5) == 0) { plain = '&';  suffix = p + 5; }
		else if (Genode::strcmp(p, "&quot;", 6) == 0) { plain = '"';  suffix = p + 6; }
		else if (Genode::strcmp(p, "&lt;",   4) == 0) { plain = '<';  suffix = p + 4; }
		else if (Genode::strcmp(p, "&gt;",   4) == 0) { plain = '>';  suffix = p + 4; }
		else if (Genode::strcmp(p, "&apos;", 6) == 0) { plain = '\''; suffix = p + 6; }

		if (suffix) {
			buf[n++] = plain;
			p = suffix;
		} else {
			buf[n++] = *p++;
		}
	}

	buf[n] = '\0';
	return Genode::String<CAP>(Genode::Cstring(buf, n));
}

/*
 * Emit one <window> entry of a window_list document in the upstream
 * attribute order (id, label, title, width, height, then the 'yes'
 * form of has_alpha/hidden/resizeable when true).
 *
 * Returns false and emits nothing when the entry has no label
 * identity or no defined size (the report-level omission rules above).
 * Use inside 'Expanding_reporter::generate_xml' (the reporter wraps
 * the <window_list> root) or 'Xml_generator::generate'.
 */
bool gen_window_list_entry(Genode::Xml_generator &g,
                           Window_list_entry const &entry);

/*
 * Emit one <window> of a window_layout document (title, xpos, ypos,
 * width, height, focused="yes" when true; pass-through attributes are
 * omitted). Returns false and emits nothing when the window has no
 * title identity.
 */
bool gen_window_layout_window(Genode::Xml_generator &g,
                              Layout_window const &window);

/*
 * Emit one <boundary> of a window_layout document (name, xpos, ypos,
 * width, height) with 'num_windows' windows nested inside. Windows
 * without a title identity are skipped ('gen_window_layout_window').
 */
void gen_boundary(Genode::Xml_generator &g, Boundary const &boundary,
                  Layout_window const *windows, Genode::size_t num_windows);


/* ========================== parsing ============================= */

/*
 * Parse a window_list document ('<window_list>' root). Returns false
 * if the root is not <window_list> (including malformed or empty
 * input — Genode::Node degrades invalid syntax to an empty node).
 * Calls 'fn(Window_list_entry const &)' for every entry with a
 * non-empty 'label', in document order; missing optional attributes
 * take the defaults of the shape types.
 */
template <typename FN>
bool parse_window_list(Genode::Node const &root, FN const &fn)
{
	if (!root.has_type("window_list"))
		return false;

	root.for_each_sub_node("window", [&] (Genode::Node const &n) {
		Window_list_entry e;
		e.label = unescape_attr<256>(
			n.attribute_value("label", Genode::String<256>()).string());

		/* no label identity -> ignored (contract §window_list) */
		if (e.label.string()[0] == '\0')
			return;

		e.title      = unescape_attr<256>(
			n.attribute_value("title", Genode::String<256>()).string());
		e.id         = n.attribute_value("id", 0UL);
		e.width      = n.attribute_value("width", 0L);
		e.height     = n.attribute_value("height", 0L);
		e.has_alpha  = n.attribute_value("has_alpha",  false);
		e.hidden     = n.attribute_value("hidden",     false);
		e.resizeable = n.attribute_value("resizeable", false);

		fn(e);
	});

	return true;
}

/*
 * Parse a window_layout document ('<window_layout>' root). Returns
 * false if the root is not <window_layout>. Calls
 * 'fn(Boundary const &, Layout_window const &)' for every window with
 * a non-empty 'title' of every <boundary>, in document order, paired
 * with its owning boundary (boundary attributes default to 0/"").
 */
template <typename FN>
bool parse_window_layout(Genode::Node const &root, FN const &fn)
{
	if (!root.has_type("window_layout"))
		return false;

	root.for_each_sub_node("boundary", [&] (Genode::Node const &b) {
		Boundary boundary;
		boundary.name   = unescape_attr<64>(
			b.attribute_value("name", Genode::String<64>()).string());
		boundary.xpos   = b.attribute_value("xpos", 0);
		boundary.ypos   = b.attribute_value("ypos", 0);
		boundary.width  = b.attribute_value("width", 0L);
		boundary.height = b.attribute_value("height", 0L);

		b.for_each_sub_node("window", [&] (Genode::Node const &n) {
			Layout_window w;
			w.title = unescape_attr<256>(
				n.attribute_value("title", Genode::String<256>()).string());

			/* no title identity -> ignored (contract §window_layout) */
			if (w.title.string()[0] == '\0')
				return;

			w.xpos    = n.attribute_value("xpos", 0);
			w.ypos    = n.attribute_value("ypos", 0);
			w.width   = n.attribute_value("width", 0L);
			w.height  = n.attribute_value("height", 0L);
			w.focused = n.attribute_value("focused", false);

			fn(boundary, w);
		});
	});

	return true;
}

/*
 * Parse a focus_request report ('<focus_request>' root with 'id' and
 * 'label' attributes). Strict: returns false unless the root type
 * matches and both attributes are present with a non-empty label.
 * On success 'out' receives the exact 'id' value (the layouter's
 * dedup key — preserved verbatim) and the exact label.
 */
inline bool parse_focus_request(Genode::Node const &root, Focus_request &out)
{
	if (!root.has_type("focus_request"))
		return false;

	/* strict: both root attributes are required by the contract */
	if (!root.has_attribute("id") || !root.has_attribute("label"))
		return false;

	Focus_request req;
	req.id    = root.attribute_value("id", 0UL);
	req.label = unescape_attr<256>(
		root.attribute_value("label", Genode::String<256>()).string());

	/* no label identity -> nothing to focus */
	if (req.label.string()[0] == '\0')
		return false;

	out = req;
	return true;
}

/*
 * Parse a rules document ('<rules>' root). Returns false if the root
 * is not <rules>. Fills 'info' with the document meta data (screen
 * nodes, press/release nodes) and calls 'fn(Assign const &)' for
 * every <assign> in document order (frontmost first). Missing
 * optional attributes take the defaults of 'Assign' ('visible'
 * defaults to true).
 */
template <typename FN>
bool parse_rules(Genode::Node const &root, Rules &info, FN const &fn)
{
	if (!root.has_type("rules"))
		return false;

	info = Rules { };

	root.for_each_sub_node("screen", [&] (Genode::Node const &n) {
		if (info.num_screens == 0)
			info.screen_name = unescape_attr<64>(
				n.attribute_value("name", Genode::String<64>()).string());
		info.num_screens++;
	});

	root.for_each_sub_node("assign", [&] (Genode::Node const &n) {
		Assign a;
		a.label        = unescape_attr<256>(
			n.attribute_value("label", Genode::String<256>()).string());
		a.label_prefix = unescape_attr<256>(
			n.attribute_value("label_prefix", Genode::String<256>()).string());
		a.label_suffix = unescape_attr<256>(
			n.attribute_value("label_suffix", Genode::String<256>()).string());
		a.target       = unescape_attr<64>(
			n.attribute_value("target", Genode::String<64>()).string());

		a.has_position = n.has_attribute("xpos") && n.has_attribute("ypos");
		a.has_size     = n.has_attribute("width") && n.has_attribute("height");
		a.xpos_any     = n.attribute_value("xpos", Genode::String<20>()) == "any";
		a.ypos_any     = n.attribute_value("ypos", Genode::String<20>()) == "any";
		a.xpos         = n.attribute_value("xpos", 0);
		a.ypos         = n.attribute_value("ypos", 0);
		a.width        = n.attribute_value("width", 0L);
		a.height       = n.attribute_value("height", 0L);
		a.maximized    = n.attribute_value("maximized", false);
		a.visible      = n.attribute_value("visible", true);

		info.num_assigns++;
		fn(a);
	});

	root.for_each_sub_node("press", [&] (Genode::Node const &n) {
		if (!info.has_press) {
			info.press.key    = unescape_attr<24>(
				n.attribute_value("key", Genode::String<24>()).string());
			info.press.action = unescape_attr<32>(
				n.attribute_value("action", Genode::String<32>()).string());
			info.has_press    = true;
		}
		info.num_press++;
	});

	root.for_each_sub_node("release", [&] (Genode::Node const &n) {
		if (!info.has_release) {
			info.release.key    = unescape_attr<24>(
				n.attribute_value("key", Genode::String<24>()).string());
			info.release.action = unescape_attr<32>(
				n.attribute_value("action", Genode::String<32>()).string());
			info.has_release    = true;
		}
		info.num_release++;
	});

	return true;
}


/* ============= internal protocol v1.1 (binding) =================
 *
 * The Sponge-owned INTERNAL contract between sponge_wm (core),
 * sponge_layouter, and sponge_decorator
 * (docs/plans/phase16-sponge-wm.md §11). Distinct from the frozen
 * Tasklist-facing protocol v1 above — v1 stays unchanged and frozen.
 *
 * Channels (every report is a COMPLETE last-state-wins snapshot):
 *
 *   focus_state      core      -> layouter   (focus truth)
 *   window_request   layouter  -> core       (complete desired state)
 *   focus            layouter  -> core       (steering)
 *   window_chrome    core      -> decorator  (lifecycle/draw snapshot)
 *   decorator_margins decorator -> layouter + core + probes
 *
 * There is NO hover report, NO event-style window_control channel,
 * and NO seq_number in the v1.1 control path. Lossless gestures use
 * core->layouter Input forwarding (component behavior). Input and
 * exclusive grabs are component behavior, documented but not part of
 * this protocol. 'focus' is steering only; 'focus_state' is the sole
 * truth for window_layout's consumed focused="yes".
 *
 * Identity: the core assigns a monotonic 'wid' per window and emits
 * it as the frozen window_list 'id'. The decorator names its chrome
 * view with the decimal 'wid' ('view_title').
 *
 * Geometry: the OUTER frame is the INNER geometry expanded by the
 * published decorator_margins ('outer_from_inner'/'inner_from_outer';
 * the title band is the top margin). window_request geometry is
 * INNER and screen-absolute; x=y=-32000 parks. Close is a STICKY
 * action: the layouter keeps the <window ... action="close"/> node in
 * every window_request snapshot until the wid disappears from
 * window_list; the core applies the app-visible 0x0 close convention
 * when processing it. window_chrome fw/fh are the OUTER frame size;
 * every entry carries the per-window focus state as focused="yes"/
 * focused="no" (an absent 'focused' parses as false, backward
 * compatible with pre-focus v1.1 snapshots), and the producer
 * publishes only create/destroy/title/resizeable/focus/size change —
 * never move.
 */

/* wid <-> chrome-view-title (the decorator view name is the decimal
 * wid). 'parse_view_title' accepts canonical decimal only. */
Genode::String<32> view_title(unsigned long wid);
bool parse_view_title(char const *title, unsigned long &wid);


/* ---- focus_state (core -> layouter) / focus (layouter -> core) ---- */

struct Focus_target
{
	bool          set { false };  /* false: empty root = no focus */
	unsigned long wid { 0 };
};

void gen_focus_state(Genode::Xml_generator &g, Focus_target const &t);
void gen_focus(Genode::Xml_generator &g, Focus_target const &t);

/* Shared singleton <window wid="..."/> reader (used by the two root
 * shapes below). Strict: a <window> without 'wid' is malformed; the
 * first window wins when several are present. */
inline bool parse_window_ref(Genode::Node const &root, char const *root_type,
                             Focus_target &out)
{
	if (!root.has_type(root_type))
		return false;

	Focus_target t;
	bool seen = false, malformed = false;

	root.for_each_sub_node("window", [&] (Genode::Node const &n) {
		if (malformed)
			return;
		if (!n.has_attribute("wid")) {
			malformed = true;
			return;
		}
		if (!seen) {
			t.wid = n.attribute_value("wid", 0UL);
			t.set = true;
			seen = true;
		}
	});

	if (malformed)
		return false;

	out = t;
	return true;
}

/* Empty root = no focus ('out.set' false). */
inline bool parse_focus_state(Genode::Node const &root, Focus_target &out)
{
	return parse_window_ref(root, "focus_state", out);
}

inline bool parse_focus(Genode::Node const &root, Focus_target &out)
{
	return parse_window_ref(root, "focus", out);
}


/* ---- window_request (layouter -> core, complete desired state) ---- */

struct Window_request
{
	unsigned long wid { 0 };
	bool          close { false };  /* action="close" (sticky) */
	int           x { 0 }, y { 0 }; /* INNER screen-absolute; -32000/-32000 parks */
	long          w { 0 }, h { 0 };
};

void gen_window_request_entry(Genode::Xml_generator &g,
                              Window_request const &r);

/*
 * Visits every <window> of a '<window_request>' document in document
 * order. A close entry is '<window wid=".." action="close"/>'
 * (geometry attributes ignored when present); a geometry entry
 * requires x/y/w/h. Returns false on root mismatch, unknown 'action'
 * value, or a missing required attribute (treat the whole snapshot as
 * malformed and drop it).
 */
template <typename FN>
bool parse_window_request(Genode::Node const &root, FN const &fn)
{
	if (!root.has_type("window_request"))
		return false;

	bool malformed = false;

	root.for_each_sub_node("window", [&] (Genode::Node const &n) {
		if (malformed)
			return;

		Window_request r;
		if (!n.has_attribute("wid")) {
			malformed = true;
			return;
		}
		r.wid = n.attribute_value("wid", 0UL);

		if (n.has_attribute("action")) {
			if (!(n.attribute_value("action", Genode::String<16>()) == "close")) {
				malformed = true;
				return;
			}
			r.close = true;
		} else {
			if (!n.has_attribute("x") || !n.has_attribute("y")
			 || !n.has_attribute("w") || !n.has_attribute("h")) {
				malformed = true;
				return;
			}
			r.x = n.attribute_value("x", 0);
			r.y = n.attribute_value("y", 0);
			r.w = n.attribute_value("w", 0L);
			r.h = n.attribute_value("h", 0L);
		}

		fn(r);
	});

	return !malformed;
}


/* ---- window_chrome (core -> decorator, lifecycle/draw snapshot) ---- */

struct Window_chrome
{
	unsigned long       wid { 0 };
	Genode::String<256> label { };       /* frozen external identity */
	Genode::String<256> title { };
	bool                resizeable { false };
	bool                focused    { false };  /* per-window focus truth */
	long                fw { 0 }, fh { 0 };  /* OUTER frame size */
};

void gen_window_chrome_entry(Genode::Xml_generator &g,
                             Window_chrome const &c);

/*
 * Visits every <window> of a '<window_chrome>' document in document
 * order. 'wid', 'fw', and 'fh' are required; label/title default to
 * empty, 'resizeable' to false, and a missing 'focused' to false
 * (backward compatible with pre-focus v1.1 snapshots). Destroy is
 * the window's ABSENCE from the snapshot. Returns false on root
 * mismatch or a missing required attribute.
 */
template <typename FN>
bool parse_window_chrome(Genode::Node const &root, FN const &fn)
{
	if (!root.has_type("window_chrome"))
		return false;

	bool malformed = false;

	root.for_each_sub_node("window", [&] (Genode::Node const &n) {
		if (malformed)
			return;

		Window_chrome c;
		if (!n.has_attribute("wid") || !n.has_attribute("fw")
		 || !n.has_attribute("fh")) {
			malformed = true;
			return;
		}
		c.wid        = n.attribute_value("wid", 0UL);
		c.label      = unescape_attr<256>(
			n.attribute_value("label", Genode::String<256>()).string());
		c.title      = unescape_attr<256>(
			n.attribute_value("title", Genode::String<256>()).string());
		c.resizeable = n.attribute_value("resizeable", false);
		c.focused    = n.attribute_value("focused", false);
		c.fw         = n.attribute_value("fw", 0L);
		c.fh         = n.attribute_value("fh", 0L);

		fn(c);
	});

	return !malformed;
}


/* ---- decorator_margins (decorator -> layouter + core + probes) ---- */

struct Margins
{
	int top { 0 }, bottom { 0 }, left { 0 }, right { 0 };
};

/* Emits the single <floating top bottom left right/> child. */
void gen_decorator_margins(Genode::Xml_generator &g, Margins const &m);

/* Strict: the '<decorator_margins>' root must carry a <floating> node
 * with all four attributes (first one wins). */
inline bool parse_decorator_margins(Genode::Node const &root, Margins &out)
{
	if (!root.has_type("decorator_margins"))
		return false;

	Margins m;
	bool seen = false, malformed = false;

	root.for_each_sub_node("floating", [&] (Genode::Node const &n) {
		if (seen || malformed)
			return;
		if (!n.has_attribute("top") || !n.has_attribute("bottom")
		 || !n.has_attribute("left") || !n.has_attribute("right")) {
			malformed = true;
			return;
		}
		m.top    = n.attribute_value("top", 0);
		m.bottom = n.attribute_value("bottom", 0);
		m.left   = n.attribute_value("left", 0);
		m.right  = n.attribute_value("right", 0);
		seen     = true;
	});

	if (malformed || !seen)
		return false;

	out = m;
	return true;
}


/* ================== inner/outer frame geometry ================== */

struct Rect { int x { 0 }, y { 0 }; long w { 0 }, h { 0 }; };

/* OUTER = INNER expanded by the margins (and back). */
Rect outer_from_inner(Rect inner, Margins const &m);
Rect inner_from_outer(Rect outer, Margins const &m);


/* ============== frozen chrome layout + hit testing ===============
 *
 * Layouter hit-tests; the decorator is a pure renderer + margins
 * publisher. These pure helpers are the single source of truth for
 * the chrome metrics shared by both.
 */

enum : int {
	CHROME_BUTTON_W   = 24,
	CHROME_BUTTON_H   = 16,
	CHROME_BUTTON_PAD = 4,
	CHROME_CORNER_PX  = 16,
	CHROME_GRIP_MAX   = 8
};

enum class Button { CLOSE, FULLSCREEN, MINIMIZE };

enum class Sizer_zone { NONE, N, S, E, W, NE, NW, SE, SW };

enum class Hit {
	NONE, TITLE_MOVE,
	BTN_CLOSE, BTN_FULLSCREEN, BTN_MINIMIZE,
	SIZER_N, SIZER_S, SIZER_E, SIZER_W,
	SIZER_NE, SIZER_NW, SIZER_SE, SIZER_SW
};

/* Title band: the top-margin strip of the outer frame. */
Rect title_band(Rect outer, Margins const &m);

/*
 * Button rect, 24x16, vertically centered in the title band, the
 * group right aligned with a 4 px pad, laid out right to left CLOSE,
 * FULLSCREEN, MINIMIZE. Closer and minimizer are always present;
 * fullscreen exists only when 'resizeable' (absent buttons are simply
 * skipped, so the remaining buttons hug the right edge). Returns a
 * zero-size rect when the affordance is absent.
 */
Rect button_rect(Rect outer, Margins const &m, Button b, bool resizeable);

/*
 * Resize zone of an outer-frame-local point: 16x16 corner squares
 * override the edge grips (outermost min(margin, 8) px of each edge);
 * NONE elsewhere.
 */
Sizer_zone sizer_zone(int px, int py, Rect outer, Margins const &m);

/*
 * Full chrome hit test, precedence corner > edge grip > button >
 * title/move; NONE for the client area or outside the frame.
 */
Hit chrome_hit(int px, int py, Rect outer, Margins const &m,
               bool resizeable);


}  /* namespace Sponge::Wm_protocol */
