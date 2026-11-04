#!/usr/bin/env bash
# Case runner shared by every scored group and by the gate.
#
# A case is one file. Its kind is its extension, and each kind observes the
# implementation at a boundary a real user reaches:
#
#   *.sql  one SQL script fed to the candidate's command-line program on stdin
#          (process-level test of the CLI and of SQL semantics)
#   *.sh   a shell script driving the command-line program several times, for
#          operation sequences that span processes, files or reopens
#   *.c    a C program compiled against /app/include/sqlite3.h and linked
#          against /app/build/libsqlite3.a (library-level test)
#
# Every case has a sibling `<case>.out` holding the exact expected standard
# output. A case may have a sibling `<case>.exit` containing `ok` (default) or
# `err`; `err` means the run must finish with a non-zero exit status. Error
# text is never compared - see TEST_INTERFACE.md section 2.3.
#
# Standard output is compared byte for byte. Standard error is captured for
# diagnostics only.

# The fixed dot-command preamble every *.sql case runs under. It pins the
# output shape documented in TEST_INTERFACE.md section 2.2 so a case never
# depends on a start-up default.
SQL_PRELUDE='.mode list
.headers off
.separator |
.nullvalue NULL
'

run_case() {  # run_case <case-path> <log-file>   -> 0 pass, 1 fail
    local case_path="$1" logf="$2"
    local base name kind expect_file expect exit_mode
    base=$(basename -- "$case_path")
    name="${base%.*}"
    kind="${base##*.}"
    expect_file="${case_path%.*}.out"
    exit_mode=ok
    [ -f "${case_path%.*}.exit" ] && exit_mode=$(tr -d ' \n' < "${case_path%.*}.exit")

    if [ ! -f "$expect_file" ]; then
        printf 'CASE %s: FAIL (no expected-output file)\n' "$base" >> "$logf"
        return 1
    fi
    # A malformed corpus must fail loudly rather than quietly measure less. An
    # `.exit` holding anything but `ok` or `err` - an empty file included - used
    # to fall through both branches below and drop the exit assertion entirely
    # while the stdout comparison still ran; an empty case file beside an empty
    # `.out` used to pass having executed nothing.
    if [ "$exit_mode" != ok ] && [ "$exit_mode" != err ]; then
        printf 'CASE %s: FAIL (.exit holds "%s", wanted ok or err)\n' \
            "$base" "$exit_mode" >> "$logf"
        return 1
    fi
    if [ ! -s "$case_path" ]; then
        printf 'CASE %s: FAIL (empty case file)\n' "$base" >> "$logf"
        return 1
    fi

    local dir status
    dir=$(mktemp -d "$work/case.XXXXXX")
    local out="$dir/.stdout" err="$dir/.stderr"

    case "$kind" in
    sql)
        { printf '%s' "$SQL_PRELUDE"; cat -- "$case_path"; } > "$dir/.script.sql"
        ( cd "$dir" && timeout 120 "$SQLITE3" -init /dev/null -batch main.db \
            < "$dir/.script.sql" ) > "$out" 2> "$err"
        status=$?
        ;;
    sh)
        ( cd "$dir" && SQLITE3="$SQLITE3" CASEDIR="$dir" \
            timeout 240 bash -- "$case_path" ) > "$out" 2> "$err"
        status=$?
        ;;
    c)
        if ! cc -std=c99 -O0 -w -I"$SQLITE_INC" -o "$dir/.driver" "$case_path" \
                "$SQLITE_LIB" -lm -lpthread -ldl > "$dir/.cc" 2>&1; then
            printf 'CASE %s: FAIL (did not compile)\n' "$base" >> "$logf"
            head -c 4000 "$dir/.cc" >> "$logf"
            rm -rf -- "$dir"
            return 1
        fi
        ( cd "$dir" && timeout 120 "$dir/.driver" ) > "$out" 2> "$err"
        status=$?
        ;;
    *)
        printf 'CASE %s: FAIL (unknown case kind)\n' "$base" >> "$logf"
        rm -rf -- "$dir"
        return 1
        ;;
    esac

    local verdict=0 why=''
    if [ "$status" -eq 124 ] || [ "$status" -eq 137 ]; then
        verdict=1; why='timed out'
    elif [ "$exit_mode" = err ] && [ "$status" -eq 0 ]; then
        verdict=1; why='expected a non-zero exit status, got 0'
    elif [ "$exit_mode" = ok ] && [ "$status" -ne 0 ]; then
        verdict=1; why="expected exit status 0, got $status"
    fi
    if ! cmp -s "$out" "$expect_file"; then
        verdict=1; why="${why:+$why; }standard output differs"
    fi

    if [ "$verdict" -ne 0 ]; then
        {
            printf 'CASE %s: FAIL (%s)\n' "$base" "$why"
            printf -- '--- expected stdout ---\n'; head -c 3000 "$expect_file"
            printf -- '--- actual stdout ---\n';   head -c 3000 "$out"
            printf -- '--- stderr ---\n';          head -c 2000 "$err"
            printf -- '--- end %s ---\n' "$base"
        } >> "$logf"
    else
        printf 'CASE %s: PASS\n' "$base" >> "$logf"
    fi

    rm -rf -- "$dir"
    return "$verdict"
}
