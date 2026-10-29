#
# \brief  runtime_simple — the simple-app runtime class (Phase 16 P2.2).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# One of three per-runtime-class targets introduced by Phase 16 P2.2
# (docs/plans/phase16-runtime-overhaul.md, "Per-runtime targets"). The
# SIMPLE class is the packages pkg_runtime starts as flat children from
# its generated config (docs/17-runtime-component-topology.md §6):
#
#   hello · textedit · files · calculator · pdf_view · debug
#
# This include owns, for that class and for that class only:
#
#   1. the package name -> REAL producer map (build target or payload
#      file), so the build block, the staging step and the quota row
#      cannot drift apart — the same single-source discipline
#      driver_runtime.inc applies to the driver packages;
#   2. runtime_simple::build_targets   — the Genode build targets;
#   3. runtime_simple::stage_binaries  — fail-loud staging of the
#      class binaries into <sys_dir>/bin/;
#   4. the class quota row (QUOTA_CAPS / QUOTA_RAM) consumed by
#      runtime-quota.inc's pkg_runtime_caps() derivation.
#
# It is PURE run-script Tcl: no C++, no package metadata, no topology
# change. Until P2.4 groups the runtimes, pkg_runtime stays one node
# and these values feed only its single grant (run/runtime-quota.inc).
#
# === TEXTEDIT'S REAL PRODUCER (pinned, not guessed) =======================
#
# textedit has NO build target. `pkg/textedit` is a depot-repackaged
# prebuilt: tool/pkg_import extracted the binary into
# pkg/textedit/payload/textedit (provenance in pkg/textedit/SOURCE —
# cproc/bin/x86_64/qt6_textedit/2025-10-12). Nothing under
# repos/sponge/src/ builds it; the product media's only copy arrives
# through bake::stage's payload step (run/bake.inc _stage_payload,
# pkg/<name>/payload/* -> <sys_dir>/pkg/<name>/payload/), which is also
# where pkg/textedit/payload/textedit.config goes.
#
# Consequence for staging (and why stage_binaries deliberately does NOT
# copy textedit into <sys_dir>/bin/): today's desktop-binaries loop
# lists `textedit` under a silent `if {[file exists "bin/$b"]}` guard
# that NEVER fires, because bin/textedit is never produced. The staged
# file set therefore carries no /system/bin/textedit — the payload
# directory is the binary's home. Reproducing that exactly is the
# P2.2 equivalence requirement; making it fail-loud (asserting the
# payload producer exists) replaces a silent skip with a real check
# without moving a single staged file.
#
# === QUOTA DERIVATION =====================================================
#
# The shipped simple-class set, from each package's <quota>:
#
#   hello       256 caps /    8M
#   textedit   1000 caps /   64M
#   files      1000 caps /   64M
#   calculator 1000 caps /   64M
#   pdf_view    256 caps / 1024M
#   debug      1000 caps /   64M
#   ---------------------------
#   total       4512 caps / 1288M
#
# QUOTA_CAPS 4512 is the exact sum — the per-class rows are inputs to
# runtime_quota::pkg_runtime_caps(), which owns the headroom/round-up
# arithmetic for the single pkg_runtime grant (docs/17 §6).
#

namespace eval runtime_simple {

    #
    # The simple-class packages, and the REAL producer of each one's
    # binary. Flat rows of `name kind producer`:
    #
    #   build   <target>  — a Genode build target; its artifact is
    #                       bin/<binary> where <binary> is the
    #                       <binary> element of pkg/<name>/metadata.xml
    #                       (read at stage time, never assumed equal to
    #                       the package name — files stages
    #                       bin/sponge_files, calculator stages
    #                       bin/calculatorform).
    #   payload <path>    — a committed prebuilt payload file; the
    #                       binary reaches the media through
    #                       bake::stage's payload copy, NOT through
    #                       <sys_dir>/bin/ (see the textedit note in
    #                       this header).
    #
    variable packages {
        hello      build   pkg_hello
        textedit   payload pkg/textedit/payload/textedit
        files      build   sponge_files
        calculator build   app/qt6/examples/calculatorform
        pdf_view   build   app/pdf_view
        debug      build   sponge_debug
    }

    #
    # Class quota row (docs/17 §6 "simple"). Sum of the six packages'
    # <quota> elements; consumed by runtime-quota.inc. The grouped
    # topology grants this class an explicit RAM budget; only the
    # browser runtime takes the remainder clamp (run/runtime-quota.inc).
    #
    variable QUOTA_CAPS 4512
    variable QUOTA_RAM  1288M

    #
    # The Genode build targets of this class, in map order. The
    # payload-produced package contributes no target (there is nothing
    # to build) — that absence is exactly the fact this map pins.
    #
    proc build_targets { } {
        variable packages
        set targets {}
        foreach {name kind producer} $packages {
            if {$kind eq "build"} { lappend targets $producer }
        }
        return $targets
    }

    #
    # runtime_simple::stage_binaries <sys_dir>
    #
    # Stage the class's binaries into <sys_dir>/bin/ (the directory the
    # scenario stages /system from). MUST run after the scenario's
    # `build {}` block and its `exec mkdir -p <sys_dir>/bin`.
    #
    # build-kind packages: the binary name comes from the package's
    # metadata <binary> element (the path pkgd's config generator
    # emits with binary_prefix, docs/12 §4), and BOTH ends of the copy
    # are loud — a missing bin/<binary> is a build-block omission, a
    # missing destination is a broken staging step. No silent
    # file-exists skip: that is the §2 H-a1 anti-pattern.
    #
    # payload-kind packages: the producer file is asserted to exist
    # (fail-loud) but NOT copied here — bake::stage already places it
    # under <sys_dir>/pkg/<name>/payload/, and that is the ONLY copy on
    # the media today. Copying into <sys_dir>/bin/ would change the
    # staged file set, which P2.2 must not do.
    #
    proc stage_binaries { sys_dir } {
        variable packages

        if {![info exists ::genode_dir]} {
            error "runtime_simple::stage_binaries: genode_dir is not set \
                   (sourced outside a run script?)"
        }
        if {![file isdirectory "${sys_dir}/bin"]} {
            error "runtime_simple::stage_binaries: ${sys_dir}/bin missing — \
                   create the /system tree first"
        }

        set repo "${::genode_dir}/.."
        set staged 0

        foreach {name kind producer} $packages {
            if {$kind eq "payload"} {
                set src "${repo}/${producer}"
                if {![file exists $src]} {
                    error "runtime_simple: payload producer for '${name}' \
                           missing: ${src} (pkg/textedit is prebuilt — see \
                           pkg/textedit/SOURCE)"
                }
                puts "runtime_simple: '${name}' binary is payload-supplied \
                       (${producer}) — staged by bake::stage into \
                       pkg/${name}/payload/, not into bin/"
                continue
            }

            set meta "${repo}/pkg/${name}/metadata.xml"
            if {![file exists $meta]} {
                error "runtime_simple: ${meta} missing"
            }

            #
            # The binary path is what pkgd's config generator emits
            # (binary_prefix "bin/" composed with <binary>), so it is
            # read out of the metadata rather than assumed to equal the
            # package name (same discipline as driver_runtime::stage).
            #
            set fh [open $meta r]
            set body [read $fh]
            close $fh
            if {![regexp {<binary>([^<]+)</binary>} $body -> binary]} {
                error "runtime_simple: ${meta} has no <binary> element"
            }
            set binary [string trim $binary]

            if {![file exists "bin/${binary}"]} {
                error "runtime_simple: package '${name}' needs binary \
                       '${binary}' in bin/ — add '${producer}' to this \
                       scenario's build block"
            }
            exec cp -fL "bin/${binary}" "${sys_dir}/bin/${binary}"
            if {![file exists "${sys_dir}/bin/${binary}"]} {
                error "runtime_simple: ${sys_dir}/bin/${binary} missing \
                       after staging from bin/${binary}"
            }
            incr staged
        }

        puts "runtime_simple: staged ${staged} simple-class binaries \
               into ${sys_dir}/bin (1 payload-supplied, see above)"
    }
}
