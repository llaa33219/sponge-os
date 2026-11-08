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
# Consequence for staging: stage_binaries copies the producer into
# <sys_dir>/bin/textedit (its filename) and textedit.config to the
# <sys_dir>/ root. The generated launcher resolves the binary through
# binary_prefix="bin/" (docs/12 §4), and the app requests
# /textedit.config at the /system root. The dynamic linker uses the
# scenario's freshly built shared libraries from <sys_dir>/lib/;
# textedit's prebuilt payload libraries must NOT overwrite that shared
# namespace, because sponge-de and the other simple-class Qt components
# load the same bare names. A payload library is added only when the
# already-staged shared set does not provide that name; presence in the
# build directory alone is insufficient because scenario exclusions may
# deliberately keep a library off the media. bake::stage separately
# carries the payload tree under
# pkg/textedit/payload/.
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
    #   payload <path>    — a committed prebuilt payload file; its
    #                       binary is copied into <sys_dir>/bin/ under
    #                       its filename (binary_prefix="bin/"
    #                       resolution); bake::stage also carries the
    #                       payload tree (see the textedit note in
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
    # payload-kind packages: the repository producer is copied into
    # <sys_dir>/bin/ under its filename — the generated launcher
    # resolves the binary through binary_prefix="bin/" (docs/12 §4) —
    # and the root-level textedit.config into <sys_dir>/ — because the
    # app requests it at the /system root (rom_sys /textedit.config).
    # The optional clipboard attribute is removed from both that file
    # and the staged package metadata, whose root config is what pkgd
    # emits for the application. Payload libraries are added only when
    # the already-staged shared set lacks the bare name; an existing
    # shared library is never overwritten by a prebuilt payload copy.
    # Both ends of every copy are loud: a missing producer is a repo
    # error, a missing copy is a staging error. bake::stage separately
    # carries the payload tree under pkg/<name>/payload/.
    #
    # files package support ROMs: every product /system also receives
    # files_demo.tar (packed from run/fixtures/files-demo/) and an
    # empty files_request, so the files package resolves both ROMs on
    # UEFI/NVMe/USB media too (not just the BIOS disk scenario that
    # used to carry its own copy of this recipe). Fail-loud on a
    # missing fixture directory.
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
        set staged_libs 0
        set shared_libs 0

        foreach {name kind producer} $packages {
            if {$kind eq "payload"} {
                set src "${repo}/${producer}"
                if {![file exists $src]} {
                    error "runtime_simple: payload producer for '${name}' \
                           missing: ${src} (pkg/textedit is prebuilt — see \
                           pkg/textedit/SOURCE)"
                }
                set dst "${sys_dir}/bin/[file tail $producer]"
                exec cp -fL $src $dst
                if {![file exists $dst]} {
                    error "runtime_simple: ${dst} missing \
                           after staging from ${src}"
                }
                if {![file isdirectory "${sys_dir}/lib"]} {
                    error "runtime_simple: ${sys_dir}/lib missing — \
                           create the /system tree first"
                }
                set payload_dir "[file dirname $src]"
                foreach lib [lsort [glob -nocomplain -directory $payload_dir *.lib.so]] {
                    set libname "[file tail $lib]"
                    set ldst "${sys_dir}/lib/${libname}"
                    if {[file exists $ldst]} {
                        incr shared_libs
                        continue
                    }
                    exec cp -fL $lib $ldst
                    if {![file exists $ldst]} {
                        error "runtime_simple: ${ldst} missing \
                               after staging from ${lib}"
                    }
                    incr staged_libs
                }
                set cfg "[file dirname $src]/textedit.config"
                if {![file exists $cfg]} {
                    error "runtime_simple: payload config for '${name}' \
                           missing: ${cfg}"
                }
                set app_cfg "${sys_dir}/textedit.config"
                set metadata_cfg "${sys_dir}/pkg_textedit.xml"
                exec cp -fL $cfg $app_cfg
                if {![file exists $app_cfg]} {
                    error "runtime_simple: ${app_cfg} missing \
                           after staging from ${cfg}"
                }
                #
                # Remove the optional clipboard ATTRIBUTE from BOTH
                # staged configs. The package metadata's root attribute
                # is copied by sponge_pkgd into the generated application
                # config and enables QGenodeClipboard; the payload config
                # carries the same optional attribute. These scenarios
                # offer no clipboard provider, so leaving either enabled
                # can stop textedit on rom_sys: /clipboard not found.
                # Only the root elements change; source metadata and
                # clipboard-enabled scenarios are not modified.
                #
                foreach cfgdst [list $app_cfg $metadata_cfg] {
                    if {![file exists $cfgdst]} {
                        error "runtime_simple: clipboard transform input \
                               missing: ${cfgdst} (run after bake::stage)"
                    }
                    set fh [open $cfgdst r]
                    set cbody [read $fh]
                    close $fh
                    set clip_anchor {<config clipboard="yes">}
                    set n 0
                    set idx 0
                    while {[set idx [string first $clip_anchor $cbody $idx]] >= 0} {
                        incr n
                        incr idx [string length $clip_anchor]
                    }
                    if {$n != 1} {
                        error "runtime_simple: expected exactly one \
                               clipboard=yes anchor in ${cfgdst}, found $n"
                    }
                    set cbody [string map \
                        [list $clip_anchor {<config>}] $cbody]
                    set fh [open $cfgdst w]
                    puts -nonewline $fh $cbody
                    close $fh
                }
                puts "runtime_simple: '${name}' binary is payload-supplied \
                       (${producer}) — copied to ${dst} and ${app_cfg}; \
                       clipboard disabled in app and package configs"
                incr staged
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

        #
        # files package support ROMs (moved here from
        # run/sponge-desktop-disk.run so every grouped product-media
        # scenario — BIOS, UEFI, NVMe, USB — stages them, single-
        # sourced; the standalone recipes in run/sponge-files.run and
        # run/sponge-de-workflow.run stay where they are).
        #
        # files_demo.tar: the files package's read-only /demo fixture
        # (pkg/files/metadata.xml VFS mounts <tar name="files_demo.tar"/>).
        # Staged by the same recipe as run/sponge-files.run lines 216-235 —
        # without it the files child dies at startup with
        # "parent denied ROM-session: label=files_demo.tar" (P5.1 R6
        # evidence, docs/evidence/phase16-p51-grouped-app-matrix.md).
        #
        set demo_src "${repo}/run/fixtures/files-demo"
        if {![file isdirectory $demo_src]} {
            error "runtime_simple: demo fixture directory not found: \
                   $demo_src"
        }
        set staging "[run_dir]/files-demo-staging"
        file delete -force $staging
        file mkdir "$staging/demo"
        foreach f [glob -directory $demo_src *] {
            file copy -force -- $f "$staging/demo/"
        }
        exec tar -cf "$sys_dir/files_demo.tar" -C "$staging" demo
        file delete -force $staging
        if {![file exists "$sys_dir/files_demo.tar"]} {
            error "runtime_simple: ${sys_dir}/files_demo.tar missing \
                   after staging from ${demo_src}"
        }

        #
        # files_request: the files package's automation channel ROM (a
        # report_rom relay in run/sponge-files.run line 117, written by its
        # probe). On product media no writer exists, but the ROM itself must
        # RESOLVE or the child stops with "parent denied ROM-session:
        # label=files_request" (P5.1 R7 evidence). An empty file behind the
        # rom_sys catch-all serves an empty ROM, which the app tolerates
        # (files_window.cc _poll_request returns on empty/invalid XML). The
        # driven channel remains proven in run/sponge-files.run.
        #
        exec touch "$sys_dir/files_request"
        if {![file exists "$sys_dir/files_request"]} {
            error "runtime_simple: ${sys_dir}/files_request missing \
                   after touch"
        }

        puts "runtime_simple: staged ${staged} simple-class binaries \
               into ${sys_dir}/bin; added ${staged_libs} missing payload \
               libs and reused ${shared_libs} shared libs"
    }
}
