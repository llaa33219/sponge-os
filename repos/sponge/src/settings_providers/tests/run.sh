#!/usr/bin/env bash
#
# run.sh — build and run the settings-provider host self-tests.
#
# Host g++ only; never touches the Genode build tree. Compile flags
# mirror sponge_configd/self_test.cc (Genode public headers use C++20
# abbreviated templates, so the host build uses -std=c++20 while the
# production sources stay in the C++17 subset).
#
# Usage (from anywhere): ./repos/sponge/src/settings_providers/tests/run.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

OUT="${TMPDIR:-/tmp}/settings_providers_self_test"

g++ -std=c++20 -O1 \
    -I "$REPO_ROOT/genode/repos/base/include" \
    -I "$REPO_ROOT/genode/repos/base/include/spec/64bit" \
    -I "$REPO_ROOT/genode/repos/base/include/spec/x86_64" \
    -I "$REPO_ROOT/genode/repos/base/include/spec/x86" \
    -I "$REPO_ROOT/genode/repos/os/include" \
    "$SCRIPT_DIR/self_test.cc" \
    "$SCRIPT_DIR/sp_logic_tests.cc" \
    -o "$OUT"

"$OUT"
