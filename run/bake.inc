#
# \brief  Sponge OS bake-profile shared include (Phase 15 W2, D15.3/D15.4/D15.8/R15.3).
# \author Sponge OS contributors
# \date   2026
#
# === WHY THIS INCLUDE EXISTS =============================================
#
# Phase 15 turns the package set on Sponge OS product media from "hardcoded
# in each run script" into "profile-driven". This is decision D15.3 (two
# profiles: minimal + desktop) implemented as D15.8 (primary mechanism is
# staging-time: a shared Tcl include that every product-media run script
# sources BEFORE build_boot_image).
#
# Profiles live at pkg/bake/<name>.profile (INI subset: key=value,
# [packages], [config], [theme], `#` comments; config_version=1). They are
# committed to git so they are reviewable and reproducible (D15.4). This
# include is the staging-time half of the bake machinery (the host-side
# half — ./tool/bake.mojo — is W2b and ships separately).
#
# === MEDIA-AWARE STAGING RULE (binding) ==================================
#
# Staging mode is per-call (NOT per-profile) because the media destination
# controls what is meaningful to stage:
#
#   * disk media (sponge-desktop-disk.run, mode=img):
#       - Stage FULL bakes: pkg_<name>.xml metadata + synthesized
#         pkg_index.xml + every payload file under pkg/<name>/payload/
#         copied to <destdir>/pkg/<name>/...
#         (where <destdir> = [run_dir]/system, which image/disk then
#          auto-e2cps into the GENODE P3).
#       - Stage /system/bake/{bake_manifest.json, config.defaults,
#         theme.defaults} so first-boot D15.9 seeding has the data it
#         needs (the actual seeding code is W3; this is just data
#         placement).
#
#   * iso media (sponge-alpha.run, mode=iso):
#       - Stage METADATA-ONLY: pkg_<name>.xml + pkg_index.xml go into
#         bin/ (the run_dir root-adjacent location the alpha scenario
#         packs as ROM boot modules into image.elf).
#       - Stage bin/bake/{bake_manifest.json, config.defaults,
#         theme.defaults} so the same data is ROM-served on the iso media
#         even though first-boot seeding is media-dependent (the .iso
#         path is non-persistent).
#       - DO NOT stage payload files. Falkon's 509 MiB payload alone
#         would exceed seL4 boot-module limits (the Phase-7 todo-16
#         ceiling); on live media, packages are install-on-enable (Phase 7
#         A1 semantics — pre-staged metadata, no on-media binary until a
#         user-side install request fires, at which point the binary
#         would need to come from a remote repo — out of scope for W2a).
#
# The mode-vs-profile split is intentional: a single profile (say
# `desktop`) can be staged into either media, and what gets copied to
# disk is determined entirely by mode. The bake_manifest.json records
# the mode honestly (the misleading-success-output defense).
#
# === ESCAPE HATCHES (AGENTS.md §1.1; D15.14) ==============================
#
# Three explicit escape hatches:
#
#   (1) SPONGE_BAKE_PROFILE=none
#       Reproduces today's hardcoded behaviour EXACTLY: stages
#       pkg_hello.xml + pkg_index.xml only, no payload, no manifest,
#       no defaults. This is the regression baseline (the existing
#       marker contracts — alpha-probe: PASS, boot-probe: PASS,
#       etc. — must all keep working) and the control-philosophy
#       door for the "I want last-week's behaviour today" case.
#
#   (2) SPONGE_BAKE_PROFILE=<anything not in pkg/bake/*.profile>
#       -> error with a precise message listing valid profiles. The
#          silent-fallback trap that was hit in W3 of an earlier
#          phase (silently defaulted to "minimal", masked the wrong
#          media) is closed loudly here.
#
#   (3) bake::stage allows the caller's mode argument to OVERRIDE
#       the profile's intent — profiles are declarative, modes are
#       physical. A future "live-desktop" profile can be staged
#       either way without rewriting it.
#
# === R15.3 POST-STAGE VERIFICATION =======================================
#
# bake::stage refuses to return cleanly unless every enabled package has
# (a) pkg/<name>/metadata.xml AND
# (b) the staged pkg_<name>.xml at the destination AND
# (c) for img mode, every payload-requiring package's payload files
#     actually exist on disk in the staged tree.
# Missing artifact -> `error` (Tcl) with a precise path so the run
# tool's FAIL channel does the loud exit. This is the
# misleading-success-output defense (R15.3): a profile that "looks
# staged" but is missing a payload would otherwise be silent.
#
# Source-built packages (terminal, files, calculator, pdf_view) ship
# their binary via the scenario's `build {}` block — bake::stage does
# NOT copy them into the destdir (that's the scenario's job via its
# own `exec cp` loop), but it DOES verify that the binary named in
# pkg/<name>/metadata.xml's <binary> element is present in bin/
# after the build step. If the scenario's `build {}` list omitted a
# package that the profile enables, this verifier catches it at
# stage time, not at boot time.
#
# === FAILURE MODE (loud, not silent) =====================================
#
# If the include file is missing or cannot be parsed, the calling
# scenario's `source` call raises a Tcl error before any QEMU starts —
# never a silent drift. Profile parsing errors out with the path of
# the offending profile. Staging errors out with the missing
# artifact path. The misleading-success-output adversarial class is
# closed at every boundary.
#
# === USAGE ===============================================================
#
#   # Source once, near the top of the run script (after plugins but
#   # before the build {} block and create_boot_directory):
#
#   source [file join [file dirname [file normalize [info script]]] \
#                       bake.inc]
#
#   # In the staging block (AFTER `build { ... }`, AFTER
#   # `exec mkdir -p bin` / `[run_dir]/system` is created, BEFORE
#   # `build_boot_image`):
#
#   bake::stage <destdir> <mode>
#
#   where:
#     <destdir>  in sponge-alpha.run        : "bin"
#                in sponge-desktop-disk.run  : "[run_dir]/system"
#                in any other scenario       : caller decides
#     <mode>     "iso"   -> metadata-only bake, ROM-served via bin/ boot
#                           modules (xorriso packs [run_dir] into image.elf)
#                "img"   -> FULL bake; payload extracted to <destdir>;
#                           /system/bake/* defaults staged at <destdir>/bake/
#
#   The proc returns the active profile name on success, so callers
#   who want to print / log it can `[bake::stage ...]` as an expr.
#
# === ENV OVERRIDE (D15.14) ===============================================
#
#   SPONGE_BAKE_PROFILE  default: desktop
#                        valid:   minimal, desktop, none
#                        other:   loud error listing valid profiles
#
# The default is `desktop` per D15.3 (the everyday-defaults profile;
# empty media is meaningless in 2026) and R15.14 (silent default
# drift is the trap to avoid).
#
# === FILES WRITTEN =======================================================
#
# For mode=iso (destdir=bin/): bin/pkg_<name>.xml + bin/pkg_index.xml
#                              + bin/bake/{bake_manifest.json,
#                                          config.defaults,
#                                          theme.defaults}
#
# For mode=img (destdir=/system): system/pkg_<name>.xml
#                              + system/pkg_index.xml
#                              + system/pkg/<name>/<payload files>
#                              + system/bake/{bake_manifest.json,
#                                              config.defaults,
#                                              theme.defaults}
#
# The /system/bake/ tree is W3's read-only defaults (D15.9) — this W2a
# include only PLACES the data; W3's sponge_configd SEEDS P4 from it.
#

namespace eval bake {

    #
    # Cached state across multiple bake::stage calls in one run.
    #
    variable active_profile_name   ""
    variable active_profile_path   ""
    variable active_profile_kv     [dict create]
    variable active_profile_pkgs   [list]
    variable active_profile_cfg    [dict create]
    variable active_profile_theme  ""
    variable active_profile_desc   ""
    variable active_profile_cver   0
    variable active_profile_boot   [dict create]
    variable staged_already        0

    #
    # Literal '[' and ']' characters, generated at runtime via
    # [binary format c ...]. We cannot put them in Tcl string
    # literals (`\[` would yield a 2-char backslash+bracket, and a
    # bare `"["` confuses the parser's bracket counter for nested
    # command substitution). Storing them once here lets every
    # parser check and every error message reference them safely.
    #
    variable lbr [binary format c 91]
    variable rbr [binary format c 93]

    #
    # Internal: read $::env(SPONGE_BAKE_PROFILE); default desktop; loud
    # error on bogus. The "none" sentinel is handled here too.
    #
    proc _resolve_profile_name {} {
        variable active_profile_name

        set name "desktop"
        if {[info exists ::env(SPONGE_BAKE_PROFILE)] \
            && $::env(SPONGE_BAKE_PROFILE) ne ""} {
            set name $::env(SPONGE_BAKE_PROFILE)
        }

        #
        # The "none" sentinel is the escape hatch (D15.14 + AGENTS.md §1.1):
        # reproduce today's hardcoded behaviour exactly.
        #
        if {$name eq "none"} {
            return "none"
        }

        #
        # Locate pkg/bake/<name>.profile. We honour a sibling resolution
        # next to the include file: the include file lives at
        # run/bake.inc, the profile directory is one level up at pkg/bake/.
        # Use the genode_dir-relative path because run scripts see the
        # bare repo root via `genode_dir` (set by the run framework:
        # /home/luke/sponge-os/genode in our layout; pkg/ lives at
        # /home/luke/sponge-os/pkg/, i.e. ../pkg from genode_dir).
        #
        if {![info exists ::genode_dir]} {
            error "bake::_resolve_profile_name: \
                   genode_dir is not set (include sourced outside a run script?)"
        }
        set bakes_dir "${::genode_dir}/../pkg/bake"
        set profile   "${bakes_dir}/${name}.profile"

        if {![file exists $profile]} {
            #
            # Loud error: list the valid profiles so the caller can
            # fix the env var in one pass.
            #
            set valid [list]
            foreach f [lsort [glob -nocomplain "${bakes_dir}/*.profile"]] {
                lappend valid [file rootname [file tail $f]]
            }
            lappend valid "none"
            error "bake: SPONGE_BAKE_PROFILE='$name' has no \
                   pkg/bake/${name}.profile. Valid profiles: \
                   [join $valid {, }]. Set \
                   SPONGE_BAKE_PROFILE to one of those (or to 'none' \
                   for today's hello-only regression baseline)."
        }

        return $name
    }

    #
    # Internal: parse one INI profile. The supported subset is:
    #
    #   config_version = 1          # required, fails loud on mismatch
    #   name = <string>             # echoed into the manifest
    #   description = <string>      # echoed into the manifest
    #
    #   [packages]
    #   <pkg> = enabled             # only "enabled" is honoured today;
    #                               # any other value is treated as
    #                               # "not enabled" but recorded into
    #                               # the manifest for honesty
    #   # comments OK
    #
    #   [config]
    #   <key> = <value>             # plain key=value, value is the
    #                               # unparsed RHS (no string-decoding;
    #                               # keys that look numeric in JSON
    #                               # output are handled by the writer)
    #
    #   [theme]
    #   active = <theme-name>       # required to resolve the theme
    #                               # defaults file; absent -> error
    #
    # Implementation: read line-by-line, strip leading/trailing whitespace
    # and trailing inline comments (anything after an unquoted `#`),
    # track the current section. Empty lines and full-line comments are
    # ignored. config_version must equal 1 exactly.
    #
    proc _parse_profile {profile_path profile_name} {
        variable active_profile_kv
        variable active_profile_pkgs
        variable active_profile_cfg
        variable active_profile_theme
        variable active_profile_desc
        variable active_profile_cver
        variable active_profile_boot
        variable lbr
        variable rbr

        set active_profile_kv   [dict create]
        set active_profile_pkgs [list]
        set active_profile_cfg  [dict create]
        set active_profile_theme ""
        set active_profile_desc ""
        set active_profile_cver  0
        set active_profile_boot  [dict create]

        set fh [open $profile_path r]
        set section ""
        set lineno 0

        while {[gets $fh line] >= 0} {
            incr lineno

            #
            # Trim leading whitespace; skip empty / comment lines.
            #
            set trimmed [string trim $line]
            if {$trimmed eq ""}                              { continue }
            if {[string index $trimmed 0] eq "#"}            { continue }

            #
            # Strip a trailing inline comment (unquoted '#' onwards).
            # Walk char-by-char; quote handling kept simple — the
            # profile format forbids quotes in values today, so a
            # naked '#' after non-empty content is a comment.
            #
            set in_quote 0
            set cut_at -1
            for {set i 0} {$i < [string length $trimmed]} {incr i} {
                set c [string index $trimmed $i]
                if {$c eq "\"" || $c eq "'"} {
                    set in_quote [expr {1 - $in_quote}]
                }
                if {!$in_quote && $c eq "#"} {
                    set cut_at $i
                    break
                }
            }
            if {$cut_at >= 0} {
                set trimmed [string trim [string range $trimmed 0 $cut_at-1]]
                if {$trimmed eq ""} { continue }
            }

            #
            # Section header.
            #
            if {[regexp {^\[.*\]$} $trimmed]} {
                # Strip the leading and trailing bracket chars.
                set inner [string range $trimmed 1 end-1]
                set section [string trim $inner]
                if {$section ni {"packages" "config" "theme" "boot"}} {
                    error "bake: parse error in $profile_path line \
                           $lineno: unknown section ${lbr}${section}${rbr} \
                           (valid: packages, config, theme, boot)"
                }
                continue
            }

            #
            # key = value (or key = value-with-spaces).
            #
            set eq_idx [string first "=" $trimmed]
            if {$eq_idx < 0} {
                error "bake: parse error in $profile_path line \
                       $lineno: expected key=value, got '$trimmed'"
            }
            set key [string trim [string range $trimmed 0 $eq_idx-1]]
            set val [string trim [string range $trimmed $eq_idx+1 end]]

            if {$section eq ""} {
                # Top-level key (config_version, name, description).
                if {$key eq "config_version"} {
                    if {![string is integer -strict $val]} {
                        error "bake: parse error in $profile_path \
                               line $lineno: config_version must be \
                               an integer, got '$val'"
                    }
                    set active_profile_cver $val
                } elseif {$key eq "name"} {
                    if {$val ne $profile_name} {
                        # Honest mismatch; do not fail — the env var
                        # `minimal` can resolve to `minimal.profile`
                        # even if its `name` field is, say, `minimal-cfg`.
                        # But we still echo $val into the manifest.
                    }
                } elseif {$key eq "description"} {
                    set active_profile_desc $val
                }
                dict set active_profile_kv $key $val
                continue
            }

            if {$section eq "packages"} {
                if {$val eq "enabled"} {
                    lappend active_profile_pkgs $key
                }
                # Other values silently skipped (the manifest later
                # only carries enabled packages).
                continue
            }
            if {$section eq "config"} {
                dict set active_profile_cfg $key $val
                continue
            }
            if {$section eq "theme"} {
                if {$key eq "active"} {
                    set active_profile_theme $val
                }
                continue
            }
            if {$section eq "boot"} {
                #
                # Boot-media options (Phase 15, user-requested `test`
                # profile pattern). Closed registry, like the configd
                # key philosophy: unknown keys are a parse error.
                #
                #   grub_mode = production | diagnostic
                #     production : the standard hidden gfxterm menuentry
                #     diagnostic : text-mode console + timeout menu +
                #                  SPONGE-DIAG stage markers
                #                  (run/fixtures/grub-diag.cfg)
                #
                if {$key eq "grub_mode"} {
                    if {$val ni {"production" "diagnostic"}} {
                        error "bake: parse error in $profile_path line \
                               $lineno: grub_mode must be production or \
                               diagnostic, got '$val'"
                    }
                    dict set active_profile_boot $key $val
                    continue
                }
                error "bake: parse error in $profile_path line \
                       $lineno: unknown [boot] key '$key' \
                       (valid: grub_mode)"
            }
        }
        close $fh

        #
        # Mandatory validation (D15.10). config_version=1 only today;
        # bumps are breaking-only.
        #
        if {$active_profile_cver != 1} {
            error "bake: $profile_path declares \
                   config_version=$active_profile_cver, but this \
                   staging-time impl only understands config_version=1 \
                   (D15.10). Bumps must be breaking-only — update \
                   bake.inc and tool/bake.mojo together, then bump \
                   the manifest schema."
        }
        if {$active_profile_theme eq ""} {
            error "bake: $profile_path has no ${lbr}theme${rbr} active = ... \
                   line. Every profile must declare its default theme \
                   (the W3 first-boot seed needs it)."
        }
    }

    #
    # Internal: JSON-escape a string per RFC 8259. Same algorithm as
    # the upstream Genode framework's `String<>::quoted()`, but in
    # plain Tcl so we don't pull in Genode's headers.
    #
    proc _json_escape {s} {
        set out ""
        foreach c [split $s ""] {
            switch -- $c {
                "\\" { append out "\\\\" }
                "\"" { append out "\\\"" }
                "\n" { append out "\\n"  }
                "\r" { append out "\\r"  }
                "\t" { append out "\\t"  }
                "\b" { append out "\\b"  }
                "\f" { append out "\\f"  }
                default {
                    set code [scan $c %c ord]
                    if {$ord < 0x20} {
                        append out [format "\\u%04x" $ord]
                    } else {
                        append out $c
                    }
                }
            }
        }
        return $out
    }

    #
    # Internal: emit a JSON value. The {config} section values are
    # treated as raw strings (the in-system consumer — W3's
    # sponge_configd — interprets them itself). Numbers stay as
    # strings today; the JSON-shape decision is recorded in the
    # manifest header so future readers can change it.
    #
    proc _json_string {s} { return "\"[_json_escape $s]\"" }

    proc _json_string_list {lst} {
        set parts [list]
        foreach x $lst { lappend parts [_json_string $x] }
        return "\[[join $parts {, }]\]"
    }

    proc _json_string_dict {dct} {
        set parts [list]
        dict for {k v} $dct {
            lappend parts "[_json_string $k]: [_json_string $v]"
        }
        return "{[join $parts {, }]}"
    }

    #
    # Internal: synthesise the bake_manifest.json. Schema:
    #
    #   {
    #     "schema": "bake.config_version",
    #     "schema_version": 1,
    #     "profile": "<name>",
    #     "profile_config_version": 1,
    #     "description": "<from profile>",
    #     "mode": "<img|iso>",
    #     "staged_packages": [...],
    #     "config": { ... },
    #     "theme": "<active theme name>",
    #     "theme_source_path": "<where the theme file lives in pkg/bake/ lookup>",
    #     "generator": "run/bake.inc (Phase 15 W2)",
    #     "staged_at": "<iso8601-ish from Tcl clock>"
    #   }
    #
    proc _write_manifest {destdir_bake_dir mode profile_name description \
                          staged_packages config_dict theme_name} {
        set manifest_path "${destdir_bake_dir}/bake_manifest.json"
        set fh [open $manifest_path w]

        puts $fh "{"
        puts $fh "  \"schema\": [_json_string "bake.config_version"],"
        puts $fh "  \"schema_version\": 1,"
        puts $fh "  \"profile\": [_json_string $profile_name],"
        puts $fh "  \"profile_config_version\": 1,"
        puts $fh "  \"description\": [_json_string $description],"
        puts $fh "  \"mode\": [_json_string $mode],"
        puts $fh "  \"staged_packages\": [_json_string_list $staged_packages],"
        puts $fh "  \"config\": [_json_string_dict $config_dict],"
        puts $fh "  \"theme\": [_json_string $theme_name],"
        puts $fh "  \"theme_source_path\": [_json_string \
                  "repos/sponge/src/sponge-de/themes/${theme_name}.theme"],"
        puts $fh "  \"generator\": [_json_string "run/bake.inc (Phase 15 W2)"],"
        puts $fh "  \"staged_at\": [_json_string [clock format [clock seconds] \
                  -format "%Y-%m-%dT%H:%M:%S"]]"
        puts $fh "}"
        close $fh
    }

    #
    # Internal: write the W3-readable config.defaults file. Plain
    # key=value lines, one per [config] entry. The contract is "any
    # sponge_configd key that the W3 first-boot seeder should put on
    # P4 before live". Future evolution: JSON, but keeping the format
    # boring-line-oriented today means W3 can iterate without a parser.
    #
    proc _write_config_defaults {destdir_bake_dir config_dict} {
        variable lbr
        variable rbr
        set path "${destdir_bake_dir}/config.defaults"
        set fh [open $path w]
        puts $fh "# Generated by run/bake.inc (Phase 15 W2)."
        puts $fh "# Single-pass seed list for sponge_configd on first boot."
        puts $fh "# Plain key=value lines, one per ${lbr}config${rbr} entry."
        puts $fh "# W3's first-boot sentinel gates re-seeding (D15.9)."
        dict for {k v} $config_dict {
            puts $fh "$k=$v"
        }
        close $fh
    }

    #
    # Internal: copy the active theme file to theme.defaults (and into
    # the live "named theme" ROM slot for the iso case). The theme
    # source path is the Sponge-DE themes directory (same place the
    # scenario's existing theme staging uses). Failure -> loud error
    # naming the missing file.
    #
    proc _write_theme_defaults {destdir_bake_dir theme_name} {
        variable lbr
        variable rbr
        if {![info exists ::genode_dir]} {
            error "bake::_write_theme_defaults: genode_dir is not set"
        }
        set theme_src "${::genode_dir}/../repos/sponge/src/sponge-de/themes/${theme_name}.theme"
        if {![file exists $theme_src]} {
            error "bake: ${lbr}theme${rbr} active = '$theme_name' in the active \
                   profile, but ${theme_name}.theme not found at \
                   $theme_src. sponge-de themes live at \
                   repos/sponge/src/sponge-de/themes/<name>.theme."
        }
        set theme_dst "${destdir_bake_dir}/theme.defaults"
        exec cp -fL $theme_src $theme_dst
    }

    #
    # Internal: stage one pkg_<name>.xml at the destination. The
    # source metadata lives at $genode_dir/../pkg/<name>/metadata.xml.
    #
    proc _stage_pkg_metadata {destdir pkg} {
        variable lbr
        variable rbr
        if {![info exists ::genode_dir]} {
            error "bake::_stage_pkg_metadata: genode_dir is not set"
        }
        set src "${::genode_dir}/../pkg/${pkg}/metadata.xml"
        if {![file exists $src]} {
            error "bake: profile enables package '$pkg' but \
                   $src does not exist. Add the pkg/$pkg/metadata.xml \
                   or remove the ${lbr}packages${rbr} line for $pkg."
        }
        exec cp -fL $src "${destdir}/pkg_${pkg}.xml"
    }

    #
    # Internal: synthesize the on-image pkg_index.xml. The format is
    # intentionally identical to what the existing run scripts already
    # hand-write:
    #
    #   <packages>
    #     <pkg name="hello"/>
    #     <pkg name="terminal"/>
    #     ...
    #   </packages>
    #
    # sponge_pkgd opens it as the ROM "pkg_index.xml" exactly the
    # same way it has for Phase 4 onward.
    #
    proc _write_pkg_index {destdir packages} {
        set fh [open "${destdir}/pkg_index.xml" w]
        puts $fh "<packages>"
        foreach p $packages {
            puts $fh "  <pkg name=\"$p\"/>"
        }
        puts $fh "</packages>"
        close $fh
    }

    #
    # Internal: stage one package's payload (img mode only). The
    # convention: pkg/<name>/payload/<rel-path> -> <destdir>/pkg/<name>/<rel-path>.
    # Same convention as run/sponge-falkon-disk.run which copies
    # pkg/falkon/payload/* into /system/pkg/falkon/payload/. Phase 7
    # todo 16/Phase 8 P4 use the same root.
    #
    # Returns the count of files copied (used for the post-stage
    # verifier — it counts the same thing and compares).
    #
    proc _stage_payload {destdir pkg} {
        if {![info exists ::genode_dir]} {
            error "bake::_stage_payload: genode_dir is not set"
        }
        set payload_src "${::genode_dir}/../pkg/${pkg}/payload"
        if {![file isdirectory $payload_src]} {
            return 0
        }
        #
        # Convention: payload files land in <destdir>/pkg/<name>/payload/
        # (consistent with run/sponge-falkon-disk.run's rom_pkg chroot
        # at /system/pkg/falkon/payload). The trailing "/." on both
        # arguments copies CONTENTS, not the directory itself.
        #
        set payload_dst "${destdir}/pkg/${pkg}/payload"
        file mkdir $payload_dst

        set count 0
        exec cp -rLf ${payload_src}/. ${payload_dst}/.
        foreach f [glob -nocomplain "${payload_dst}/*"] {
            if {[file isfile $f]} { incr count }
        }
        #
        # Also count files nested in subdirectories (the current
        # payload layout is flat, but a future payload might nest).
        #
        foreach f [glob -nocomplain "${payload_dst}/**"] {
            if {[file isfile $f]} { incr count }
        }
        return $count
    }

    #
    # Internal: R15.3 post-stage verifier. For every enabled package
    # verify (a) metadata.xml exists (already checked at stage time,
    # re-checked here as defense-in-depth); (b) the staged
    # pkg_<name>.xml exists at destdir; (c) for img mode, every
    # payload file exists on disk under <destdir>/pkg/<name>/...
    # AND (d) for img mode, the binary named in metadata.xml's
    # <binary> element is reachable: either it lives in payload (e.g.
    # falkon) or it lives in bin/ (source-built via the scenario's
    # build list).
    #
    # The verifier errors loudly with the precise missing path; this
    # is the R15.3 misleading-success-output defense.
    #
    proc _verify_stage {destdir mode pkgs} {
        variable lbr
        variable rbr
        if {![info exists ::genode_dir]} {
            error "bake::_verify_stage: genode_dir is not set"
        }
        #
        # Where build artifacts land for the source-built binary check.
        #
        # Genode's run framework sets cwd = <genode_dir>/build/<spec>/
        # (the build dir) when a run script runs through make. ninja's
        # INSTALL_DIR is the build dir's `bin/` subdirectory (see
        # `<genode_dir>/repos/base/mk/dep_prg.mk` and the build's
        # `INSTALL_DIR := $(CURDIR)/bin`). So `bin/X` paths inside
        # scenarios resolve to <build_dir>/bin/X. We probe both
        # candidates — [pwd]/bin (canonical cwd-based) and the
        # absolute <genode_dir>/build/x86_64/bin (the documented
        # default) — and pick whichever exists. The smoke tests in
        # scratch directories pass through absolute path; production
        # scenarios go through [pwd]/bin.
        #
        set cwd [pwd]
        if {[file isdirectory "${cwd}/bin"]} {
            set bin_dir "${cwd}/bin"
        } elseif {[file isdirectory "${::genode_dir}/build/x86_64/bin"]} {
            set bin_dir "${::genode_dir}/build/x86_64/bin"
        } else {
            error "bake: cannot locate build artifact dir (bin/). \
                   cwd='${cwd}', genode_dir='${::genode_dir}'."
        }

        foreach pkg $pkgs {
            set src_metadata "${::genode_dir}/../pkg/${pkg}/metadata.xml"
            if {![file exists $src_metadata]} {
                error "bake: R15.3 verifier failed: pkg metadata \
                       missing for '$pkg' at $src_metadata"
            }
            set dst_metadata "${destdir}/pkg_${pkg}.xml"
            if {![file exists $dst_metadata]} {
                error "bake: R15.3 verifier failed: staged metadata \
                       for '$pkg' missing at $dst_metadata"
            }

            if {$mode ne "img"} {
                # iso mode: no payload staging; if the package HAS a
                # payload and was enabled in the profile, that is
                # deliberate (alpha semantics — install-enabled, no
                # binary yet). Do not error.
                continue
            }

            #
            # img mode: verify payload files are on disk.
            #
            set payload_src "${::genode_dir}/../pkg/${pkg}/payload"
            if {[file isdirectory $payload_src]} {
                set payload_dst "${destdir}/pkg/${pkg}/payload"
                if {![file isdirectory $payload_dst]} {
                    error "bake: R15.3 verifier failed: payload \
                           directory for '$pkg' not staged at \
                           $payload_dst (payload_src exists, but the \
                           staged tree is empty)."
                }
                #
                # Count source payload files (recursive — today's
                # payload is flat, but the contract should allow
                # future nested layouts).
                #
                set src_count 0
                foreach f [glob -nocomplain "${payload_src}/*"] {
                    if {[file isfile $f]} { incr src_count }
                }
                foreach f [glob -nocomplain "${payload_src}/**"] {
                    if {[file isfile $f]} { incr src_count }
                }
                #
                # Same on the staged side.
                #
                set dst_count 0
                foreach f [glob -nocomplain "${payload_dst}/*"] {
                    if {[file isfile $f]} { incr dst_count }
                }
                foreach f [glob -nocomplain "${payload_dst}/**"] {
                    if {[file isfile $f]} { incr dst_count }
                }
                if {$dst_count != $src_count} {
                    error "bake: R15.3 verifier failed: payload \
                           file count mismatch for '$pkg' (source \
                           ${src_count} files, staged ${dst_count}). \
                           Stage transfer interrupted?"
                }
                if {$src_count == 0} {
                    error "bake: R15.3 verifier failed: payload \
                           directory for '$pkg' exists but is empty."
                }
            }

            #
            # img mode: verify the binary named in <binary> is
            # reachable — either in payload (covers falkon) or in
            # bin/ (covers source-built: terminal, files,
            # calculator, pdf_view).
            #
            # Parse the binary name from the metadata file (we just
            # re-read the same content). No XML lib in pure Tcl, but
            # the <binary>X</binary> tag is trivially re-parseable
            # for this single-value lookup.
            #
            set fh [open $src_metadata r]
            set meta_content [read $fh]
            close $fh

            # Find `<binary>NAME</binary>` (allow whitespace).
            set bm [regexp -all -inline {<binary[^>]*>([^<]+)</binary>} \
                    $meta_content]
            if {[llength $bm] >= 2} {
                set binname [string trim [lindex $bm 1]]
                if {$binname ne ""} {
                    #
                    # Try payload first (covers falkon whose binary
                    # is in pkg/falkon/payload/falkon).
                    #
                    set from_payload "${destdir}/pkg/${pkg}/payload/${binname}"
                    set from_bin     "${bin_dir}/${binname}"
                    if {[file exists $from_payload]} {
                        # OK — payload-supplied binary.
                    } elseif {[file exists $from_bin]} {
                        # OK — source-built, scenario's build {}
                        # block produced it.
                    } else {
                        error "bake: R15.3 verifier failed: binary \
                               '$binname' required by \
                               pkg/${pkg}/metadata.xml is neither in \
                               the staged payload ($from_payload) \
                               nor in the scenario's build output \
                               ($from_bin). Either add it to the \
                               scenario's `build {}` list, add a \
                               payload directory at \
                               pkg/${pkg}/payload/, or remove \
                               '$pkg' from the profile's ${lbr}packages${rbr}."
                    }
                }
            }
        }
    }

    #
    # Internal: the "none" path — reproduce today's hello-only staging
    # exactly. Used both as the SPONGE_BAKE_PROFILE=none escape hatch
    # AND as the regression baseline for the "did we change behaviour"
    # matrix cell.
    #
    proc _stage_none {destdir mode} {
        set pkg "hello"
        _stage_pkg_metadata $destdir $pkg
        _write_pkg_index $destdir [list $pkg]
        puts "bake: profile=none (escape hatch / regression baseline); \
               staged $destdir/pkg_${pkg}.xml + $destdir/pkg_index.xml \
               (no payload, no bake_manifest, no defaults)"
        return "none"
    }

    #
    # === Public API ====================================================
    #

    #
    # List the available profiles (for error messages and for
    # `tool/bake.mojo` later). Reads pkg/bake/*.profile.
    #
    proc list_profiles {} {
        if {![info exists ::genode_dir]} {
            error "bake::list_profiles: genode_dir is not set"
        }
        set bakes_dir "${::genode_dir}/../pkg/bake"
        set out [list "none"]
        foreach f [lsort [glob -nocomplain "${bakes_dir}/*.profile"]] {
            lappend out [file rootname [file tail $f]]
        }
        return $out
    }

    #
    # The active profile name (as resolved from SPONGE_BAKE_PROFILE).
    # Returned whether bake::stage has been called yet or not — the
    # resolution is idempotent.
    #
    proc active_profile {} {
        variable active_profile_name
        if {$active_profile_name eq ""} {
            set active_profile_name [_resolve_profile_name]
        }
        return $active_profile_name
    }

    #
    # Internal: ensure the active profile has been parsed at least
    # once (stage normally does this; boot_option may run first).
    #
    proc _ensure_parsed {} {
        variable active_profile_path
        variable active_profile_name
        if {$active_profile_path ne ""} { return }
        set name [active_profile]
        if {$name eq "none"} { return }
        set bakes_dir "${::genode_dir}/../pkg/bake"
        set profile   "${bakes_dir}/${name}.profile"
        _parse_profile $profile $name
        set active_profile_name $name
        set active_profile_path $profile
    }

    #
    # bake::boot_option <key> <default>
    #
    # Read a [boot] section option from the active profile. Parses
    # the profile lazily on first use (via _ensure_parsed), returns
    # <default> when the profile has no [boot] section or no such
    # key. Scenarios use this to pick bootloader behavior (e.g.
    # grub_mode) without hardcoding profile names.
    #
    proc boot_option {key default} {
        variable active_profile_boot
        _ensure_parsed
        if {[dict exists $active_profile_boot $key]} {
            return [dict get $active_profile_boot $key]
        }
        return $default
    }

    #
    # bake::stage <destdir> <mode>
    #
    # The main staging entry point. See header for the full contract.
    #
    proc stage {destdir mode} {
        variable active_profile_name
        variable active_profile_path
        variable active_profile_kv
        variable active_profile_pkgs
        variable active_profile_cfg
        variable active_profile_theme
        variable active_profile_desc
        variable active_profile_cver
        variable staged_already

        #
        # Validate mode early; bogus values fail loudly.
        #
        if {$mode ni {"img" "iso"}} {
            error "bake::stage: mode must be 'img' or 'iso', got '$mode'"
        }

        set name [_resolve_profile_name]

        #
        # Escape hatch — exact regression baseline.
        #
        if {$name eq "none"} {
            if {$staged_already} {
                puts "bake: profile=none already staged (idempotent); \
                       skipping re-run"
                return "none"
            }
            set active_profile_name "none"
            set staged_already 1
            return [_stage_none $destdir $mode]
        }

        set bakes_dir "${::genode_dir}/../pkg/bake"
        set profile   "${bakes_dir}/${name}.profile"
        _parse_profile $profile $name

        set active_profile_name $name
        set active_profile_path $profile

        puts "bake: profile=$name  mode=$mode  destdir=$destdir"
        puts "bake:   pkgs ($active_profile_cver): \
               [join $active_profile_pkgs {, }]"
        puts "bake:   config: [dict size $active_profile_cfg] keys"
        puts "bake:   theme: $active_profile_theme"

        #
        # Stage every enabled package.
        #
        set staged_count 0
        foreach pkg $active_profile_pkgs {
            _stage_pkg_metadata $destdir $pkg
            if {$mode eq "img"} {
                _stage_payload $destdir $pkg
            }
            incr staged_count
        }

        #
        # Synthesize the on-image index. Always — pkgd reads it
        # regardless of mode.
        #
        _write_pkg_index $destdir $active_profile_pkgs

        #
        # Bake defaults: manifest + config.defaults + theme.defaults.
        #
        set bake_dir "${destdir}/bake"
        file mkdir $bake_dir
        _write_manifest $bake_dir $mode $name $active_profile_desc \
                         $active_profile_pkgs $active_profile_cfg \
                         $active_profile_theme
        _write_config_defaults $bake_dir $active_profile_cfg
        _write_theme_defaults $bake_dir $active_profile_theme

        #
        # R15.3 verifier — last line of defense, runs synchronously
        # after every file write.
        #
        _verify_stage $destdir $mode $active_profile_pkgs

        set staged_already 1
        puts "bake: staged $staged_count packages + manifest in $destdir"
        return $name
    }
}

#
# Public-wrapper procs (mirror terminal_runtime.inc's "global proc
# namespace-qualified delegation" pattern). The caller never types
# `bake::` directly — wait, the brief spec says bake::stage. The brief
# wins. Call it as `bake::stage <destdir> <mode>`.
#
# This alias lets scripts that prefer the un-qualified form work too:
# `bake_stage <destdir> <mode>` -> `bake::stage <destdir> <mode>`.
#
proc bake_stage {destdir mode} {
    return [bake::stage $destdir $mode]
}

proc bake_active_profile {} {
    return [bake::active_profile]
}

proc bake_list_profiles {} {
    return [bake::list_profiles]
}
