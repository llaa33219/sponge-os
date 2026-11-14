#!/usr/bin/env bash
#
# structural_checks.sh — structural evidence for the first-party
# settings provider set (Criterion 18). Pure file/label assertions,
# no compilation: it pins the target names, the configd channel
# labels (byte-identical to sponge_configd/key_domain_acl.cc's
# writer table), the quick-pair labels, the seam markers, and the
# metadata declarations. Run from anywhere; exits non-zero on the
# first divergence and prints it.
#
#   ./repos/sponge/src/settings_providers/tests/structural_checks.sh

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROVIDERS="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROVIDERS/../../../.." && pwd)"

FAILURES=0

fail() {
    echo "FAIL: $1"
    FAILURES=$((FAILURES + 1))
}

pass() {
    echo "ok:   $1"
}

# name:target:config_request:config_result:quick_request:quick_result:settings_id:package
PROVIDER_SPECS=(
  "panel:panel_settings:panel_settings_config_request:panel_settings_config_result:panel_settings_quick_request:panel_settings_quick_result:panel:sponge-de"
  "background:background_settings:background_settings_config_request:background_settings_config_result:background_settings_quick_request:background_settings_quick_result:background:sponge-de"
  "theme:theme_settings:theme_settings_config_request:theme_settings_config_result:theme_settings_quick_request:theme_settings_quick_result:theme:sponge-de"
  "shortcuts:shortcuts_settings:shortcuts_settings_config_request:shortcuts_settings_config_result:shortcuts_settings_quick_request:shortcuts_settings_quick_result:shortcuts:sponge-de"
  "defaults:defaults_settings:defaults_settings_config_request:defaults_settings_config_result:defaults_settings_quick_request:defaults_settings_quick_result:defaults:sponge-de"
  "display:display_settings:display_settings_config_request:display_settings_config_result:display_settings_quick_request:display_settings_quick_result:display:sponge-de"
  "wm:wm_settings:wm_settings_config_request:wm_settings_config_result:wm_settings_quick_request:wm_settings_quick_result:wm:sponge-de"
  "terminal:terminal_settings:terminal_settings_config_request:terminal_settings_config_result:terminal_settings_quick_request:terminal_settings_quick_result:terminal:terminal"
)

for spec in "${PROVIDER_SPECS[@]}"; do
    IFS=: read -r dir target creq cres qreq qres sid pkg <<< "$spec"

    [ -d "$PROVIDERS/$dir" ]            && pass "$dir/ exists"            || fail "$dir/ missing"
    [ -f "$PROVIDERS/$dir/target.mk" ]  && pass "$dir/target.mk exists"   || fail "$dir/target.mk missing"
    [ -f "$PROVIDERS/$dir/main.cc" ]    && pass "$dir/main.cc exists"     || fail "$dir/main.cc missing"
    [ -f "$PROVIDERS/$dir/README.md" ]  && pass "$dir/README.md exists"   || fail "$dir/README.md missing"

    grep -q "^TARGET   := $target\$" "$PROVIDERS/$dir/target.mk" \
        && pass "$dir TARGET == $target" \
        || fail "$dir TARGET is not '$target'"

    for needle in "$creq" "$cres" "$qreq" "$qres" "$sid" "$pkg"; do
        grep -q "$needle" "$PROVIDERS/$dir/main.cc" \
            && pass "$dir main.cc pins '$needle'" \
            || fail "$dir main.cc does not pin '$needle'"
    done
done

ACL="$REPO_ROOT/repos/sponge/src/sponge_configd/key_domain_acl.cc"
if [ -f "$ACL" ]; then
    for spec in "${PROVIDER_SPECS[@]}"; do
        IFS=: read -r dir target creq cres qreq qres sid pkg <<< "$spec"
        grep -q "\"$creq\"" "$ACL" \
            && pass "ACL writer table pins '$creq'" \
            || fail "ACL writer table lacks '$creq'"
        grep -q "\"$cres\"" "$ACL" \
            && pass "ACL writer table pins '$cres'" \
            || fail "ACL writer table lacks '$cres'"
    done
else
    echo "note: key_domain_acl.cc not present — label cross-check skipped"
fi

for dir in panel background theme shortcuts defaults display wm terminal; do
    for forbidden in SettingsController de_config_request de_config_result settings_controller; do
        if grep -rq "$forbidden" "$PROVIDERS/$dir"; then
            fail "$dir/ references forbidden '$forbidden'"
        else
            pass "$dir/ free of '$forbidden'"
        fi
    done
done

grep -q "TODO(configd follow-up)" "$PROVIDERS/display/main.cc" \
    && pass "display/main.cc carries the typed-key seam marker" \
    || fail "display/main.cc lacks the typed-key seam marker"
grep -q "TODO(configd follow-up)" "$PROVIDERS/terminal/main.cc" \
    && pass "terminal/main.cc carries the typed-key seam marker" \
    || fail "terminal/main.cc lacks the typed-key seam marker"

SDE_META="$REPO_ROOT/pkg/sponge-de/metadata.xml"
if [ -f "$SDE_META" ]; then
    for spec in "${PROVIDER_SPECS[@]}"; do
        IFS=: read -r dir target creq cres qreq qres sid pkg <<< "$spec"
        [ "$pkg" = "sponge-de" ] || continue
        grep -q "target=\"$target\"" "$SDE_META" \
            && pass "pkg/sponge-de/metadata.xml declares $target" \
            || fail "pkg/sponge-de/metadata.xml lacks $target"
    done
    COUNT=$(grep -c "<settings id=" "$SDE_META")
    [ "$COUNT" = "7" ] \
        && pass "pkg/sponge-de/metadata.xml declares exactly 7 settings entries" \
        || fail "pkg/sponge-de/metadata.xml declares $COUNT settings entries (want 7)"
else
    fail "pkg/sponge-de/metadata.xml missing"
fi

TERM_META="$REPO_ROOT/pkg/terminal/metadata.xml"
if [ -f "$TERM_META" ]; then
    grep -q "target=\"terminal_settings\"" "$TERM_META" \
        && pass "pkg/terminal/metadata.xml declares terminal_settings" \
        || fail "pkg/terminal/metadata.xml lacks terminal_settings"
else
    fail "pkg/terminal/metadata.xml missing"
fi

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "structural checks: PASS"
    exit 0
fi
echo "structural checks: FAIL ($FAILURES)"
exit 1
