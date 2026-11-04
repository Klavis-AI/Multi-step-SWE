#!/usr/bin/env bash
# Group keyflag-removed-from-binaries.
#
# v1.0.21 deletes the `flag` global that the ROOT package declared for
# allow_verification_with_non_compliant_keys (CHANGELOG v1.0.20: "The
# allow_verification_with_non_compliant_keys flag has been removed from
# signatures.go"). Because every command in the tree links the root package,
# the flag vanished from every command line at once -- it is not defaulted
# off, it is gone.
#
# Observed at the command line, as the documentation names it: each binary is
# run as a subprocess with the flag on its argv and must show the standard
# unrecognised-flag behaviour CLI.md's preamble fixes -- a "Usage of" block on
# stderr and exit status 2, with the rejected flag named in the output.
#
# Only the flag NAME is matched, never the sentence around it. The sentence
# ("flag provided but not defined: ...") is the Go standard library's own
# wording and TEST_INTERFACE.md section 3.3 promises such a message is graded
# on the identifiers it carries and on nothing else; "Usage of " and exit 2
# are graded because CLI.md's preamble states both as the required behaviour.
#
# Two controls keep "exit 2" from being evidence of nothing:
#   - a flag that this release still has (--quota_remote) must be accepted,
#     so the binary's flag parser is demonstrably working;
#   - a flag no release ever had must be rejected the same way, so the
#     detection itself is shown to fire.
#
# fixchain/chainfix is deliberately not in the list: it reads os.Args
# directly and never calls flag.Parse, so it has no flag grammar to observe
# at any anchor.
#
# At v1.0.19 every one of the thirteen binaries below prints the flag in its
# own usage and exits 0, so thirteen cases cannot pass against that tree.
set -u

N="$CT_VERIFY_NONCE"
ex=0
fa=0
pass() { ex=$((ex + 1)); printf '%s CASE %s: PASS\n' "$N" "$1"; }
fail() { ex=$((ex + 1)); fa=$((fa + 1)); printf '%s CASE %s: FAIL %s\n' "$N" "$1" "$2"; }

OUT="$CT_SCRATCH/flag.out"
FLAG=allow_verification_with_non_compliant_keys

# removed <binary>: the flag must not exist on that binary's command line.
removed() {
    local b="$1" rc
    if [ ! -x "$CT_BIN/$b" ]; then
        fail "removed-$b" "\$CT_BIN/$b was not built"
        return
    fi
    timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/$b" "--$FLAG=true" -h >"$OUT" 2>&1
    rc=$?
    if [ "$rc" -ne 2 ]; then
        fail "removed-$b" "'$b --$FLAG=true' exited $rc, want 2"
        return
    fi
    if ! grep -qF -- "-$FLAG" "$OUT"; then
        fail "removed-$b" "$b exited 2 without naming -$FLAG: $(tr '\n' ' ' < "$OUT" | cut -c1-200)"
        return
    fi
    if ! grep -q '^Usage of ' "$OUT"; then
        fail "removed-$b" "$b did not print its usage block"
        return
    fi
    pass "removed-$b"
}

# --- the thirteen binaries that exist at both anchors -----------------------
removed certcheck
removed crlcheck
removed ct_hammer
removed ct_server
removed ctclient
removed dumpscts
removed findlog
removed gossip_server
removed migrillian
removed preloader
removed scanlog
removed sctcheck
removed sctscan

# --- control: a flag this release still has is still accepted ---------------
if [ ! -x "$CT_BIN/ct_server" ]; then
    fail control-existing-flag-accepted "\$CT_BIN/ct_server was not built"
else
    timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/ct_server" --quota_remote=true -h >"$OUT" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ] && grep -q '^  -quota_remote' "$OUT"; then
        pass control-existing-flag-accepted
    else
        fail control-existing-flag-accepted "'ct_server --quota_remote=true -h' exited $rc: $(tr '\n' ' ' < "$OUT" | cut -c1-200)"
    fi
fi

# --- control: the detection above does fire on a genuinely unknown flag -----
if [ ! -x "$CT_BIN/ct_server" ]; then
    fail control-bogus-flag-rejected "\$CT_BIN/ct_server was not built"
else
    timeout 60 env -u CT_VERIFY_NONCE "$CT_BIN/ct_server" --this_flag_has_never_existed -h >"$OUT" 2>&1
    rc=$?
    if [ "$rc" -eq 2 ] && grep -qF -- "-this_flag_has_never_existed" "$OUT"; then
        pass control-bogus-flag-rejected
    else
        fail control-bogus-flag-rejected "unknown flag gave exit $rc: $(tr '\n' ' ' < "$OUT" | cut -c1-200)"
    fi
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$N" "$ex" "$fa"
exit 0
