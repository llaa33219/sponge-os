/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * sponge_pkgd — package backend daemon (Phase 4a).
 *
 * A long-lived, signal-driven Genode component. It watches a "request"
 * ROM (relayed by report_rom from vct's request report), resolves the
 * requested package's metadata and dependency graph, and writes a
 * structured "result" report that report_rom relays back to vct as a
 * ROM. This Report/ROM channel is the settled vct<->backend design
 * (docs/04-components.md §5); there is no RPC stub or IDL.
 *
 * Resolution follows docs/12-package-format.md §6: a deterministic DFS
 * with cycle detection. Each package's metadata is staged in the boot
 * image as a ROM module named "pkg_<name>.xml" and opened here via
 * Attached_rom_dataspace.
 *
 * This slice (4a) implements only the "explain" operation: it produces
 * the install plan but performs no side effects. Actual component
 * installation is Phase 4b; the empty "installed" set below is the seam
 * for 4b's reuse check.
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/heap.h>
#include <base/log.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <util/reconstructible.h>
#include <util/string.h>
#include <util/xml_generator.h>
#include <util/xml_node.h>

namespace Sponge::Pkgd {

class Main;
struct Package;

namespace { }

}  /* namespace Sponge::Pkgd */


/* ===================== parsed package metadata ===================== */

struct Sponge::Pkgd::Package
{
	static constexpr unsigned MAX_DEPS     = 16;
	static constexpr unsigned MAX_SESSIONS = 16;

	Genode::String<64>  name;
	Genode::String<32>  version;
	Genode::String<256> description;

	Genode::String<64>  binary;   /* defaults to name */
	Genode::String<16>  ram;      /* "48M"            */
	unsigned            caps;     /* 512 default      */

	bool                has_launcher { false };
	Genode::String<32>  launcher_category;

	struct Session
	{
		Genode::String<32> name;        /* "Gui"              */
		Genode::String<32> route;       /* "nitpicker"        */
		bool               readonly;    /* File_system only   */
		Genode::String<64> subpath;     /* "/app/nano"        */
	};

	Session   sessions[MAX_SESSIONS] { };
	unsigned  num_sessions           { 0 };

	Genode::String<64> deps[MAX_DEPS] { };
	unsigned           num_deps       { 0 };

	bool valid { false };  /* required fields present */

	/* Comma-joined session names, e.g. "Gui, Input, File_system".
	 * Built at result-generation time for the explain step 2 line. */
	Genode::String<128> requires_sessions() const
	{
		if (num_sessions == 0)
			return Genode::String<128>();

		char buf[128] { };
		Genode::size_t pos = 0;
		for (unsigned i = 0; i < num_sessions && pos + 2 < sizeof(buf); ++i) {
			char const *s = sessions[i].name.string();
			if (i > 0 && pos + 2 < sizeof(buf)) {
				buf[pos++] = ',';
				buf[pos++] = ' ';
			}
			while (*s && pos + 1 < sizeof(buf))
				buf[pos++] = *s++;
		}
		buf[pos] = 0;
		return Genode::String<128>(buf);
	}
};


/* ===================== dependency resolver ===================== */

/*
 * Deterministic DFS topological expansion with cycle detection
 * (docs/12-package-format.md §6).
 *
 * The plan is built in install order: dependencies appear before the
 * packages that depend on them. Each package is parsed at most once
 * (single-pass). The "installed" set is empty in Phase 4a — that is the
 * seam where 4b will consult the live init tree to reuse components.
 */
class Sponge::Pkgd::Main
{
	public:

		explicit Main(Genode::Env &env);

	private:

		static constexpr unsigned MAX_PACKAGES = 32;

		Genode::Env &_env;

		/* Request ROM: report_rom relays vct's "request" report here. */
		Genode::Attached_rom_dataspace _request_rom { _env, "request" };

		/* Result report: report_rom relays this to vct's "result" ROM. */
		Genode::Expanding_reporter _result_reporter { _env, "result", "result" };

		Genode::Signal_handler<Main> _request_handler {
			_env.ep(), *this, &Main::_handle_request };

		/*
		 * Known-package set, loaded once from the pkg_index.xml boot
		 * module. Genode boot modules are not enumerable, and a request
		 * for a missing module is a fatal ROM-session denial under the
		 * base Env policy (component.cc), so we consult this index
		 * before opening any pkg_<name>.xml ROM. The index is a derived
		 * staging artifact (generated at boot-image assembly from the
		 * actual pkg/ contents), not the per-system runtime index that
		 * docs/12-package-format.md §5.3 rules out.
		 */
		Genode::String<64> _known[MAX_PACKAGES] { };
		unsigned           _num_known            { 0 };
		bool               _index_loaded         { false };

		/* Constructed per package during resolution (sequential DFS,
		 * so a single slot is enough). Starts deconstructed so that a
		 * missing ROM surfaces only when we actually look it up. */
		Genode::Constructible<Genode::Attached_rom_dataspace> _meta_rom { };

		/* resolution state */
		Package          _plan[MAX_PACKAGES] { };
		unsigned         _num_plan            { 0 };

		Genode::String<64> _visiting[MAX_PACKAGES] { };
		unsigned           _num_visiting          { 0 };

		Genode::String<64> _done[MAX_PACKAGES] { };
		unsigned           _num_done           { 0 };

		bool               _ok    { false };
		Genode::String<256> _error { };

		/* ---- request handling ---- */
		void _handle_request();
		void _do_explain(Genode::String<128> const &pkg);

		/* ---- resolution ---- */
		void _resolve(char const *root);
		void _visit(char const *name);

		void _load_index();
		bool _load_package(char const *name, Package &out);
		void _parse_package(Genode::Xml_node const &pkg, char const *name, Package &out);

		/* ---- result generation ---- */
		void _report_ok(Genode::String<128> const &pkg);
		void _report_error(char const *op, Genode::String<128> const &pkg,
		                   char const *message);

		/* ---- small helpers ---- */
		static bool _contains(Genode::String<64> const *set, unsigned n,
		                      char const *name);
		static Genode::String<256> _cycle_message(Genode::String<64> const *visiting,
		                                           unsigned n, char const *repeat);
};


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

	Genode::String<96> const rom_label("pkg_", name, ".xml");

	try {
		_meta_rom.construct(_env, rom_label.string());
		_meta_rom->update();
	} catch (Genode::Rom_connection::Rom_connection_failed) {
		/* Should not happen: the index guarantees the module exists.
		 * Treat defensively as a malformed repository. */
		_error = Genode::String<256>("package not found: ", name);
		_ok = false;
		return false;
	}

	if (!_meta_rom->valid()) {
		_error = Genode::String<256>("package not found: ", name);
		_ok = false;
		return false;
	}

	try {
		Genode::Xml_node const root(_meta_rom->local_addr<char>(),
		                            _meta_rom->size());
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

	/* Phase 4a: the installed set is empty. 4b will consult the live init
	 * tree here and return early for already-present components. */
	/* if (_contains(_installed, _num_installed, name)) return; */

	if (_contains(_visiting, _num_visiting, name)) {
		_error = _cycle_message(_visiting, _num_visiting, name);
		_ok = false;
		return;
	}

	Package p { };
	if (!_load_package(name, p))
		return;

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


/* ===================== request handling ===================== */

void Sponge::Pkgd::Main::_handle_request()
{
	_request_rom.update();

	if (!_request_rom.valid())
		return;

	try {
		Genode::Xml_node const req = _request_rom.xml();

		if (!req.has_type("request")) {
			_report_error("explain", Genode::String<128>(),
			              "request root is not <request>");
			return;
		}

		Genode::String<32>  const op  = req.attribute_value("op",
		                                         Genode::String<32>());
		Genode::String<128> const pkg = req.attribute_value("pkg",
		                                         Genode::String<128>());

		if (Genode::strcmp(pkg.string(), "") == 0) {
			_report_error(op.string(), pkg, "no package specified in request");
			return;
		}

		if (Genode::strcmp(op.string(), "explain") == 0) {
			_do_explain(pkg);
			return;
		}

		_report_error(op.string(), pkg, "unknown operation");
	}
	catch (Genode::Xml_node::Invalid_syntax) {
		_report_error("explain", Genode::String<128>(),
		              "malformed request ROM");
	}
}


void Sponge::Pkgd::Main::_do_explain(Genode::String<128> const &pkg)
{
	_resolve(pkg.string());

	if (!_ok) {
		_report_error("explain", pkg, _error.string());
		return;
	}

	_report_ok(pkg);
}


/* ===================== result generation ===================== */

void Sponge::Pkgd::Main::_report_ok(Genode::String<128> const &pkg)
{
	/* The root package is the last entry in the install-ordered plan. */
	Package const &root = _plan[_num_plan > 0 ? _num_plan - 1 : 0];

	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "ok");
		g.attribute("op",     "explain");
		g.attribute("pkg",    pkg);
		g.attribute("version", root.version);

		g.node("description", [&] {
			g.append_sanitized(root.description); });

		/* Step 1: dependencies (root's direct deps, in declared order). */
		g.node("dependencies", [&] {
			for (unsigned i = 0; i < root.num_deps; ++i) {
				g.node("dep", [&] {
					g.attribute("name", root.deps[i]);
				});
			}
		});

		/* Step 2: expanded component tree, install order (deps first). */
		g.node("components", [&] {
			for (unsigned i = 0; i < _num_plan; ++i) {
				Package const &c = _plan[i];
				g.node("component", [&] {
					g.attribute("name",    c.name);
					g.attribute("binary",  c.binary);
					g.attribute("ram",     c.ram);
					g.attribute("caps",    c.caps);
					g.attribute("requires", c.requires_sessions());
					/* new="yes": Phase 4a has no installed set, so every
					 * resolved component is new. 4b will set this from the
					 * reuse check. */
					g.attribute("new", "yes");
				});
			}
		});

		/* Step 3: session routes, grouped by component, in plan order. */
		g.node("routes", [&] {
			for (unsigned i = 0; i < _num_plan; ++i) {
				Package const &c = _plan[i];
				for (unsigned s = 0; s < c.num_sessions; ++s) {
					Package::Session const &session = c.sessions[s];
					g.node("route", [&] {
						g.attribute("component", c.name);
						g.attribute("session",   session.name);
						g.attribute("target",    session.route);
						if (session.readonly)
							g.attribute("readonly", "true");
						if (Genode::strcmp(session.subpath.string(), "") != 0)
							g.attribute("subpath", session.subpath);
					});
				}
			}
		});

		/* Step 4: launcher entry (omitted entirely if absent). */
		if (root.has_launcher) {
			g.node("launcher", [&] {
				g.attribute("category", root.launcher_category);
			});
		}
	});
}


void Sponge::Pkgd::Main::_report_error(char const *op,
                                       Genode::String<128> const &pkg,
                                       char const *message)
{
	_result_reporter.generate_xml([&](Genode::Xml_generator &g) {
		g.attribute("status", "error");
		g.attribute("op",     op);
		g.attribute("pkg",    pkg);
		g.attribute("error",  message);
	});
}


/* ===================== component wiring ===================== */

Sponge::Pkgd::Main::Main(Genode::Env &env) : _env(env)
{
	Genode::log("sponge_pkgd: ready");

	_load_index();

	_request_rom.sigh(_request_handler);
	_request_rom.update();

	/* Process a request that arrived before the signal handler was wired. */
	_handle_request();
}


void Component::construct(Genode::Env &env)
{
	static Sponge::Pkgd::Main main { env };
}


/* The daemon carries the resolver state on its stack during request
 * handling; keep it comfortable. */
Genode::size_t Component::stack_size() { return 64 * 1024 * sizeof(Genode::addr_t); }
