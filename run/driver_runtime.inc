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
# === QUOTA DERIVATION ====================================================
#
# The full shipped driver set, from the <quota> of each package:
#
#   pc_nic       32M /  1000 caps   (autostart, QEMU-verified)
#   usb_net      64M /  1500 caps
#   pc_wifi     128M /  2500 caps
#   intel_gpu    32M /  1200 caps
#   pc_intel_fb  64M /  1500 caps
#   -------------------------
#   total       320M /  7700 caps
#
# GRANT_CAPS adds the ~10% headroom docs/17 §5 requires, so adding one
# more driver is not a cliff-edge operation on a grant sized to fit:
# 7700 * 1.10 = 8470, rounded up to 9000.
#
# GRANT_RAM is the exact 320M sum — RAM carries no hang failure mode (a
# shortfall is a visible init warning and a refused launch via admission
# control, not a silent hang), so headroom is spent where it buys
# something: driver_runtime's own accounting plus the per-child overhead
# the admission margin already budgets separately.
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
    }

    variable GRANT_CAPS 9000
    variable GRANT_RAM  320M

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
    proc start_node {} {
        variable GRANT_CAPS
        variable GRANT_RAM
        return "+ start driver_runtime | caps: $GRANT_CAPS | ram: $GRANT_RAM
  + binary | name: bin/init
  + heartbeat | restart_after_skipped: 3
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
    # Only ETHERNET is listed. It is the class the QEMU-verified pc_nic
    # path needs (run/sponge-pc-nic.run, upstream-proven shape). WIFI
    # (pc_wifi) and VGA (intel_gpu, pc_intel_fb) are deliberately NOT
    # granted here: VGA is already spoken for by the static fb policy
    # that feeds vesa_fb, and taking it away is a Tier-0 decision the
    # operator must make on purpose. docs/15-hardware-compatibility.md
    # documents both.
    #
    proc platform_policies {} {
        return "+ policy | label_prefix: system -> driver_runtime -> pc_nic | info: yes | + pci | class: ETHERNET"
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
    proc stage {sys_dir} {
        variable packages
        set index "${sys_dir}/pkg_index.xml"
        if {![file exists $index]} {
            error "driver_packages: ${index} missing — call bake::stage first"
        }

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

        puts "driver_packages: staged [expr {[llength $packages] / 2}] driver packages \
               into ${sys_dir} (opt-in: 'vct install <driver>')"
    }
}
