#!/usr/bin/env bash
# Instruction-set and threading envelope.
#
# The scoring host executes AVX2 and not AVX-512, and the reference
# implementation saturates AVX2 only: a candidate reaching for AVX-512 would be
# measured on a capability the reference could not use. Threads are out for the
# same kind of reason -- the workload is pinned to one core on both sides, so an
# implementation that spawns threads wins the ratio without being faster.
#
# What is judged is the code the pinned configuration would actually compile.
# An AVX-512 block behind `#if defined(__AVX512F__)` is dead in this build
# (MARCH=x86-64-v3 never defines it) and buys nothing, so it is not a finding;
# a `-mavx512*` compiler flag, a `#pragma GCC target("avx512...")` or
# `-march=native` is, because those are the ways to make such a block live, and
# so is any AVX-512 construct outside such a guard. Those are judged wherever
# they appear, guard or no guard: a build flag is not inside a conditional. Upstream's
# own sources are full of both, which is why lines that also appear in
# upstream's file of the same name -- and lines that lie inside an AVX-512
# guard in the candidate's own file -- are not judged.
#
# WHAT IS JUDGED IS WHAT THE CANDIDATE WROTE, NOT WHAT THE STEP'S BASE IS
# MISSING. A file the candidate never touched is not evidence about the
# candidate: it is the task's base, or a later stage's absence from it, and a
# gate that reads it wholesale reports a tree that changed nothing as having
# written the step's answer. The verifier's base is THIS STEP's base, so at a
# late step the two trees also differ by everything the ladder had already
# restored -- and a no-op tree, which is the task's base at every step, differs
# from a late step's base in exactly those files. The gate is therefore scoped,
# per file, to the files the candidate actually wrote (byte-different from the
# TASK's base, which every step starts from). A tree that changed nothing
# passes vacuously and the check says so, with the counts, rather than
# reporting a number of files it did not read. A tree that changed files is
# judged exactly as before: the ban is global, so every file the candidate
# wrote is judged, whatever it is named.
#
# check_envelope <base-tree> <candidate-tree> <upstream-tree> [<task-base-tree>]
check_envelope() {
    local base="$1" cand="$2" upstream="${3:-}" taskbase="${4:-}"
    local nfiles=0 nlines=0 nskip=0 nguard=0 bad=0
    local ndiff=0 nadded=0 nremoved=0 nwritten=0 nunwritten=0

    is_source() {
        case "$1" in
            *.cpp|*.h|*.hpp|*.cc|*CMakeLists.txt) return 0 ;;
            *) return 1 ;;
        esac
    }

    scan_one() {
        local rel="$1"
        case "$rel" in
            build/*|*/build/*|.git/*) return ;;
        esac
        # `*CMakeLists.txt`, not `CMakeLists.txt`: every CMakeLists in this tree
        # is nested, and a pattern without a wildcard matches the whole string
        # only, so the bare spelling silently exempted every one of them --
        # including the build-flag half of the ban, which exists there and
        # nowhere else.
        is_source "$rel" || return
        nfiles=$((nfiles + 1))
        local added
        if [ -f "$base/$rel" ]; then
            # The leading '+' of a unified diff is diff syntax, not content:
            # keeping it here stopped every modified file's lines from being
            # matched against upstream and failed the reference solution.
            added=$(diff -u "$base/$rel" "$cand/$rel" | grep -E '^\+' | grep -vE '^\+\+\+' | sed -e 's/^+//')
        else
            added=$(grep -E '^' "$cand/$rel")
        fi
        [ -z "$added" ] && return
        if [ -n "$upstream" ] && [ -f "$upstream/$rel" ]; then
            local before after skipped
            before=$(printf '%s\n' "$added" | grep -c . || true)
            added=$(printf '%s\n' "$added" | awk -v f="$upstream/$rel" '
                NR == FNR { up[$0] = 1; next }
                !($0 in up)' "$upstream/$rel" -)
            after=$(printf '%s\n' "$added" | grep -c . || true)
            skipped=$((before - after))
            nskip=$((nskip + skipped))
            [ -z "$added" ] && return
        fi
        # Lines the candidate's own file keeps inside an AVX-512 guard are dead
        # here for the same reason upstream's are.
        #
        # Exemption is by occurrence, not by text. A text that appears k times
        # in the candidate's file and only j < k times inside its AVX-512 guards
        # leaves k - j copies live, and a set of texts would exempt every one of
        # them on the strength of the one dead copy: write the banned line once
        # inside `#if defined(__AVX512F__)` and use it freely outside. Consuming
        # one exemption per matched line flags exactly the copies a guard cannot
        # account for, and it can only ever judge a line the set would have
        # exempted when some copy of that line really is outside a guard.
        local guarded
        guarded=$(python3 /tests/guarded_lines.py "$cand/$rel")
        if [ -n "$guarded" ]; then
            local before after g
            before=$(printf '%s\n' "$added" | grep -c . || true)
            added=$(printf '%s\n' "$added" | awk -v g="$guarded" '
                BEGIN { n = split(g, arr, "\n"); for (i = 1; i <= n; i++) dead[arr[i]]++ }
                { if (($0 in dead) && (dead[$0] > 0)) { dead[$0]--; next } ; print }')
            after=$(printf '%s\n' "$added" | grep -c . || true)
            g=$((before - after))
            nguard=$((nguard + g))
            [ -z "$added" ] && return
        fi
        nlines=$((nlines + $(printf '%s\n' "$added" | grep -c . || true)))
        local hits
        # A build flag is global: it makes every guarded block live, so it is
        # judged whether or not it sits inside a guard.
        # A denylist, and therefore a lower bound: it matches the spellings the
        # reference code uses, not every way to reach a 512-bit register.
        # `-mavx512`, `#pragma GCC target("avx512...")` and `-march=native` are
        # all ways to make one live, so all three are named.
        # Comments are not compiled, so they are not judged: upstream's own AVX2
        # sources end an `#else`/`#endif` with `// defined(__AVX512F__) ...`,
        # and scanning raw text flagged those comments on a candidate tree that
        # had copied the style. Line structure is preserved, so the numbers
        # below still point at the offending line.
        hits=$(printf '%s\n' "$added" \
            | python3 /tests/strip_comments.py - 2>/dev/null \
            | grep -nE 'mavx512|_mm512|__m512|__AVX512|HAVE_AVX512|target\("avx512|target\(.avx512|march=native|std::thread|pthread_create|pthread_join|#pragma omp|<thread>|std::async' || true)
        if [ -n "$hits" ]; then
            echo "  ENVELOPE VIOLATION in $rel:"
            printf '%s\n' "$hits" | head -5 | sed 's/^/    /'
            bad=1
        fi
    }

    # written_by_candidate <relative path>: did the candidate write this file?
    # Yes when the candidate's copy is byte-different from the TASK's base --
    # absent from it (the candidate added the file) or different from it (the
    # candidate edited it). Without a task-base tree every file in the step's
    # diff is judged, which is what this check did before the scoping existed.
    written_by_candidate() {
        local rel="$1"
        [ -z "$taskbase" ] && return 0
        if [ ! -f "$taskbase/$rel" ]; then
            return 0
        fi
        ! cmp -s "$taskbase/$rel" "$cand/$rel"
    }

    local line rel nm
    while IFS= read -r line; do
        case "$line" in
            "Files "*)
                rel="${line#Files }"; rel="${rel%% and *}"; rel="${rel#$base/}"
                [ "$rel" = "$line" ] && continue
                is_source "$rel" || continue
                ndiff=$((ndiff + 1))
                if written_by_candidate "$rel"; then
                    nwritten=$((nwritten + 1))
                    scan_one "$rel"
                else
                    nunwritten=$((nunwritten + 1))
                fi
                ;;
            "Only in $base"*)
                # A file of this step's base that the candidate does not have.
                # It has no candidate line to judge -- a deletion cannot carry
                # one -- and it is counted rather than read at a path that
                # cannot exist. The earlier parse built `$cand/base/...` out of
                # this line and handed it to grep, so the count below claimed
                # files it had never looked at.
                nm="${line##*: }"
                is_source "$nm" || continue
                nremoved=$((nremoved + 1))
                ;;
            "Only in $cand"*)
                rel="${line#Only in }"; nm="${rel##*: }"; rel="${rel%%: *}"
                rel="${rel#$cand}"; rel="${rel#/}"
                is_source "$nm" || continue
                nadded=$((nadded + 1))
                if written_by_candidate "$rel/$nm"; then
                    nwritten=$((nwritten + 1))
                    scan_one "$rel/$nm"
                else
                    nunwritten=$((nunwritten + 1))
                fi
                ;;
        esac
    done < <(diff -rq "$base" "$cand" 2>/dev/null)

    local nscoped=$((ndiff + nadded + nremoved))
    echo "  envelope: ${nscoped} scoped file(s) differ from this step's base" \
         "(${ndiff} modified, ${nadded} added, ${nremoved} missing);" \
         "${nwritten} written by the candidate, ${nunwritten} unchanged from the task's base"
    if [ "$nscoped" -eq 0 ]; then
        echo "  envelope: 0 of 0 scoped files changed by the candidate -- the library is exactly this step's base"
        return 0
    fi
    if [ "$nwritten" -eq 0 ]; then
        echo "  envelope: $((nscoped - nwritten)) of ${nscoped} scoped files unchanged by the candidate -- check skipped" \
             "(nothing this step's candidate wrote; the difference is the ladder's earlier work, not the candidate's)"
        return 0
    fi
    echo "  envelope examined $nfiles candidate-written source files, $nlines contributed lines" \
         "($nskip lines are upstream's own and $nguard lie in an AVX-512 guard; neither was judged)"
    [ "$bad" = 0 ]
}
