#!/usr/bin/env bash
# Group build-clean.
#
# Proof of life, and nothing else: the prerequisite build produced every
# command the tree already carried at v1.1.2, and each of them still runs.
# It is a GATE group. It earns nothing and only vetoes, so it may assert only
# binaries and behaviour that already exist at the predecessor anchor.
#
# Until the 2026-09-21 tier audit this driver also asserted the four new
# `package main` directories internal/witness brings in (witness, feeder,
# client, config), their flag sets and their refusals, and that ctclient
# answers --help as a Cobra command tree rather than as a flag-package usage
# block. None of that exists at v1.1.2: eight of the ten cases failed against
# the step-09 tree, so a no-op continuation for step 10 failed the gate and
# measured nothing. Those cases are this milestone's new capability (gate
# rule 3) and they moved, not deleted:
#
#   * the four witness programs, their flags and their refusals -> the scored
#     group `witness-tooling-cli`;
#   * ctclient's command-tree help -> the scored group `ctclient-command-tree`,
#     which already grades the same root help dump line by line.
#
# Observed at the process boundary only: which names the prerequisite build
# produced in $CT_BIN, and what each one prints when asked for its usage.
# Nothing here reads a clock, a source file or the network.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# The fifteen commands carried forward from v1.1.2. v1.1.3 deletes ingestor/
# and watcher/, neither of which is a package main, so this list is the same
# at both anchors.
KEPT="certcheck chainfix crlcheck ct_hammer ct_server ctclient dumpscts findlog
      hammer migrillian preloader scanlog sctcheck sctscan server"

# --- 1. every command carried forward was built and is runnable ------------
missing=""
for b in $KEPT; do
    if [ ! -s "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail inherited-command-binaries-present "not built, empty or not executable:$missing"
else
    pass inherited-command-binaries-present
fi

# --- 2. every flag-parsing command prints its own usage and exits 0 --------
# chainfix parses its positional arguments before the flag package runs, and
# is checked below instead. ctclient is excluded at both anchors: at v1.1.2 it
# is a flag-package main and at v1.1.3 it is a Cobra tree, so its --help is
# exactly the transition this milestone makes and belongs in the scored tier.
#
# The usage block is written by the standard library, so its sentence is not
# graded: what is graded is that --help succeeds and that the block the binary
# prints names *that binary's own* path, which is what makes it a flag-parsing
# main of its own rather than a shim onto someone else's.
bad=""
for b in $KEPT; do
    case "$b" in chainfix|ctclient) continue ;; esac
    [ -x "$CT_BIN/$b" ] || { bad="$bad $b:not-built"; continue; }
    out="$CT_SCRATCH/usage-$b.txt"
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$b" --help > "$out" 2>&1 < /dev/null
    rc=$?
    got=$(head -n 1 "$out")
    if [ "$rc" -ne 0 ]; then
        bad="$bad $b:rc=$rc"
    elif ! printf '%s' "$got" | grep -qF -- "$CT_BIN/$b"; then
        bad="$bad $b:usage-does-not-name-the-binary"
    fi
done
if [ -n "$bad" ]; then
    fail flag-parsing-binaries-report-their-own-usage "$bad"
else
    pass flag-parsing-binaries-report-their-own-usage
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
