#!/usr/bin/env bash
# Group certcheck-flag-rename.
#
# v1.1.0 rewrites x509util/certcheck's flag grammar. It deletes -timecheck and
# -ignore_unknown_critical_exts and introduces -strict (default true) plus one
# switch per check: -check_time, -check_name, -check_eku, -check_path_len,
# -check_name_constraint, -check_unknown_critical_exts.
#
# Go's flag package exits 2 on an unknown flag, so each name is probed by
# running the candidate's own certcheck binary on a pinned certificate and
# comparing the exact exit status. The v1.0.21 binary answers 2 for every new
# name and 0 for both deleted ones, i.e. the exact opposite, so this group
# cannot pass against it.
#
# The same release also changes what certcheck does when it is given NO pool
# flags: the file it was handed is read as a chain, later entries are taken as
# intermediates and a self-signed last entry is adopted as the trust anchor.
# That is graded here too (CLI.md §4.3), because it is reachable only through
# the new flag grammar -- the deleted grammar had no way to leave both pools
# unset and still validate.
#
# certcheck's stdout never reaches this script's stdout: it is redirected to a
# file under CT_SCRATCH and only the exit status is graded.
set -u

N="$CT_VERIFY_NONCE"
CC="$CT_BIN/certcheck"
F="$CT_FIXTURES"
OUT="$CT_SCRATCH/certcheck.out"
ex=0
fa=0

pass() { ex=$((ex + 1)); printf '%s CASE %s: PASS\n' "$N" "$1"; }
fail() { ex=$((ex + 1)); fa=$((fa + 1)); printf '%s CASE %s: FAIL %s\n' "$N" "$1" "$2"; }

# status <case-id> <expected-exit-status> <certcheck args...>
status() {
    local id="$1" want="$2"
    shift 2
    if [ ! -x "$CC" ]; then
        fail "$id" "no certcheck binary was built at \$CT_BIN/certcheck"
        return
    fi
    env -u CT_VERIFY_NONCE "$CC" "$@" >"$OUT" 2>&1
    local got=$?
    if [ "$got" -eq "$want" ]; then
        pass "$id"
    else
        fail "$id" "exit status $got, want $want"
    fi
}

LEAF="$F/minted/leaf-valid.pem"
NONFATAL="$F/malformed/xf-subject-nonprintable.pem"
CHAIN="-root=$F/minted/bench-root.pem -intermediate=$F/minted/bench-intermediate.pem -system_roots=false -validate"

# --- the two deleted flags: unknown to v1.1.0, so the flag package exits 2 ---
status removed-timecheck-rejected                  2 -timecheck "$LEAF"
status removed-ignore-unknown-critical-exts-rejected 2 -ignore_unknown_critical_exts "$LEAF"

# --- the seven flags introduced at v1.1.0: accepted, so the run succeeds -----
status strict-flag-accepted                        0 -strict=false "$LEAF"
status check-time-flag-accepted                    0 -check_time=false "$LEAF"
status check-name-flag-accepted                    0 -check_name=false "$LEAF"
status check-eku-flag-accepted                     0 -check_eku=false "$LEAF"
status check-path-len-flag-accepted                0 -check_path_len=false "$LEAF"
status check-name-constraint-flag-accepted         0 -check_name_constraint=false "$LEAF"
status check-unknown-critical-exts-flag-accepted   0 -check_unknown_critical_exts=false "$LEAF"

# --- flags that survived the rename, as controls -----------------------------
status surviving-verbose-flag-accepted             0 -verbose "$LEAF"
status surviving-check-revocation-flag-accepted    0 -check_revocation=false "$LEAF"

# --- -strict is not merely accepted, it changes the verdict ------------------
# xf-subject-nonprintable parses with a non-fatal error at v1.1.0. With -strict
# left at its default that is an error; with -strict=false it is not.
status strict-default-rejects-nonfatal-certificate 1 "$NONFATAL"
status strict-off-accepts-nonfatal-certificate     0 -strict=false "$NONFATAL"

# --- -check_time is not merely accepted either -------------------------------
# shellcheck disable=SC2086
status validate-expired-rejected-when-time-checked   1 $CHAIN -check_time=true  "$F/minted/leaf-expired.pem"
# shellcheck disable=SC2086
status validate-expired-accepted-when-time-unchecked 0 $CHAIN -check_time=false "$F/minted/leaf-expired.pem"
# shellcheck disable=SC2086
status validate-future-rejected-when-time-checked    1 $CHAIN -check_time=true  "$F/minted/leaf-notyetvalid.pem"
# shellcheck disable=SC2086
status validate-chain-accepted                       0 $CHAIN -check_time=false "$LEAF"
# shellcheck disable=SC2086
status validate-unrooted-chain-rejected              1 $CHAIN -check_time=false "$F/minted/leaf-unrooted.pem"

# --- the same release's other certcheck change: chain adoption ---------------
# CLI.md §4.3. Every -validate case above names both pools explicitly, which is
# precisely the configuration in which adoption does NOT happen, so none of them
# can see it. With the pools left empty the tool reads the file it was given as
# a chain: later entries become intermediates, and a self-signed last entry
# becomes the trust anchor. chain-unrooted-with-root.pem is leaf + its own
# self-signed rogue root and has no path to any configured root, so it can only
# validate if the anchor inside it was adopted.
status validate-adopts-anchor-from-chain 0 \
    -validate -check_time=false "$F/minted/chain-unrooted-with-root.pem"

# The same reading applied to a chain whose last entry is NOT self-signed finds
# no anchor to adopt and fails: adoption invents nothing.
status validate-chain-without-self-signed-anchor-rejected 1 \
    -validate -check_time=false "$F/minted/chain-valid.pem"

# -root restores explicit-pool behaviour for the anchor: bench-root is now the
# only trusted root, the rogue root inside the file is no longer adopted, and
# the same file that passed two cases up fails.
status validate-explicit-root-suppresses-anchor-adoption 1 \
    "-root=$F/minted/bench-root.pem" -system_roots=false -validate -check_time=false \
    "$F/minted/chain-unrooted-with-root.pem"

# The two halves are independent conditions, not one: -intermediate suppresses
# only the intermediate half, so with -root still unset the anchor inside the
# file is adopted anyway and the same file passes.
status validate-intermediate-flag-leaves-anchor-adoption-on 0 \
    "-intermediate=$F/minted/bench-intermediate.pem" -system_roots=false \
    -validate -check_time=false "$F/minted/chain-unrooted-with-root.pem"

# --- and the tool still fails on input it cannot read ------------------------
status missing-input-file-rejected                 1 "$CT_SCRATCH/no-such-certificate.pem"

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
