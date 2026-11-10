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
#   2. runtime_simple::build_targets   — the Genode build targets,
#      including pdf_view's MuPDF shared-library closure (the class
#      map produces only the app binary; the closure is payload);
#   3. runtime_simple::stage_binaries  — fail-loud staging of the
#      class binaries into <sys_dir>/bin/ and of pdf_view's complete
#      payload: the MuPDF closure into <sys_dir>/lib/, the two-page
#      sample.pdf document ROM at <sys_dir>/, and the staged
#      pkg_pdf_view.xml binary="no" transform;
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
# === PDF_VIEW'S REAL PAYLOAD (mupdf closure + sample.pdf) =================
#
# pdf_view's binary is an ordinary build producer (app/pdf_view), but
# the binary alone cannot start. The document-rendering closure and
# the document ROM are RUN-SCENARIO payload (P5.1 evidence,
# docs/evidence/phase16-p51-grouped-app-matrix.md):
#
#   - the MuPDF shared-library closure (mupdf.lib.so + freetype,
#     jpeg, jbig2dec, openjpeg) is built by the closure build
#     targets (build_targets below) and staged into <sys_dir>/lib/.
#     Without it the child stops with "parent denied ROM-session:
#     label=mupdf.lib.so", then label=freetype.lib.so, ... (R13).
#     Stage ONLY the real *.lib.so artifacts — the var/libcache
#     *.abi.so ABI-stability stubs must NEVER reach the media
#     (zlib.abi.so is a 17KB stub vs zlib.lib.so 319KB real; staging
#     the mupdf stub crashed pdf_view with a WRITE violation, R14).
#   - sample.pdf, the metadata VFS <rom name="sample.pdf"/>, is a
#     TWO-PAGE document generated at staging time ("the boot module
#     of that same name MUST be staged by the run scenario" — the
#     metadata's own words): page-down and scroll need a real page
#     flip (R21 pgdn_changed=0, R22 the port blit applies no
#     panx/pany), so the second page paints a filled block.
#   - the staged pkg_pdf_view.xml gains binary="no" on that ROM so
#     the disk-serving path's block-rounded size (632 -> 4096) is
#     trimmed at the first NUL (R17-R20 "cannot find startxref").
#
# All three moved here from run/sponge-desktop-disk.run's local
# recipe so every grouped product-media scenario (BIOS, UEFI, NVMe,
# USB) inherits them single-sourced — same migration shape as the
# files package support ROMs below.
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
    # pdf_view's MuPDF shared-library closure build targets. The
    # class map above produces only the app binary (app/pdf_view);
    # without these five lib targets there is nothing REAL to stage
    # for the document-rendering closure (P5.1 R13 — the lean
    # run/sponge-pdf-view.run builds the same set). Keep this list
    # in sync with the mupdf closure staging list in stage_binaries.
    #
    variable mupdf_closure {
        lib/mupdf lib/openjpeg lib/freetype lib/jbig2dec lib/jpeg
    }

    #
    # The Genode build targets of this class, in map order, plus
    # pdf_view's MuPDF closure. The payload-produced package
    # contributes no target (there is nothing to build) — that
    # absence is exactly the fact this map pins.
    #
    proc build_targets { } {
        variable packages
        variable mupdf_closure
        set targets {}
        foreach {name kind producer} $packages {
            if {$kind eq "build"} { lappend targets $producer }
        }
        lappend targets {*}$mupdf_closure
        return $targets
    }

    #
    # runtime_simple::stage_binaries <sys_dir>
    #
    # Stage the class's binaries into <sys_dir>/bin/ (the directory the
    # scenario stages /system from). MUST run after the scenario's
    # `build {}` block and its `exec mkdir -p <sys_dir>/bin`, and
    # after bake::stage — the pdf_view payload below stages the
    # document ROM and transforms the BAKED pkg_pdf_view.xml.
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
        # --- pdf_view payload: MuPDF closure + document ROM ---
        # (moved here from run/sponge-desktop-disk.run's local recipe
        # so every grouped product-media scenario stages it
        # single-sourced; P5.1 R13/R14/R17-R22 evidence,
        # docs/evidence/phase16-p51-grouped-app-matrix.md.)
        #
        # MuPDF shared-library closure: pdf_view's document-rendering
        # library and its shared closure. Without them pdf_view stops
        # with "parent denied ROM-session: label=mupdf.lib.so", then
        # label=freetype.lib.so, ... (R13). The five real *.lib.so
        # artifacts below are copied from bin/ — the build targets are
        # runtime_simple::build_targets (the lean run's
        # sponge-pdf-view.run builds the same set). Do NOT stage the
        # `*.abi.so` stubs next to the app — those are the small
        # ABI-stability interfaces (compare var/libcache/zlib/:
        # zlib.abi.so 17KB stub vs zlib.lib.so 319KB real), and
        # staging the mupdf stub crashed pdf_view with a WRITE
        # violation (R14 evidence). Both ends of every copy are loud:
        # a missing bin/<lib> is a build-block omission, a missing
        # destination copy is a staging error.
        #
        if {![file isdirectory "${sys_dir}/lib"]} {
            error "runtime_simple: ${sys_dir}/lib missing — \
                   create the /system tree first"
        }
        foreach mupdf_dep { mupdf.lib.so freetype.lib.so jpeg.lib.so \
                            jbig2dec.lib.so openjpeg.lib.so } {
            if {![file exists "bin/${mupdf_dep}"]} {
                error "runtime_simple: mupdf closure lib missing from \
                       the build: bin/${mupdf_dep} (are the \
                       lib/openjpeg lib/freetype lib/jbig2dec \
                       lib/mupdf lib/jpeg build targets in the build \
                       list? runtime_simple::build_targets provides \
                       them)"
            }
            exec cp -fL "bin/${mupdf_dep}" "${sys_dir}/lib/${mupdf_dep}"
            if {![file exists "${sys_dir}/lib/${mupdf_dep}"]} {
                error "runtime_simple: ${sys_dir}/lib/${mupdf_dep} \
                       missing after staging from bin/${mupdf_dep}"
            }
        }

        #
        # sample.pdf: the pdf_view package's document ROM (metadata
        # VFS <rom name="sample.pdf"/>). The run scenario owns the
        # staged bytes (the metadata: "the boot module of that same
        # name MUST be staged by the run scenario"); this generates a
        # TWO-PAGE document instead of copying
        # pkg/pdf_view/payload/sample.pdf:
        #
        #   - pdfapp.c clamps pageno to pagecount and re-renders only
        #     on a real page change, so page-down is a dead input on
        #     a one-page document (R21: pgdn_changed=0);
        #   - the port blit (app/pdf_view main.cc Pdf_view::show)
        #     applies no panx/pany, so scroll alone never changes a
        #     pixel (R22).
        #
        # The P5.1 page-down/scroll input delta therefore needs a
        # real page flip. Page 1 keeps the payload's wording; page 2
        # paints a filled block so the flip is a large unmistakable
        # delta. Pure ASCII (no NUL bytes) and a computed xref: the
        # staged-metadata text-mode rom trim (binary="no") then yields
        # exactly the file bytes inside the padded ROM dataspace and
        # mupdf's startxref scan lands on the real table.
        #
        set sp1 "BT\n/F1 24 Tf\n72 720 Td\n(Hello Sponge PDF) Tj\n0 -32 Td\n(Phase 13 W4 sample.) Tj\nET\n"
        set sp2 "0 0 0 rg\n36 600 540 120 re f\nBT\n/F1 24 Tf\n72 720 Td\n(Sample page two.) Tj\nET\n"
        set obj4 "<< /Length [string length $sp1] >>\nstream\n${sp1}endstream"
        set obj6 "<< /Length [string length $sp2] >>\nstream\n${sp2}endstream"
        set pdf_objs [list \
            {<< /Type /Catalog /Pages 2 0 R >>} \
            {<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>} \
            {<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 7 0 R >> >> >>} \
            $obj4 \
            {<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 6 0 R /Resources << /Font << /F1 7 0 R >> >> >>} \
            $obj6 \
            {<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>}]
        set pdf "%PDF-1.4\n"
        set pdf_offs {}
        set pdf_num 1
        foreach pdf_b $pdf_objs {
            lappend pdf_offs [string length $pdf]
            append pdf "$pdf_num 0 obj\n$pdf_b\nendobj\n"
            incr pdf_num
        }
        set pdf_xref [string length $pdf]
        append pdf "xref\n0 8\n0000000000 65535 f \n"
        foreach pdf_o $pdf_offs {
            append pdf [format "%010d 00000 n \n" $pdf_o]
        }
        append pdf "trailer << /Size 8 /Root 1 0 R >>\nstartxref\n$pdf_xref\n%%EOF\n"
        set pdf_fh [open "$sys_dir/sample.pdf" w]
        fconfigure $pdf_fh -translation binary
        puts -nonewline $pdf_fh $pdf
        close $pdf_fh
        if {![file exists "$sys_dir/sample.pdf"]} {
            error "runtime_simple: ${sys_dir}/sample.pdf missing \
                   after staging"
        }

        #
        # pdf_view's document ROM needs `binary="no"` on the served
        # media (P5.1 evidence): the disk-serving path reports the
        # ext2 block-rounded size (632 -> 4096) and the vfs rom plugin
        # keeps the whole padded dataspace in its default binary mode,
        # so mupdf seeks to size-1024 inside the zero padding and
        # reports "cannot find startxref" (the lean
        # run/sponge-pdf-view.run serves the same file as an
        # exact-size core ROM module and is unaffected). The plugin's
        # text mode trims the content at the first NUL — the document
        # is NUL-free and the block padding is zeros, so the trim
        # yields exactly the 632-byte file. Same staged-metadata
        # transform pattern as the P2.4 clipboard removal above; the
        # repo metadata stays untouched and this media-only fix is
        # recorded as the residual gap.
        #
        # The transform applies to whatever pdf_view metadata the
        # active bake profile stages; a focused profile
        # (SPONGE_APP_ISOLATION's `isolation`) carries no pdf_view
        # and skips it — skip-don't-error.
        #
        if {[file exists "$sys_dir/pkg_pdf_view.xml"]} {
            set pv_fh [open "$sys_dir/pkg_pdf_view.xml" r]
            set pv_xml [read $pv_fh]
            close $pv_fh
            if {[string first {<rom name="sample.pdf"/>} $pv_xml] < 0} {
                error "runtime_simple: pkg_pdf_view.xml: sample.pdf \
                       rom element missing or drifted"
            }
            set pv_xml [string map [list \
                {<rom name="sample.pdf"/>} \
                {<rom name="sample.pdf" binary="no"/>}] $pv_xml]
            set pv_fh [open "$sys_dir/pkg_pdf_view.xml" w]
            puts -nonewline $pv_fh $pv_xml
            close $pv_fh
            puts "runtime_simple: staged pkg_pdf_view.xml — sample.pdf \
                   rom gained binary=\"no\""
        } else {
            puts "runtime_simple: no staged pkg_pdf_view.xml — the \
                   sample.pdf binary=\"no\" transform is skipped \
                   (focused profile without pdf_view)"
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
