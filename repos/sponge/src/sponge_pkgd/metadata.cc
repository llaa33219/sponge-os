/* SPDX-License-Identifier: Apache-2.0
 *
 * sponge_pkgd metadata module - package metadata parsing and
 * dependency resolution.
 *
 * Phase 16 P1.3 responsibility split (zero behavior change): verbatim
 * move of Main::_load_index/_load_package/_parse_package/_resolve/
 * _visit from main.cc, plus their sole-caller helpers _contains and
 * _cycle_message. The parse-time binary_prefix rewrite inside
 * _parse_package now delegates to the pure sponge_binary_prefix
 * library (Phase 16 P1.2 extraction, byte-identical to the former
 * inline block; P2.3 owns any semantic change).
 *
 * Stack discipline is preserved exactly as it was: _parse_package
 * keeps its `static` cfg_buf/out_buf scratch (small ep stack).
 */

#include "pkgd.h"

#include <base/log.h>
#include <sponge/binary_prefix.h>
#include <util/xml_generator.h>

bool Sponge::Pkgd::Main::_contains(Genode::String<64> const *set, unsigned n,
                                   char const *name)
{
	for (unsigned i = 0; i < n; ++i)
		if (Genode::strcmp(set[i].string(), name) == 0)
			return true;
	return false;
}


Genode::String<256> Sponge::Pkgd::Main::_cycle_message(
	Genode::String<64> const *visiting, unsigned n, char const *repeat)
{
	/* "dependency cycle: a -> b -> c -> a" */
	char buf[256] { "dependency cycle: " };
	Genode::size_t pos = Genode::strlen(buf);
	for (unsigned i = 0; i < n; ++i) {
		char const *s = visiting[i].string();
		while (*s && pos + 4 < sizeof(buf)) buf[pos++] = *s++;
		if (pos + 4 < sizeof(buf)) { buf[pos++] = ' '; buf[pos++] = '-'; buf[pos++] = '>'; buf[pos++] = ' '; }
	}
	char const *s = repeat;
	while (*s && pos + 1 < sizeof(buf)) buf[pos++] = *s++;
	buf[pos] = 0;
	return Genode::String<256>(buf);
}


/* ===================== index + metadata loading ===================== */

void Sponge::Pkgd::Main::_load_index()
{
	/* The pkg_index.xml boot module lists every pkg_<name>.xml that
	 * exists in the image. Loading it must succeed; a scenario that
	 * forgets to stage it is misconfigured. */
	try {
		Genode::Attached_rom_dataspace index(_env, "pkg_index.xml");
		index.update();
		if (!index.valid())
			return;

		Genode::Xml_node const root(index.local_addr<char>(), index.size());
		if (!root.has_type("packages"))
			return;

		root.for_each_sub_node("pkg", [&](Genode::Xml_node const &p) {
			if (_num_known < MAX_PACKAGES)
				_known[_num_known++] = p.attribute_value("name",
				                                Genode::String<64>());
		});
		_index_loaded = true;
	}
	catch (Genode::Rom_connection::Rom_connection_failed) {
		Genode::warning("sponge_pkgd: pkg_index.xml not available — "
		                "every package will be reported as not found");
	}
}


/*
 * Copy every indexed pkg_<name>.xml into the bounded metadata cache.
 * Runs ONCE on the Bootstrap thread, before any request handler is
 * armed, with one transient ROM session per document (constructed and
 * destructed here — the same pattern _load_package used per resolve
 * before P3.3). From here on the dispatch path parses from the cache
 * and opens no ROM session at all: a create is storage I/O on the
 * request path and a denied or stalled create parks the entrypoint.
 *
 * Bounds: METADATA_MAX bytes per document (the shipped inventory tops
 * out around 10 KiB, pkg/terminal). An oversized document is copied
 * truncated and will parse as "malformed metadata" at dispatch — the
 * loud failure beats a silent cut.
 */
void Sponge::Pkgd::Main::_load_metadata_cache()
{
	_num_meta_cache = 0;

	for (unsigned i = 0; i < _num_known && _num_meta_cache < MAX_PACKAGES; ++i) {
		char const *const name = _known[i].string();
		Genode::String<96> const rom_label("pkg_", name, ".xml");

		try {
			Genode::Attached_rom_dataspace rom(_env, rom_label.string());
			rom.update();

			if (!rom.valid() || rom.size() == 0) {
				Genode::warning("sponge_pkgd: no metadata for ", name);
				continue;
			}

			Cached_metadata &entry = _meta_cache[_num_meta_cache];
			entry.name = Genode::String<64>(name);

			char const *src = rom.local_addr<char const>();

			/*
			 * Content length: the module dataspace is page-granular
			 * and may trail past the document, but its content is
			 * NUL-terminated in practice (fresh backing store); the
			 * end of the text is the first NUL.
			 */
			Genode::size_t n { 0 };
			while (n < rom.size() && src[n] != 0) ++n;

			if (n > METADATA_MAX) {
				Genode::warning("sponge_pkgd: metadata for ", name,
				                " exceeds ", (Genode::size_t)METADATA_MAX,
				                " bytes — truncating");
				n = METADATA_MAX;
			}
			for (Genode::size_t j = 0; j < n; ++j)
				entry.xml[j] = src[j];
			entry.len = n;
			++_num_meta_cache;
		}
		catch (Genode::Rom_connection::Rom_connection_failed) {
			/* Defensive: the index guarantees the module exists. A
			 * miss here surfaces at dispatch as the preserved
			 * "package not found: <name>" error. */
			Genode::warning("sponge_pkgd: metadata ROM missing for ", name);
		}
	}

	Genode::log("sponge_pkgd: cached ", _num_meta_cache,
	            " metadata document(s) at bootstrap");
}


bool Sponge::Pkgd::Main::_load_package(char const *name, Package &out)
{
	/* Never request a metadata ROM for a package that has no staged
	 * module: a missing boot module is a fatal ROM-session denial under
	 * Genode's base Env policy, not a catchable error. The index is the
	 * gate that keeps resolution denial-safe. */
	if (!_index_loaded || !_contains(_known, _num_known, name)) {
		if (_num_visiting == 0)
			_error = Genode::String<256>("package not found: ", name);
		else
			_error = Genode::String<256>(_visiting[_num_visiting - 1],
			                            " requires ", name,
			                            ", which is not in the repository");
		_ok = false;
		return false;
	}

	Cached_metadata const *entry { nullptr };
	for (unsigned i = 0; i < _num_meta_cache; ++i)
		if (Genode::strcmp(_meta_cache[i].name.string(), name) == 0) {
			entry = &_meta_cache[i];
			break;
		}

	if (!entry || entry->len == 0) {
		_error = Genode::String<256>("package not found: ", name);
		_ok = false;
		return false;
	}

	try {
		Genode::Xml_node const root(entry->xml, entry->len);
		if (!root.has_type("package")) {
			_error = Genode::String<256>("malformed metadata for package: ", name,
			                            " (root is not <package>)");
			_ok = false;
			return false;
		}
		_parse_package(root, name, out);
	} catch (Genode::Xml_node::Invalid_syntax) {
		_error = Genode::String<256>("malformed metadata for package: ", name);
		_ok = false;
		return false;
	}

	if (!out.valid) {
		if (_error.length() == 0)
			_error = Genode::String<256>("malformed metadata for package: ", name);
		_ok = false;
		return false;
	}

	return true;
}


void Sponge::Pkgd::Main::_parse_package(Genode::Xml_node const &pkg,
                                        char const *name, Package &out)
{
	bool has_name { false };
	bool has_version { false };
	bool has_description { false };
	bool has_runtime_class { false };

	pkg.for_each_sub_node([&](Genode::Xml_node const &child) {
		if (child.has_type("name")) {
			out.name = child.decoded_content<Genode::String<64>>();
			has_name = true;
		}
		else if (child.has_type("version")) {
			out.version = child.decoded_content<Genode::String<32>>();
			has_version = true;
		}
		else if (child.has_type("description")) {
			out.description = child.decoded_content<Genode::String<256>>();
			has_description = true;
		}
		else if (child.has_type("binary")) {
			out.binary = child.decoded_content<Genode::String<64>>();
		}
		else if (child.has_type("quota")) {
			out.ram  = child.attribute_value("ram",  Genode::String<16>("32M"));
			out.caps = child.attribute_value("caps", 512U);
		}
		else if (child.has_type("config")) {
			/*
			 * Serialize the metadata <config> element (with its inner
			 * XML) into out.config_xml as a complete "<config>...</config>"
			 * block, emitted verbatim into the <start> node later. We
			 * rebuild the element via Xml_generator::generate +
			 * append_node_content so arbitrary Qt/libc fragments
			 * (<libc>, <vfs>, <tar>, ...) are preserved structurally.
			 */
			/* static: the parse runs on the (small-stack) ep thread;
			 * a 16K stack local would overflow it (observed as an
			 * illegal-WRITE pager fault on thread 'ep'). Single ep
			 * thread parses serially — no reentrancy. */
			static char cfg_buf[16384] { };
			Genode::Xml_generator::Result const cfg_res =
				Genode::Xml_generator::generate(
					Genode::Byte_range_ptr(cfg_buf, sizeof(cfg_buf)),
					Genode::Xml_generator::Tag_name("config"),
					[&](Genode::Xml_generator &g) {
						/*
						 * The metadata <config> element's own attributes
						 * are configuration and are copied verbatim;
						 * append_node_content alone preserves only the
						 * CHILD nodes, so a bare `<config>` used to be
						 * emitted with every attribute dropped. Silent
						 * for the shipped app packages (children-only
						 * configs) and fatal for an attribute-driven one:
						 * vesa_fb reads width/height off its config root,
						 * so a packaged framebuffer driver came up with
						 * 0x0 and set no mode at all.
						 */
						child.for_each_attribute([&] (auto const &attr) {
							Genode::String<64> v { };
							attr.value(v);
							g.attribute(attr.name().string(), v);
						});
						/* append_node_content is nodiscard: failure
						 * (max-depth exceeded) just yields a shorter
						 * serialized fragment, logged below. */
						(void)g.append_node_content(child,
						                            { .value = 16 });
					});
			if (cfg_res.ok())
				out.config_xml = Genode::String<16384>(cfg_buf);
			else
				Genode::warning("pkg: <config> for ", name,
				                " exceeded serialization buffer");

			/*
			 * Sponge (product-media fix): a nested sub-init package
			 * (binary <binary>init</binary>) carries <start> children
			 * whose binaries resolve via pkg_runtime's ROM routing —
			 * on disk-served media that reaches rom_sys chrooted at
			 * /system, so unprefixed names miss bin/. When
			 * binary_prefix is set, rewrite the serialized config:
			 * every <binary name="B"> (B without '/') becomes
			 * prefix/B, and every <start name="X"> (X without '/')
			 * lacking a <binary> gains <binary name="prefix/X"/>
			 * right after its open tag. Path-like names (/bin/bash
			 * — served by the sub-init's vfs_rom child) are left
			 * untouched. Boot-module (flat) topologies have an empty
			 * prefix and skip this entirely.
			 *
			 * The rewrite itself is the pure sponge_binary_prefix
			 * library (Phase 16 P1.2 extraction,
			 * <sponge/binary_prefix.h>): byte-identical to the former
			 * inline block; P2.3 owns any semantic change. The
			 * buffers stay here — the caller owns them, the library
			 * is pure.
			 */
			Genode::size_t const cfg_len = cfg_res.convert<Genode::size_t>(
				[&] (Genode::size_t v) { return v; },
				[&] (Genode::Buffer_error) { return Genode::size_t(0); });
			if (_binary_prefix != "" && cfg_len > 0) {
				/*
				 * Static char-buffer rewrite: this runs on the (small-
				 * stack) ep thread, where per-character String<16384>
				 * temporaries overflowed the stack (observed as an
				 * illegal-WRITE pager fault on thread 'ep'). One ep
				 * thread parses serially — no reentrancy.
				 */
				static char out_buf[2*16384] { };
				Sponge::Binary_prefix::rewrite(out_buf, sizeof(out_buf),
				                               cfg_buf, cfg_len,
				                               _binary_prefix.string());
				out.config_xml = out_buf;
			}
		}
		else if (child.has_type("autostart")) {
			out.has_autostart = true;
		}
		else if (child.has_type("exclusive")) {
			/*
			 * Closed-form: only the `group` attribute is read, and an
			 * empty / missing value means "no exclusive claim" rather
			 * than an error — a package with no hardware exclusivity to
			 * declare simply omits the element (docs/12 §4.6).
			 */
			out.exclusive_group =
				child.attribute_value("group", Genode::String<16>());
		}
		else if (child.has_type("kind")) {
			/* Closed-form set (docs/12 §4.5): anything other than
			 * "driver" is the "app" default. Rejecting unknown kinds
			 * loudly rather than guessing keeps a typo from silently
			 * landing a driver in the application runtime. */
			Genode::String<16> const v =
				child.decoded_content<Genode::String<16>>();
			if (v == Genode::String<16>("driver") ||
			    v == Genode::String<16>("app"))
				out.kind = v;
			else {
				Genode::warning("pkg: <kind> in ", name,
				                " must be app or driver (got '",
				                v, "'); defaulting to app");
				out.kind = Genode::String<16>("app");
			}
		}
		else if (child.has_type("runtime_class")) {
			/* Closed-form set (docs/12 §4.5): simple|terminal|browser,
			 * default simple. Mirrors <kind>'s typo handling exactly —
			 * an unknown value warns and falls back to simple, so a
			 * typo can never silently land a package in a heavier
			 * runtime. Placement consumes the token only in grouped
			 * mode (<grouped_runtimes/>); see Package::runtime_class. */
			Genode::String<16> const v =
				child.decoded_content<Genode::String<16>>();
			if (v == Genode::String<16>("simple") ||
			    v == Genode::String<16>("terminal") ||
			    v == Genode::String<16>("browser"))
				out.runtime_class = v;
			else {
				Genode::warning("pkg: <runtime_class> in ", name,
				                " must be simple, terminal or browser "
				                "(got '", v, "'); defaulting to simple");
				out.runtime_class = Genode::String<16>("simple");
			}
			has_runtime_class = true;
		}
		else if (child.has_type("resizeable")) {
			/*
			 * Phase 16 W9 (D16.10). Closed-form `yes` / `no`. Default-yes
			 * in the constructor (matches Qt6 QPA automatic info_sigh).
			 * The value is documentary for Phase 16 (the criterion-9
			 * release-media gate exercises the existing wm + window_layouter
			 * + motif decorator propagation, not a new init-time gate);
			 * Phase 17+ non-Qt apps that subscribe-to-resize explicitly
			 * can use this attribute as the source of truth for the
			 * future signal-plumbing hook.
			 */
			Genode::String<8> const v = child.decoded_content<Genode::String<8>>();
			out.resizeable = (v == Genode::String<8>("yes"));
			/* Anything other than yes / no is closed-form rejected. */
			if (v != Genode::String<8>("yes") &&
			    v != Genode::String<8>("no")) {
				Genode::warning("pkg: <resizeable> in ", name,
				                " must be yes or no (got '", v, "');",
				                " defaulting to yes");
				out.resizeable = true;
			}
		}
		else if (child.has_type("launcher")) {
			out.has_launcher      = true;
			out.launcher_category = child.attribute_value("category",
			                            Genode::String<32>());
		}
		else if (child.has_type("dependencies")) {
			child.for_each_sub_node("pkg", [&](Genode::Xml_node const &dep) {
				if (out.num_deps < Package::MAX_DEPS) {
					out.deps[out.num_deps++] =
					    dep.decoded_content<Genode::String<64>>();
				}
			});
		}
		else if (child.has_type("sessions")) {
			child.for_each_sub_node("session", [&](Genode::Xml_node const &s) {
				if (out.num_sessions < Package::MAX_SESSIONS) {
					Package::Session &session = out.sessions[out.num_sessions++];
					session.name      = s.attribute_value("name",
					                    Genode::String<32>());
					session.route     = s.attribute_value("default-route",
					                    Genode::String<32>());
					session.readonly  = s.attribute_value("readonly", false);
					session.subpath   = s.attribute_value("subpath",
					                    Genode::String<64>());
					session.label     = s.attribute_value("label",
					                    Genode::String<64>());
				}
			});
		}
		else {
			/* Unknown element: warn and ignore (docs/12 §4.4). */
			Genode::warning("pkg: unknown element <",
			                child.type().string(), "> in package ", name);
		}
	});

	/* binary defaults to name (docs/12 §4.1). */
	if (Genode::strcmp(out.binary.string(), "") == 0)
		out.binary = Genode::String<64>(name);

	/*
	 * docs/12 §4.5: <kind> is the security and capability boundary and
	 * outranks <runtime_class>. Document order cannot be relied on
	 * (the element may precede <kind>), so this check runs after the
	 * full parse: a <runtime_class> element on a driver package is
	 * ignored with a warning — placement stays driver_runtime, never
	 * an app runtime.
	 */
	if (out.is_driver() && has_runtime_class) {
		Genode::warning("pkg: <runtime_class> in ", name,
		                " is ignored for a driver package");
		out.runtime_class = Genode::String<16>("simple");
	}

	if (Genode::strcmp(out.ram.string(), "") == 0)
		out.ram = Genode::String<16>("32M");
	if (out.caps == 0)
		out.caps = 512;

	/* Required fields (docs/12 §4.4: reject on missing name/version/description). */
	if (!has_name || !has_version || !has_description) {
		char const *missing = !has_name        ? "name"
		                    : !has_version     ? "version"
		                    :                    "description";
		_error = Genode::String<256>("package '", Genode::String<64>(name),
		                            "' is missing required element <", missing, ">");
		out.valid = false;
		return;
	}

	out.valid = true;
}


/* ===================== DFS resolution ===================== */

void Sponge::Pkgd::Main::_resolve(char const *root)
{
	_ok = true;
	_error = Genode::String<256>();
	_num_plan = 0;
	_num_visiting = 0;
	_num_done = 0;

	_visit(root);
}


void Sponge::Pkgd::Main::_visit(char const *name)
{
	if (!_ok) return;

	if (_contains(_done, _num_done, name))
		return;

	/* Already-installed packages are reused, never re-installed
	 * (docs/12 §6.1). The resolver still loads their metadata so the
	 * explain plan can show their sessions, but marks them reused. */
	bool const reused = _installed_contains(name);

	if (_contains(_visiting, _num_visiting, name)) {
		_error = _cycle_message(_visiting, _num_visiting, name);
		_ok = false;
		return;
	}

	Package p { };
	if (!_load_package(name, p))
		return;

	p.reused = reused;

	if (_num_visiting < MAX_PACKAGES)
		_visiting[_num_visiting++] = Genode::String<64>(name);

	/* Visit dependencies in declared order (deterministic). */
	for (unsigned i = 0; i < p.num_deps && _ok; ++i)
		_visit(p.deps[i].string());

	if (_num_visiting > 0)
		_num_visiting--;

	if (_num_done < MAX_PACKAGES)
		_done[_num_done++] = Genode::String<64>(name);

	if (_num_plan < MAX_PACKAGES)
		_plan[_num_plan++] = p;
}
