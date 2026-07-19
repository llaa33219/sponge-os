# SPDX-License-Identifier: LicenseRef-SpongeOS-Proprietary
#
# Generate a vct config-ROM HID blob from command-line tokens.
#
# vct reads its arguments from a <config> ROM provided by `init`. At runtime
# that ROM is Genode's HID (human-intelligible data) format, e.g.:
#
#   + config
#   | + arg status
#   | + arg --json
#
# This tool produces that HID from a flat argv, so a host-side shell or test
# harness can feed vct without hand-writing HID. vct also accepts the legacy
# XML form <config><args><arg>...</arg></args></config> for existing tests.
# See docs/06-vct.md section 3 for the locked design.
#
# Usage:
#   mojo tool/gen_vct_config.mojo status --json
#   mojo tool/gen_vct_config.mojo component list

from std.sys import argv


def main() raises:
    var args = argv()
    var tokens: List[String] = []
    for i in range(1, len(args)):
        tokens.append(String(args[i]))

    print("+ config")
    for token in tokens:
        print("| + arg " + token)
