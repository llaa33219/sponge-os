> **Breadcrumb:** [08 - Development Guide](../08-development.md) → §12. Importing Depot Packages (Alpha)

## 12. Importing Depot Packages (Alpha)

Phase 7 introduces host-side depot interop: instead of building every
binary from in-tree source, some Alpha packages are repackaged from
[cproc's published Genode depot archives](https://depot.genode.org/cproc/)
into Sponge `pkg/<name>/` directories. The pinned depot references and
their SHA-256 fingerprints live in `docs/11-environment.md` §5. This
section documents the importer tool and its manual equivalent.

**What is a depot package, and what is not.** A Genode depot *pkg*
archive (`<user>/pkg/<recipe>/<version>`) is a recipe that lists
transitive *src*, *raw*, and *api* dependencies and a `runtime` file
describing how the binary expects to be wired at boot (libc vfs,
required sessions, content ROMs). The *pkg* archive itself contains no
binaries — those live in a separate *bin* archive
(`<user>/bin/<arch>/<recipe>/<version>`) that the importer downloads on
demand. Neither archive is fetched at Sponge runtime (Metis amendment
A1: all binaries are baked into the boot image at build time;
`sponge_pkgd` never contacts a depot).

### 12.1 One-command flow (default)

```bash
# Step 1 (once per machine): set up an in-repo GNUPGHOME holding the
# cproc pubkey. Modern GnuPG (2.4+) with use-keyboxd silently ignores
# `--keyring <file>` on the depot tool's verify path, which makes the
# in-tree pubkey at genode/repos/gems/sculpt/depot/cproc/pubkey
# unreachable. Importing it into a fresh in-repo keybox under
# var/scratch/gnupg sidesteps that without writing outside the repo
# (AGENTS.md §3.5).
mkdir -p var/scratch/gnupg && chmod 700 var/scratch/gnupg
export GNUPGHOME="$PWD/var/scratch/gnupg"
gpg --no-tty --import genode/repos/gems/sculpt/depot/cproc/pubkey

# Step 2: fetch the pkg + its transitive deps into genode/depot/ and
# genode/public/ (signature-verified against the in-tree pubkey).
./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27

# Step 3: repackage the depot pkg + its binary archive into pkg/textedit/.
# The importer downloads the matching bin archive on demand.
./tool/pkg_import cproc/pkg/qt6_textedit/2025-10-27 \
    --bin-version 2025-10-12
```

The wrapper ([`tool/pkg_import`](../../tool/pkg_import) →
[`tool/pkg_import.mojo`](../../tool/pkg_import.mojo)):

1. Verifies the pkg is downloaded under `genode/depot/<ref>/`.
2. Reads the `runtime` metadata (binary name, ram/caps, required
   sessions, content ROMs) and hashes the original `pkg.tar.xz`.
3. Resolves the matching `bin/<arch>/<recipe>/<version>` archive by
   trying the recipe-name, binary-name, and pkg-recipe-name paths in
   order; downloads it on demand via `genode/tool/depot/download`.
4. Stages every content ROM the runtime declares into
   `pkg/<name>/payload/`, copying files already present in the local
   `genode/depot/` tree (raw archives, the bin archive, plus any
   `*.lib.so` or `*.tar` in sibling bin archives). Missing entries are
   recorded in `pkg/<name>/PAYLIST` for follow-up staging.
5. Generates `pkg/<name>/metadata.xml` per
   [docs/12-package-format.md §4](../../docs/12-package-format.md) with an
   inline `<config>` carrying the libc+vfs boilerplate (modeled on
   `run/sponge-launcher.run:126-136`) and a `<sessions>` block mirrored
   from the runtime's `<requires>`. The caps floor is raised to 1000
   for GUI apps (per `docs/09-roadmap.md` §11.1 — the Qt-on-seL4
   capability-exhaustion lesson).
6. Writes `pkg/<name>/SOURCE` recording the depot user/pkg/version plus
   the SHA-256 of both the pkg archive and the bin archive
   (reproducibility, `docs/11-environment.md` §1).
7. Builds everything under `pkg/<name>.tmp/` and atomically renames to
   `pkg/<name>/` on success, so a bogus depot reference never leaves a
   partial package directory behind (the failure channel).

`./tool/pkg_import` never touches anything outside the repository, and
never re-implements the depot tool (it shells out to
`genode/tool/depot/download` exactly as a contributor would by hand,
AGENTS.md §3.5).

### 12.2 Manual flow (the canonical reference)

Per AGENTS.md §3.5 every automated step has a documented manual
equivalent. The procedure below reproduces `./tool/pkg_import` step by
step. Run it from the repository root after `./tool/build prepare` and
`./tool/build ports` have set up `genode/build/x86_64/`.

```bash
# 0. One-time PGP setup (see Step 1 above for the rationale).
mkdir -p var/scratch/gnupg && chmod 700 var/scratch/gnupg
export GNUPGHOME="$PWD/var/scratch/gnupg"
gpg --no-tty --import genode/repos/gems/sculpt/depot/cproc/pubkey

# 1. Fetch the pkg + its transitive src/raw/api dependencies.
./genode/tool/depot/download cproc/pkg/qt6_textedit/2025-10-27

# 2. Fetch the matching binary archive. The pkg-vs-bin path mapping is
#    not always 1:1 (cproc publishes `falkon_qt6-jemalloc` under the
#    bin path `falkon_qt6`). When the pkg version does not map to a
#    published bin version, list the available versions and pick one:
#      curl -s https://depot.genode.org/cproc/bin/x86_64/qt6_textedit/
./genode/tool/depot/download cproc/bin/x86_64/qt6_textedit/2025-10-12

# 3. Verify both archives landed under genode/depot/:
ls genode/depot/cproc/pkg/qt6_textedit/2025-10-27/
ls genode/depot/cproc/bin/x86_64/qt6_textedit/2025-10-12/

# 4. Compute the SHA-256 of each depot archive (for the SOURCE record).
sha256sum genode/public/cproc/pkg/qt6_textedit/2025-10-27.tar.xz
sha256sum genode/public/cproc/bin/x86_64/qt6_textedit/2025-10-12.tar.xz

# 5. Read the runtime metadata to learn the binary name, required
#    sessions, and the content ROMs that must be staged.
cat genode/depot/cproc/pkg/qt6_textedit/2025-10-27/runtime

# 6. Create the Sponge package directory. Mirror the runtime's binary
#    name, quota, and sessions into metadata.xml (see
#    docs/12-package-format.md §4 for the schema). Copy every content
#    ROM from the local depot tree into payload/. Write SOURCE
#    recording the depot pin + sha256.
mkdir -p pkg/textedit/payload
# (edit pkg/textedit/metadata.xml by hand following docs/12 §4.1)
# (edit pkg/textedit/SOURCE by hand following the format in 12.3 below)
cp genode/depot/cproc/bin/x86_64/qt6_textedit/2025-10-12/textedit \
   pkg/textedit/payload/
cp genode/depot/cproc/raw/qt6_textedit/2025-09-19/textedit.config \
   pkg/textedit/payload/
# (also copy every *.lib.so and *.tar listed in the runtime <content>
#  from sibling bin/ archives into payload/)

# 7. Stage the package into a run scenario alongside the other boot
#    modules — see run/sponge-launcher.run:208-216 for the staging
#    pattern. The scenario's build_boot_image then includes the
#    payload files as boot modules.
```

### 12.3 SOURCE record format

Every imported package carries a `SOURCE` file. The importer writes it
automatically; the manual flow writes it by hand. The format:

```
# Sponge OS package source record (reproducibility).
depot_user: cproc
depot_pkg_recipe: qt6_textedit
depot_pkg_version: 2025-10-27
depot_pkg_archive_sha256: <64-hex-char sha256 of the pkg.tar.xz>
depot_bin_archive_ref: cproc/bin/x86_64/qt6_textedit/2025-10-12
depot_bin_archive_sha256: <64-hex-char sha256 of the bin.tar.xz>
depot_url: https://depot.genode.org
imported_by: tool/pkg_import
```

The records match the depot-pin table in
`docs/11-environment.md` §5 so a contributor can verify any imported
binary by re-running `sha256sum` against the locally-downloaded
archive.

### 12.4 Caveats

- The cproc depot's pubkey uses SHA-1 self-signatures. The depot tool's
  preferred verifier is Sequoia PGP (`sq`), which carries an explicit
  `--policy-as-of 2013-01-31` workaround for SHA-1 keys. When `sq` is
  absent the depot tool falls back to GnuPG, and GnuPG 2.4+ with
  `use-keyboxd` silently ignores `--keyring <file>`. The in-repo
  `GNUPGHOME` setup in Step 1 is the supported workaround. Installing
  `sq` (`sequoia-sq` on Arch, `sequoia-pgp` on Debian) is the
  upstream-preferred alternative.
- The pkg archive's `runtime` declares every library ROM the binary
  expects at boot, but the depot's pkg download path only pulls the
  pkg/src/raw/api archives — not the per-library bin archives. The
  importer downloads the *main binary's* bin archive automatically;
  transitive library bins must be fetched separately (the importer
  records them in `pkg/<name>/PAYLIST`).
- The depot's pkg-vs-bin versions are independent: the
  `cproc/pkg/qt6_textedit/2025-10-27` package version (the packaging
  metadata version) does not match its latest
  `cproc/bin/x86_64/qt6_textedit/2025-10-12` (the binary build
  version). The importer takes an explicit `--bin-version` for this
  reason.



