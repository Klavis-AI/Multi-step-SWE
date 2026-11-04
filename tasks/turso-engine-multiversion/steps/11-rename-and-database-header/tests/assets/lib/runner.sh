#!/usr/bin/env bash
# Case runner shared by the author-side generator and every step's verifier.
#
# The same file produces the expected outputs and checks them, so an
# expectation can never drift from the way it is graded.
#
# Inputs (environment):
#   CLI       absolute path to the candidate engine's shell binary   (required)
#   SQLITE3   absolute path to the pinned stock sqlite3 3.50.2       (optional)
#   SCRATCH   writable directory for per-case working directories    (required)
#
# A case is one file:
#   <name>.sql   pure SQL, fed on stdin to `$CLI -q main.db`.
#                Dot-commands are NOT portable across the chain and are
#                rejected by the linter below; a case that needs shell surface
#                is a .sh case instead.
#   <name>.sh    bash, run with cwd set to a fresh working directory and with
#                CLI, SQLITE3 and CASEDIR exported. Whatever it prints on
#                stdout is the observation.
#
# Expected output is the sibling <name>.out, compared byte for byte.
# Optional sibling <name>.exit holds `ok` (default) or `err`.

CASE_TIMEOUT="${CASE_TIMEOUT:-120}"

# The shell's result-row rendering is selected explicitly, never left to the
# default. Upstream changed both the default and the selector's spelling part
# way along the chain, so the exact invocation is part of each step's stated
# interface contract rather than something a case may assume.
CLI_MODE="${CLI_MODE:-list}"

# A .sql case must be pure SQL. The shell's dot-command surface is a
# capability that arrives partway along the chain, and before it arrives the
# shell prints a diagnostic on *stdout* and keeps going -- which would bake a
# diagnostic string into an expected output. Cases that need the shell surface
# are written as .sh cases that assert it explicitly.
runner_lint_sql() {
    local case_file="$1"
    if grep -qE '^[[:space:]]*\.' "$case_file"; then
        echo "lint: dot-command in .sql case (use a .sh case instead)"
        return 1
    fi
    if [ ! -s "$case_file" ]; then
        echo "lint: empty case file"
        return 1
    fi
    return 0
}

# runner_run <case_file> <out_dir>
# Always returns 0. Writes <out_dir>/{stdout,stderr,rc}.
runner_run() {
    local case_file="$1" out_dir="$2"
    local work rc
    work="$(mktemp -d "${SCRATCH:?SCRATCH must be set}/case.XXXXXXXX")"
    mkdir -p "$out_dir"

    case "$case_file" in
        *.sql)
            ( cd "$work" && timeout -k 5 "$CASE_TIMEOUT" \
                "$CLI" -m "$CLI_MODE" -q main.db ) < "$case_file" \
                > "$out_dir/stdout" 2> "$out_dir/stderr"
            rc=$?
            ;;
        *.sh)
            ( cd "$work" \
              && CLI="$CLI" SQLITE3="${SQLITE3:-}" CLI_MODE="$CLI_MODE" \
                 CASEDIR="$(cd "$(dirname "$case_file")" && pwd)" \
                 timeout -k 5 "$CASE_TIMEOUT" bash "$case_file" ) \
                > "$out_dir/stdout" 2> "$out_dir/stderr"
            rc=$?
            ;;
        *)
            printf 'runner: unsupported case kind: %s\n' "$case_file" \
                > "$out_dir/stderr"
            rc=125
            ;;
    esac

    printf '%s\n' "$rc" > "$out_dir/rc"
    rm -rf "$work"
    return 0
}

# The graded rendering of a .sql case is the sequence of its non-blank result
# lines. Blank separator lines are not part of the contract: upstream emits one
# before every result block at some releases along the chain and none at
# others, and that spacing says nothing about the SQL. Every value, its type
# rendering, its column separator and its row order are still compared byte for
# byte.
#
# This normalisation would silently swallow a result row that is itself blank
# -- a bare NULL or empty-string scalar. It is never allowed to: the generator
# refuses any .sql case whose own checkpoint emits a blank result line, and
# such behaviour is asserted in a .sh case, which owns its stdout and is not
# normalised at all.
runner_normalize() {
    local kind="$1" src="$2" dst="$3"
    if [ "$kind" = sql ]; then
        sed '/^[[:space:]]*$/d' "$src" > "$dst"
    else
        cp "$src" "$dst"
    fi
}

# runner_has_blank_result_line <stdout-file> -> 0 if it contains a blank line
runner_has_blank_result_line() {
    grep -qE '^[[:space:]]*$' "$1"
}

# runner_expected_status <case_file> -> prints `ok` or `err`
runner_expected_status() {
    local exit_file="${1%.*}.exit"
    if [ -f "$exit_file" ]; then
        local want
        want="$(tr -d '[:space:]' < "$exit_file")"
        case "$want" in
            ok|err) printf '%s\n' "$want"; return 0 ;;
            *)      printf 'malformed\n'; return 1 ;;
        esac
    fi
    printf 'ok\n'
}

# runner_check <case_file> <scratch_out_dir>
# 0 = pass. On failure prints exactly one diagnostic line (no case content,
# no expected output -- diagnostics are visible to the candidate).
runner_check() {
    local case_file="$1" out_dir="$2"
    local want_file="${case_file%.*}.out"
    local want_status rc

    want_status="$(runner_expected_status "$case_file")" || {
        echo "malformed .exit sidecar"; return 1; }

    if [ ! -f "$want_file" ]; then
        echo "missing expected output"; return 1
    fi

    runner_run "$case_file" "$out_dir"
    rc="$(cat "$out_dir/rc")"

    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        echo "timed out after ${CASE_TIMEOUT}s"; return 1
    fi

    case "$want_status" in
        ok)  if [ "$rc" != 0 ]; then
                 echo "expected success, exit status $rc"; return 1
             fi ;;
        err) if [ "$rc" = 0 ]; then
                 echo "expected failure, exit status 0"; return 1
             fi ;;
    esac

    runner_normalize "${case_file##*.}" "$out_dir/stdout" "$out_dir/stdout.graded"

    if ! cmp -s "$out_dir/stdout.graded" "$want_file"; then
        local got_lines want_lines first
        got_lines="$(wc -l < "$out_dir/stdout.graded" | tr -d ' ')"
        want_lines="$(wc -l < "$want_file" | tr -d ' ')"
        first="$(cmp "$out_dir/stdout.graded" "$want_file" 2>&1 \
                 | sed -n 's/.*line \([0-9]*\).*/\1/p' | head -1)"
        echo "output mismatch (got ${got_lines} lines, expected ${want_lines}; first difference at line ${first:-1})"
        return 1
    fi

    return 0
}
