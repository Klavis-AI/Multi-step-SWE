#!/usr/bin/env bash
# The capability-envelope gate.
#
#   source /tests/check_isa.sh
#   check_isa <base-tree> <candidate-tree> <ceiling> <funcs-file> <scope-files-file>
#       -> 0 pass, 1 fail
#
# The envelope is AVX2 / 256-bit, with no AVX-512 anywhere. It is stated in the
# step's instruction as a fairness rule: the reference implementation for every
# scoped kernel is a 256-bit one, so a candidate may use exactly the machine the
# reference had. The gate inspects only the lines the candidate ADDED: for a
# changed file, the diff's added lines; for a new file, all of it.
#
# C/C++ comments are stripped before the scan (vendor/strip_comments.py), and
# CMake `#` comments by the awk filter in the build-file loop. The AVX-512
# pattern matches `avx512[a-z]`, the flag and xsimd spellings, so upstream guard
# macro names such as ARROW_HAVE_RUNTIME_AVX512 pass. Build files are also
# checked on added lines for literal machine selectors (`-march=`, `-mtune=`,
# `-mcpu=`) and feature flags (`-mavx*`, `-msse*`, ...); narrowing `-mno-...`
# spellings do not match. Upstream's own build-file lines are not added by the
# candidate, so they are not inspected.
#
#   * scope. <scope-files-file> lists the files THIS STEP OWNS. The gate reports
#     how many of them the candidate changed -- "N of M" -- so a reader can see
#     it examined something and what it found. An unchanged tree passes
#     vacuously and the gate says so rather than reporting a tree that changed
#     nothing as having changed behaviour.
#   * enforcement. A tree that changed any file at all is inspected over every
#     added/changed file in the tree -- a superset of the step's own scope,
#     never a subset, so scoping cannot let an out-of-envelope addition through.
#
# What the host enforces by itself: it reports `avx avx2 sse4_2 bmi1 bmi2` and
# no AVX-512, so a kernel that emitted AVX-512 would raise SIGILL the first time
# the harness called it. This gate is for what the hardware cannot catch -- an
# AVX-512 path left in the tree but never executed -- and to make the rule
# explicit rather than implicit.
set -u

# added_lines <base-file-path> <candidate-file>  -> the candidate's own lines
added_lines() {
    if [ ! -e "$1" ]; then
        cat "$2"
    else
        diff -u "$1" "$2" 2>/dev/null | sed -n 's/^+//p' | grep -v '^++' || true
    fi
}

# changed_files <base> <candidate>  -> every path the candidate added or changed
changed_files() {
    diff -rq "$1" "$2" 2>/dev/null \
        | sed -n -e 's/^Files .* and \(.*\) differ$/\1/p' \
                 -e 's/^Only in \(.*\): \(.*\)$/\1\/\2/p' || true
}

check_isa() {
    local base="$1" cand="$2" ceiling="$3" funcs="$4" scopefiles="${5:-}"
    local rc=0 n_src=0 n_build=0 n_off_build=0 n_off_src=0

    echo "  ceiling: $ceiling; scoped functions in this step: $(grep -cE '^[a-z]' "$funcs" 2>/dev/null || echo 0)"

    local changed
    changed=$(changed_files "$base" "$cand")
    local n_changed=0
    [ -n "$changed" ] && n_changed=$(printf '%s\n' "$changed" | grep -c . || true)

    # --- scoped accounting: how many of THIS STEP's files the candidate changed
    # A missing or empty list is a broken image, not a passing gate: without it
    # the gate cannot tell whether the step was touched at all, and a gate that
    # cannot tell must not pass.  The verifier image bakes the file in and the
    # binding check at the top of test.sh digests it.
    local scope_total=0 scope_hit=0 rel
    if [ -n "$scopefiles" ] && [ -f "$scopefiles" ]; then
        while IFS= read -r rel; do
            case "$rel" in ''|'#'*) continue ;; esac
            scope_total=$((scope_total + 1))
            # Absent on BOTH sides is unchanged: several of this step's files are
            # the ones the base was stripped of, so a no-op tree does not have
            # them either and must not be charged with having touched them.
            if [ ! -e "$cand/$rel" ] && [ ! -e "$base/$rel" ]; then
                continue
            elif [ ! -e "$cand/$rel" ] || [ ! -e "$base/$rel" ]; then
                scope_hit=$((scope_hit + 1))
            elif ! cmp -s "$base/$rel" "$cand/$rel"; then
                scope_hit=$((scope_hit + 1))
            fi
        done < "$scopefiles"
    fi
    if [ "$scope_total" -eq 0 ]; then
        echo "  envelope: SCOPE FILE LIST MISSING OR EMPTY ($scopefiles) -- this step's"
        echo "  envelope: scope cannot be established, so the gate refuses to pass"
        return 1
    fi
    echo "  envelope: scope: $scope_total files this step owns ($(basename "$scopefiles"))"
    echo "  envelope: $scope_hit of $scope_total scoped files changed by the candidate; $n_changed files changed anywhere in the tree"
    if [ "$scope_hit" -eq 0 ]; then
        echo "  envelope: no file this step owns was touched -- the scoped envelope check"
        echo "  envelope: is vacuous for this tree and is skipped; the scan below still"
        echo "  envelope: runs over anything else the candidate changed, if anything"
    fi

    local f tmp
    tmp=$(mktemp)

    # --- build files the candidate added or changed -------------------------
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        case "$f" in
            *CMakeLists.txt|*.cmake|*meson.build|*Makefile*|*Makefile) ;;
            *) continue ;;
        esac
        [ -f "$f" ] || continue
        rel=${f#"$cand"/}
        n_build=$((n_build + 1))
        added_lines "$base/$rel" "$f" > "$tmp"
        local bad
        # Strip CMake comments first: '#' to end of line, outside a quoted
        # string.  A comment cannot enable anything, and a comment that merely
        # *names* AVX-512 (the way upstream's own notes do) must not fail the
        # step.  Line numbers survive the filter, so the report still points at
        # the candidate's own line.
        bad=$(awk '{ out = ""; inq = 0;
                     for (i = 1; i <= length($0); i++) {
                       c = substr($0, i, 1);
                       if (c == "\"") inq = !inq;
                       if (c == "#" && !inq) break;
                       out = out c
                     }
                     print out }' "$tmp" \
              | grep -nE -- '-m(avx512[a-z]*|arch=[^ ]*avx512[^ ]*|prefer-vector-width=512|m512)|append_runtime_avx512_src|-m(arch|tune|cpu)=|-m(avx|sse|mmx|bmi|fma|f16c|popcnt|lzcnt|movbe|aes|pclmul|sha|rdrnd|rdseed|adx|gfni|vaes|amx)' | head -5 || true)
        if [ -n "$bad" ]; then
            echo "  ISA VIOLATION ($rel adds AVX-512 build configuration or a literal ISA flag):"
            printf '%s\n' "$bad" | sed 's/^/    /'
            n_off_build=$((n_off_build + 1)); rc=1
        fi
    done <<< "$changed"
    echo "  ISA: inspected the added lines of $n_build added/changed build files; $n_off_build add AVX-512 or a literal ISA flag"

    # --- C/C++/asm the candidate added or changed ---------------------------
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        case "$f" in
            *.cc|*.cpp|*.cxx|*.c|*.h|*.hpp|*.hxx|*.inc|*.ipp|*.S|*.asm) ;;
            *) continue ;;
        esac
        [ -f "$f" ] || continue
        rel=${f#"$cand"/}
        n_src=$((n_src + 1))
        added_lines "$base/$rel" "$f" > "$tmp"
        local hits
        hits=$(python3 /tests/vendor/strip_comments.py "$tmp" \
               | grep -niE '(_mm512_|__m512|zmm[0-9]|vpternlog|avx512[a-z])' | head -5 || true)
        if [ -n "$hits" ]; then
            echo "  ISA VIOLATION in $rel:"
            printf '%s\n' "$hits" | sed 's/^/    /'
            n_off_src=$((n_off_src + 1)); rc=1
        fi
    done <<< "$changed"

    rm -f "$tmp"
    echo "  ISA: inspected the added lines of $n_src added/changed C/C++/asm translation units; $n_off_src contain AVX-512 constructs"
    if [ "$n_build" -eq 0 ] && [ "$n_src" -eq 0 ]; then
        echo "  ISA: the candidate added and changed nothing, so there is nothing to inspect"
    fi

    # One line a reader can quote: what was examined, and the verdict.
    local verdict
    if [ "$rc" -ne 0 ]; then
        verdict="VIOLATION"
    elif [ "$n_changed" -eq 0 ]; then
        verdict="PASS (vacuous: the candidate changed nothing at all)"
    elif [ "$scope_hit" -eq 0 ]; then
        verdict="PASS (scan enforced over $n_changed changed files, none of them this step's)"
    else
        verdict="PASS (enforced: $scope_hit of $scope_total scoped files changed)"
    fi
    echo "  envelope: checked $n_build build files and $n_src translation units amongst $n_changed changed files; $((n_off_build + n_off_src)) AVX-512 violations -- $verdict"
    return "$rc"
}
