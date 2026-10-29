/* SPDX-License-Identifier: Apache-2.0
 *
 * Implementation of Sponge::Runtime_config_validate — pure validation
 * of the runtime <config> documents sponge_pkgd publishes (Phase 16
 * P1.5 publication seam).
 *
 * The reader is a small syntax-agnostic XML pull parser written right
 * here: Genode's util/xml_node.h raises C++ exceptions on invalid
 * syntax (and links to a noreturn abort under -fno-exceptions), which
 * cannot deliver the structured Result this library promises. This
 * file therefore touches nothing outside the C++ core language: no
 * headers, no libc, no allocation, no exceptions, no mutable globals.
 *
 * See <sponge/runtime_config_validate.h> for the subset contract, the
 * lexemes, and the documented exceptions/limitations.
 */

#include <sponge/runtime_config_validate.h>

using namespace Sponge::Runtime_config_validate;


/* ===================== bounded message builder ===================== */

namespace {

struct Msg
{
	char     buf[Result::DETAIL_CAPACITY] { };
	unsigned len { 0 };

	void add(char const *s)
	{
		for (; *s && len + 1 < Result::DETAIL_CAPACITY; ++s)
			buf[len++] = *s;
	}

	void add(char const *s, unsigned n)
	{
		for (unsigned i = 0; i < n && len + 1 < Result::DETAIL_CAPACITY; ++i)
			buf[len++] = s[i];
	}

	/* decimal, no leading zeros except "0" */
	void add_num(unsigned v)
	{
		char tmp[10];
		unsigned n = 0;
		do {
			tmp[n++] = (char)('0' + v % 10);
			v /= 10;
		} while (v && n < sizeof(tmp));
		while (n)
			add(&tmp[--n], 1);
	}

	/* 'text' with long values truncated */
	void add_quoted(char const *s, unsigned n)
	{
		add("'", 1);
		unsigned const shown = n > 32 ? 32 : n;
		add(s, shown);
		if (shown < n)
			add("...", 3);
		add("'", 1);
	}
};


Result error(Code code, unsigned line, Msg const &m)
{
	Result r;
	r.ok = false;
	r.code = code;

	Msg out;
	out.add("line ");
	out.add_num(line);
	out.add(": ");
	out.add(m.buf, m.len);

	for (unsigned i = 0; i < out.len; ++i)
		r.detail[i] = out.buf[i];
	r.detail[out.len] = '\0';
	return r;
}


/* ===================== lexical primitives ===================== */

struct Cursor
{
	char const *p;
	char const *end;
	unsigned    line;
};

bool at_end(Cursor const &c) { return c.p >= c.end; }

char peek(Cursor const &c, unsigned n = 0)
{
	return (c.p + n < c.end) ? c.p[n] : '\0';
}

void advance(Cursor &c, unsigned n)
{
	for (unsigned i = 0; i < n && !at_end(c); ++i) {
		if (*c.p == '\n')
			++c.line;
		++c.p;
	}
}

bool is_ws(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_name_start(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
	    || c == '_' || c == ':';
}

bool is_name_char(char c)
{
	return is_name_start(c) || (c >= '0' && c <= '9')
	    || c == '.' || c == '-';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_hex(char c)
{
	return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

bool is_printable(char c)
{
	unsigned char const u = (unsigned char)c;
	return u >= 0x20 && u <= 0x7e;
}


/*
 * Entity reference at c.p == '&': &name; | &#NNN; | &#xHH;
 * On success consumes it and returns true.
 */
bool consume_entity(Cursor &c)
{
	if (peek(c) != '&')
		return false;

	Cursor t = c;
	advance(t, 1);

	if (peek(t) == '#') {
		advance(t, 1);
		bool hex = false;
		if (peek(t) == 'x' || peek(t) == 'X') {
			hex = true;
			advance(t, 1);
		}
		unsigned digits = 0;
		while (!at_end(t) && (hex ? is_hex(peek(t)) : is_digit(peek(t)))) {
			advance(t, 1);
			++digits;
		}
		if (!digits || peek(t) != ';')
			return false;
		advance(t, 1);
		c = t;
		return true;
	}

	if (!is_name_start(peek(t)))
		return false;
	advance(t, 1);
	while (!at_end(t) && is_name_char(peek(t)))
		advance(t, 1);
	if (peek(t) != ';')
		return false;
	advance(t, 1);
	c = t;
	return true;
}

/*
 * Comment or CDATA at c.p == "<!". kind: 1 = comment, 2 = CDATA.
 * Consumes the construct on success.
 */
bool consume_comment_or_cdata(Cursor &c, int &kind)
{
	Cursor t = c;
	advance(t, 2);  /* "<!" */

	if (peek(t, 0) == '-' && peek(t, 1) == '-') {
		kind = 1;
		advance(t, 2);
		while (!at_end(t)) {
			if (peek(t, 0) == '-' && peek(t, 1) == '-' && peek(t, 2) == '>') {
				advance(t, 3);
				c = t;
				return true;
			}
			advance(t, 1);
		}
		return false;
	}

	static char const CDATA[] = "[CDATA[";
	bool is_cdata = true;
	for (unsigned i = 0; i < 7; ++i)
		if (peek(t, i) != CDATA[i])
			is_cdata = false;
	if (is_cdata) {
		kind = 2;
		advance(t, 7);
		while (!at_end(t)) {
			if (peek(t, 0) == ']' && peek(t, 1) == ']' && peek(t, 2) == '>') {
				advance(t, 3);
				c = t;
				kind = 2;
				return true;
			}
			advance(t, 1);
		}
		return false;
	}
	return false;
}

bool at_comment_or_cdata(Cursor const &c)
{
	return peek(c, 0) == '<' && peek(c, 1) == '!';
}

bool at_end_tag(Cursor const &c)
{
	return peek(c, 0) == '<' && peek(c, 1) == '/';
}


/* ===================== element parsing ===================== */

enum { MAX_ATTRS = 16 };

struct Attr
{
	char const *name     { nullptr };
	unsigned    name_len { 0 };
	char const *val      { nullptr };
	unsigned    val_len  { 0 };
	unsigned    line     { 0 };
};

struct Elem
{
	char const *name     { nullptr };
	unsigned    name_len { 0 };
	unsigned    line     { 0 };
	Attr        attrs[MAX_ATTRS] { };
	unsigned    num_attrs   { 0 };
	bool        self_closing { false };
};

bool name_is(Elem const &e, char const *s)
{
	unsigned i = 0;
	for (; i < e.name_len && s[i]; ++i)
		if (e.name[i] != s[i])
			return false;
	return i == e.name_len && s[i] == '\0';
}

Attr const *find_attr(Elem const &e, char const *s)
{
	for (unsigned i = 0; i < e.num_attrs; ++i) {
		unsigned j = 0;
		for (; j < e.attrs[i].name_len && s[j]; ++j)
			if (e.attrs[i].name[j] != s[j])
				break;
		if (j == e.attrs[i].name_len && s[j] == '\0')
			return &e.attrs[i];
	}
	return nullptr;
}


/* parse "<name a="v" ...>" or "<name .../>" at c */
Result parse_begin(Cursor &c, Elem &e)
{
	unsigned const line = c.line;
	advance(c, 1);  /* "<" */

	if (at_end(c) || !is_name_start(peek(c))) {
		Msg m;
		m.add("malformed XML: expected element name after '<'");
		return error(Code::XML_SYNTAX, line, m);
	}

	e.name = c.p;
	e.line = line;
	while (!at_end(c) && is_name_char(peek(c)))
		advance(c, 1);
	e.name_len = (unsigned)(c.p - e.name);

	for (;;) {
		while (!at_end(c) && is_ws(peek(c)))
			advance(c, 1);

		if (at_end(c)) {
			Msg m;
			m.add("malformed XML: unterminated start tag ");
			m.add_quoted(e.name, e.name_len);
			return error(Code::XML_SYNTAX, line, m);
		}

		if (peek(c) == '>') {
			advance(c, 1);
			e.self_closing = false;
			return Result();
		}

		if (peek(c) == '/') {
			if (peek(c, 1) != '>') {
				Msg m;
				m.add("malformed XML: expected '>' after '/' in start tag");
				return error(Code::XML_SYNTAX, c.line, m);
			}
			advance(c, 2);
			e.self_closing = true;
			return Result();
		}

		/* attribute */
		if (!is_name_start(peek(c))) {
			Msg m;
			m.add("malformed XML: expected attribute name in ");
			m.add_quoted(e.name, e.name_len);
			return error(Code::XML_SYNTAX, c.line, m);
		}

		if (e.num_attrs >= MAX_ATTRS) {
			Msg m;
			m.add("malformed XML: too many attributes on ");
			m.add_quoted(e.name, e.name_len);
			return error(Code::XML_SYNTAX, c.line, m);
		}

		Attr &a = e.attrs[e.num_attrs];
		a.line = c.line;
		a.name = c.p;
		while (!at_end(c) && is_name_char(peek(c)))
			advance(c, 1);
		a.name_len = (unsigned)(c.p - a.name);

		for (unsigned i = 0; i < e.num_attrs; ++i) {
			Attr const &o = e.attrs[i];
			if (o.name_len == a.name_len) {
				bool same = true;
				for (unsigned k = 0; k < o.name_len; ++k)
					if (o.name[k] != a.name[k])
						same = false;
				if (same) {
					Msg m;
					m.add("malformed XML: duplicate attribute ");
					m.add_quoted(a.name, a.name_len);
					m.add(" on ");
					m.add_quoted(e.name, e.name_len);
					return error(Code::XML_SYNTAX, a.line, m);
				}
			}
		}

		while (!at_end(c) && is_ws(peek(c)))
			advance(c, 1);
		if (peek(c) != '=') {
			Msg m;
			m.add("malformed XML: expected '=' after attribute ");
			m.add_quoted(a.name, a.name_len);
			return error(Code::XML_SYNTAX, a.line, m);
		}
		advance(c, 1);
		while (!at_end(c) && is_ws(peek(c)))
			advance(c, 1);

		char const quote = peek(c);
		if (quote != '"' && quote != '\'') {
			Msg m;
			m.add("malformed XML: expected quoted value for attribute ");
			m.add_quoted(a.name, a.name_len);
			return error(Code::XML_SYNTAX, a.line, m);
		}
		advance(c, 1);

		a.val = c.p;
		for (;;) {
			if (at_end(c)) {
				Msg m;
				m.add("malformed XML: unterminated attribute value on ");
				m.add_quoted(e.name, e.name_len);
				return error(Code::XML_SYNTAX, a.line, m);
			}
			if (peek(c) == quote)
				break;
			if (peek(c) == '<') {
				Msg m;
				m.add("malformed XML: '<' inside attribute value");
				return error(Code::XML_SYNTAX, c.line, m);
			}
			if (peek(c) == '&') {
				if (!consume_entity(c)) {
					Msg m;
					m.add("malformed XML: bad entity reference in attribute ");
					m.add_quoted(a.name, a.name_len);
					return error(Code::XML_SYNTAX, c.line, m);
				}
				continue;
			}
			advance(c, 1);
		}
		a.val_len = (unsigned)(c.p - a.val);
		advance(c, 1);  /* closing quote */
		++e.num_attrs;
	}
}


/* parse "</name>" at c */
Result parse_end(Cursor &c, char const *name, unsigned name_len)
{
	unsigned const line = c.line;
	advance(c, 2);  /* "</" */

	unsigned i = 0;
	for (; i < name_len && !at_end(c) && is_name_char(peek(c)); ++i) {
		if (peek(c) != name[i])
			break;
		advance(c, 1);
	}

	while (!at_end(c) && is_ws(peek(c)))
		advance(c, 1);

	if (i != name_len || (peek(c) != '>')) {
		Msg m;
		m.add("malformed XML: expected '</");
		m.add(name, name_len);
		m.add(">'");
		return error(Code::XML_SYNTAX, line, m);
	}
	advance(c, 1);
	return Result();
}


/* whitespace and comments only; stops at anything else */
Result skip_misc(Cursor &c)
{
	for (;;) {
		while (!at_end(c) && is_ws(peek(c)))
			advance(c, 1);

		if (at_comment_or_cdata(c)) {
			int kind = 0;
			unsigned const line = c.line;
			if (!consume_comment_or_cdata(c, kind)) {
				Msg m;
				m.add(kind == 2 ? "malformed XML: unterminated CDATA section"
				                : kind == 1 ? "malformed XML: unterminated comment"
				                            : "unsupported '<!' construct");
				return error(Code::XML_SYNTAX, line, m);
			}
			if (kind == 2) {
				Msg m;
				m.add("CDATA outside <config> content is outside the "
				      "pkgd-emitted subset");
				return error(Code::INVALID_VALUE, line, m);
			}
			continue;
		}
		return Result();
	}
}


/*
 * Character data up to the next '<'. Consumes it; sets 'saw_non_ws'.
 * Used in structural positions (where only whitespace may appear).
 */
Result scan_text(Cursor &c, bool &saw_non_ws, char const *&first,
                 unsigned &first_len)
{
	saw_non_ws = false;
	first = c.p;
	first_len = 0;

	while (!at_end(c) && peek(c) != '<') {
		if (peek(c) == '&') {
			if (!consume_entity(c)) {
				Msg m;
				m.add("malformed XML: bad entity reference");
				return error(Code::XML_SYNTAX, c.line, m);
			}
			if (!saw_non_ws) {
				saw_non_ws = true;
				first = c.p;  /* placeholder; length reported as 1 */
				first_len = 1;
			}
			continue;
		}
		if (!is_ws(peek(c)) && !saw_non_ws) {
			saw_non_ws = true;
			first = c.p;
			first_len = 1;
		}
		if (saw_non_ws && first_len < 24 && first + first_len == c.p)
			++first_len;
		advance(c, 1);
	}
	return Result();
}


/*
 * Opaque subtree skip (the inline <config> content): consume one
 * element with arbitrary attributes and content, checking well-
 * formedness only.
 */
Result skip_element(Cursor &c);

Result skip_content(Cursor &c, Elem const &e)
{
	for (;;) {
		bool saw = false;
		char const *first = nullptr;
		unsigned first_len = 0;
		Result r = scan_text(c, saw, first, first_len);
		if (!r.ok)
			return r;

		if (at_comment_or_cdata(c)) {
			int kind = 0;
			unsigned const line = c.line;
			if (!consume_comment_or_cdata(c, kind)) {
				Msg m;
				m.add(kind == 2 ? "malformed XML: unterminated CDATA section"
				                : kind == 1 ? "malformed XML: unterminated comment"
				                            : "unsupported '<!' construct");
				return error(Code::XML_SYNTAX, line, m);
			}
			continue;
		}

		if (at_end(c)) {
			Msg m;
			m.add("malformed XML: unterminated element ");
			m.add_quoted(e.name, e.name_len);
			return error(Code::XML_SYNTAX, e.line, m);
		}

		if (at_end_tag(c))
			return parse_end(c, e.name, e.name_len);

		if (peek(c) == '<') {
			if (peek(c, 1) == '?' || peek(c, 1) == '!') {
				Msg m;
				m.add("malformed XML: unsupported construct inside ");
				m.add_quoted(e.name, e.name_len);
				return error(Code::XML_SYNTAX, c.line, m);
			}
			r = skip_element(c);
			if (!r.ok)
				return r;
		}
	}
}

Result skip_element(Cursor &c)
{
	Elem e;
	Result r = parse_begin(c, e);
	if (!r.ok)
		return r;
	if (e.self_closing)
		return Result();
	return skip_content(c, e);
}


/* ===================== lexemes ===================== */

bool all_digits(char const *s, unsigned n)
{
	if (!n)
		return false;
	for (unsigned i = 0; i < n; ++i)
		if (!is_digit(s[i]))
			return false;
	return true;
}

bool has_non_zero(char const *s, unsigned n)
{
	for (unsigned i = 0; i < n; ++i)
		if (s[i] != '0')
			return true;
	return false;
}

/* init.hsd 'positive': \d*[1-9]\d*, bounded to 10 digits */
bool lex_positive(char const *s, unsigned n)
{
	return n >= 1 && n <= 10 && all_digits(s, n) && has_non_zero(s, n);
}

/* init.hsd 'natural': \d+, bounded to 10 digits */
bool lex_natural(char const *s, unsigned n)
{
	return n >= 1 && n <= 10 && all_digits(s, n);
}

/* init.hsd 'num_bytes': \d+[GMK]? with a non-zero numeric part */
bool lex_num_bytes(char const *s, unsigned n)
{
	if (n < 1 || n > 15)
		return false;
	unsigned digits = n;
	if (s[n - 1] == 'G' || s[n - 1] == 'M' || s[n - 1] == 'K')
		digits = n - 1;
	return all_digits(s, digits) && has_non_zero(s, digits);
}

/* base_types.xsd Boolean */
bool lex_bool(char const *s, unsigned n)
{
	static char const *const VALUES[] = {
		"true", "yes", "on", "false", "no", "off" };
	for (char const *v : VALUES) {
		unsigned i = 0;
		for (; i < n && v[i]; ++i)
			if (s[i] != v[i])
				break;
		if (i == n && v[i] == '\0')
			return true;
	}
	return false;
}

bool in_set(char c, char const *set)
{
	for (; *set; ++set)
		if (c == *set)
			return true;
	return false;
}

bool lex_charset(char const *s, unsigned n, unsigned min, unsigned max,
                 char const *set)
{
	if (n < min || n > max)
		return false;
	for (unsigned i = 0; i < n; ++i)
		if (!in_set(s[i], set))
			return false;
	return true;
}

/* docs/12 §4.1 package identifier */
bool lex_package_name(char const *s, unsigned n)
{
	return lex_charset(s, n, 1, 63, "abcdefghijklmnopqrstuvwxyz0123456789_");
}

/* Genode session-type name at the parse width (String<32>) */
bool lex_service_name(char const *s, unsigned n)
{
	return lex_charset(s, n, 1, 31,
	                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                   "abcdefghijklmnopqrstuvwxyz0123456789_");
}

/* binary path incl. the disk-served "bin/" prefix (String<96>) */
bool lex_binary_path(char const *s, unsigned n)
{
	return lex_charset(s, n, 1, 95,
	                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                   "abcdefghijklmnopqrstuvwxyz0123456789_./+-");
}

/* sibling/component name used as a <child name> route target */
bool lex_child_name(char const *s, unsigned n)
{
	return lex_charset(s, n, 1, 63,
	                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                   "abcdefghijklmnopqrstuvwxyz0123456789_.+-");
}

/* session label (String<96>): printable ASCII */
bool lex_label(char const *s, unsigned n)
{
	if (n < 1 || n > 95)
		return false;
	for (unsigned i = 0; i < n; ++i)
		if (!is_printable(s[i]))
			return false;
	return true;
}


/* ===================== attribute rules ===================== */

enum Lex {
	LEX_NONE, LEX_POSITIVE, LEX_NATURAL, LEX_NUM_BYTES, LEX_BOOL,
	LEX_PACKAGE_NAME, LEX_SERVICE_NAME, LEX_BINARY_PATH, LEX_CHILD_NAME,
	LEX_LABEL, LEX_RAM_QUANTUM
};

char const *lex_name(Lex lex)
{
	switch (lex) {
	case LEX_POSITIVE:     return "positive decimal";
	case LEX_NATURAL:      return "decimal number";
	case LEX_NUM_BYTES:    return "number of bytes";
	case LEX_RAM_QUANTUM:  return "number of bytes";
	case LEX_BOOL:         return "yes|no|true|false|on|off";
	case LEX_PACKAGE_NAME: return "package name [a-z0-9_]";
	case LEX_SERVICE_NAME: return "service name [A-Za-z0-9_]";
	case LEX_BINARY_PATH:  return "binary path";
	case LEX_CHILD_NAME:   return "component name";
	case LEX_LABEL:        return "label";
	default:               return "value";
	}
}

bool lex_ok(Lex lex, char const *s, unsigned n)
{
	switch (lex) {
	case LEX_POSITIVE:     return lex_positive(s, n);
	case LEX_NATURAL:      return lex_natural(s, n);
	case LEX_NUM_BYTES:    return lex_num_bytes(s, n);
	case LEX_RAM_QUANTUM:  return lex_num_bytes(s, n);
	case LEX_BOOL:         return lex_bool(s, n);
	case LEX_PACKAGE_NAME: return lex_package_name(s, n);
	case LEX_SERVICE_NAME: return lex_service_name(s, n);
	case LEX_BINARY_PATH:  return lex_binary_path(s, n);
	case LEX_CHILD_NAME:   return lex_child_name(s, n);
	case LEX_LABEL:        return lex_label(s, n);
	default:               return n > 0;
	}
}

struct Attr_rule
{
	char const *name;
	bool        required;
	Lex         lex;
};

Result check_attrs(Elem const &e, Msg const &ctx,
                   Attr_rule const *rules, unsigned num_rules)
{
	/* every present attribute must be known */
	for (unsigned i = 0; i < e.num_attrs; ++i) {
		Attr const &a = e.attrs[i];
		bool known = false;
		for (unsigned r = 0; r < num_rules; ++r) {
			char const *rn = rules[r].name;
			unsigned j = 0;
			for (; j < a.name_len && rn[j]; ++j)
				if (a.name[j] != rn[j])
					break;
			if (j == a.name_len && rn[j] == '\0') {
				known = true;
				break;
			}
		}
		if (!known) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unknown attribute ");
			m.add_quoted(a.name, a.name_len);
			m.add(" (outside the pkgd-emitted subset)");
			return error(Code::UNKNOWN_ATTRIBUTE, a.line, m);
		}
	}

	/* required attributes must be present and well-formed */
	for (unsigned r = 0; r < num_rules; ++r) {
		Attr const *a = find_attr(e, rules[r].name);
		if (!a) {
			if (!rules[r].required)
				continue;
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": missing required attribute ");
			m.add("'");
			m.add(rules[r].name);
			m.add("'");
			return error(Code::MISSING_REQUIRED, e.line, m);
		}
		if (rules[r].lex != LEX_NONE &&
		    !lex_ok(rules[r].lex, a->val, a->val_len)) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": attribute '");
			m.add(rules[r].name);
			m.add("' has invalid value ");
			m.add_quoted(a->val, a->val_len);
			m.add(" (expected ");
			m.add(lex_name(rules[r].lex));
			m.add(")");
			return error(Code::INVALID_VALUE, a->line, m);
		}
	}
	return Result();
}

Result no_children_allowed(Cursor &c, Elem const &e, Msg const &ctx)
{
	for (;;) {
		Result r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c))
			return parse_end(c, e.name, e.name_len);

		if (at_end(c)) {
			Msg m;
			m.add("malformed XML: unterminated element ");
			m.add_quoted(e.name, e.name_len);
			return error(Code::XML_SYNTAX, e.line, m);
		}

		if (peek(c) != '<') {
			bool saw = false;
			char const *first = nullptr;
			unsigned first_len = 0;
			r = scan_text(c, saw, first, first_len);
			if (!r.ok)
				return r;
			if (saw) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": unexpected character data ");
				m.add_quoted(first, first_len);
				return error(Code::INVALID_VALUE, e.line, m);
			}
			continue;
		}

		Elem child;
		unsigned const line = c.line;
		r = parse_begin(c, child);
		if (!r.ok)
			return r;
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": unexpected child node ");
		m.add_quoted(child.name, child.name_len);
		m.add(" (outside the pkgd-emitted subset)");
		return error(Code::UNKNOWN_NODE, line, m);
	}
}


/* ===================== node validators ===================== */

/* forward */
Result validate_config(Cursor &c, Elem const &root, Workspace &ws);


/* <report delay_ms init_ram init_caps child_ram/> */
Result validate_report(Cursor &c, Elem const &e)
{
	Msg ctx;
	ctx.add("<report>");

	static Attr_rule const RULES[] = {
		{ "delay_ms", true, LEX_POSITIVE },
		{ "init_ram", true, LEX_BOOL },
		{ "init_caps", true, LEX_BOOL },
		{ "child_ram", true, LEX_BOOL },
	};
	Result r = check_attrs(e, ctx, RULES, 4);
	if (!r.ok)
		return r;

	if (e.self_closing)
		return Result();
	return no_children_allowed(c, e, ctx);
}


/*
 * <repro_padding> — the ONLY test-only exception (see the header):
 * the repro-harness knobs of sponge_pkgd emit
 * pad_bytes/64 <p v="64 chars"/> nodes. Shape checked conservatively:
 * no attributes, <p> children only, each v exactly 64 characters.
 * The <p> node COUNT is deliberately NOT bounded — repro_pad_grow's
 * budget grows linearly with the write counter without an upper bound
 * (pkgd.h REPRO_PAD_GROW_BYTES), so a count cap returns INVALID_VALUE
 * for legitimate repro writes (the write-3 regression the self-test
 * pins).
 */
Result validate_repro_padding(Cursor &c, Elem const &e, Workspace &)
{
	Msg ctx;
	ctx.add("<repro_padding> (test-only exception)");

	static Attr_rule const RULES[] = { { nullptr, false, LEX_NONE } };
	Result r = check_attrs(e, ctx, RULES, 0);
	if (!r.ok)
		return r;

	if (e.self_closing)
		return Result();

	for (;;) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c))
			return parse_end(c, e.name, e.name_len);

		if (at_end(c) || peek(c) != '<') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": only <p> children are allowed");
			return error(Code::INVALID_VALUE, e.line, m);
		}

		Elem p;
		unsigned const line = c.line;
		r = parse_begin(c, p);
		if (!r.ok)
			return r;

		if (!name_is(p, "p")) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected child node ");
			m.add_quoted(p.name, p.name_len);
			m.add(" (only <p> is allowed)");
			return error(Code::UNKNOWN_NODE, line, m);
		}

		Msg pctx;
		pctx.add("<repro_padding>/<p>");
		static Attr_rule const P_RULES[] = {
			{ "v", true, LEX_NONE },
		};
		r = check_attrs(p, pctx, P_RULES, 1);
		if (!r.ok)
			return r;

		Attr const *v = find_attr(p, "v");
		if (v->val_len != 64) {
			Msg m;
			m.add(pctx.buf, pctx.len);
			m.add(": attribute 'v' must be exactly 64 characters, got ");
			m.add_quoted(v->val, v->val_len);
			return error(Code::INVALID_VALUE, v->line, m);
		}

		if (p.self_closing)
			continue;
		r = no_children_allowed(c, p, pctx);
		if (!r.ok)
			return r;
	}
}


/* <parent-provides><service name="..."/>...</parent-provides> */
Result validate_parent_provides(Cursor &c, Elem const &e, Workspace &ws)
{
	Msg ctx;
	ctx.add("<parent-provides>");

	static Attr_rule const RULES[] = { { nullptr, false, LEX_NONE } };
	Result r = check_attrs(e, ctx, RULES, 0);
	if (!r.ok)
		return r;

	if (e.self_closing) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": must list at least one <service>");
		return error(Code::MISSING_REQUIRED, e.line, m);
	}

	unsigned num_services = 0;
	for (;;) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c)) {
			if (!num_services) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": must list at least one <service>");
				return error(Code::MISSING_REQUIRED, e.line, m);
			}
			return parse_end(c, e.name, e.name_len);
		}

		if (at_end(c) || peek(c) != '<') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": only <service> children are allowed");
			return error(Code::INVALID_VALUE, e.line, m);
		}

		Elem s;
		unsigned const line = c.line;
		r = parse_begin(c, s);
		if (!r.ok)
			return r;

		if (!name_is(s, "service")) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected child node ");
			m.add_quoted(s.name, s.name_len);
			m.add(" (outside the pkgd-emitted subset)");
			return error(Code::UNKNOWN_NODE, line, m);
		}

		Msg sctx;
		sctx.add("<parent-provides>/<service>");
		static Attr_rule const S_RULES[] = {
			{ "name", true, LEX_SERVICE_NAME },
		};
		r = check_attrs(s, sctx, S_RULES, 1);
		if (!r.ok)
			return r;

		if (!s.self_closing) {
			r = no_children_allowed(c, s, sctx);
			if (!r.ok)
				return r;
		}

		Attr const *name = find_attr(s, "name");
		if (++num_services > Workspace::MAX_SERVICE_NAMES) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": more than ");
			m.add_num(Workspace::MAX_SERVICE_NAMES);
			m.add(" services");
			return error(Code::INVALID_VALUE, line, m);
		}

		for (unsigned i = 0; i < ws.num_service_names; ++i) {
			char const *seen = ws.service_names[i];
			unsigned k = 0;
			for (; k < name->val_len && seen[k]; ++k)
				if (seen[k] != name->val[k])
					break;
			if (k == name->val_len && seen[k] == '\0') {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": duplicate service name ");
				m.add_quoted(name->val, name->val_len);
				return error(Code::DUPLICATE_NAME, name->line, m);
			}
		}
		for (unsigned i = 0; i < name->val_len; ++i)
			ws.service_names[ws.num_service_names][i] = name->val[i];
		ws.service_names[ws.num_service_names][name->val_len] = '\0';
		++ws.num_service_names;
	}
}


/*
 * <default-route><any-service><parent/><any-child/></any-service>
 * </default-route> — exactly the emitted shape; per-service default
 * routes and child targets are outside the subset.
 */
Result validate_default_route(Cursor &c, Elem const &e)
{
	Msg ctx;
	ctx.add("<default-route>");

	static Attr_rule const RULES[] = { { nullptr, false, LEX_NONE } };
	Result r = check_attrs(e, ctx, RULES, 0);
	if (!r.ok)
		return r;

	unsigned num_any = 0;
	bool closed = false;

	if (e.self_closing)
		closed = true;

	while (!closed) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c)) {
			r = parse_end(c, e.name, e.name_len);
			if (!r.ok)
				return r;
			closed = true;
			break;
		}

		if (at_end(c) || peek(c) != '<') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": only one <any-service> child is allowed");
			return error(Code::INVALID_VALUE, e.line, m);
		}

		Elem a;
		unsigned const line = c.line;
		r = parse_begin(c, a);
		if (!r.ok)
			return r;

		if (!name_is(a, "any-service")) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected child node ");
			m.add_quoted(a.name, a.name_len);
			m.add(" (only <any-service> is in the pkgd-emitted subset)");
			return error(Code::UNKNOWN_NODE, line, m);
		}

		if (++num_any > 1) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": more than one <any-service> child");
			return error(Code::UNKNOWN_NODE, line, m);
		}

		Msg actx;
		actx.add("<default-route>/<any-service>");
		static Attr_rule const A_RULES[] = { { nullptr, false, LEX_NONE } };
		r = check_attrs(a, actx, A_RULES, 0);
		if (!r.ok)
			return r;

		unsigned num_parent = 0, num_any_child = 0;
		bool a_closed = a.self_closing;

		while (!a_closed) {
			r = skip_misc(c);
			if (!r.ok)
				return r;

			if (at_end_tag(c)) {
				r = parse_end(c, a.name, a.name_len);
				if (!r.ok)
					return r;
				a_closed = true;
				break;
			}

			if (at_end(c) || peek(c) != '<') {
				Msg m;
				m.add(actx.buf, actx.len);
				m.add(": only <parent/> and <any-child/> are allowed");
				return error(Code::INVALID_VALUE, a.line, m);
			}

			Elem t;
			unsigned const tline = c.line;
			r = parse_begin(c, t);
			if (!r.ok)
				return r;

			Msg tctx;
			tctx.add(actx.buf, actx.len);
			tctx.add("/");
			tctx.add_quoted(t.name, t.name_len);

			if (name_is(t, "parent")) {
				++num_parent;
				if (num_parent > 1) {
					Msg m;
					m.add(actx.buf, actx.len);
					m.add(": more than one <parent/>");
					return error(Code::UNKNOWN_NODE, tline, m);
				}
			} else if (name_is(t, "any-child")) {
				++num_any_child;
				if (num_any_child > 1) {
					Msg m;
					m.add(actx.buf, actx.len);
					m.add(": more than one <any-child/>");
					return error(Code::UNKNOWN_NODE, tline, m);
				}
			} else {
				Msg m;
				m.add(actx.buf, actx.len);
				m.add(": unexpected child node ");
				m.add_quoted(t.name, t.name_len);
				m.add(" (only <parent/> and <any-child/> are allowed)");
				return error(Code::UNKNOWN_NODE, tline, m);
			}

			static Attr_rule const T_RULES[] = { { nullptr, false, LEX_NONE } };
			r = check_attrs(t, tctx, T_RULES, 0);
			if (!r.ok)
				return r;

			if (!t.self_closing) {
				r = no_children_allowed(c, t, tctx);
				if (!r.ok)
					return r;
			}
		}

		if (num_parent != 1 || num_any_child != 1) {
			Msg m;
			m.add(actx.buf, actx.len);
			m.add(": needs exactly one <parent/> and one <any-child/>");
			return error(Code::MISSING_REQUIRED, a.line, m);
		}
	}

	if (!closed || num_any != 1) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": needs exactly one <any-service> child");
		return error(Code::MISSING_REQUIRED, e.line, m);
	}
	return Result();
}


/* <default caps="..."/> */
Result validate_default(Cursor &c, Elem const &e)
{
	Msg ctx;
	ctx.add("<default>");

	static Attr_rule const RULES[] = {
		{ "caps", true, LEX_POSITIVE },
	};
	Result r = check_attrs(e, ctx, RULES, 1);
	if (!r.ok)
		return r;

	if (e.self_closing)
		return Result();
	return no_children_allowed(c, e, ctx);
}


/* <service ...><parent/> | <child name="..."/></service> in <route> */
Result validate_route_service(Cursor &c, Elem const &e)
{
	Msg ctx;
	ctx.add("<route>/<service");

	Attr const *name = find_attr(e, "name");
	if (name) {
		ctx.add(" name=");
		ctx.add_quoted(name->val, name->val_len);
	}
	ctx.add(">");

	static Attr_rule const RULES[] = {
		{ "name",      true,  LEX_SERVICE_NAME },
		{ "label",     false, LEX_LABEL },
		{ "label_last", false, LEX_LABEL },
	};
	Result r = check_attrs(e, ctx, RULES, 3);
	if (!r.ok)
		return r;

	if (e.self_closing) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": needs exactly one route target (<parent/> or "
		      "<child name=\"...\"/>)");
		return error(Code::MISSING_REQUIRED, e.line, m);
	}

	unsigned num_targets = 0;
	for (;;) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c)) {
			if (num_targets != 1) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": needs exactly one route target (<parent/> or "
				      "<child name=\"...\"/>)");
				return error(Code::MISSING_REQUIRED, e.line, m);
			}
			return parse_end(c, e.name, e.name_len);
		}

		if (at_end(c) || peek(c) != '<') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected character data in route entry");
			return error(Code::INVALID_VALUE, e.line, m);
		}

		Elem t;
		unsigned const line = c.line;
		r = parse_begin(c, t);
		if (!r.ok)
			return r;

		if (++num_targets > 1) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": more than one route target (outside the "
			      "pkgd-emitted subset)");
			return error(Code::UNKNOWN_NODE, line, m);
		}

		Msg tctx;
		tctx.add(ctx.buf, ctx.len);
		tctx.add("/");
		tctx.add_quoted(t.name, t.name_len);

		if (name_is(t, "parent")) {
			static Attr_rule const P_RULES[] = {
				{ nullptr, false, LEX_NONE } };
			r = check_attrs(t, tctx, P_RULES, 0);
			if (!r.ok)
				return r;
		} else if (name_is(t, "child")) {
			static Attr_rule const C_RULES[] = {
				{ "name", true, LEX_CHILD_NAME } };
			r = check_attrs(t, tctx, C_RULES, 1);
			if (!r.ok)
				return r;
		} else {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected route target ");
			m.add_quoted(t.name, t.name_len);
			m.add(" (only <parent/> or <child name=\"...\"/>)");
			return error(Code::UNKNOWN_NODE, line, m);
		}

		if (!t.self_closing) {
			r = no_children_allowed(c, t, tctx);
			if (!r.ok)
				return r;
		}
	}
}


/* <route> ... </route> inside <start> */
Result validate_route(Cursor &c, Elem const &e)
{
	Msg ctx;
	ctx.add("<start>/<route>");

	static Attr_rule const RULES[] = { { nullptr, false, LEX_NONE } };
	Result r = check_attrs(e, ctx, RULES, 0);
	if (!r.ok)
		return r;

	unsigned num_services = 0, num_any = 0;
	bool closed = e.self_closing;

	while (!closed) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c)) {
			r = parse_end(c, e.name, e.name_len);
			if (!r.ok)
				return r;
			closed = true;
			break;
		}

		if (at_end(c) || peek(c) != '<') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": only <service>/<any-service> children are allowed");
			return error(Code::INVALID_VALUE, e.line, m);
		}

		Elem child;
		unsigned const line = c.line;
		r = parse_begin(c, child);
		if (!r.ok)
			return r;

		if (name_is(child, "service")) {
			++num_services;
			r = validate_route_service(c, child);
			if (!r.ok)
				return r;
		} else if (name_is(child, "any-service")) {
			Msg actx;
			actx.add("<start>/<route>/<any-service>");
			if (++num_any > 1) {
				Msg m;
				m.add(actx.buf, actx.len);
				m.add(": more than one <any-service> (outside the "
				      "pkgd-emitted subset)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			static Attr_rule const A_RULES[] = {
				{ nullptr, false, LEX_NONE } };
			r = check_attrs(child, actx, A_RULES, 0);
			if (!r.ok)
				return r;

			/* exactly one bare <parent/> */
			unsigned num_parent = 0;
			bool a_closed = child.self_closing;
			while (!a_closed) {
				r = skip_misc(c);
				if (!r.ok)
					return r;
				if (at_end_tag(c)) {
					r = parse_end(c, child.name, child.name_len);
					if (!r.ok)
						return r;
					a_closed = true;
					break;
				}
				if (at_end(c) || peek(c) != '<') {
					Msg m;
					m.add(actx.buf, actx.len);
					m.add(": only <parent/> is allowed");
					return error(Code::INVALID_VALUE, child.line, m);
				}
				Elem t;
				unsigned const tline = c.line;
				r = parse_begin(c, t);
				if (!r.ok)
					return r;
				Msg tctx;
				tctx.add(actx.buf, actx.len);
				tctx.add("/");
				tctx.add_quoted(t.name, t.name_len);
				if (!name_is(t, "parent")) {
					Msg m;
					m.add(actx.buf, actx.len);
					m.add(": unexpected child node ");
					m.add_quoted(t.name, t.name_len);
					m.add(" (only bare <parent/> is allowed)");
					return error(Code::UNKNOWN_NODE, tline, m);
				}
				static Attr_rule const T_RULES[] = {
					{ nullptr, false, LEX_NONE } };
				r = check_attrs(t, tctx, T_RULES, 0);
				if (!r.ok)
					return r;
				if (++num_parent > 1) {
					Msg m;
					m.add(actx.buf, actx.len);
					m.add(": more than one <parent/>");
					return error(Code::UNKNOWN_NODE, tline, m);
				}
				if (!t.self_closing) {
					r = no_children_allowed(c, t, tctx);
					if (!r.ok)
						return r;
				}
			}
			if (num_parent != 1) {
				Msg m;
				m.add(actx.buf, actx.len);
				m.add(": needs exactly one <parent/>");
				return error(Code::MISSING_REQUIRED, child.line, m);
			}
		} else {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected child node ");
			m.add_quoted(child.name, child.name_len);
			m.add(" (only <service> and <any-service> are in the "
			      "pkgd-emitted subset)");
			return error(Code::UNKNOWN_NODE, line, m);
		}
	}

	if (!num_services) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": needs at least one <service> route entry");
		return error(Code::MISSING_REQUIRED, e.line, m);
	}
	if (num_any != 1) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": needs exactly one trailing <any-service><parent/>"
		      "</any-service>");
		return error(Code::MISSING_REQUIRED, e.line, m);
	}
	return Result();
}


/* <start name caps> <binary>? <resource> <config>? <route> </start> */
Result validate_start(Cursor &c, Elem const &e, Workspace &ws)
{
	Msg ctx;
	ctx.add("<start");

	Attr const *name = find_attr(e, "name");
	if (name) {
		ctx.add(" name=");
		ctx.add_quoted(name->val, name->val_len);
	}
	ctx.add(">");

	static Attr_rule const RULES[] = {
		{ "name", true, LEX_PACKAGE_NAME },
		{ "caps", true, LEX_NATURAL },
	};
	Result r = check_attrs(e, ctx, RULES, 2);
	if (!r.ok)
		return r;

	/* unique start names */
	for (unsigned i = 0; i < ws.num_start_names; ++i) {
		char const *seen = ws.start_names[i];
		unsigned k = 0;
		for (; k < name->val_len && seen[k]; ++k)
			if (seen[k] != name->val[k])
				break;
		if (k == name->val_len && seen[k] == '\0') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": duplicate <start> name ");
			m.add_quoted(name->val, name->val_len);
			return error(Code::DUPLICATE_NAME, name->line, m);
		}
	}
	if (ws.num_start_names >= Workspace::MAX_START_NAMES) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": more than ");
		m.add_num(Workspace::MAX_START_NAMES);
		m.add(" <start> nodes");
		return error(Code::INVALID_VALUE, e.line, m);
	}
	for (unsigned i = 0; i < name->val_len; ++i)
		ws.start_names[ws.num_start_names][i] = name->val[i];
	ws.start_names[ws.num_start_names][name->val_len] = '\0';
	++ws.num_start_names;

	unsigned num_binary = 0, num_resource = 0, num_config = 0,
	         num_route = 0;

	if (e.self_closing) {
		Msg m;
		m.add(ctx.buf, ctx.len);
		m.add(": missing required <resource> and <route> nodes");
		return error(Code::MISSING_REQUIRED, e.line, m);
	}

	for (;;) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c)) {
			if (num_resource != 1 || num_route != 1) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": needs exactly one <resource> and one <route> "
				      "(missing ");
				if (num_resource != 1)
					m.add("<resource>");
				if (num_resource != 1 && num_route != 1)
					m.add(" and ");
				if (num_route != 1)
					m.add("<route>");
				m.add(")");
				return error(Code::MISSING_REQUIRED, e.line, m);
			}
			return parse_end(c, e.name, e.name_len);
		}

		if (at_end(c) || peek(c) != '<') {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected character data in <start>");
			return error(Code::INVALID_VALUE, e.line, m);
		}

		Elem child;
		unsigned const line = c.line;
		r = parse_begin(c, child);
		if (!r.ok)
			return r;

		if (name_is(child, "binary")) {
			Msg bctx;
			bctx.add(ctx.buf, ctx.len);
			bctx.add("/<binary>");
			if (++num_binary > 1) {
				Msg m;
				m.add(bctx.buf, bctx.len);
				m.add(": more than one <binary> (outside the "
				      "pkgd-emitted subset)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			static Attr_rule const B_RULES[] = {
				{ "name", true, LEX_BINARY_PATH } };
			r = check_attrs(child, bctx, B_RULES, 1);
			if (!r.ok)
				return r;
			if (!child.self_closing) {
				r = no_children_allowed(c, child, bctx);
				if (!r.ok)
					return r;
			}
		} else if (name_is(child, "resource")) {
			Msg rctx;
			rctx.add(ctx.buf, ctx.len);
			rctx.add("/<resource>");
			if (++num_resource > 1) {
				Msg m;
				m.add(rctx.buf, rctx.len);
				m.add(": more than one <resource> (outside the "
				      "pkgd-emitted subset)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			static Attr_rule const R_RULES[] = {
				{ "name",    true, LEX_NONE },
				{ "quantum", true, LEX_RAM_QUANTUM },
			};
			r = check_attrs(child, rctx, R_RULES, 2);
			if (!r.ok)
				return r;
			Attr const *rn = find_attr(child, "name");
			if (rn->val_len != 3 || rn->val[0] != 'R' || rn->val[1] != 'A'
			    || rn->val[2] != 'M') {
				Msg m;
				m.add(rctx.buf, rctx.len);
				m.add(": attribute 'name' has invalid value ");
				m.add_quoted(rn->val, rn->val_len);
				m.add(" (only \"RAM\" is in the pkgd-emitted subset)");
				return error(Code::INVALID_VALUE, rn->line, m);
			}
			if (!child.self_closing) {
				r = no_children_allowed(c, child, rctx);
				if (!r.ok)
					return r;
			}
		} else if (name_is(child, "config")) {
			/*
			 * Opaque production content (documented limitation 2):
			 * the inline <config> block is package metadata emitted
			 * verbatim; init treats its content as "anything". Only
			 * whole-document well-formedness applies, so skip it.
			 */
			if (++num_config > 1) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add("/<config>: more than one inline <config> "
				      "(outside the pkgd-emitted subset)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			if (!child.self_closing) {
				r = skip_content(c, child);
				if (!r.ok)
					return r;
			}
		} else if (name_is(child, "route")) {
			Msg rtctx;
			rtctx.add(ctx.buf, ctx.len);
			rtctx.add("/<route>");
			if (++num_route > 1) {
				Msg m;
				m.add(rtctx.buf, rtctx.len);
				m.add(": more than one <route> (outside the "
				      "pkgd-emitted subset)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			r = validate_route(c, child);
			if (!r.ok)
				return r;
		} else {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected child node ");
			m.add_quoted(child.name, child.name_len);
			m.add(" (only <binary>, <resource>, <config> and <route> "
			      "are in the pkgd-emitted subset)");
			return error(Code::UNKNOWN_NODE, line, m);
		}
	}
}


/* root <config> document */
Result validate_config(Cursor &c, Elem const &root, Workspace &ws)
{
	Msg ctx;
	ctx.add("<config>");

	static Attr_rule const RULES[] = { { nullptr, false, LEX_NONE } };
	Result r = check_attrs(root, ctx, RULES, 0);
	if (!r.ok)
		return r;

	bool seen_report = false, seen_padding = false,
	     seen_provides = false, seen_route = false, seen_default = false;

	bool closed = root.self_closing;

	while (!closed) {
		r = skip_misc(c);
		if (!r.ok)
			return r;

		if (at_end_tag(c)) {
			r = parse_end(c, root.name, root.name_len);
			if (!r.ok)
				return r;
			closed = true;
			break;
		}

		if (at_end(c)) {
			Msg m;
			m.add("malformed XML: unterminated <config>");
			return error(Code::XML_SYNTAX, root.line, m);
		}

		if (peek(c) != '<') {
			bool saw = false;
			char const *first = nullptr;
			unsigned first_len = 0;
			r = scan_text(c, saw, first, first_len);
			if (!r.ok)
				return r;
			if (saw) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": unexpected character data ");
				m.add_quoted(first, first_len);
				return error(Code::INVALID_VALUE, c.line, m);
			}
			continue;
		}

		if (peek(c, 1) == '?' || peek(c, 1) == '!') {
			Msg m;
			m.add("unsupported XML construct at document level "
			      "(no prolog/DOCTYPE/PI in the pkgd-emitted subset)");
			return error(Code::XML_SYNTAX, c.line, m);
		}

		Elem child;
		unsigned const line = c.line;
		r = parse_begin(c, child);
		if (!r.ok)
			return r;

		if (name_is(child, "report")) {
			if (seen_report) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": duplicate <report> node (at most one)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			seen_report = true;
			r = validate_report(c, child);
		} else if (name_is(child, "repro_padding")) {
			if (seen_padding) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": duplicate <repro_padding> node (at most one)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			seen_padding = true;
			r = validate_repro_padding(c, child, ws);
		} else if (name_is(child, "parent-provides")) {
			if (seen_provides) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": duplicate <parent-provides> node (at most one)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			seen_provides = true;
			r = validate_parent_provides(c, child, ws);
		} else if (name_is(child, "default-route")) {
			if (seen_route) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": duplicate <default-route> node (at most one)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			seen_route = true;
			r = validate_default_route(c, child);
		} else if (name_is(child, "default")) {
			if (seen_default) {
				Msg m;
				m.add(ctx.buf, ctx.len);
				m.add(": duplicate <default> node (at most one)");
				return error(Code::UNKNOWN_NODE, line, m);
			}
			seen_default = true;
			r = validate_default(c, child);
		} else if (name_is(child, "start")) {
			r = validate_start(c, child, ws);
		} else {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": unexpected child node ");
			m.add_quoted(child.name, child.name_len);
			m.add(" (outside the pkgd-emitted subset)");
			r = error(Code::UNKNOWN_NODE, line, m);
		}
		if (!r.ok)
			return r;
	}

	struct { bool seen; char const *what; } const REQUIRED[] = {
		{ seen_report,  "<report>" },
		{ seen_provides, "<parent-provides>" },
		{ seen_route,   "<default-route>" },
		{ seen_default, "<default>" },
	};
	for (auto const &req : REQUIRED) {
		if (!req.seen) {
			Msg m;
			m.add(ctx.buf, ctx.len);
			m.add(": missing required node ");
			m.add(req.what);
			return error(Code::MISSING_REQUIRED, root.line, m);
		}
	}
	return Result();
}

}  /* anonymous namespace */


Result Sponge::Runtime_config_validate::validate(
	char const *xml, unsigned xml_len, Workspace &workspace)
{
	workspace.num_start_names   = 0;
	workspace.num_service_names = 0;

	Cursor c { xml, xml + xml_len, 1 };

	Result r = skip_misc(c);
	if (!r.ok)
		return r;

	if (at_end(c)) {
		Msg m;
		m.add("malformed XML: no root element");
		return error(Code::XML_SYNTAX, c.line, m);
	}

	if (peek(c, 0) == '<' && (peek(c, 1) == '?' || peek(c, 1) == '!')) {
		Msg m;
		m.add("unsupported XML construct at document level "
		      "(no prolog/DOCTYPE/PI in the pkgd-emitted subset)");
		return error(Code::XML_SYNTAX, c.line, m);
	}

	Elem root;
	r = parse_begin(c, root);
	if (!r.ok)
		return r;

	if (!name_is(root, "config")) {
		Msg m;
		m.add("unexpected root element ");
		m.add_quoted(root.name, root.name_len);
		m.add(" (expected <config>)");
		return error(Code::UNKNOWN_NODE, root.line, m);
	}

	r = validate_config(c, root, workspace);
	if (!r.ok)
		return r;

	/* trailing misc only */
	r = skip_misc(c);
	if (!r.ok)
		return r;

	if (!at_end(c)) {
		Msg m;
		m.add("malformed XML: content after root element");
		return error(Code::XML_SYNTAX, c.line, m);
	}

	return Result();
}
