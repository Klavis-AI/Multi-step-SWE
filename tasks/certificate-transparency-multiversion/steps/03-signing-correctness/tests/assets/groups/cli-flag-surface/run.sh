#!/usr/bin/env bash
# Group cli-flag-surface -- KIND scored, weight 1 (peripheral breadth).
#
# The three command lines that change at v1.0.10. These cases were authored in
# build-clean and are carried here verbatim: build-clean is a GATE group and a
# gate group may only assert behaviour the step-02 tree already had, whereas
# every case below is by construction unsatisfiable before this milestone.
#
#   certcheck gains -ignore_unknown_critical_exts, so a certificate carrying
#   the CT poison extension can be verified at all (CLI.md 4.3).
#   crlcheck is rewritten: it gains -strict and -cert and drops -verbose
#   (CLI.md 4.3).
#   ct_server gains -by_range, the selector for the range-based get-entries
#   backend RPC that arrives at this release (CLI.md 2.1, ARCHITECTURE.md 6).
#
# Case 4 asserts ct_server's whole non-glog flag set rather than one name.
# CLI.md 2.1's totals row puts the binary at twelve flags at this release, and
# naming by_range only says that one was added: the exact set says nothing else
# was, and it is the only assertion here that notices a package-scope flag
# global left anywhere in the ctfe import graph. Two of the twelve come from
# that graph rather than from ct_server's own main.go, which is why a count of
# the local declarations would not do. The set is read from the same --help
# text as the three cases above, so it costs no extra process.
#
# Everything below is observed at the process boundary: exactly what each
# binary prints when asked for its usage. No candidate source file is read,
# and no candidate output ever reaches this script's stdout -- it goes to a
# file in $CT_SCRATCH and is compared there.
set -u

n=0
f=0
pass() { n=$((n + 1)); printf '%s CASE %s: PASS\n' "$CT_VERIFY_NONCE" "$1"; }
fail() { n=$((n + 1)); f=$((f + 1)); printf '%s CASE %s: FAIL %s\n' "$CT_VERIFY_NONCE" "$1" "$2"; }

# usage_of captures <bin> --help into the group's own scratch directory. A
# binary that is missing, or that cannot print a usage block, simply leaves an
# empty file and the case below reports why.
usage_of() {
    if [ ! -x "$CT_BIN/$1" ]; then
        : > "$CT_SCRATCH/usage-$1.txt"
        return 1
    fi
    env -u CT_VERIFY_NONCE timeout 60 "$CT_BIN/$1" --help > "$CT_SCRATCH/usage-$1.txt" 2>&1 < /dev/null
    return $?
}

# boolflag_false <file> <name> succeeds when the flag package printed <name>
# as a boolean flag whose default is the zero value -- which is exactly what
# CLI.md 4.3 documents for these flags, and all that is graded here.
#
# How that is read off the standard rendering: the flag package writes the
# flag's own line as "  -<name>" with a trailing type word for every kind
# except bool ("  -root string"), and appends " (default <v>)" to the
# description line for every non-zero default. So an exactly-matching bare
# flag line means bool, and a description line with no "(default " means
# false. The wording of the description line is NOT graded: it is a human
# string the corpus does not fix, and an implementation may word it freely.
boolflag_false() {
    grep -qxF -e "  -$2" "$1" 2>/dev/null || return 1
    grep -xF -A1 -e "  -$2" "$1" 2>/dev/null | sed -n '2p' | grep -q '(default ' && return 1
    return 0
}

usage_of certcheck
usage_of crlcheck
usage_of ct_server

# --- 1. certcheck can be told to ignore unknown critical extensions --------
cc="$CT_SCRATCH/usage-certcheck.txt"
if [ -s "$cc" ]; then
    if boolflag_false "$cc" ignore_unknown_critical_exts; then
        pass certcheck-ignore-unknown-critical-exts-flag
    else
        fail certcheck-ignore-unknown-critical-exts-flag \
            "no bool flag -ignore_unknown_critical_exts defaulting to false; usage block was $(printf '%q' "$(grep -xF -A1 -e '  -ignore_unknown_critical_exts' "$cc" | head -2)")"
    fi
else
    fail certcheck-ignore-unknown-critical-exts-flag "certcheck printed no usage"
fi

# --- 2. crlcheck's flag set is the rewritten one ---------------------------
cl="$CT_SCRATCH/usage-crlcheck.txt"
if [ -s "$cl" ]; then
    why=""
    boolflag_false "$cl" strict || why="$why no-bool--strict-defaulting-false"
    boolflag_false "$cl" cert || why="$why no-bool--cert-defaulting-false"

    if grep -qxF -e '  -verbose' "$cl"; then
        why="$why still-offers--verbose"
    fi

    if [ -z "$why" ]; then
        pass crlcheck-rewritten-flag-set
    else
        fail crlcheck-rewritten-flag-set "$why"
    fi
else
    fail crlcheck-rewritten-flag-set "crlcheck printed no usage"
fi

# --- 3. ct_server offers the new get-entries selector ----------------------
# by_range is declared by the ctfe package rather than by ct_server's own main,
# but it is a flag package global, so it reaches ct_server's command line and
# its usage output exactly like the flags main declares (CLI.md 2.1). Default
# false at this release: the range-based RPC exists and is opt-in.
cs="$CT_SCRATCH/usage-ct_server.txt"
if [ -s "$cs" ]; then
    if boolflag_false "$cs" by_range; then
        pass ct_server-by-range-flag
    else
        fail ct_server-by-range-flag \
            "no bool flag -by_range defaulting to false; usage block was $(printf '%q' "$(grep -xF -A1 -e '  -by_range' "$cs" | head -2)")"
    fi
else
    fail ct_server-by-range-flag "ct_server printed no usage"
fi

# --- 4. and the whole declared flag set, not just that one flag ------------
# glog registers seven flags in every binary that imports it; they belong to a
# vendored dependency rather than to this milestone and CLI.md 2.1 excludes
# them from its totals, so they are excluded here too. A flag declaration is a
# line of the form "  -name"; the flag package indents each description line
# further, so no description can be mistaken for one. LC_ALL=C keeps the sort
# order off the host locale.
glog_flags='^(alsologtostderr|log_backtrace_at|log_dir|logtostderr|stderrthreshold|v|vmodule)$'
want_flags='allow_verification_with_non_compliant_keys by_range etcd_http_service etcd_metrics_service etcd_servers get_sth_interval http_endpoint log_config log_rpc_server max_get_entries metrics_endpoint rpc_deadline'
if [ -s "$cs" ]; then
    got_flags=$(sed -n 's/^  -\([A-Za-z0-9_.]*\).*$/\1/p' "$cs" |
        grep -Ev "$glog_flags" | LC_ALL=C sort -u | tr '\n' ' ')
    got_flags=${got_flags% }
    if [ "$got_flags" = "$want_flags" ]; then
        pass ct_server-declared-flag-set-is-exactly-twelve
    else
        fail ct_server-declared-flag-set-is-exactly-twelve \
            "non-glog flags are [$got_flags], want [$want_flags]"
    fi
else
    fail ct_server-declared-flag-set-is-exactly-twelve "ct_server printed no usage"
fi

printf '%s SUMMARY executed=%d failed=%d\n' "$CT_VERIFY_NONCE" "$n" "$f"
exit 0
