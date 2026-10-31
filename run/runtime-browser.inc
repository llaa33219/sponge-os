#
# \brief  runtime_browser — the browser runtime class (Phase 16 P2.2).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# One of three per-runtime-class targets introduced by Phase 16 P2.2
# (docs/plans/phase16-runtime-overhaul.md, "Per-runtime targets"). The
# BROWSER class is the `falkon` package (docs/17-runtime-component-
# topology.md §6): the full Qt6 WebEngine browser, ~509 MiB of payload,
# the heaviest package on the product media.
#
# P2.2 IS EXCLUSION-ONLY FOR THIS CLASS. Falkon's closure is NOT built
# here and NOT staged here — that is P2.6 ("Falkon / browser staging",
# the rom_pkg payload area). What this include owns today:
#
#   1. payload_libs — the EXACT Falkon/WebEngine exclusion list the
#      five desktop-disk scenarios carry inline. The /system/lib staging
#      loop copies every bin/*.lib.so EXCEPT these (they exist only in
#      falkon's payload area, never in /system/lib) plus the Tier-0
#      exceptions the scenario keeps inline (ld-*, ld.lib.so, rump*,
#      vfs_rump).
#   2. exclude_lib  — the fail-loud membership test for that list.
#   3. the class quota row (QUOTA_CAPS / QUOTA_RAM) consumed by
#      run/runtime-quota.inc's pkg_runtime_caps() derivation.
#   4. the shared rom_pkg serving fragment (P2.6): the vfs_sys policy
#      line and the rom_pkg cached_fs_rom start node that serve the
#      payload area as ROM. Wired into ALL FIVE desktop-disk variants;
#      the fragment output is byte-for-byte the former uefi-usb literal
#      (root /system/pkg/falkon/payload, writeable: no, caps 20000 /
#      ram 768M) and is drift-checked by assert_tier0.
#
# It is PURE run-script Tcl: no C++, no package metadata, no topology
# change, and NO staging behavior change — the list below is byte-for-
# byte the `falkon_libs` literal of run/sponge-desktop-disk-uefi-usb.run
# today (verified by the P2.2 static check).
#
# === WHY THE LIST IS EXACT (not derived) =================================
#
# The list is the observed falkon_qt6-jemalloc closure (64 ROMs, Phase
# 7 todo 11 / Phase 8 P4): WebEngine's own Qt modules + Quick/Qml
# plugins + NSS/TLS + the lwip/shm/jitterentropy/xoroshiro/oss vfs
# plugins + ncurses (pulled in transitively). It is enumerated rather
# than globbed because the staging rule is a DENIAL: anything NOT on
# this list ships in /system/lib, so a new WebEngine-private .lib.so
# must be ADDED here on purpose — an implicit pattern match would
# silently move a payload lib (or silently keep out a desktop lib).
#
# === QUOTA DERIVATION ====================================================
#
#   falkon   200000 caps / 1024M   (pkg/falkon/metadata.xml
#                                   <quota ram="1G" caps="200000"/>)
#
# QUOTA_CAPS 200000 is the exact row — input to
# runtime_quota::pkg_runtime_caps()'s single-grant derivation.
#

namespace eval runtime_browser {

    #
    # Class quota row (docs/17 §6 "browser"). The RAM figure is
    # documentation for the pkg_runtime admission math — the grant
    # itself is the remainder clamp (run/runtime-quota.inc).
    #
    variable QUOTA_CAPS 200000
    variable QUOTA_RAM  1024M

    #
    # The Falkon/WebEngine closure — libs that belong to falkon's
    # payload area ONLY and are therefore excluded from /system/lib.
    # Byte-for-byte the `falkon_libs` literal of
    # run/sponge-desktop-disk-uefi-usb.run (P2.2 must not change the
    # staged set).
    #
    variable payload_libs {
        libFalkonPrivate.lib.so
        libQt6WebEngineCore.lib.so
        libQt6WebEngineQuick.lib.so
        libQt6WebEngineWidgets.lib.so
        libQt6WebChannel.lib.so
        libQt6WebChannelQuick.lib.so
        libQt6PrintSupport.lib.so
        libQt6Qml.lib.so
        libQt6QmlMeta.lib.so
        libQt6QmlModels.lib.so
        libQt6QmlWorkerScript.lib.so
        libQt6Quick.lib.so
        libQt6QuickWidgets.lib.so
        libqtquick2plugin.lib.so
        libqtwebenginequickplugin.lib.so
        libworkerscriptplugin.lib.so
        nss3.lib.so
        nssckbi.lib.so
        freebl3.lib.so
        softokn3.lib.so
        libssl.lib.so
        libcrypto.lib.so
        libssh.lib.so
        curl.lib.so
        vfs_lwip.lib.so
        vfs_qtwebengine_shm.lib.so
        vfs_jitterentropy.lib.so
        vfs_xoroshiro.lib.so
        vfs_oss.lib.so
        ncurses.lib.so
    }

    #
    # runtime_browser::exclude_lib <libname>
    #
    # True when <libname> is part of the falkon/WebEngine payload
    # closure and must NOT be staged into /system/lib. The /system/lib
    # staging loop calls this per bin/*.lib.so; the scenario keeps its
    # Tier-0 exceptions (ld-*.lib.so, ld.lib.so, rump.lib.so,
    # rump_fs.lib.so, vfs_rump.lib.so) inline because they are
    # storage-chain properties of the media, not browser properties.
    #
    # Unknown names return false (not excluded): the default stays
    # "ships in /system/lib", so a new desktop lib is never dropped
    # silently.
    #
    proc exclude_lib { libname } {
        variable payload_libs
        return [expr {[lsearch -exact $payload_libs $libname] >= 0}]
    }

    #
    # The closure size — for staging logs and drift checks.
    #
    proc payload_lib_count { } {
        variable payload_libs
        return [llength $payload_libs]
    }

    #
    # === P2.6 rom_pkg payload serving ================================
    #
    # The payload area is served as ROM by a dedicated cached_fs_rom
    # instance (rom_pkg) whose File_system session is rooted at
    # PAYLOAD_ROOT by the vfs policy below. The two fragments are wired
    # into all five desktop-disk variants; the uefi-usb scenario's
    # former inline literals are the reference spelling (P2.6 keeps
    # their bytes: root .../payload, writeable: no, caps 20000 /
    # ram 768M).
    #
    # HID fragment: the File_system policy line. Insert at the
    # scenario's vfs node config, after the rom_lib policy, as
    # `    [runtime_browser::vfs_policy]` (braced-literal + subst
    # idiom). No leading indentation on the first line — the call site
    # supplies it (the driver_runtime.inc fragment convention).
    #
    variable PAYLOAD_ROOT "/system/pkg/falkon/payload"
    variable ROM_PKG_CAPS 20000
    variable ROM_PKG_RAM  768M

    proc vfs_policy {} {
        variable PAYLOAD_ROOT
        return "+ policy | label_prefix: rom_pkg | root: ${PAYLOAD_ROOT} | writeable: no"
    }

    #
    # HID fragment: the rom_pkg start node. Insert at the Tier-0
    # config between the rom_lib node and nitpicker (the uefi-usb
    # slot), as `[runtime_browser::rom_pkg_node <fs-child>]`. The
    # File_system child is the scenario's vfs node name (`vfs_sys` on
    # uefi-usb media, `vfs` on the AHCI/UEFI variants) — the only
    # per-scenario element of the fragment.
    #
    proc rom_pkg_node {fs_child} {
        variable ROM_PKG_CAPS
        variable ROM_PKG_RAM
        return "+ start rom_pkg | caps: ${ROM_PKG_CAPS} | ram: ${ROM_PKG_RAM}
  + binary cached_fs_rom
  + provides | + service ROM
  + route
    + service File_system | + child ${fs_child}
    + service ROM | label_last: ld.lib.so | + parent
    + any-service
      + parent
      + any-child"
    }

    #
    # runtime_browser::assert_tier0 <tier0_config_text> <fs_child>
    #
    # Fail-loud structural check for the rom_pkg wiring: the vfs policy
    # line and every rom_pkg node line must be present verbatim, and
    # the node must sit after rom_lib (same slot as the uefi-usb
    # reference). Called by every desktop-disk scenario alongside
    # runtime_grouped::assert_tier0.
    #
    proc assert_tier0 { config_text fs_child } {
        variable PAYLOAD_ROOT

        set policy [vfs_policy]
        if {[string first $policy $config_text] < 0} {
            error "runtime_browser: structural assertion failed — the rom_pkg \
                   vfs policy is missing (expected '${policy}')"
        }
        foreach line [split [rom_pkg_node $fs_child] "\n"] {
            if {[string first $line $config_text] < 0} {
                error "runtime_browser: structural assertion failed — rom_pkg \
                       node line is missing (expected '${line}')"
            }
        }

        set rom_pkg_pos [string first "+ start rom_pkg |" $config_text]
        set rom_lib_pos [string first "+ start rom_lib |" $config_text]
        if {$rom_lib_pos < 0 || $rom_pkg_pos < 0 || $rom_pkg_pos < $rom_lib_pos} {
            error "runtime_browser: structural assertion failed — the rom_pkg \
                   node must follow the rom_lib node"
        }

        puts "runtime_browser: rom_pkg payload serving verified \
               (root ${PAYLOAD_ROOT}, File_system child '${fs_child}')"
        return
    }
}
