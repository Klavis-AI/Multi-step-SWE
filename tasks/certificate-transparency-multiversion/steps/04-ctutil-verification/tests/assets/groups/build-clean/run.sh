#!/usr/bin/env bash
# Group build-clean -- KIND gate (proof of life).
#
# The ten package main directories that were already in product scope at
# v1.0.10 must still build and still be runnable after this step's work, and
# each one must still answer --help with its own usage.
#
# v1.0.13 also ADDS three commands -- findlog (loglist/findlog), sctcheck and
# sctscan (both under ctutil/) -- taking the count in product scope from ten to
# thirteen. That is this step's new capability, not inherited regression, so it
# is graded -- and priced -- by the scored groups new-command-binaries,
# findlog-cli and sctcheck-cli, and is deliberately absent from here. A gate
# case only a v1.0.13 tree can satisfy is an unpriced veto (gate rule 4) and
# would make this step's own no-op continuation report a FAILED GATE rather
# than a zero score, which measures nothing at all.
#
# The v1.0.13 sctscan flag-default assertion that used to live here has moved
# with them: whether -dns renders "(default true)" is a detail of a new
# command's flag set, which is finer-grained than a proof of life needs.
#
# Observed at the process boundary only: which names the prerequisite build
# produced in $CT_BIN, and what each binary does when asked for its usage.
# Every binary's output goes to a file, never to this script's stdout. No
# candidate source file is read, no clock is consulted, no socket is opened.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# The ten package main directories already in product scope at v1.0.10.
INHERITED="certcheck chainfix crlcheck ct_hammer ct_server ctclient dumpscts
           gossip_server preloader scanlog"

# --- 1. every inherited command was built and is runnable -------------------
missing=""
for b in $INHERITED; do
    if [ ! -f "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail inherited-commands-are-built "not built or not executable:$missing"
else
    pass inherited-commands-are-built
fi

# --- 2. each flag-parsing command prints its own usage and exits 0 ---------
# chainfix reads its positional arguments before the flag package runs, so it
# is checked separately below.
bad=""
for b in $INHERITED; do
    [ "$b" = chainfix ] && continue
    [ -x "$CT_BIN/$b" ] || { bad="$bad $b:not-built"; continue; }
    out="$CT_SCRATCH/usage-$b.txt"
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --help > "$out" 2>&1 < /dev/null
    rc=$?
    want="Usage of $CT_BIN/$b:"
    got=$(head -n 1 "$out")
    if [ "$rc" -ne 0 ]; then
        bad="$bad $b:rc=$rc"
    elif [ "$got" != "$want" ]; then
        bad="$bad $b:first-line-mismatch"
    fi
done
if [ -n "$bad" ]; then
    fail inherited-commands-report-their-own-usage "$bad"
else
    pass inherited-commands-report-their-own-usage
fi

# --- 3. chainfix reads argv before the flag package and aborts -------------
if [ -x "$CT_BIN/chainfix" ]; then
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/chainfix" --help > "$CT_SCRATCH/usage-chainfix.txt" 2>&1 < /dev/null
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
