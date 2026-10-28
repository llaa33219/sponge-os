#
# \brief  runtime_terminal — the terminal runtime class (Phase 16 P2.2).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# One of three per-runtime-class targets introduced by Phase 16 P2.2
# (docs/plans/phase16-runtime-overhaul.md, "Per-runtime targets"). The
# TERMINAL class is the `terminal` package (docs/17-runtime-component-
# topology.md §6): a nested sub-init (pkg/terminal/metadata.xml,
# <binary>init</binary>) hosting the gems terminal server, a vfs with
# the <terminal/> plugin, a cached_fs_rom for the bash-minimal.tar
# toolset, and the noux /bin/bash child.
#
# This include owns, for that class and for that class only:
#
#   1. build_targets  — the class build list, REUSED from
#      terminal_runtime::extra_packages (run/terminal_runtime.inc) so
#      the toolset lives in exactly one place. Adding a CLI tool is one
#      edit there, and every terminal scenario plus this product path
#      pick it up — the Phase 13 W1 drift this shared block exists to
#      prevent.
#   2. stage_binaries — the sub-init children's binaries into
#      <sys_dir>/bin/, fail-loud.
#   3. stage_payload  — the flat ROM payload (10 noux toolset tars +
#      vct-bin.tar + VeraMono.ttf) into <sys_dir>/, fail-loud. This is
#      the exact product payload set of
#      run/sponge-desktop-disk-uefi-usb.run today, moved verbatim.
#   4. the class quota row (QUOTA_CAPS / QUOTA_RAM) consumed by
#      run/runtime-quota.inc's pkg_runtime_caps() derivation.
#
# It is PURE run-script Tcl: no C++, no package metadata, no topology
# change. Until P2.4 groups the runtimes, pkg_runtime stays one node.
#
# === SOURCE ORDER ========================================================
#
# This include sources run/terminal_runtime.inc itself (below). A
# scenario that also wants the terminal-scenario helpers
# (terminal_stage_vera_metadata, terminal_append_boot_modules) may
# source terminal_runtime.inc directly as well — the include is
# idempotent (plain namespace variables + proc definitions).
#
# === THE VCT-BIN PRODUCER (kept in the build dir) ========================
#
# vct-bin.tar carries the /bin/vct in-terminal front-end for the
# vct_tty bridge. Its producer is reproduced VERBATIM from the product
# scenario: the script and the .vct-stage scratch tree stay inside the
# repository/build dir (the run tool's cwd), and the tar lands in
# bin/vct-bin.tar like every other build-produced payload. It is NOT
# relocated to a temp dir — the build dir is the auditable place where
# every staged tar comes from.
#
# === QUOTA DERIVATION ====================================================
#
#   terminal   1100 caps / 224M   (pkg/terminal/metadata.xml
#                                  <quota ram="224M" caps="1100"/>)
#
# QUOTA_CAPS 1100 is the exact row — input to
# runtime_quota::pkg_runtime_caps()'s single-grant derivation.
#
# === VCT_TTY IS DELIBERATELY ABSENT ======================================
#
# The vct_tty component is uefi-usb-specific scenario wiring (the
# in-terminal vct bridge), not part of the terminal package class. It
# stays inline in the scenario; this include never stages or builds it.
#

# Reuse the shared terminal toolset — never a second copy of the list.
source [file join [file dirname [file normalize [info script]]] \
                    terminal_runtime.inc]

namespace eval runtime_terminal {

    #
    # Class quota row (docs/17 §6 "terminal"). Consumed by
    # runtime-quota.inc; the RAM figure is documentation for the
    # pkg_runtime admission math — the grant itself is the remainder
    # clamp (run/runtime-quota.inc).
    #
    variable QUOTA_CAPS 1100
    variable QUOTA_RAM  224M

    #
    # The terminal package's nested sub-init children whose binaries
    # must be disk-served (pkg/terminal/metadata.xml <start> nodes;
    # vfs_rom names its binary `cached_fs_rom` explicitly). Their ELF
    # files resolve via pkg_runtime's ROM routing — on disk media that
    # reaches rom_sys chrooted at /system, so each needs a
    # /system/bin/<binary> copy. vct_tty is NOT here on purpose (see
    # the header).
    #
    variable sub_init_binaries {
        terminal
        vfs
        cached_fs_rom
    }

    #
    # The flat ROM payload of the product media, in staging order:
    # the noux toolset tars derived from terminal_runtime's toolset
    # list (one source of truth — the same list that drives build_targets)
    # + the vct bridge tar + the terminal font.
    #
    # Expected set (run/sponge-desktop-disk-uefi-usb.run today):
    #   bash-minimal.tar vim-minimal.tar coreutils-minimal.tar
    #   grep.tar sed.tar tar.tar less.tar findutils.tar
    #   diffutils.tar which.tar vct-bin.tar VeraMono.ttf
    #
    proc payload_files { } {
        set files {}
        foreach t $terminal_runtime::extra_packages {
            if {[string match "noux-pkg/*" $t]} {
                lappend files "[file tail $t].tar"
            }
        }
        lappend files vct-bin.tar
        lappend files $terminal_runtime::staged_font_file
        return $files
    }

    #
    # The Genode build targets of this class: the shared toolset list
    # from run/terminal_runtime.inc, reused — never duplicated here.
    #
    proc build_targets { } {
        return $terminal_runtime::extra_packages
    }

    #
    # runtime_terminal::stage_binaries <sys_dir>
    #
    # Stage the sub-init children's binaries into <sys_dir>/bin/. MUST
    # run after the scenario's `build {}` block and its
    # `exec mkdir -p <sys_dir>/bin`. Fail-loud on both ends of each
    # copy: a missing bin/<binary> is a build-block omission, a missing
    # destination is a broken staging step. No silent file-exists skip
    # (the §2 H-a1 anti-pattern).
    #
    proc stage_binaries { sys_dir } {
        variable sub_init_binaries

        if {![file isdirectory "${sys_dir}/bin"]} {
            error "runtime_terminal::stage_binaries: ${sys_dir}/bin missing — \
                   create the /system tree first"
        }

        foreach b $sub_init_binaries {
            if {![file exists "bin/$b"]} {
                error "runtime_terminal::stage_binaries: bin/$b missing — \
                       add the terminal toolset targets \
                       (runtime_terminal::build_targets) to this scenario's \
                       build block"
            }
            exec cp -fL "bin/$b" "${sys_dir}/bin/$b"
            if {![file exists "${sys_dir}/bin/$b"]} {
                error "runtime_terminal::stage_binaries: ${sys_dir}/bin/$b \
                       missing after staging from bin/$b"
            }
        }

        puts "runtime_terminal: staged [llength $sub_init_binaries] \
               sub-init binaries into ${sys_dir}/bin"
    }

    #
    # runtime_terminal::stage_payload <sys_dir>
    #
    # Stage the flat ROM payload (10 noux toolset tars + vct-bin.tar +
    # VeraMono.ttf) into <sys_dir>/. These resolve through rom_sys at
    # the /system root (same layout as the qt6 tars): the sub-init's
    # vfs child mounts the noux tars as ROM sessions
    # (<tar name="..."/>), and the terminal server loads VeraMono.ttf
    # via the vfs_ttf rom plugin.
    #
    # Order (identical to the product scenario today):
    #   1. vct-bin producer — script + .vct-stage scratch in the build
    #      dir, tar into bin/vct-bin.tar;
    #   2. VeraMono.ttf producer — terminal_runtime::copy_vera_font
    #      (THE single producer, shared with the terminal scenarios),
    #      bin/ intermediate then staged;
    #   3. fail-loud copy of every payload file from bin/ into <sys_dir>.
    #
    # Every copy is loud on both ends — a silent file-exists skip is
    # exactly what hid the §2 H-a1 font gap (only build-dir residue
    # masked it), and this product path must never reproduce that.
    #
    proc stage_payload { sys_dir } {
        if {![file isdirectory $sys_dir]} {
            error "runtime_terminal::stage_payload: ${sys_dir} missing — \
                   create the /system tree first"
        }

        #
        # 1. vct-bin.tar producer (kept inside the repository/build
        #    dir — see the header).
        #
        exec mkdir -p .vct-stage/bin
        set vct_fd [open .vct-stage/bin/vct w]
        puts $vct_fd {#!/bin/bash
# vct - in-terminal front-end; the vct_tty component answers on /dev/vct
if [ $# -eq 0 ]; then
	echo "usage: vct <explain|install|remove|list> [package]"
	exit 0
fi
printf '%s\n' "$*" > /dev/vct
while IFS= read -r line; do
	printf '%s\n' "$line"
	case "$line" in
		"=== vct: done ===") break ;;
	esac
done < /dev/vct}
        close $vct_fd
        exec chmod +x .vct-stage/bin/vct
        exec tar -cf bin/vct-bin.tar -C .vct-stage bin
        if {![file exists "bin/vct-bin.tar"]} {
            error "runtime_terminal::stage_payload: bin/vct-bin.tar missing \
                   after the vct-bin producer ran"
        }

        #
        # 2. VeraMono.ttf — the shared producer (run/terminal_runtime.inc
        #    terminal_runtime::copy_vera_font), then the staging hop. The
        #    bin/ intermediate is kept: it is the producer's output in
        #    the build dir, exactly as the product scenario stages it.
        #
        terminal_runtime::copy_vera_font "bin/VeraMono.ttf"
        exec cp -fL "bin/VeraMono.ttf" "${sys_dir}/VeraMono.ttf"
        if {![file exists "${sys_dir}/VeraMono.ttf"]} {
            error "runtime_terminal::stage_payload: ${sys_dir}/VeraMono.ttf \
                   missing after staging from bin/VeraMono.ttf"
        }

        #
        # 3. The payload set, fail-loud (the tars are build-produced by
        #    the noux-pkg targets in build_targets — a missing one is a
        #    build-block omission, never a silent skip).
        #
        set staged 0
        foreach t [payload_files] {
            if {$t eq $terminal_runtime::staged_font_file} {
                # staged by the producer above
                incr staged
                continue
            }
            if {![file exists "bin/$t"]} {
                error "runtime_terminal::stage_payload: bin/$t missing — \
                       the terminal toolset build targets \
                       (runtime_terminal::build_targets) must run first"
            }
            exec cp -fL "bin/$t" "${sys_dir}/$t"
            if {![file exists "${sys_dir}/$t"]} {
                error "runtime_terminal::stage_payload: ${sys_dir}/$t \
                       missing after staging from bin/$t"
            }
            incr staged
        }

        puts "runtime_terminal: staged ${staged} payload files \
               (10 noux toolset tars + vct-bin.tar + VeraMono.ttf) \
               into ${sys_dir}"
    }
}
