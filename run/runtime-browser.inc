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
}
