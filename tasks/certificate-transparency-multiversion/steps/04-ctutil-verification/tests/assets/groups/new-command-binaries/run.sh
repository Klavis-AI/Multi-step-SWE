#!/usr/bin/env bash
# Group new-command-binaries -- KIND scored, WEIGHT 1.
#
# v1.0.13 adds three commands to the tree -- findlog (loglist/findlog),
# sctcheck and sctscan (both under ctutil/) -- taking the count in product
# scope from ten to thirteen, and withdraws none. These three cases came out of
# build-clean, where they were gate cases that only a v1.0.13 tree could
# satisfy: an unpriced veto under gate rule 4, and the reason this step's own
# no-op continuation reported a FAILED GATE rather than a zero score. The ten
# inherited commands stay in build-clean as proof of life; the three new ones
# are priced here.
#
# Weight 1, peripheral breadth: this group grades only that the three
# commands exist, start, and declare the flag set the spec gives them. What
# they actually do is graded by findlog-cli and sctcheck-cli.
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

# The three that first appear at v1.0.13.
NEW="findlog sctcheck sctscan"

# --- 1. the three new commands were built and are runnable ------------------
missing=""
for b in $NEW; do
    if [ ! -f "$CT_BIN/$b" ] || [ ! -x "$CT_BIN/$b" ]; then
        missing="$missing $b"
    fi
done
if [ -n "$missing" ]; then
    fail the-three-new-commands-are-built "not built or not executable:$missing"
else
    pass the-three-new-commands-are-built
fi

# --- 2. each of them prints its own usage and exits 0 -----------------------
bad=""
for b in $NEW; do
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
    fail new-commands-report-their-own-usage "$bad"
else
    pass new-commands-report-their-own-usage
fi

# --- 3. sctscan declares its inclusion-checking flags -----------------------
# The flags that distinguish sctscan from the older scanlog: inclusion
# checking, the DNS access points, and the master log list it consults.
# Names and types come from CLI.md SS4.4; so do the two defaults, which the
# flag package renders as a trailing "(default <value>)" on the description
# line when, and only when, the default is not the type's zero value. The
# description text is not looked at here. CLI.md SS4.4 does fix one of them
# -- --log_list's, for both sctcheck and sctscan -- but no case in this
# package greps a description, so nothing depends on it.
out="$CT_SCRATCH/usage-sctscan.txt"
if [ -x "$CT_BIN/sctscan" ] && [ -f "$out" ]; then
    bad=""
    grep -qxF '  -inclusion' "$out" || bad="$bad -inclusion"
    grep -qxF '  -dns' "$out" || bad="$bad -dns"
    grep -qxF '  -log_list string' "$out" || bad="$bad -log_list"
    grep -qxF '  -log_uri string' "$out" || bad="$bad -log_uri"

    # dns is Bool true at v1.0.13: its description line ends "(default true)".
    dnsdesc=$(grep -A1 -xF '  -dns' "$out" | sed -n '2p')
    case "$dnsdesc" in
        *'(default true)') : ;;
        *) bad="$bad -dns:not-defaulted-true" ;;
    esac

    # inclusion is Bool false: the flag package prints no default at all.
    incdesc=$(grep -A1 -xF '  -inclusion' "$out" | sed -n '2p')
    case "$incdesc" in
        *'(default '*) bad="$bad -inclusion:defaulted" ;;
    esac

    if [ -n "$bad" ]; then
        fail sctscan-declares-its-inclusion-flags "wrong or missing usage lines:$bad"
    else
        pass sctscan-declares-its-inclusion-flags
    fi
else
    fail sctscan-declares-its-inclusion-flags "sctscan was not built"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
