#!/usr/bin/env bash
# Sourced by every case script. A case is an ordinary executable bash script
# under /tests/assets/cases/<group>/; it passes by exiting 0 and fails by
# exiting non-zero. Nothing here writes to /app.
#
# Environment a case can rely on (exported by test.sh):
#
#   ESMINI        absolute path to the candidate's `esmini` binary
#   ODRPLOT       absolute path to the candidate's `odrplot` binary, if built
#   ESMINI_DYN    absolute path to the candidate's `esmini-dyn` binary
#   DAT2CSV       absolute path to the candidate's `dat2csv` binary
#   LIBDIR        directory holding libesminiLib.so / libesminiRMLib.so
#   DRIVERS       directory holding the compiled verifier-owned C drivers
#   RESOURCES     the candidate's resources/ directory (scenarios, roads, schemas)
#   FIXTURES      /tests/assets/fixtures - verifier-owned, never in the workspace
#   WORK          a fresh empty scratch directory, unique to this case
#   CASE_TIMEOUT  seconds any single product invocation may take
#
# A case MUST NOT depend on anything under /tests/assets/cases/<other group>/.
set -u

# run_esmini <args...>
#   Runs the candidate engine headless with a hard timeout, capturing combined
#   output in $WORK/last.out. Returns the product's exit status; 124 means the
#   timeout fired, which is always a failure and never a pass.
run_esmini() {
    LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        timeout "$CASE_TIMEOUT" "$ESMINI" "$@" > "$WORK/last.out" 2>&1
}

# run_tool <binary> <args...>
# LD_LIBRARY_PATH is set for every product invocation, not only the drivers:
# esmini-dyn is the shared-library-driven player and does not start without
# libesminiLib.so on the loader path, and a candidate is free to put the
# libraries somewhere the loader would not look by default.
run_tool() {
    local b="$1"; shift
    LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        timeout "$CASE_TIMEOUT" "$b" "$@" > "$WORK/last.out" 2>&1
}

# run_driver <name> <args...>
#   Runs a verifier-owned C driver linked against the candidate's shared
#   libraries. The driver prints machine-readable `key=value` lines.
run_driver() {
    local d="$1"; shift
    LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        timeout "$CASE_TIMEOUT" "$DRIVERS/$d" "$@" > "$WORK/last.out" 2>&1
}

# fail <message>
fail() { printf 'CASE-FAIL: %s\n' "$*" >&2; [ -s "$WORK/last.out" ] && tail -c 4000 "$WORK/last.out" >&2; exit 1; }

# need_status <expected> <actual> <what>
need_status() { [ "$2" = "$1" ] || fail "$3: expected exit $1, got $2"; }

# need_file <path> [<what>]
#   The second argument is a human label for the failure message; call sites
#   pass one, so it is used rather than silently dropped.
need_file() { [ -s "$1" ] || fail "expected a non-empty file at $1${2:+ ($2)}"; }

# need_grep <regex> <file> <what>
need_grep() { grep -Eq -- "$1" "$2" || fail "$3: /$1/ not found in $(basename "$2")"; }

# need_not_grep <regex> <file> <what>
need_not_grep() { grep -Eq -- "$1" "$2" && fail "$3: /$1/ unexpectedly present in $(basename "$2")"; return 0; }

# need_kv <key> <expected> <what>
#   Asserts a `key=value` line from the last driver or tool run.
need_kv() {
    local got
    got=$(sed -n "s/^$1=//p" "$WORK/last.out" | tail -1)
    [ -n "$got" ] || fail "$3: driver printed no '$1=' line"
    [ "$got" = "$2" ] || fail "$3: $1 expected '$2', got '$got'"
}

# need_neg <key> <what>
#   Asserts a `key=value` line reports a NEGATIVE integer, without pinning which
#   one. Use this wherever the instruction says an entry point "returns a
#   negative value" or "is rejected": the documents promise the sign, not the
#   magnitude, and a solution that answers -2 is as correct as one that answers
#   -1. Pinning the magnitude would score an implementation detail the candidate
#   was never told.
need_neg() {
    local got
    got=$(sed -n "s/^$1=//p" "$WORK/last.out" | tail -1)
    [ -n "$got" ] || fail "$2: driver printed no '$1=' line"
    case "$got" in
        -[0-9]*) ;;
        *) fail "$2: $1 expected a negative value, got '$got'" ;;
    esac
}

# need_nonneg <key> <what>
#   Asserts a `key=value` line reports a NON-NEGATIVE integer, without pinning
#   which one. The counterpart of need_neg, and the right assertion wherever the
#   documents say "a non-negative return is success": the road manager's position
#   setters and both lookaheads report 1 ("entered a new road segment") and 2
#   ("made a junction choice") alongside 0, all of them successes, so a check
#   written as `need_kv <key> 0` would fail a conforming solution.
need_nonneg() {
    local got
    got=$(sed -n "s/^$1=//p" "$WORK/last.out" | tail -1)
    [ -n "$got" ] || fail "$2: driver printed no '$1=' line"
    case "$got" in
        [0-9]*) ;;
        *) fail "$2: $1 expected a non-negative value, got '$got'" ;;
    esac
}

# need_close <key> <expected> <tolerance> <what>
#   Numeric comparison for floating-point state, so a case never depends on the
#   exact decimal formatting of a build.
need_close() {
    local got
    got=$(sed -n "s/^$1=//p" "$WORK/last.out" | tail -1)
    [ -n "$got" ] || fail "$4: driver printed no '$1=' line"
    awk -v g="$got" -v e="$2" -v t="$3" 'BEGIN { d = g - e; if (d < 0) d = -d; exit (d <= t) ? 0 : 1 }' \
        || fail "$4: $1 expected $2 +/- $3, got $got"
}

# csv_column <header> <file>
#   Prints the 1-based index of a column in a CSV whose first line is a header
#   row, or nothing if it is absent. Used so a case asserts on a column NAME
#   rather than a position that upstream is free to move.
csv_column() {
    awk -F, -v want="$1" 'NR==1 {
        for (i = 1; i <= NF; i++) {
            h = $i; gsub(/^[ \t"]+|[ \t"\r]+$/, "", h)
            if (h == want) { print i; exit }
        }
    }' "$2"
}
