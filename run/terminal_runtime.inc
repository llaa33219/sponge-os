#
# \brief  Shared terminal-runtime build + staging blocks (Phase 14 W11, #33)
# \author Sponge OS contributors
# \date   2026
#
# Phase 13 introduced the terminal package's bash + UNIX toolset vfs
# block (the noux-pkg closures for bash-minimal/vim-minimal/coreutils-
# minimal/grep/sed/tar/less/findutils/diffutils/which plus the
# libc/libm/posix/ncurses/pcre libs, the vfs/vfs_pipe/vfs_ttf vfs
# plugins, and the gems terminal server's `server/terminal` + vfs
# `<terminal/>` plugin `server/vfs` + bash-minimal.tar loader
# `server/cached_fs_rom`). Three run scenarios share this exact block:
#
#   - run/sponge-terminal.run             (base-linux + base-sel4 headless)
#   - run/sponge-terminal-qmp.run         (base-sel4 + QMP keyboard)
#   - run/sponge-usb-kbd-via-qmp.run      (base-sel4 + QMP USB hotplug)
#
# The block also includes the three boot-module extras that are NOT
# build targets but must be staged into bin/ BEFORE build_boot_image
# (base-sel4 packs a single image.elf from bin/ — modules added after
# `build_boot_image` never reach the guest; docs/11-environment.md
# §10.4):
#
#   - pkg_terminal.xml   (sponge_pkgd reads it to resolve `terminal`)
#   - pkg_index.xml      (the on-image repository index pkgd reads)
#   - VeraMono.ttf       (gems terminal server's monospace font;
#                        loaded by the vfs_ttf plugin from a ROM)
#
# Before this include existed, the three scenarios each duplicated the
# build tail, the vera + pkg metadata staging, and the boot-modules
# append verbatim. Adding a new CLI tool to the toolset required
# updating three files; forgetting one silently broke that scenario's
# noux toolset (Phase 13's `coreutils-minimal` carryover commit hit
# this exact drift in the W1 scenario).
#
# Usage:
#
#   build { core lib/ld init timer server/report_rom server/nitpicker \
#           [scenario-specific prefixes ...] \
#           [source [file join [file dirname [file normalize [info script]]] terminal_runtime.inc]] }
#
#   source [file join [file dirname [file normalize [info script]]] \
#                       terminal_runtime.inc]
#
#   terminal_stage_vera_metadata "bin"   ;# stages VeraMono.ttf, pkg_terminal.xml, pkg_index.xml
#
#   set boot_modules [build_artifacts]
#   terminal_append_boot_modules boot_modules
#   build_boot_image $boot_modules
#
# The include is intentionally NOT sourced automatically — each
# scenario sources it explicitly so its position in the build list
# (after the scenario-specific driver prefix, before the closure) is
# always explicit and readable.
#
# Failure mode (loud, not silent): if the include file is missing or
# cannot be parsed, the scenario's `source` call raises a Tcl error
# before any QEMU is started — never a silent drift.
#
# Procs provided:
#
#   terminal_runtime::copy_vera_font <dst>
#       THE single VeraMono.ttf producer. Copies the font out of the
#       prepared ttf-bitstream-vera port to <dst>, loudly on both ends
#       of the copy. Used by stage_vera_metadata below AND by the
#       product media payload stage (run/runtime-terminal.inc,
#       runtime_terminal::stage_payload), so the terminal scenarios and
#       the product media can never drift apart again (the §2 H-a1
#       class of bug: one staging path had the font, the other had a
#       silent skip).
#
#   terminal_stage_vera_metadata <bin_dir>
#       Stages pkg_terminal.xml, pkg_index.xml, and VeraMono.ttf into
#       the scenario's bin_dir. Errors loudly if the pkg metadata or
#       ttf-bitstream-vera port is missing. Idempotent — safe to call
#       once per scenario.
#
#   terminal_append_boot_modules <boot_modules_var>
#       Appends VeraMono.ttf, pkg_terminal.xml, pkg_index.xml to the
#       caller's boot-modules list variable (passed by name). The
#       caller is expected to pass the output of [build_artifacts].
#
# Variables exposed (caller may override):
#
#   terminal_runtime_extra_packages  — default Tcl list of build-target
#                                     packages in the shared tail
#                                     (server/terminal server/vfs
#                                     server/cached_fs_rom + noux-pkg/*
#                                     + lib/libc lib/libm lib/posix
#                                     lib/ncurses lib/pcre + lib/vfs
#                                     lib/vfs_pipe lib/vfs_ttf).
#   terminal_runtime_extra_libs      — default Tcl list of test/libs
#                                     (test/terminal_probe lib/blit).
#                                     Override per-scenario to add
#                                     probe siblings (e.g. test/usb_hid_probe).
#   terminal_runtime_staged_pkg      — default "terminal".
#                                     Override per-scenario for
#                                     scenarios staging other packages.
#

namespace eval terminal_runtime {

    variable extra_packages {
        server/terminal server/vfs server/cached_fs_rom
        noux-pkg/bash-minimal noux-pkg/vim-minimal
        noux-pkg/coreutils-minimal noux-pkg/grep noux-pkg/sed
        noux-pkg/tar noux-pkg/less noux-pkg/findutils
        noux-pkg/diffutils noux-pkg/which
        lib/libc lib/libm lib/posix lib/ncurses lib/pcre
        lib/vfs lib/vfs_pipe lib/vfs_ttf
    }

    variable extra_libs {
        test/terminal_probe lib/blit
    }

    variable staged_pkg "terminal"

    variable staged_metadata_file "pkg_terminal.xml"
    variable staged_index_file    "pkg_index.xml"
    variable staged_font_file     "VeraMono.ttf"

    #
    # THE single VeraMono.ttf producer.
    #
    # terminal_runtime::copy_vera_font <dst>
    #
    # Copies VeraMono.ttf out of the prepared ttf-bitstream-vera port
    # to <dst>. Loud on BOTH ends of the copy (Phase 16 §2 H-a1): a
    # missing source is an unprepared port, a missing destination is a
    # broken staging step — neither may pass silently. Every staging
    # path that ships the font funnels through this one proc so the
    # producer exists exactly once in the tree:
    #
    #   - the terminal scenarios' boot-module staging (via
    #     stage_vera_metadata below), and
    #   - the product media's flat payload (via
    #     runtime_terminal::stage_payload in run/runtime-terminal.inc).
    #
    proc copy_vera_font { dst } {
        variable staged_font_file

        if {![info exists ::genode_dir]} {
            error "terminal_runtime::copy_vera_font: \
                   genode_dir is not set (sourced outside a run script?)"
        }

        set vera_dir [lindex [glob -nocomplain \
            "${::genode_dir}/contrib/ttf-bitstream-vera-*/ttf/bitstream-vera"] 0]
        if {$vera_dir eq "" || ![file exists "$vera_dir/${staged_font_file}"]} {
            error "${staged_font_file} source missing — ttf-bitstream-vera \
                   port not prepared; run: \
                   ./genode/tool/ports/prepare_port ttf-bitstream-vera"
        }
        exec cp -fL "$vera_dir/${staged_font_file}" $dst
        if {![file exists $dst]} {
            error "terminal_runtime::copy_vera_font: ${dst} missing after \
                   copy from ${vera_dir}"
        }
        return $dst
    }

    #
    # Stage the package metadata + index + VeraMono.ttf into the
    # caller's bin/ directory. Mirrors the scheme in
    # run/sponge-terminal.run (which this include replaces).
    #
    proc stage_vera_metadata { bin_dir } {
        variable staged_pkg
        variable staged_metadata_file
        variable staged_index_file
        variable staged_font_file

        if {![info exists ::genode_dir]} {
            error "terminal_runtime::stage_vera_metadata: \
                   genode_dir is not set (sourced outside a run script?)"
        }

        set src "${::genode_dir}/../pkg/${staged_pkg}/metadata.xml"
        if {![file exists $src]} {
            error "package metadata not found: $src"
        }
        exec cp -L $src "${bin_dir}/${staged_metadata_file}"

        set index_fh [open "${bin_dir}/${staged_index_file}" w]
        puts $index_fh "<packages>"
        puts $index_fh "  <pkg name=\"${staged_pkg}\"/>"
        puts $index_fh "</packages>"
        close $index_fh

        copy_vera_font "${bin_dir}/${staged_font_file}"

        return $staged_metadata_file
    }
}

#
# Public API. The public procs live in the global namespace so the
# caller's `upvar` resolution works in one hop — the namespace procs
# below are package-internal helpers and not intended for direct
# invocation. The wrappers expose the minimum surface area the
# scenarios need (no namespace-qualified procs in the scenarios).
#
proc terminal_stage_vera_metadata { bin_dir } {
    terminal_runtime::stage_vera_metadata $bin_dir
}

proc terminal_append_boot_modules { boot_modules_var } {
    upvar 1 $boot_modules_var modules
    foreach m [list $terminal_runtime::staged_font_file \
                    $terminal_runtime::staged_metadata_file \
                    $terminal_runtime::staged_index_file] {
        lappend modules $m
    }
    return $modules
}