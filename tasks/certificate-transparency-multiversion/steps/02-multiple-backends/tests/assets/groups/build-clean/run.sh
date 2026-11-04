#!/usr/bin/env bash
# Group build-clean -- KIND gate (proof of life).
#
# The eight package main directories that were already in product scope at v1.0
# must still build and still be runnable after this step's work.
#
# v1.0.5 also ADDS two commands, x509util/certcheck and x509util/crlcheck,
# taking the count in product scope from eight to ten. That is this step's new
# capability, not inherited regression, so it is graded -- and priced -- by the
# scored groups certcheck-binary and crlcheck-binary, and is deliberately
# absent from here. A gate case only a v1.0.5 tree can satisfy is an unpriced
# veto (gate rule 4) and would make the step's own no-op continuation report a
# FAILED GATE instead of a zero score, which measures nothing at all.
#
# Observed at the process boundary only: which names the prerequisite build
# produced in $CT_BIN, and what each one does when asked for its usage. No
# candidate source file is read, no clock is consulted, no socket is opened.
#
# WHAT THIS GROUP DEPENDS ON, AND DOES NOT DO ITSELF.
# This group does not compile anything. The harness runs one whole-tree build
# before any group starts -- `go build -o "$W/bin/" ./...` over /app, best
# effort, its exit status discarded -- and exports that directory as $CT_BIN.
# Three consequences are deliberate and are why the cases below are written the
# way they are:
#
#   1. Because the build is whole-tree, one package that does not compile can
#      leave several binaries unbuilt. Case 1 therefore reports every missing
#      name rather than stopping at the first, so the FAIL line says which part
#      of the tree is broken.
#   2. Because the build's status is discarded, "the tree compiles" is not
#      observable here as a status; it is observable only as the presence of
#      the binaries it was supposed to emit. That is what case 1 grades, and it
#      is why this group is the one that scores the compile.
#   3. Because $CT_BIN is shared by every group in the step, this group must
#      not write to it, and does not: its two scratch files go to $CT_SCRATCH.
#
# The binaries are invoked by their full $CT_BIN path, which is also the path
# the Go flag package echoes in its `Usage of <prog>:` banner -- that is why
# $want below is built from $CT_BIN/$b and not from $b alone.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# The eight package main directories in product scope at v1.0, by binary name.
INHERITED="chainfix ct_hammer ct_server ctclient dumpscts gossip_server preloader scanlog"

# --- 1. the eight inherited commands are still built ------------------------
missing=""
for b in $INHERITED; do
    if [ ! -f "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail inherited-binaries-present "not built or not executable:$missing"
else
    pass inherited-binaries-present
fi

# --- 2. every flag-parsing inherited command prints its own usage, exit 0 ----
# chainfix indexes os.Args before the flag package runs, so it is checked
# separately below.
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
    fail binaries-report-their-own-usage "$bad"
else
    pass binaries-report-their-own-usage
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
