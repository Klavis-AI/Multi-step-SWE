#!/usr/bin/env bash
# Group build-clean -- KIND gate (proof of life).
#
# The gate is proof that the candidate tree builds and that the artifacts it
# produces start. Nothing here may be new capability, because a gate group
# earns nothing and only vetoes: a case that the step-02 tree cannot satisfy
# would zero every candidate whose only sin is not having finished this
# milestone, and would make this step's own no-op continuation report a failed
# gate rather than a zero score.
#
# So this group asserts only what was already true at v1.0.5: the ten package
# main directories in product scope still build into ten runnable binaries,
# each flag-parsing one still prints its own usage and exits 0, and chainfix
# still aborts on its missing positional arguments.
#
# What moved OUT of this group and into the scored group cli-flag-surface,
# verbatim, is everything that is new at v1.0.10 and so cannot be regression:
#
#   certcheck's -ignore_unknown_critical_exts, crlcheck's rewritten flag set
#   (-strict and -cert in, -verbose out), and ct_server's -by_range.
#
# Everything below is observed at the process boundary: which names the
# prerequisite build produced in $CT_BIN, and exactly what each one prints when
# asked for its usage. No candidate source file is read, and no candidate
# output ever reaches this script's stdout -- it goes to a file in $CT_SCRATCH
# and is compared there.
#
# Determinism: no clock, no elapsed time, no network, no host property. The
# command list is a compile-time constant and is iterated in a fixed order.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# The ten package main directories in product scope at this milestone,
# excluding the two //go:build ignore generators under x509/.
CMDS="certcheck chainfix crlcheck ct_hammer ct_server ctclient dumpscts
      gossip_server preloader scanlog"

# --- 1. every command was built and is runnable ----------------------------
missing=""
for b in $CMDS; do
    if [ ! -f "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail product-binaries-present "not built or not executable:$missing"
else
    pass product-binaries-present
fi

# --- 2. each flag-parsing command prints its own usage and exits 0 ---------
# chainfix indexes its positional arguments before the flag package runs, so
# it is checked on its own below.
usage_of() {
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$1" --help > "$CT_SCRATCH/usage-$1.txt" 2>&1 < /dev/null
    return $?
}

bad=""
for b in $CMDS; do
    [ "$b" = chainfix ] && continue
    if [ ! -x "$CT_BIN/$b" ]; then
        bad="$bad $b:not-built"
        continue
    fi
    usage_of "$b"
    rc=$?
    want="Usage of $CT_BIN/$b:"
    got=$(head -n 1 "$CT_SCRATCH/usage-$b.txt")
    if [ "$rc" -ne 0 ]; then
        bad="$bad $b:rc=$rc"
    elif [ "$got" != "$want" ]; then
        bad="$bad $b:first-line-mismatch"
    fi
done
if [ -n "$bad" ]; then
    fail binaries-report-their-own-usage "$bad"
else
    pass binaries-report-their-own-usage
fi

# --- 3. chainfix needs its positional arguments ----------------------------
if [ -x "$CT_BIN/chainfix" ]; then
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/chainfix" --help \
        > "$CT_SCRATCH/usage-chainfix.txt" 2>&1 < /dev/null
    rc=$?
    if [ "$rc" -eq 2 ]; then
        pass chainfix-aborts-without-its-positional-arguments
    else
        fail chainfix-aborts-without-its-positional-arguments "exit status $rc, want 2"
    fi
else
    fail chainfix-aborts-without-its-positional-arguments "chainfix was not built"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
