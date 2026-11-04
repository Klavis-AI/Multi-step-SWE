#!/usr/bin/env bash
# The graded ceiling is AVX2 / 256-bit.  This host reports `avx avx2 sse4_2`
# and no AVX-512, so the dispatcher can only ever install haswell, westmere or
# fallback, and AVX-512 code cannot run here at all -- the ceiling is enforced
# by the hardware.  This is the static half: the sources of the implementations
# that CAN be installed must not carry AVX-512 constructs, because a candidate
# who writes them is either aiming above the machine or about to crash on it.
#
# Reference and candidate are held to the same rule, and the check is scoped to
# code: the construct has to be in what the file compiles to, so a file whose
# compiled content is AVX2-or-narrower passes whatever its comments or string
# literals say.  tests/vendor/strip_code.py removes both before the match -- a
# gate that fires on a comment fires on the honest tree that documents the
# envelope it obeys.  icelake is excluded because it IS the AVX-512 back-end,
# present in the tree at every level as upstream wrote it.
#
# The scan set is every source file the tree can compile, not just the two
# extensions upstream happens to use: the find below takes .cpp/.h/.c/.hpp/
# .inl/.inc anywhere under src/ and include/ (minus the exclusions), including
# the dispatcher src/implementation.cpp.  Widening it costs nothing -- only
# files that DIFFER from the inherited tree are grepped, so an untouched tree
# is still skipped -- and a file the build reads but the gate cannot see is a
# hole the rule does not cover.
#
# Where this gate sits.  It is a rule of the task ("this milestone is scored on
# AVX2 or narrower"), not a statement about behaviour: a construct in a file
# the candidate touched fails the step, and a tree that touched nothing passes
# -- vacuously, and it says so, because "the tree changed nothing" and "the
# tree is within the envelope" are the same statement here.  The reference tree
# this gate is scoped against is provably clean in every file it scans (the
# count below is taken on it, and the grep reports zero hits), so a construct
# found in a file the candidate changed cannot have come from anywhere else.
check_isa() {
    local src="$1" inherited="$2"
    local files n changed nch bad f code
    files=$(find "$src/src" "$src/include" -type f \
              \( -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.hpp' \
                 -o -name '*.inl' -o -name '*.inc' \) \
              -not -path '*/icelake/*' -not -name 'icelake*' -not -name 'avx512.h' \
              -not -name 'isadetection.h' 2>/dev/null)
    n=$(printf '%s\n' "$files" | grep -c .)
    if [ "$n" -lt 40 ]; then
        echo "  envelope check examined only $n files; refusing to report a pass"
        return 1
    fi
    # Of the files this gate can speak about, which ones did the candidate
    # actually change?  Relative to the tree it inherited (the verifier's own
    # reconstruction of it), which is what "scoped to the candidate's work"
    # means and what keeps a no-op from being charged for behaviour it did not
    # change.
    changed=$(while IFS= read -r f; do
        [ -n "$f" ] || continue
        rel="${f#"$src"/}"
        if [ ! -f "$inherited/$rel" ] || ! cmp -s "$f" "$inherited/$rel"; then
            printf '%s\n' "$f"
        fi
    done <<< "$files")
    nch=$(printf '%s\n' "$changed" | grep -c .)
    if [ "$nch" -eq 0 ]; then
        echo "  envelope: ${n} of ${n} scoped files unchanged by the candidate -- check skipped"
        echo "  (the inherited tree carries no AVX-512 construct in any scannable file, so there is nothing here it could have introduced)"
        return 0
    fi
    echo "  envelope: ${nch} of ${n} scoped files changed by the candidate; scanning those"
    bad=""
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        # Code only.  If the strip ever fails, fall back to the raw text: a
        # gate must not miss a construct because its own helper broke.
        code=$(python3 /tests/vendor/strip_code.py "$f" 2>/dev/null) || code=$(cat "$f")
        printf '%s' "$code" | grep -qE '_mm512_|__m512|__mmask[0-9]|__AVX512' \
            && bad="${bad}${f}"$'\n'
    done <<< "$changed"
    if [ -n "$bad" ]; then
        echo "  AVX-512 constructs in sources the candidate changed, which build the AVX2 path:"
        printf '%s' "$bad" | sed 's/^/    /'
        return 1
    fi
    echo "  no AVX-512 constructs in the ${nch} changed files that build the AVX2 path"
    return 0
}
