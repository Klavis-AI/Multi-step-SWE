#!/usr/bin/env bash
# Hold the candidate to the instruction-set envelope this task is scored on.
#
#     check_isa <candidate_tree> <markers_file> <scope_file>
#
# Why this exists
# ---------------
# The candidate and the reference must be allowed the same machine, or beating
# the reference measures access rather than skill.  Here the reference uses
# 256-bit AVX2 and nothing wider -- the whole backend is AVX2, with no AVX-512
# sibling anywhere -- and the grading host executes AVX2 and no AVX-512.  So the
# envelope is "AVX2, 256-bit" and both sides sit at it.  What this gate rejects
# is a candidate that reaches for a wider register file anyway: it cannot pay
# off on this host, so a tree that uses it is either broken or measuring
# something the grader does not run.
#
# What it inspects, and why it is not the whole diff
# --------------------------------------------------
# The workspace is cumulative and /base is the reference tree for THIS step,
# while the candidate's tree carries its own earlier milestones -- so a diff
# against /base also contains work this step did not ask for, and checking it
# wholesale charges a late step for early work.  Two restrictions together
# scope this to the candidate's own work on this step:
#
#   WHERE: a file is in scope only when it is one of
#     (a) the paths this step's own reference implementation writes
#         (SCOPE.stepNN, generated from steps/NN/solution/oracle.patch: the
#         step's implementation files, the dispatch layer they are wired
#         through, the amalgamation and ABI-checker references that go with
#         them, and the backend source directory its implementation lives in),
#         or
#     (b) a file that names one of this step's own backend hooks, which is how
#         a dispatch site (poly.c / poly_kl.c / polyvec_lazy.c / meta.h) or a
#         candidate's own hook body is reached wherever it is put.
#
#   WHAT: only the lines the candidate ADDED relative to the reference copy at
#     the same path, with comments removed by vendor/strip_comments.py before
#     the comparison.  A file identical to /base is inherited, not this
#     candidate's work; a line the reference already has is not code the
#     candidate wrote; and a comment -- the ordinary prose a candidate writes
#     to explain what it did, including prose about this very envelope -- is
#     not code either, so a comment that mentions `zmm` or `vpternlogd` does
#     not fail the step.  Comments are stripped, not the lines' endings, so an
#     added line that hides a wide token behind a semicolon is still found.
#     A tree that added nothing but comments in this step's scope is inspected
#     nowhere, and the `envelope:` line below says so out loud -- so "this
#     step's code respects the ceiling" is only ever claimed about code the
#     candidate actually wrote.  (Deleting code is not reaching for a wider
#     machine, and this gate does not consider it.)
#
#   A tree that contains none of the step's files at all is refused rather
#     than passed: `0 of 0 scoped file(s)` is not "the candidate wrote
#     nothing", it is "this gate examined nothing", and a gate must be able to
#     tell those apart.  Such a tree fails the build gate first, so this is a
#     backstop, not a route anyone gradable takes.
#
#   Build flags are global by nature: any added flag that widens the compiler
#     is an offence wherever it appears -- but only a build file can widen
#     anything, so that scan is narrowed to the added lines of this tree's
#     Makefiles and *.mk files, with their comments removed.  A README, a
#     design note or a comment that merely mentions `-mavx512f` is not a wider
#     machine and must not fail a step; a `CFLAGS += -mavx512f` in a Makefile
#     still does.
#
# Residual, stated rather than hidden: a candidate that puts an implementation
# in a file that neither names one of this step's hooks nor sits under the
# step's own backend directory (SCOPE.stepNN (a)) is not inspected.  In
# practice the dispatch sites named above do name the hooks, an implementation
# reachable from the scored entry points goes through or beside those files,
# and that backend directory is where the build system finds such a file at all
# -- but it is not a proof, and the constructs this gate looks for cannot
# execute on this host anyway.  This is a fairness check, not a sandbox.  A
# token scan also cannot see a *capability* the compiler is asked for in some
# other spelling (a `#pragma GCC target("avx512f")` without a wide token
# anywhere); what it can see is a wider register file, a wider instruction or a
# wider build flag, and those are what the envelope names.
set -u

# Deliberately no bare `k1`..`k7`: those are the AVX-512 mask registers, but
# they are also ordinary variable names in C, and a first cut of this gate
# failed a candidate on two loop counters in poly.c.
_WIDE='\<zmm[0-9]+\>|\<__m512[a-z_]*\>|\<_mm512_[a-z0-9_]*|vperm[it]2[bf]16x|vpternlogd|vpermb|vpmov[a-z0-9]*x[2-9]|%k[1-7]'
_FLAGS='\-mavx512[a-z0-9]*|\-march=[a-z0-9_-]*(skylake-avx512|cascade|icelake|sapphirerapids|znver4|znver5|graniterapids)|\-mprefer-vector-width=512|\-mabi=avx512'

# source-like names: a file under a directory in scope counts when it is a
# source file, so a candidate's own kernel file is covered while build output
# that happens to land there (objects, dependency files) is not.
_SRC='*.c *.h *.inc *.S *.s *.asm'

# the reference tree and the comment stripper are verifier-side; both are
# overridable so the gate can also be exercised outside a verifier image
BASE=${ISA_BASE:-/base}
STRIP=${ISA_STRIP:-/tests/vendor/strip_comments.py}

# _isa_scope <cand> <markers> <scope>: the candidate files this step's envelope
# covers, one path relative to the tree root per line, deduplicated.
_isa_scope() {
    local cand="$1" markers="$2" scope="$3" r ext
    {
        while IFS= read -r r; do
            [ -n "$r" ] || continue
            case "$r" in \#*) continue ;; esac
            r="${r%/}"
            if [ -d "$cand/$r" ]; then
                for ext in $_SRC; do
                    find "$cand/$r" -type f -name "$ext" 2>/dev/null
                done
            elif [ -f "$cand/$r" ]; then
                printf '%s\n' "$cand/$r"
            fi
        done < "$scope"

        grep -rlF -f "$markers" "$cand" 2>/dev/null | grep -vE '/\.git/' | head -200
    } | sed "s|^$cand/||" | sort -u
}

# _isa_decomment <file>: the file with its comments blanked out (line
# structure preserved).  Falls back to the file itself if the helper is
# missing, which errs towards inspecting more, never less.
_isa_decomment() {
    if [ -f "$STRIP" ]; then
        python3 "$STRIP" "$1" 2>/dev/null
    else
        cat "$1"
    fi
}

# _isa_added <cand> <relpath>: the lines this candidate added relative to the
# reference copy at the same path (the whole file, when the reference has
# none), comments removed on both sides first.
_isa_added() {
    local cand="$1" p="$2"
    if [ -f "$BASE/$p" ]; then
        diff -u <(_isa_decomment "$BASE/$p") <(_isa_decomment "$cand/$p") 2>/dev/null || true
    elif LC_ALL=C grep -qI '' "$cand/$p" 2>/dev/null; then
        _isa_decomment "$cand/$p" | sed 's/^/+/'
    fi | awk '/^\+/ && substr($0,1,3) != "+++" { sub(/^\+/,""); print }'
}

check_isa() {
    local cand="$1" markers="$2" scope="${3:-}"
    local n_scope=0 n_added=0 p added hits="" flags="" inspected=""

    if [ ! -s "$markers" ]; then
        echo "  ISA check: no markers for this step -- gate cannot run, reporting as inspected 0"
        return 1
    fi
    if [ ! -s "$scope" ]; then
        echo "  ISA check: no scope for this step -- gate cannot run, reporting as inspected 0"
        return 1
    fi

    while IFS= read -r p; do
        [ -n "$p" ] || continue
        case "$p" in */.git/*) continue ;; esac
        n_scope=$((n_scope + 1))
        added=$(_isa_added "$cand" "$p")
        [ -n "$added" ] || continue
        n_added=$((n_added + 1))
        inspected="$inspected $p"
        hits="$hits$(
            printf '%s\n' "$added" \
                | tr 'A-Z' 'a-z' | grep -oE -e "$_WIDE" 2>/dev/null || true
        )"$'\n'
    done < <(_isa_scope "$cand" "$markers" "$scope")
    hits=$(printf '%s\n' "$hits" | sed '/^$/d' | sort -u | head -12)

    # build flags are global by nature: any added flag that widens the compiler
    # is an offence wherever it appears -- but only a build file can widen the
    # build, so the scan reads this tree's Makefiles and *.mk files, added
    # lines only, comments removed.
    flags=$(
        find "$cand" -type f \
            \( -name Makefile -o -name makefile -o -name GNUmakefile \
               -o -name '*.mk' -o -name '*.mak' \
               -o -name CMakeLists.txt -o -name '*.cmake' \) 2>/dev/null \
            | grep -vE '/\.git/' | while IFS= read -r f; do
                p=${f#"$cand/"}
                if [ -f "$BASE/$p" ]; then
                    diff -u "$BASE/$p" "$f" 2>/dev/null || true
                elif LC_ALL=C grep -qI '' "$f" 2>/dev/null; then
                    sed 's/^/+/' "$f"
                fi
            done \
            | awk '/^\+/ && substr($0,1,3) != "+++" { sub(/^\+/,""); print }' \
            | sed -e 's/#.*//' \
            | tr 'A-Z' 'a-z' | grep -oE -e "$_FLAGS" 2>/dev/null | sort -u | head -6
    )

    if [ -n "$hits" ] || [ -n "$flags" ]; then
        echo "  WIDER-THAN-256-BIT CODE FOUND in this step's implementation. The"
        echo "  reference backend is AVX2 and the grading host executes AVX2, so"
        echo "  AVX2 is the ceiling both sides are scored at. Offending tokens:"
        { [ -n "$hits" ] && printf '%s\n' "$hits"; [ -n "$flags" ] && printf '%s\n' "$flags"; } \
            | sort -u | sed 's/^/    /'
        if [ -n "$inspected" ]; then
            echo "  in:"
            printf '%s\n' $inspected | sed 's/^/    /'
        fi
        return 1
    fi

    if [ "$n_scope" -eq 0 ]; then
        echo "  envelope: none of this step's scoped files exist in the candidate tree --"
        echo "  refusing to pass: this gate examined nothing, which is not the same as"
        echo "  'the candidate wrote nothing wider than AVX2'"
        return 1
    fi

    if [ "$n_added" -eq 0 ]; then
        echo "  envelope: 0 of ${n_scope} scoped file(s) carry code added by the candidate -- check skipped"
        echo "  (${n_scope} of this step's own files exist in the candidate's tree and none of"
        echo "   them adds a line of code the reference tree does not already have, so there is"
        echo "   no candidate-authored code for the AVX2/256-bit ceiling to apply to; the"
        echo "   AVX-512-class constructs this gate looks for cannot execute on this host)"
        return 0
    fi

    echo "  ISA check: AVX2/256-bit envelope respected (${n_added} of ${n_scope} scoped file(s) carry added code, inspected)"
    return 0
}
