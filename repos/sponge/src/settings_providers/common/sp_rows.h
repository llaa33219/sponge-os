/* SPDX-License-Identifier: Apache-2.0
 *
 * sp_rows — the provider window's row model (pure logic).
 *
 * A provider window is a vertical list of settings rows. Every row
 * is either read-only (INFO), directly writable to one configd key
 * (UINT / ENUM / TOGGLES / TEXT), or a named action (ACTION). The
 * wire value of a row is always its `text` buffer — the exact string
 * that goes to configd — so the window, the quick pair, and the
 * host self-test all reason about one representation.
 *
 * Pure logic: no sessions, no rendering. The helpers here run both
 * in the provider runtime and in tests/self_test.cc.
 */

#pragma once

#include "sp_validate.h"

namespace Sponge::Settings {

enum class Row_kind { INFO, UINT, ENUM, TOGGLES, TEXT, ACTION };

enum { ROW_LABEL_MAX = 48, ROW_KEY_MAX = 64, ROWS_MAX = 40 };

struct Row
{
	Row_kind    kind { Row_kind::INFO };
	char        label[ROW_LABEL_MAX] { };
	char        key[ROW_KEY_MAX]     { };   /* configd key; "" = custom/no write */
	char        text[VAL_MAX]        { };   /* wire value / text edit buffer */
	Value_spec  spec { };                  /* value constraint (choices, range) */
	unsigned    step { 1 };                /* UINT: increment per key press */

	/* derived / interaction state */
	int      choice  { -1 };   /* ENUM: index into spec.choices, -1 = custom */
	unsigned cursor  { 0 };    /* TOGGLES: focused token index */
	unsigned mask   { 0 };    /* TOGGLES: bit per enabled token */
	bool     editing { false };/* TEXT: edit buffer in progress (skip sync) */
	bool     confirm { false };/* ACTION: armed, a second Enter fires */
	bool     custom  { false };/* writes go through Provider::on_apply */

	/*
	 * The last applied / broadcast-confirmed value. Esc reverts an
	 * in-progress edit to it and a refused write snaps the row back
	 * to it, so the window never shows a value that is not stored.
	 */
	char     applied[VAL_MAX] { };
};


inline void row_set_label(Row &r, char const *label)
{
	unsigned i = 0;
	for (; label[i] && i + 1 < sizeof(r.label); ++i)
		r.label[i] = label[i];
	r.label[i] = 0;
}

inline void row_set_key(Row &r, char const *key)
{
	unsigned i = 0;
	for (; key[i] && i + 1 < sizeof(r.key); ++i)
		r.key[i] = key[i];
	r.key[i] = 0;
}

inline void row_set_text(Row &r, char const *text)
{
	unsigned i = 0;
	for (; text[i] && i + 1 < sizeof(r.text); ++i)
		r.text[i] = text[i];
	r.text[i] = 0;
	r.editing = false;
}

inline void row_set_applied(Row &r)
{
	unsigned i = 0;
	for (; r.text[i] && i + 1 < sizeof(r.applied); ++i)
		r.applied[i] = r.text[i];
	r.applied[i] = 0;
}

/*
 * Derive the kind-specific state (ENUM index, TOGGLES mask/cursor)
 * from the wire text. Used at row creation and whenever an external
 * value (configd broadcast) refreshes the row.
 */
inline void row_sync_state_from_text(Row &r)
{
	if (r.kind == Row_kind::ENUM) {
		r.choice = -1;
		for (unsigned i = 0; i < r.spec.n_choices; ++i)
			if (Genode::strcmp(r.text, r.spec.choices[i]) == 0) {
				r.choice = (int)i;
				break;
			}
		return;
	}

	if (r.kind == Row_kind::TOGGLES) {
		r.mask = 0;
		for (unsigned i = 0; i < r.spec.n_choices && i < 32; ++i) {
			/* exact token membership in the comma list */
			char const *tokens = r.text;
			Genode::size_t const len = Genode::strlen(tokens);
			unsigned start = 0;
			while (start <= len) {
				unsigned end = start;
				while (end < len && tokens[end] != ',')
					++end;

				unsigned first = start;
				unsigned last  = end;
				while (first < last && (tokens[first] == ' ' ||
				       tokens[first] == '\t'))
					++first;
				while (last > first && (tokens[last - 1] == ' ' ||
				       tokens[last - 1] == '\t'))
					--last;

				char token[VAL_MAX] { };
				unsigned const token_length = last - first;
				for (unsigned k = 0; k < token_length &&
				     k + 1 < sizeof(token); ++k)
					token[k] = tokens[first + k];

				if (token_length > 0 &&
				    Genode::strcmp(token, r.spec.choices[i]) == 0) {
					r.mask |= (1u << i);
					break;
				}

				if (end == len)
					break;
				start = end + 1;
			}
		}
		if (r.cursor >= r.spec.n_choices)
			r.cursor = 0;
	}
}

/*
 * Rebuild a TOGGLES row's wire text from its mask, in the canonical
 * token order (no duplicates, no stray whitespace).
 */
inline void row_text_from_mask(Row &r)
{
	r.text[0] = 0;
	unsigned pos = 0;

	for (unsigned i = 0; i < r.spec.n_choices && i < 32; ++i) {
		if (!(r.mask & (1u << i)))
			continue;

		char const *token = r.spec.choices[i];
		if (pos > 0 && pos + 1 < sizeof(r.text))
			r.text[pos++] = ',';
		for (unsigned k = 0; token[k] && pos + 1 < sizeof(r.text); ++k)
			r.text[pos++] = token[k];
	}
	r.text[pos] = 0;
}

/*
 * The displayed value of a row. ENUM rows show the current choice
 * (or the custom value when outside the closed set); ACTION rows
 * show their armed state; everything else shows `text`.
 */
inline char const *row_display(Row const &r)
{
	if (r.kind == Row_kind::ACTION)
		return r.confirm ? "press Enter again to confirm" : "select and press Enter";

	if (r.kind == Row_kind::ENUM && r.choice < 0 && r.text[0])
		return r.text;   /* custom/outside value, shown as-is */

	return r.text;
}

}  /* namespace Sponge::Settings */
