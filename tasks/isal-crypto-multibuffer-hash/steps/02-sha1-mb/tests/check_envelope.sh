#!/usr/bin/env bash
# Capability envelope, scoped to the step under test.
#
#     check_envelope <base-tree> <candidate-tree> <stage-dirs> [<label>]
#         -> 0 pass, 1 fail (the caller turns a fail into correctness 0)
#
# The rule
# --------
# The candidate may use any capability the GRADING HOST executes, and nothing
# else -- that is what makes it the reference solution's peer rather than a
# different machine's.  On this host that set is SSE2, AVX2 (256-bit) and the
# SHA1/SHA256 extensions (`sha_ni`); AVX-512, VAES, GFNI, SHA512 and SM3 are
# absent.  Code that requires an absent capability cannot be scored here at
# all: it faults at first execution, the digest comparison fails, and
# correctness is 0.  That run-time fault is the enforcement; this gate is its
# static half, so the same offence is also named in the file that commits it,
# before the fault, rather than after a one-line SIGILL in a child process.
#
# Scope
# -----
# Only files under THIS step's stage directories are inspected, and only the
# lines the candidate added to them.  The workspace is cumulative and /base is
# the task's base tree, so a diff against /base at step k also contains every
# earlier stage the candidate implemented; a gate reading that diff wholesale
# fails a late step for early work.
#
# Assembly is not token-scanned.  The instruction permits it, upstream's own
# kernels are assembly, and upstream annotates its assembly with the intrinsic
# each instruction corresponds to -- so reading it can only produce false
# positives.  Assembly that executes is covered by the run-time fault; assembly
# that is never selected cannot be scored.
#
# Which files that is, is decided per file and not by suffix alone.  `.asm`,
# `.S`, `.s` and `.nas` are assembly.  A `.inc` is assembly only when an
# assembly file includes it; a `.inc` no assembly file includes is C -- e.g.
# a `sm3_mb_xkernel.inc` of intrinsics macros included by a `.c` -- and is
# token-scanned like any other C source, so a suffix cannot carry an
# out-of-envelope token past the static half of this gate.
#
# A token is only a capability request where it is code.  Inside a string
# literal it is data -- a diagnostic message naming the intrinsic it falls back
# from, a tier name, a test string -- and inside a build-wiring file a `#`
# comment is not read by any compiler at all.  Both are stripped before the
# scan (a string literal is one of the two places a token can hide, and the
# other, assembly, is out of scope for the reason above); the one string that
# is kept is the argument of a `target` attribute, because there the string
# *is* the request.
#
# Build flags are the one global thing: a compiler flag is not scoped to a
# directory, so a flag that asks for a capability this host does not have is an
# offence wherever it appears.  Only flags that ASK for an absent capability
# are flagged; -mavx2, -msse4.2 and -march=native stay legal.
#
# Vacuity is stated, not implied
# ------------------------------
# A tree that changed nothing in this step's scope passes and prints the count
# it examined.  A no-op is the tree it started from: it cannot be out of
# envelope, and the gate must be able to say so rather than pass silently.
# Every branch below prints what it looked at.
set -u

# Each capability is written in both spellings it is reached by: the intrinsic
# name a candidate writes, and the compiler builtin underneath it.  A gate that
# can be sidestepped by spelling is not a gate.
_ENV_ISA_TOKENS='_mm512_[a-z0-9_]+|__m512[a-z]*|\bzmm[0-9]+\b|__builtin_ia32_[a-z0-9_]*512[a-z0-9_]*|gf2p8|_mm(256)?_aes(enc|dec)(last)?_epi128|__builtin_ia32_vaes[a-z0-9_]*|_mm(256)?_sha512[a-z0-9_]*|__builtin_ia32_sha512[a-z0-9_]*|_mm(256)?_sm3[a-z0-9_]*|__builtin_ia32_sm3[a-z0-9_]*'
_ENV_TARGET_TOKENS='target[[:space:]]*\([[:space:]]*"(avx512[a-z0-9]*|avx10[a-z0-9.]*|vaes|gfni|sha512|sm3)"'
_ENV_FLAG_TOKENS='-mavx512[a-z0-9]*|-mvaes|-mgfni|-msha512|-msm3|-mavx10[a-z0-9.]*|-march=(skylake-avx512|cascadelake|cooperlake|icelake[a-z-]*|tigerlake|sapphirerapids|emeraldrapids|graniterapids|knl|knm|znver4|znver5|x86-64-v[34])'

# Name the capability a hit asks for, for the operator reading the log.
_env_name_hit() {
    case "$1" in
        *sha512*)                                echo "the SHA512 extensions, which this host does not execute" ;;
        *sm3*)                                   echo "the SM3 extension, which this host does not execute" ;;
        *gf2p8*)                                 echo "GFNI, which this host does not execute" ;;
        *vaes*|*aesenc_epi128*|*aesdec_epi128*)  echo "VAES, which this host does not execute" ;;
        *avx512*|*zmm*|*__m512*|*_mm512_*|*512*) echo "AVX-512, which this host does not execute" ;;
        *)                                       echo "$1" ;;
    esac
}

# _env_added_code <base-file> <cand-file> [c|wiring]
#   The lines the candidate added to a source file, comments removed.  A file
#   that does not exist in the base counts entirely; a deleted one adds none.
#   In `wiring` mode (# starts a comment in every build-wiring language this
#   gate scans) the `#`-to-EOL comment is removed too; in C mode a string
#   literal's contents are dropped -- data is not a capability request --
#   except where the literal is a `target` attribute's argument.
_env_added_code() {
    local base="$1" cand="$2" mode="${3:-c}"
    if [ -e "$base" ]; then
        diff -u "$base" "$cand" 2>/dev/null
    else
        diff -u /dev/null "$cand" 2>/dev/null
    fi | awk -v mode="$mode" '
        function skipstr(s, i,   q, e, n) {
            q = substr(s, i, 1); n = length(s); i++
            while (i <= n) {
                e = substr(s, i, 1)
                if (e == "\\") { i += 2; continue }
                if (e == q) return i + 1
                i++
            }
            return i
        }
        function strip(s,   out, i, n, c, j) {
            out = ""; n = length(s); i = 1
            while (i <= n) {
                if (inblock) {
                    j = index(substr(s, i), "*/")
                    if (j == 0) { i = n + 1 } else { inblock = 0; i += j + 1 }
                    continue
                }
                c = substr(s, i, 2)
                if (c == "/*") { inblock = 1; i += 2; continue }
                if (c == "//") { i = n + 1; continue }
                if (wiring && substr(s, i, 1) == "#") { i = n + 1; continue }
                if (substr(s, i, 1) == "\"" || substr(s, i, 1) == "\047") {
                    j = skipstr(s, i)
                    if (out ~ /target[ \t]*\([ \t]*$/) { out = out substr(s, i, j - i) }
                    i = j
                    continue
                }
                out = out substr(s, i, 1); i++
            }
            return out
        }
        BEGIN { inblock = 0; wiring = (mode == "wiring") }
        substr($0, 1, 4) == "+++ " { inblock = 0; next }
        substr($0, 1, 1) != "+"    { next }
        { line = strip(substr($0, 2)); if (line ~ /[^ \t]/) print line }
    '
}

# _env_inc_is_assembly <base-tree> <cand-tree> <relative path>
#   True when an assembly file in either tree includes this `.inc`.
#
#   A `.inc` is a text include, and the assembler is the only thing in this
#   tree that consumes one: nasm reaches it with `%include "x.inc"`, gas with
#   `.include "x.inc"`, and a preprocessed `.S` with `#include "x.inc"`.
#   Upstream's own `.inc` files are referenced exactly that way from the
#   sources beside them.  An `.inc` that no assembly file includes is C that a
#   C source includes, and it is scanned like any other C source rather than
#   escaping the gate through its suffix (e.g. a `sm3_mb_xkernel.inc` of
#   intrinsics macros in C, included by a `.c`).
_env_inc_is_assembly() {
    local base="$1" cand="$2" rel="$3" name d f
    name=$(basename "$rel")
    for d in "$base" "$cand"; do
        [ -d "$d" ] || continue
        while IFS= read -r f; do
            [ -n "$f" ] || continue
            if awk -v want="$name" '
                BEGIN {
                    dir = "^[ \t]*(%[ \t]*include|[.][ \t]*include|[#][ \t]*include|[Ii][Nn][Cc][Ll][Uu][Dd][Ee])[ \t]+"
                }
                $0 ~ dir {
                    line = $0
                    sub(dir, "", line)
                    sub(/[ \t]*[;#].*$/, "", line)      # trailing asm comment
                    sub(/[ \t]*\/\/.*$/, "", line)      # trailing // comment
                    gsub(/^[ \t"<]+/, "", line)
                    gsub(/[ \t">]+$/, "", line)
                    sub(/^.*\//, "", line)
                    if (line == want) found = 1
                }
                END { exit found ? 0 : 1 }
            ' "$f" 2>/dev/null; then
                return 0
            fi
        done < <(find "$d" -type f \
                 \( -name '*.asm' -o -name '*.S' -o -name '*.s' -o -name '*.nas' \) 2>/dev/null)
    done
    return 1
}

# _env_changed <base-tree> <cand-tree> <relative dir>
#   One entry per file or directory the candidate added, removed or modified
#   under <dir>, as a path relative to the tree root.
_env_changed() {
    local base="$1" cand="$2" d="$3"
    diff -rq "$base/$d" "$cand/$d" 2>/dev/null | awk -v b="$base/" -v c="$cand/" '
        /^Files /   { p = $2; sub("^" b, "", p); print p }
        /^Only in / { dir = $3; sub(/:$/, "", dir); rel = dir "/" $4
                      sub("^" c, "", rel); sub("^" b, "", rel); print rel }
    ' | sort -u
}

check_envelope() {
    local base="$1" cand="$2" dirs="${3:-}" label="${4:-this step}"
    local d f p n_scoped=0 n_changed=0 n_c=0 n_asm=0 n_wiring=0 n_newdir=0
    local hits="" flags="" bad=0
    local -a changed=()

    [ -n "$dirs" ] || { echo "  envelope: no stage list for this step -- nothing to scope the check to"; return 1; }

    # ---- what is in scope, and what the candidate did to it -----------------
    for d in $dirs; do
        [ -d "$base/$d" ] || { echo "  envelope: $d is not in the base tree"; bad=1; continue; }
        [ -d "$cand/$d" ] || { echo "  envelope: $d is MISSING from the candidate tree"; bad=1; continue; }
        n_scoped=$((n_scoped + $(find "$base/$d" -type f 2>/dev/null | wc -l)))
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            if [ -d "$cand/$p" ]; then            # a directory the candidate added
                n_newdir=$((n_newdir + 1))
                while IFS= read -r f; do
                    [ -n "$f" ] || continue
                    changed+=("$f"); n_changed=$((n_changed + 1))
                done < <(find "$cand/$p" -type f 2>/dev/null | sed "s|^$cand/||" | sort)
                continue
            fi
            changed+=("$p"); n_changed=$((n_changed + 1))
        done < <(_env_changed "$base" "$cand" "$d")
    done
    [ "$bad" -eq 0 ] || return 1

    echo "  envelope: scope [$dirs] holds $n_scoped file(s) in the base tree; the candidate changed $n_changed entr(ies)"
    if [ "$n_changed" -eq 0 ]; then
        echo "  envelope: 0 of $n_scoped scoped file(s) changed by the candidate -- check skipped (vacuous: an unchanged tree is the tree it started from)"
        return 0
    fi

    # ---- static scan, scoped: the C/C++ sources the candidate touched -------
    for f in "${changed[@]}"; do
        case "$f" in
            *.asm|*.S|*.s|*.nas) n_asm=$((n_asm + 1)); continue ;;
            *.inc)
                # A `.inc` is only assembly when an assembly file includes it
                # (_env_inc_is_assembly); otherwise it is C included by a `.c`
                # and is token-scanned below like any other C source.
                if _env_inc_is_assembly "$base" "$cand" "$f"; then
                    n_asm=$((n_asm + 1)); continue
                fi
                ;;
            *.c|*.h|*.cc|*.cpp|*.cxx|*.hpp|*.hh) ;;
            *) continue ;;
        esac
        [ -f "$cand/$f" ] || continue
        n_c=$((n_c + 1))
        hits="$hits$(_env_added_code "$base/$f" "$cand/$f" c \
            | grep -nEo -e "$_ENV_ISA_TOKENS" -e "$_ENV_TARGET_TOKENS" 2>/dev/null \
            | sed "s|^|$f:|" || true)"$'\n'
    done

    # ---- build flags, global by nature -------------------------------------
    while IFS= read -r f; do
        [ -n "$f" ] || continue
        case "$(basename "$f")" in
            Makefile*|*.mk|*.am|*.cmake|CMakeLists.txt|*.in|configure*) ;;
            *) continue ;;
        esac
        [ -f "$cand/$f" ] || continue
        n_wiring=$((n_wiring + 1))
        flags="$flags$(_env_added_code "$base/$f" "$cand/$f" wiring \
            | grep -oE -e "$_ENV_FLAG_TOKENS" 2>/dev/null \
            | sed "s|^|$f: |" || true)"$'\n'
    done < <(diff -rq -x bin -x '*.o' -x '*.a' -x '*.so' "$base" "$cand" 2>/dev/null | awk -v b="$base" -v c="$cand" '
        /^Files /   { p = $2; sub("^" b "/", "", p); print p }
        /^Only in / { dir = $3; sub(/:$/, "", dir); rel = dir "/" $4
                      if (dir == c) { sub("^" c "/", "", rel); print rel } }
    ' | sort -u)

    hits=$(printf '%s\n' "$hits" | sed '/^[[:space:]]*$/d')
    flags=$(printf '%s\n' "$flags" | sed '/^[[:space:]]*$/d')

    if [ -n "$hits" ] || [ -n "$flags" ]; then
        echo "  envelope: OUT OF ENVELOPE. The candidate's own code asks for a capability"
        echo "  the grading host does not execute, so it cannot be a peer of the reference"
        echo "  solution and cannot be scored here:"
        if [ -n "$hits" ]; then
            printf '%s\n' "$hits" | sort -u | head -20 | while IFS= read -r h; do
                printf '    %s  (%s)\n' "$h" "$(_env_name_hit "${h#*:}")"
            done
        fi
        [ -n "$flags" ] && printf '%s\n' "$flags" | sort -u | head -10 | sed 's/^/    /'
        echo "  This host executes SSE2, AVX2 (256-bit) and the SHA1/SHA256 extensions;"
        echo "  AVX-512, VAES, GFNI, SHA512 and SM3 are absent. A candidate may use what"
        echo "  the reference itself may use on this host, and nothing wider."
        return 1
    fi

    echo "  envelope: clean -- $n_c C/C++ source(s) inspected line by line, $n_asm assembly file(s) left to the run-time fault check, $n_wiring build-wiring file(s) checked for out-of-envelope flags"
    return 0
}
