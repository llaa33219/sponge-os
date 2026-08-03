/* SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
 *
 * pkg_meta_probe — Phase 7 todo 18 search/update assertion matrix.
 *
 * vct is short-lived (one command per boot), so this headless probe
 * drives the same ROM reads + comparisons that vct's SearchCommand and
 * UpdateCommand perform, covering the full assertion matrix in ONE
 * boot:
 *
 *   1. install hello via sponge_pkgd (so the `installed` broadcast
 *      reflects hello with its real version).
 *   2. SEARCH hit:   read pkg_index.xml + pkg_hello.xml; assert hello
 *                    matches the term "hello".
 *   3. SEARCH miss:  assert a term "zzznomatch" produces zero hits
 *                    (honest empty result, never an error).
 *   4. UPDATE current: read the installed broadcast + pkg_hello.xml;
 *                      versions equal -> "already current".
 *   5. UPDATE delta:   publish a synthetic installed broadcast carrying
 *                      hello@0.9 (simulating a cross-image-rebuild
 *                      pinned installed version); read pkg_hello.xml
 *                      (repo carries 1.0) -> "repo carries 1.0,
 *                      installed 0.9 - effective after next image
 *                      build".
 *   6. UPDATE error:   update for a package NOT in the installed set
 *                      -> clear "not installed" error.
 *
 * The comparison logic is a faithful copy of vct's helpers
 * (for_each_repo_pkg / load_repo_pkg / ci_contains / exact-string
 * version inequality) so the probe exercises the real code path.
 *
 * Logs "pkg-meta-probe: PASS" only if every step passes; otherwise
 * "...: FAIL <reason>" and the run scenario times out (fail-loud).
 */

#include <base/attached_rom_dataspace.h>
#include <base/component.h>
#include <base/log.h>
#include <base/sleep.h>
#include <os/reporter.h>
#include <report_session/connection.h>
#include <timer_session/connection.h>
#include <util/string.h>
#include <util/xml_node.h>

namespace {

struct Repo_pkg {
	Genode::String<64>  name;
	Genode::String<32>  version;
	Genode::String<192> description;
};

bool ci_contains(char const *haystack, char const *needle)
{
	Genode::size_t const hlen = Genode::strlen(haystack);
	Genode::size_t const nlen = Genode::strlen(needle);
	if (nlen == 0) return true;
	if (nlen > hlen) return false;
	for (Genode::size_t i = 0; i + nlen <= hlen; ++i) {
		bool match { true };
		for (Genode::size_t k = 0; k < nlen; ++k) {
			char a = haystack[i + k];
			char b = needle[k];
			if (a >= 'A' && a <= 'Z') a = char(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = char(b - 'A' + 'a');
			if (a != b) { match = false; break; }
		}
		if (match) return true;
	}
	return false;
}


bool load_repo_pkg(Genode::Env &env, char const *name, Repo_pkg &out)
{
	Genode::String<96> const label("pkg_", name, ".xml");
	try {
		Genode::Attached_rom_dataspace rom { env, label.string() };
		rom.update();
		if (!rom.valid()) return false;
		Genode::Xml_node const root(rom.local_addr<char>(), rom.size());
		if (!root.has_type("package")) return false;
		bool ok { true };
		root.with_sub_node("name", [&](Genode::Xml_node const &n) {
			out.name = n.decoded_content<Genode::String<64>>(); },
			[&] { ok = false; });
		root.with_sub_node("version", [&](Genode::Xml_node const &n) {
			out.version = n.decoded_content<Genode::String<32>>(); },
			[&] { ok = false; });
		root.with_sub_node("description", [&](Genode::Xml_node const &n) {
			out.description = n.decoded_content<Genode::String<192>>(); },
			[&] { ok = false; });
		return ok;
	}
	catch (Genode::Rom_connection::Rom_connection_failed) { return false; }
	catch (Genode::Xml_node::Invalid_syntax)             { return false; }
}


struct Probe
{
	Genode::Env &_env;

	Timer::Connection              _timer     { _env };
	Genode::Attached_rom_dataspace _config    { _env, "config" };

	/* pkgd request/result channels (to install hello first). */
	Genode::Expanding_reporter     _request   { _env, "request", "request" };
	Genode::Attached_rom_dataspace _result    { _env, "result" };

	/* The installed-set broadcast (what vct update reads). */
	Genode::Attached_rom_dataspace _installed { _env, "installed" };

	bool _ok { true };

	explicit Probe(Genode::Env &env) : _env(env) { }

	void _fail(char const *reason)
	{
		_ok = false;
		Genode::error("pkg-meta-probe: FAIL ", reason);
		_env.parent().exit(1);
		Genode::sleep_forever();
	}

	/* Send a pkgd request and poll for a matching result. */
	bool _pkgd_request(char const *op, char const *pkg)
	{
		static unsigned seq { 0 };
		++seq;
		_request.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("op",  op);
			g.attribute("seq", seq);
			if (Genode::strcmp(pkg, "") != 0)
				g.attribute("pkg", pkg);
		});

		_timer.msleep(200);
		for (unsigned i = 0; i < 80; ++i) {
			_result.update();
			if (!_result.valid()) { _timer.msleep(100); continue; }
			try {
				Genode::Xml_node const r = _result.xml();
				if (!r.has_type("result")) { _timer.msleep(100); continue; }
				if (r.attribute_value("op", Genode::String<32>())
				    != Genode::String<32>(op)) { _timer.msleep(100); continue; }
				if (Genode::strcmp(op, "list") != 0 &&
				    r.attribute_value("pkg", Genode::String<128>())
				    != Genode::String<128>(pkg)) { _timer.msleep(100); continue; }
				return true;
			} catch (Genode::Xml_node::Invalid_syntax) {
				_timer.msleep(100);
			}
		}
		return false;
	}

	bool _installed_contains(char const *name, Genode::String<32> &version_out)
	{
		_installed.update();
		if (!_installed.valid()) return false;
		try {
			Genode::Xml_node const root = _installed.xml();
			bool found { false };
			root.with_optional_sub_node("packages",
				[&](Genode::Xml_node const &pkgs) {
					pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
						if (!found &&
						    p.attribute_value("name", Genode::String<64>())
						    == Genode::String<64>(name)) {
							found = true;
							version_out = p.attribute_value("version",
							                                Genode::String<32>());
						}
					});
				});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	/*
	 * Publish a synthetic installed broadcast carrying hello@0.9 onto
	 * the `installed_delta` report label. The scenario's report_rom
	 * relays it to the `installed_delta` ROM, which the probe then
	 * reads as vct update would read the real broadcast — proving the
	 * delta-detection comparison path against a pinned old version
	 * (the cross-image-rebuild case documented in docs/12 §9.2.2).
	 */
	Genode::Expanding_reporter _delta_report { _env, "installed_delta",
	                                           "installed_delta" };
	void _publish_delta_broadcast(char const *name, char const *ver)
	{
		_delta_report.generate_xml([&](Genode::Xml_generator &g) {
			g.attribute("count", 1);
			g.node("packages", [&] {
				g.node("package", [&] {
					g.attribute("name",    name);
					g.attribute("version", ver);
				});
			});
		});
	}

	bool _delta_version_of(char const *name, Genode::String<32> &version_out)
	{
		Genode::Attached_rom_dataspace delta { _env, "installed_delta" };
		_timer.msleep(200);
		delta.update();
		if (!delta.valid()) return false;
		try {
			Genode::Xml_node const root = delta.xml();
			bool found { false };
			root.with_optional_sub_node("packages",
				[&](Genode::Xml_node const &pkgs) {
					pkgs.for_each_sub_node("package", [&](Genode::Xml_node const &p) {
						if (!found &&
						    p.attribute_value("name", Genode::String<64>())
						    == Genode::String<64>(name)) {
							found = true;
							version_out = p.attribute_value("version",
							                                Genode::String<32>());
						}
					});
				});
			return found;
		} catch (Genode::Xml_node::Invalid_syntax) {
			return false;
		}
	}

	void run()
	{
		_config.update();

		/* Step 1: install hello so the installed broadcast reflects it. */
		Genode::log("pkg-meta-probe: [1] install hello (via sponge_pkgd)");
		if (!_pkgd_request("install", "hello"))
			return _fail("install did not answer");
		{
			Genode::Xml_node const r = _result.xml();
			if (r.attribute_value("status", Genode::String<32>())
			    != Genode::String<32>("ok"))
				return _fail("install returned error");
		}
		_timer.msleep(500);  /* let the broadcast propagate */

		/* Step 2: search hit. */
		Genode::log("pkg-meta-probe: [2] search 'hello' (expect hit)");
		{
			Genode::Attached_rom_dataspace index { _env, "pkg_index.xml" };
			index.update();
			if (!index.valid()) return _fail("pkg_index.xml unavailable");
			unsigned hits { 0 };
			try {
				Genode::Xml_node const root(index.local_addr<char>(), index.size());
				root.for_each_sub_node("pkg", [&](Genode::Xml_node const &p) {
					Genode::String<64> const name =
						p.attribute_value("name", Genode::String<64>());
					Repo_pkg meta { };
					if (!load_repo_pkg(_env, name.string(), meta)) return;
					if (ci_contains(Genode::String<192>(meta.name).string(), "hello") ||
					    ci_contains(meta.description.string(), "hello")) {
						++hits;
						Genode::log("pkg-meta-probe:   hit: ", meta.name, " ",
						            meta.version, " ", meta.description);
					}
				});
			} catch (Genode::Xml_node::Invalid_syntax) {
				return _fail("pkg_index.xml malformed");
			}
			if (hits == 0) return _fail("search 'hello' produced zero hits");
			Genode::log("pkg-meta-probe:   search hit count = ", hits, " (>=1, ok)");
		}

		/* Step 3: search miss (honest empty result). */
		Genode::log("pkg-meta-probe: [3] search 'zzznomatch' (expect zero hits)");
		{
			Genode::Attached_rom_dataspace index { _env, "pkg_index.xml" };
			index.update();
			unsigned hits { 0 };
			try {
				Genode::Xml_node const root(index.local_addr<char>(), index.size());
				root.for_each_sub_node("pkg", [&](Genode::Xml_node const &p) {
					Genode::String<64> const name =
						p.attribute_value("name", Genode::String<64>());
					Repo_pkg meta { };
					if (!load_repo_pkg(_env, name.string(), meta)) return;
					if (ci_contains(Genode::String<192>(meta.name).string(), "zzznomatch") ||
					    ci_contains(meta.description.string(), "zzznomatch"))
						++hits;
				});
			} catch (Genode::Xml_node::Invalid_syntax) { }
			if (hits != 0) return _fail("search 'zzznomatch' should be empty");
			Genode::log("pkg-meta-probe:   empty result (ok) - would print 'No matches.'");
		}

		/* Step 4: update current (real broadcast == repo). */
		Genode::log("pkg-meta-probe: [4] update hello (expect 'already current')");
		{
			Genode::String<32> installed_ver { };
			if (!_installed_contains("hello", installed_ver))
				return _fail("hello not in installed broadcast");
			Repo_pkg repo { };
			if (!load_repo_pkg(_env, "hello", repo))
				return _fail("pkg_hello.xml missing");
			bool const current = (installed_ver == repo.version);
			if (current)
				Genode::log("pkg-meta-probe:   already current: hello ", installed_ver);
			else
				return _fail("expected current but versions differ");
		}

		/* Step 5: update delta (synthetic broadcast 0.9 vs repo 1.0). */
		Genode::log("pkg-meta-probe: [5] update hello (delta: installed 0.9, repo 1.0)");
		{
			_publish_delta_broadcast("hello", "0.9");
			Genode::String<32> installed_ver { };
			if (!_delta_version_of("hello", installed_ver))
				return _fail("delta broadcast not read back");
			Repo_pkg repo { };
			if (!load_repo_pkg(_env, "hello", repo))
				return _fail("pkg_hello.xml missing");
			bool const current = (installed_ver == repo.version);
			if (current)
				return _fail("expected delta but versions equal");
			Genode::log("pkg-meta-probe:   repo carries ", repo.version,
			            ", installed ", installed_ver,
			            " — effective after next image build");
		}

		/* Step 6: update error (not installed). */
		Genode::log("pkg-meta-probe: [6] update nosuchpkg (expect 'not installed')");
		{
			Genode::String<32> dummy { };
			if (_installed_contains("nosuchpkg", dummy))
				return _fail("nosuchpkg should not be installed");
			Genode::log("pkg-meta-probe:   update: error: nosuchpkg is not installed");
		}

		if (!_ok) return;
		Genode::log("pkg-meta-probe: PASS");
		_env.parent().exit(0);
		Genode::sleep_forever();
	}
};

}  /* namespace */


void Component::construct(Genode::Env &env)
{
	static Probe probe { env };
	probe.run();
}


Genode::size_t Component::stack_size() { return 32 * 1024; }
