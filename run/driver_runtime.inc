#
# \brief  driver_runtime wiring shared include (Sponge OS driver packages).
# \author Sponge OS contributors
# \date   2026
#
# === WHAT THIS INCLUDE IS =================================================
#
# A driver package (pkg/<name>/metadata.xml with <kind>driver</kind>,
# docs/12-package-format.md §4.5) is NOT started by the application
# runtime. sponge_pkgd emits its <start> node into a second sandbox,
# driver_runtime, which is a sibling nested init inside system next to
# pkg_runtime. Its config is owned by the same daemon over the same
# report_rom relay pattern, so the two sandboxes can never disagree about
# the installed set.
#
# Why a separate sandbox at all: pkg_runtime's generated
# <parent-provides> carries the application service set only, so no
# application package can obtain a Platform / Irq / Io_mem / Io_port
# session no matter what it asks for. Hardware capabilities reach driver
# packages and nothing else. That is the minimum-privilege boundary of
# AGENTS.md §1.2, expressed as two generated configs instead of one.
#
# Every product-media scenario that ships driver packages sources this
# file and calls the four procs below. They are procs rather than a
# second copy of the config text because the pieces live at two different
# places inside one init config (the report_rom policies near the top of
# system, the <start> node at the bottom) — a single literal fragment
# could not be dropped into both.
#
# === ORDERING INVARIANT (binding) ========================================
#
# driver_runtime MUST be emitted BEFORE the pkg_runtime <start> node.
# pkg_runtime carries ram: 512G as the deliberate remainder-clamp
# (docs/14 §4.4 / genode/repos/os/src/lib/sandbox/child.cc:481-486): it
# receives every MiB init still has. That is only true while nothing is
# created after it. driver_runtime therefore takes an EXPLICIT ram grant
# and must precede pkg_runtime; appending it below pkg_runtime would
# silently take its RAM out of pkg_runtime's clamp.
#
# driver_runtime's RAM is explicit rather than a clamp for the same
# reason pkg_runtime's caps are explicit rather than a clamp: caps
# exhaustion on seL4 HANGS (docs/09-roadmap.md §11.1), and a grant that
# absorbed the whole remainder would starve the next sibling added to the
# config.
#
# === A GENODE ROUTING TRAP THE PACKAGED fb DRIVERS WALKED INTO ==========
#
# `platform_info` is a CORE rom module (base-sel4 exports it from the
# seL4 boot info, repos/base-sel4/src/core/platform.cc:629), so only
# init's parent serves it. A packaged driver reaches the Tier-0 `system`
# node as a GRANDCHILD request (fb -> driver_runtime -> system), and
# two independent Genode facts turn that into a SILENT denial:
#
#   1. init evaluates a route query with the label SCOPED to the
#      requesting child, i.e. "fb_boot -> platform_info"
#      (repos/os/src/lib/sandbox/route_model.h: `Query::label_checksum`
#      calls `skip_label_prefix(child, label)`). So a route rule of
#      `+ service ROM | label: platform_info` never matches such a
#      request no matter where it sits - `label_last:` does, because it
#      compares only the final element.
#
#   2. every `<service>` node sets `_specific_service`
#      (route_model.h), and Route_model::resolve RETHROWS on a denial
#      from such a rule instead of trying the next one. The
#      `+ service ROM | + child rom_sys` catch-all on the `system` node
#      denies the label, so it swallows the request before
#      `any-service -> parent` (which would have reached core) is tried.
#
# Together the symptom is a completely silent
# `parent denied ROM-session` with no "no route to service" line. The fix
# is one explicit rule placed BEFORE the rom_sys catch-all in the UEFI
# product scenarios' Tier-0 `system` route:
#
#   + service ROM | label_last: platform_info | + parent
#
# Recorded here because both facts are properties of Genode's policy
# model rather than of any one scenario, and because the silent failure
# is expensive to re-diagnose.
#
# === QUOTA DERIVATION ====================================================
#
# The derivation now lives next to the `packages` list and the grant
# variables at the top of the namespace below, so the arithmetic, the
# package list and the staged set cannot drift apart. The short version:
#
#   pc_nic       32M /  1000 caps
#   usb_net      64M /  1500 caps
#   pc_wifi     128M /  2500 caps
#   intel_gpu    32M /  1200 caps
#   pc_intel_fb  64M /  1500 caps
#   fb_vesa      32M /  1000 caps   (BIOS media, autostarted)
#   fb_boot      96M / 24000 caps   (UEFI media, autostarted)
#   -------------------------
#   total       448M / 31700 caps
#
# GRANT_CAPS 36000 (+13.4% over the sum), GRANT_RAM 448M (the exact sum).
# The two framebuffer packages dominate the caps figure because only one
# of them ever runs (docs/12 §4.6 exclusive group); the grant is sized
# from the SUM anyway so that changing the media's firmware does not
# change the number.
#

namespace eval driver_runtime {
    #
    # The driver packages shipped on the product media, and the build
    # target each one comes from. Kept as a single list so the build
    # block, the staging step and the index entries cannot drift apart.
    #
    variable packages {
        pc_nic      driver/nic/pc
        usb_net     driver/usb_net
        pc_wifi     driver/wifi
        intel_gpu   driver/gpu/intel
        pc_intel_fb driver/framebuffer/intel/pc/spec/x86_64
        fb_vesa     driver/framebuffer/vesa
        fb_boot     driver/framebuffer/boot
    }

    #
    # The display driver is an ordinary package now (docs/17 §1): there
    # is no static `+ start fb` node left in any product topology, and
    # the boot-time display comes up because the media's bake manifest
    # names one of the two fb packages as a first-boot root and that
    # package carries <autostart/> (docs/12 §4.5).
    #
    # WHICH one is a property of the firmware the media was built for,
    # not of the machine class: vesa_fb programs the BIOS INT 10h mode
    # set (BIOS / SeaBoot media), boot_fb maps the GOP framebuffer the
    # kernel reported (UEFI / OVMF media). A scenario states it once:
    #
    #     driver_runtime::fb_package fb_vesa
    #
    # Both packages are STAGED on every medium, so the other one is one
    # `vct remove` + `vct install` away (docs/15 §8.2).
    #
    variable fb_default "fb_vesa"
    variable fb_active ""

    #
    # Grant sizing. The shipped driver set, from each package's
    # <quota>:
    #
    #   pc_nic       32M /  1000 caps
    #   usb_net      64M /  1500 caps
    #   pc_wifi     128M /  2500 caps
    #   intel_gpu    32M /  1200 caps
    #   pc_intel_fb  64M /  1500 caps
    #   fb_vesa      32M /  1000 caps   (BIOS media, autostarted)
    #   fb_boot      96M / 24000 caps   (UEFI media, autostarted)
    #   -------------------------
    #   total       448M / 31700 caps
    #
    # Only ONE fb driver ever runs (docs/12 §4.6 exclusive group), but
    # the grant is sized from the SUM so that a media whose manifest
    # names the other one needs no edit here — a grant sized to the
    # active driver would have to change with the firmware, and the
    # arithmetic would live in five scenarios instead of one place.
    #
    # GRANT_CAPS adds the ~10% headroom docs/17 §5 requires, so adding
    # one more driver is not a cliff-edge operation on a grant sized to
    # fit: 31700 * 1.10 = 34870, rounded up to 36000.
    #
    # GRANT_RAM is the exact 448M sum — RAM carries no hang failure mode
    # (a shortfall is a visible init warning and a refused launch via
    # admission control, not a silent hang), so headroom is spent where
    # it buys something: driver_runtime's own accounting plus the
    # per-child overhead the admission margin already budgets
    # separately.
    #
    variable GRANT_CAPS 36000
    variable GRANT_RAM  448M

    #
    # The `system` sandbox's caps grant WITHOUT driver_runtime. Each
    # scenario's product topology derives its own figure (the USB-boot
    # media carries more storage/Tier-0 weight than the BIOS media), so a
    # scenario that is not on 263000 must say so with base_caps rather
    # than have this include silently shrink its grant - which init only
    # reports as "configured caps exceed available caps" on a child,
    # long after the sandbox itself was created.
    #
    variable BASE_CAPS 263000

    proc base_caps {n} {
        variable BASE_CAPS
        set BASE_CAPS $n
    }

    #
    # Caps the system sandbox must be granted for driver_runtime to
    # fit, given a caller-supplied base value for the app-side children.
    # Takes no argument: the base figure comes from BASE_CAPS above, which
    # a scenario with a different product topology sets with base_caps.
    #
    proc caps_for_system {} {
        variable GRANT_CAPS
        variable BASE_CAPS
        return [expr {$BASE_CAPS + $GRANT_CAPS}]
    }

    #
    # HID fragment A — the report_rom relay policies that belong inside
    # system's report_rom <config> node.
    #
    #   driver_runtime -> config             the pkgd-owned init config
    #   driver_runtime_state                 driver_runtime's sandbox
    #                                       state report, read by both
    #                                       sponge_pkgd (admission
    #                                       control) and the probes
    #
    # A single report_rom module serves both readers: report_rom is
    # single-writer per label, and ROM readers are label-addressed, so two
    # names would be two copies of the same periodic report.
    #
    proc relay_policies {} {
        #
        # NOTE: no leading indentation on the first line. The @PLACEHOLDER@
        # sits at the indentation its context needs, and a fragment that
        # carried its own would land one level deeper (the HID parser
        # treats the substituted text as starting at the placeholder's
        # column, so an extra 4 spaces nests the node inside whatever
        # policy precedes it).
        #
        return "+ policy | label: driver_runtime -> config | report: sponge_pkgd -> driver_runtime
    + policy | label: sponge_pkgd -> driver_runtime_state | report: driver_runtime -> state"
    }

    #
    # HID fragment B — the extra sponge_pkgd route the driver admission
    # control needs. Goes inside sponge_pkgd's <route> node.
    #
    proc pkgd_route {} {
        return "    + service ROM | label_last: driver_runtime_state | + child report_rom"
    }

    #
    # HID fragment C — the <start driver_runtime> node itself. MUST be
    # emitted before the <start pkg_runtime> node (ordering invariant
    # above).
    #
    # The Report route is what init itself uses to PUBLISH the sandbox
    # state report that pkgd's admission gate and the probes read.
    #
    # The route list is otherwise deliberately short. driver_runtime is a pure
    # sandbox: it opens no hardware session of its own, so every driver
    # child resolves its Platform/Irq/Io_mem/Io_port requests through its
    # own + parent route and lands on system's default-route, which
    # resolves them against the Tier-0 parent and the Tier-0 platform
    # child. Naming those services here would grant the sandbox a
    # capability no code in it ever requests.
    #
    # restart_budget on every heartbeat of the product media: without it
    # a child that can never start is abandoned and respawned forever at
    # the full heartbeat rate (measured 2.5 s per cycle on
    # run/sponge-containment-churn.run), which is both a permanent CPU
    # tax in init and in the kernel and a fault the outer supervisor can
    # never escalate. Three restarts is enough to outlast a transient
    # (a stalled storage read, a slow first paint) and short enough that
    # sponge_supervisor reaches its console phase while the user is still
    # looking at it.
    #
    proc start_node {} {
        variable GRANT_CAPS
        variable GRANT_RAM
        return "+ start driver_runtime | caps: $GRANT_CAPS | ram: $GRANT_RAM
  + binary | name: bin/init
  + heartbeat | restart_after_skipped: 3 | restart_budget: 3
  + route
    + service ROM | label: config               | + child report_rom
    + service ROM | label: driver_runtime_state | + child report_rom
    + service Report | + child report_rom
    + any-service
      + parent"
    }

    #
    # HID fragment D — the Tier-0 platform policies that make the
    # packaged drivers able to find their hardware. Goes inside the
    # Tier-0 platform <config> node.
    #
    # These are GRANTS, not start nodes: the platform driver publishes a
    # PCI device under a name prefix whether or not any driver is
    # installed to claim it. Declaring the grant statically and loading
    # the driver on demand is what makes vct install <driver> the
    # automation while the policy below stays the manual escape hatch
    # (AGENTS.md §1.1) — the operator can also start a driver straight
    # from a run script with no package manager involved.
    #
    # ETHERNET is the class the QEMU-verified pc_nic path needs
    # (run/sponge-pc-nic.run, upstream-proven shape).
    #
    # VGA is granted to BOTH fb packages, under each package's OWN label
    # prefix — the grant follows the package, which is what replaced the
    # static `label_prefix: fb` policy of the old Tier-0 `+ start fb`
    # node. Two grants for one class is safe precisely because the
    # packages are mutually exclusive at RUN time (docs/12 §4.6,
    # <exclusive group="fb"/>): the platform driver publishes the device
    # under both prefixes, and only the running driver opens a Platform
    # session. What the grant cannot do is decide WHICH driver runs —
    # that is the bake manifest (first boot) or `vct install` /
    # `vct remove` (afterwards), both of which are user-facing actions.
    #
    # This is also the change that dissolves docs/15 §8.2's "re-point the
    # Tier-0 policy from scratch" bring-up step for the fb case: the
    # grant no longer names a static node, so switching display drivers
    # is a package operation on this media and a Tier-0 edit only on
    # media built before it.
    #
    # WIFI (pc_wifi) and the two GPU/Intel drivers stay ungranted: they
    # need a device class nothing on the product media claims, and
    # taking a class away from a working driver is still a Tier-0
    # decision the operator must make on purpose.
    # docs/15-hardware-compatibility.md documents them.
    #
    proc platform_policies {} {
        return "+ policy | label_prefix: system -> driver_runtime -> pc_nic | info: yes | + pci | class: ETHERNET
    + policy | label_prefix: system -> driver_runtime -> fb_vesa | info: yes | + pci | class: VGA
    + policy | label_prefix: system -> driver_runtime -> fb_boot | info: yes | + pci | class: VGA"
    }

    #
    # Expand every @PLACEHOLDER@ in a config fragment and fail loudly if
    # one is missing. Callers use this because both places a fragment can
    # land — the Tier-0 install_config literal and the generated
    # system.config.hid — are braced Tcl strings in which no substitution
    # happens, so an unexpanded placeholder would reach the HID parser as
    # a policy node and fail schema validation with a message that points
    # at the wrong line entirely.
    #
    proc expand {text {required ""}} {
        #
        # Built with `list`, NOT with braces: a braced literal would
        # leave the [procs] unevaluated and substitute their literal
        # source text into the config. The substituted values contain
        # spaces and newlines, but command substitution is a single word
        # in Tcl, so each stays exactly one list element.
        #
        set map [list \
            @DRIVER_RELAY_POLICIES@    [relay_policies] \
            @DRIVER_PLATFORM_POLICIES@ [platform_policies] \
            @DRIVER_START_NODE@        [start_node] \
            @SYSTEM_CAPS@              [caps_for_system]]
        foreach {ph value} $map {
            if {[string first $ph $text] < 0} { continue }
            set text [string map [list $ph $value] $text]
        }
        foreach ph $required {
            if {[string first $ph $text] >= 0} {
                error "driver_runtime::expand: placeholder ${ph} survived expansion"
            }
        }
        if {[regexp {@[A-Z_]+@} $text]} {
            error "driver_runtime::expand: unexpanded @PLACEHOLDER@ left in the config"
        }
        return $text
    }

    #
    # driver_runtime::fb_package <name>
    #
    # State which framebuffer package THIS media boots with, once. The
    # call also seeds the package into the bake manifest (via
    # bake::add_package, when run/bake.inc was sourced) so the display
    # comes up on the FIRST boot of a freshly installed medium — there
    # is no other way in, because pkgd's first-boot seed is
    # manifest-driven (docs/12 §13.5) and the two fb packages differ per
    # firmware, which no single profile file can express.
    #
    # `stage` then ASSERTS that the choice actually reached the staged
    # manifest, so a scenario that forgets the call aborts loudly instead
    # of shipping media that boots to a dark screen.
    #
    proc fb_package {name} {
        variable packages
        variable fb_default
        variable fb_active

        if {[llength $packages] % 2 != 0} {
            error "driver_runtime: packages list is malformed"
        }
        set known {}
        foreach {n t} $packages { lappend known $n }
        if {[lsearch -exact $known $name] < 0} {
            error "driver_runtime::fb_package '$name' is not a staged \
                   driver package (have: $known)"
        }

        if {$fb_active eq $name} { return $name }
        if {$fb_active ne ""} {
            error "driver_runtime::fb_package: media already declared \
                   '$fb_active'; cannot also declare '$name'"
        }
        set fb_active $name

        if {[info commands ::bake::add_package] ne ""} {
            ::bake::add_package $name
        }
        return $name
    }

    #
    # Stage the driver packages onto a disk-served product media tree.
    #
    #   driver_packages::stage <sys_dir>
    #
    # <sys_dir> is the directory the scenario stages /system from (so
    # that <sys_dir>/pkg_<name>.xml, <sys_dir>/bin/<binary> and
    # <sys_dir>/pkg_index.xml line up with what bake::stage produced for
    # the application packages). MUST be called AFTER bake::stage, which
    # owns pkg_index.xml.
    #
    # Drivers are NOT listed in any bake profile on purpose: a profile
    # entry would seed the package as a first-boot root (pkgd's bake
    # seeding), which would install every driver on every boot. A driver
    # is opt-in — vct install pc_nic — and this include keeps it that
    # way while still making it installable.
    #
    # The ONE exception is the display driver, seeded by the scenario
    # through fb_package rather than by the profile: a medium whose
    # manifest does not name an fb package boots a desktop nobody can
    # see, and `vct install fb_vesa` is not available yet at that point
    # (the medium is not installed, and the storage chain is what brought
    # the system up at all). The choice is per-scenario because it
    # depends on the firmware, and the assertion at the end of this proc
    # is what keeps the choice and the staged media from drifting apart.
    #
    proc stage {sys_dir} {
        variable packages
        variable fb_active
        variable fb_default
        set index "${sys_dir}/pkg_index.xml"
        if {![file exists $index]} {
            error "driver_packages: ${index} missing — call bake::stage first"
        }
        if {$fb_active eq ""} { set fb_active $fb_default }

        set repo "${::genode_dir}/.."

        foreach {name target} $packages {
            set meta "${repo}/pkg/${name}/metadata.xml"
            if {![file exists $meta]} {
                error "driver_packages: ${meta} missing"
            }

            #
            # The binary path is what pkgd's config generator emits
            # (binary_prefix "bin/" composed with <binary>), so it is
            # read out of the metadata rather than assumed to equal the
            # package name — pc_wifi's binary is wifi, not pc_wifi.
            #
            set fh [open $meta r]
            set body [read $fh]
            close $fh
            if {![regexp {<binary>([^<]+)</binary>} $body -> binary]} {
                error "driver_packages: ${meta} has no <binary> element"
            }
            set binary [string trim $binary]

            if {![file exists "bin/${binary}"]} {
                error "driver_packages: driver '${name}' needs binary \
                       '${binary}' in bin/ — add '${target}' to this \
                       scenario's build block"
            }

            exec cp -fL "bin/${binary}" "${sys_dir}/bin/${binary}"
            exec cp -fL $meta "${sys_dir}/pkg_${name}.xml"
        }

        #
        # Append the drivers to the on-image index. sponge_pkgd refuses to
        # open a metadata ROM whose package is not in pkg_index.xml
        # (docs/12 §5.3: the index is the denial-safe gate, because a
        # missing boot module is a FATAL ROM-session denial under the
        # base Env policy, not a catchable error).
        #
        set fh [open $index r]
        set body [read $fh]
        close $fh
        if {![string match "*</packages>*" $body]} {
            error "driver_packages: ${index} is not a <packages> document"
        }
        set body [string range $body 0 [expr {[string first "</packages>" $body] - 1}]]
        foreach {name target} $packages {
            append body "  <pkg name=\"${name}\"/>\n"
        }
        append body "</packages>\n"

        set fh [open $index w]
        puts -nonewline $fh $body
        close $fh

        #
        # The boot-display assertion. The bake manifest is the ONLY
        # first-boot root mechanism sponge_pkgd has (docs/12 §13.5), so
        # the active fb package has to be named IN IT — being in
        # pkg_index.xml is not enough (index = "installable",
        # manifest = "seeded on first boot").
        #
        # Two cases, and the distinction is the scenario's, not this
        # proc's: a scenario that sources run/bake.inc owns the manifest
        # and must have named the fb package through
        # driver_runtime::fb_package (which forwards to bake::add_package);
        # a scenario that stages its own minimal package set (the NVMe
        # storage variant) has no manifest at all, and one is synthesized
        # here naming exactly the fb package — a display, and nothing
        # else, is what that variant needs seeded.
        #
        set manifest "${sys_dir}/bake/bake_manifest.json"
        if {[file exists $manifest]} {
            set fh [open $manifest r]
            set body [read $fh]
            close $fh
            if {![string match "*\"${fb_active}\"*" $body]} {
                error "driver_packages: '${fb_active}' is staged but NOT \
                       named in ${manifest} — the media would boot with no \
                       display driver at all. Call driver_runtime::fb_package \
                       ${fb_active} before bake::stage."
            }
        } else {
            #
            # pkgd reads the manifest as the FLAT /system/bake_manifest.json
            # (every scenario's pkgd route asks its parent for exactly that
            # label), which is why the file is written here and not into
            # bake/.
            #
            set flat "${sys_dir}/bake_manifest.json"
            set fh [open $flat w]
            puts $fh "{"
            puts $fh "  \"profile\": \"$fb_active\","
            # The brackets are escaped: inside a Tcl double-quoted
            # string a bare [ starts command substitution, and the JSON
            # array would be evaluated as a command named after the
            # package.
            puts $fh "  \"staged_packages\": \[\"${fb_active}\"\]"
            puts $fh "}"
            close $fh
            puts "driver_packages: this scenario stages no bake profile —\
                   synthesized ${flat} seeding the display driver alone"
        }

        puts "driver_packages: staged [expr {[llength $packages] / 2}] driver packages \
               into ${sys_dir} (opt-in: 'vct install <driver>')"
        puts "driver_packages: boot display driver = '${fb_active}' \
               (seeded from the bake manifest)"
    }
}
