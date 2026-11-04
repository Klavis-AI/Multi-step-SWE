#!/usr/bin/env bash
# Hold the candidate to the instruction-set envelope the reference implementation
# uses for this milestone.
#
#     check_isa.sh <base_tree> <candidate_tree>     -> 0 pass, 1 fail
#
# Why this exists
# ---------------
# The oracle is only a yardstick when both sides may use the same machine.  The
# reference for every entry point in this ladder is upstream's AVX2 backend
# (`src/bmavx2.h`, 256-bit registers, `-march=skylake -mavx2 -mpopcnt`), and the
# target host has AVX2 and no AVX-512.  A candidate that reaches for AVX-512
# would not be solving the reference's problem, and the speedup it measured
# would compare nothing.
#
# What is checked, and why this is the whole check
# -----------------------------------------------
# Compiler flags are already out of reach: the verifier compiles the harness
# itself, with its own ENVELOPE, against the candidate's headers.  Nothing the
# candidate writes into the build system can widen the instruction set.  The
# only remaining route is a construct inside a header, so that is what this
# looks for:
#
#   1. AVX-512 intrinsics, registers or mnemonics in any line the candidate ADDED;
#   2. `#pragma GCC target` / `__attribute__((target(...)))`, which would let a
#      single header widen the instruction set for the rest of the translation
#      unit without any build-system change at all.
#
# The set of files inspected is the set the ladder's own stripper treats as
# sources (`vendor/strip.py` SOURCE_EXT: `.h .hpp .hh .ht .c .cc .cpp .cxx .inl
# .ipp .tcc`).  It was `.c|h|cc|cpp|cxx|hpp|inc` once, which let a `.ipp` or
# `.hh` file smuggle the tokens past a gate that then reported "examined
# nothing"; the two lists must not drift.
#
# Matching is done on code only: comments AND string/char literal bodies are
# masked before the tokens are searched, exactly as the stripper's own
# `mask_comments` does.  Without the literal masking a diagnostic string that
# mentions `_mm512_` fails an in-envelope tree, and -- worse -- a `//` inside a
# string looked like a comment and swallowed the rest of the line, so a real
# `_mm512_...` call after such a string went unseen.
#
# Scope: added lines only, whole tree.  That is deliberately not scoped to "this
# step's files" -- writing AVX-512 is not allowed at any milestone, so charging
# every milestone for it is uniform rather than an inherited-work penalty.  The
# base tree's own content is never inspected, so upstream's AVX-512 header (whose
# kernels this ladder keeps out of the base entirely) cannot cause a false
# failure.
#
# Who decides to call it: helpers.sh's envelope_check.  It counts the files this
# milestone owns (/tests/SCOPE.stepNN, derived from its oracle patch), prints the
# count, and skips this script only for a tree that the candidate did not add to
# or modify anywhere -- such a tree carries no added line, so there is nothing
# here to find.  Every tree the candidate added to or modified, in this
# milestone's scope or outside it, is checked by this script exactly as before,
# whole-tree.  The old unconditional call and this one inspect the same set of
# trees; the difference is that the vacuous case is now a decision with counts
# printed next to it, not an empty diff discovered afterwards.
set -u

_AVX512='_mm512_|__m512[a-z]*|_mmask[0-9]+|__mmask[0-9]+|\<zmm[0-9]+|vternlog|vpternlog[a-z]*|vpermi2[a-z]*|vpermt2[a-z]*|vpmovm2[a-z]*|vpmovmb|_kand_|_kor_|_kxnor_|_knot_|_kadd_'
# NOTE: the added lines are lowercased before matching, so every alternative
# here must be spelled in lowercase.  Uppercase spellings such as `GCC` would
# not match, and the pragma route -- which instruction.md bans by name -- would
# pass silently.  Do not "tidy" the case of this pattern.
_TARGET='#[[:space:]]*pragma[[:space:]]+gcc[[:space:]]+target|__attribute__[[:space:]]*\(\([[:space:]]*target[[:space:]]*\(|__attribute__[[:space:]]*\(\([[:space:]]*cpu_specific'

check_isa() {
    local base="$1" cand="$2"
    local added hits flags nlines

    [ -d "$cand" ] || { echo "  ISA check: no candidate tree at $cand"; return 1; }

    # Every added line of every C/C++ source, comments and literal bodies
    # stripped, lowercased.  The literal handling mirrors strip.py's
    # mask_comments: `'` and `"` bodies (escapes included) are blanked, and a
    # literal left open at end of line stays open into the next one.
    added=$(diff -ruN "$base" "$cand" 2>/dev/null | awk '
        function strip(s,   out, i, n, c, c2) {
            out = ""; n = length(s); i = 1
            while (i <= n) {
                if (inblock) {
                    c2 = index(substr(s, i), "*/")
                    if (c2 == 0) { i = n + 1 } else { inblock = 0; i += c2 + 1 }
                } else if (inlit != "") {
                    c = substr(s, i, 1)
                    if (c == "\\")       { i += 2 }
                    else if (c == inlit) { inlit = ""; i += 1 }
                    else                 { i += 1 }
                } else {
                    c  = substr(s, i, 1)
                    c2 = substr(s, i, 2)
                    if (c2 == "/*")                { inblock = 1; i += 2 }
                    else if (c2 == "//")           { i = n + 1 }
                    else if (c == DQ || c == SQ)   { inlit = c; i += 1 }
                    else                           { out = out c; i += 1 }
                }
            }
            return out
        }
        BEGIN { code = 0; inblock = 0; inlit = ""
                DQ = sprintf("%c", 34); SQ = sprintf("%c", 39) }
        substr($0, 1, 4) == "+++ " {
            code = ($2 ~ /\.(c|h|cc|cpp|cxx|hpp|inc|hh|ht|inl|ipp|tcc)$/) ? 1 : 0
            inblock = 0; inlit = ""
            next
        }
        substr($0, 1, 1) != "+" { next }
        !code { next }
        { line = strip(substr($0, 2)); if (line ~ /[^ \t]/) print tolower(line) }
    ')
    nlines=$(printf '%s\n' "$added" | grep -c . || true)

    if [ -z "$added" ] || [ "$nlines" -eq 0 ]; then
        # Not a failure: a tree that added no C/C++ line at all (an untouched
        # workspace, or one whose whole change is in a header the measured build
        # does not include) has nothing that could violate the envelope.  Say so
        # loudly rather than reporting a clean pass, because "0 violations over 0
        # lines" and "0 violations over 5,000 lines" must not read the same.
        echo "  ISA check: examined nothing -- no added C/C++ lines between"
        echo "    $base"
        echo "    $cand"
        echo "  (no AVX-512 construct can be present in a change that adds no code)"
        return 0
    fi

    hits=$(printf '%s\n' "$added" | grep -nEo "$_AVX512" | sort -u -t: -k2 | head -12 || true)
    flags=$(printf '%s\n' "$added" | grep -nEo "$_TARGET" | head -6 || true)

    if [ -n "$hits" ] || [ -n "$flags" ]; then
        echo "  AVX-512 CONSTRUCT FOUND. This milestone targets AVX2 or"
        echo "  narrower: the reference implementation uses 256-bit registers here"
        echo "  and the target host has no AVX-512. Offending tokens:"
        { [ -n "$hits" ] && printf '%s\n' "$hits"; [ -n "$flags" ] && printf '%s\n' "$flags"; } \
            | sed 's/^[0-9]*://' | sort -u | sed 's/^/    /'
        return 1
    fi

    echo "  ISA check: AVX2 envelope respected ($nlines added C/C++ line(s) inspected)"
    return 0
}

# Direct invocation: check_isa.sh <base> <cand>
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    check_isa "$1" "$2" || exit 1
    exit 0
fi
