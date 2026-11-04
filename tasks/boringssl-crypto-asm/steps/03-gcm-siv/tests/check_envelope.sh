#!/usr/bin/env bash
# The capability envelope, enforced rather than merely stated.
#
# The candidate and the reference are held to the same machine: the reference is
# upstream's own perlasm, which on this host tops out at AVX2 with 256-bit
# registers, so the ceiling is whatever the grading host executes and the
# reference already uses. Two things can still leave that envelope, and both are
# checkable:
#
#   1. Changing how the library is compiled, for everything. A wider -march or a
#      different optimisation level is not an optimization of this milestone; it
#      re-compiles the whole library against a different machine model. The
#      build system is outside every milestone's scope, and the reference never
#      touches it.
#
#   2. Writing a kernel for a capability this host does not have. /proc/cpuinfo
#      carries no avx512f, no vaes and no vpclmulqdq, so such code cannot run
#      and cannot be compared against anything. `tests/ISA` states all three
#      and says the verifier refuses them; this scan is that refusal, and it
#      covers the encodings of all three: AVX-512 registers and its EVEX-only
#      mnemonics, and the VAES / VPCLMULQDQ mnemonics and intrinsics (which
#      appear on 128- and 256-bit registers and so carry no `zmm` to catch them
#      by -- the first version of this pattern missed `vpclmulqdq` entirely and
#      a scan that "refused" VPCLMULQDQ could not have fired on it).
#
# Scoped to the milestone, not to the diff against the task's base: in a
# cumulative task the candidate's tree carries every other milestone's work too,
# and comparing the whole diff would blame this step for theirs. Measured -- an
# exploit tree that had done all five milestones was failed at milestone 01
# because upstream's own AVX-512 GCM assembly, restored by milestone 02, was
# attributed to it.
#
# Scoped further, to what the candidate WROTE, and that is a question about
# content, not about paths. A tree that changed nothing -- a no-op, whose answer
# to "did behaviour change" is no by definition -- passes both halves vacuously,
# and the log says so with the counts it examined, because a gate reporting "0
# offending" over an empty input set is indistinguishable from a clean pass
# unless it prints the size of the set. A tree that changed files still gets
# both halves exactly as before: the build configuration is compared, and every
# changed code file is examined.
#
# This is what changed in the second fix pass. The exemption used to be by PATH:
# a changed file was skipped whenever any milestone's reference restores that
# path -- which is where the candidate actually does this milestone's work, so
# the scan could never fire on the files the milestone owns, and out-of-envelope
# code written into, say, crypto/fipsmodule/aes/asm/aesni-x86_64.pl was exempt
# by construction. The exemption is now by CONTENT: the verifier image keeps
# upstream's own copy of every file the ladder's references restore, under
# /ref, and a changed file is exempt exactly while it is byte-identical to that
# copy. (Measured, step 01: upstream's restored AES perlasm with an AVX-512
# block appended reads `offending: 1`, `correctness 0`; the same file untouched,
# and every file an oracle patch restores, reads `offending: 0`.)
#
# The rule is the honest reading of the exemption. The reference DOES write
# out-of-envelope code -- aes-gcm-avx512-x86_64.pl is upstream's own AVX-512
# GHASH, dispatched at run time and never executed here -- and a tree that
# carries upstream's copy of it verbatim is carrying the reference's code, not
# the candidate's. Any deviation from that copy is the candidate's own code,
# and is scanned like any other file they touched. A candidate working inside
# the envelope is unaffected: the scan only rejects the three capabilities this
# host cannot execute.
#
# The scan filter is `crypto|third_party|gen`. The first two were the original
# scope; `gen` was missing even though the instruction invites exactly that
# route ("hand-written assembly in a new file wired into gen/sources.cmake"),
# which left every new assembly file the instruction asks for outside the scan.
#
# Scanned files are comment-stripped first; see vendor/strip_comments.py.
#
# `check_envelope <candidate-tree> <base-tree> <reference-file-list> <step-id>`

check_envelope() {
    local cand="$1" base="$2" reffile="$3" sid="$4"
    local bad=0 compared=0

    # --- 1. the build system ------------------------------------------------
    if [ ! -f "$base/CMakeLists.txt" ] || [ ! -f "$cand/CMakeLists.txt" ]; then
        echo "  ENVELOPE: CMakeLists.txt missing from one of the trees"; return 1
    fi
    compared=$((compared + 1))
    if ! cmp -s "$base/CMakeLists.txt" "$cand/CMakeLists.txt"; then
        echo "  ENVELOPE: CMakeLists.txt was modified; the build configuration is frozen"
        bad=1
    fi
    while read -r f; do
        [ -z "$f" ] && continue
        compared=$((compared + 1))
        if ! cmp -s "$base/cmake/$f" "$cand/cmake/$f"; then
            echo "  ENVELOPE: cmake/$f was modified; the build configuration is frozen"
            bad=1
        fi
    done < <(cd "$base/cmake" && find . -type f | sed 's#^\./##' | sort)
    echo "  build-configuration files compared: $compared"

    # --- 2. code the candidate wrote ---------------------------------------
    # The changed-file list comes from the rebuild step (rsync --out-format).
    # It exists whenever the rebuild ran. An EMPTY list means no file in the
    # delivered tree differs from this milestone's base: a candidate that
    # changed nothing, which is a pass here and is reported as one below. A
    # MISSING list means the rebuild did not produce what this check assumes,
    # and scoping a source scan without knowing what changed is the failure
    # this half exists to prevent -- so that case still fails closed.
    if [ ! -f /tmp/rsync-changed.txt ]; then
        echo "  ENVELOPE: no changed-file list from the rebuild; cannot scope the instruction scan"
        return 1
    fi
    awk '/^>f/ {print $2}' /tmp/rsync-changed.txt | sort -u > /tmp/env_changed.txt
    awk '{print $1}' "$reffile" | sort -u > /tmp/env_ref.txt

    # The reference's own copies, kept by the verifier image's build (see the
    # Dockerfile): upstream's version of every file any reference restores.
    # This is the instrument the content-based exemption compares against, so
    # its absence is not a reason to fall back to a weaker rule -- it is the
    # failure the rule exists to prevent, and it fails closed, loudly.
    local refpresent=0 reftotal
    reftotal=$(grep -c . /tmp/env_ref.txt || true); reftotal=${reftotal:-0}
    while read -r rel; do
        [ -z "$rel" ] && continue
        [ -f "/ref/$rel" ] && refpresent=$((refpresent + 1))
    done < /tmp/env_ref.txt
    echo "  reference copies kept in /ref: $refpresent of $reftotal"
    if [ "$refpresent" -eq 0 ] || [ "$refpresent" -ne "$reftotal" ]; then
        echo "  ENVELOPE: this verifier image has no complete set of the reference's own file copies in /ref;"
        echo "            cannot tell the candidate's code from the reference's, so the scan fails closed"
        return 1
    fi

    # Only code, and only under the trees a kernel can be compiled from.
    grep -E '^(crypto|third_party|gen)/' /tmp/env_changed.txt > /tmp/env_code.txt || true

    local changed code listed scanned exempt
    listed=$(grep -c . /tmp/rsync-changed.txt || true)
    changed=$(grep -c . /tmp/env_changed.txt || true)
    code=$(grep -c . /tmp/env_code.txt || true)
    scanned=0
    exempt=0
    echo "  files changed: $changed, of which under crypto/, third_party/ or gen/: $code"

    local hits=0
    while read -r rel; do
        [ -z "$rel" ] && continue
        case "$rel" in
            *.c|*.cc|*.h|*.inc|*.S|*.asm|*.pl) ;;
            *) continue ;;
        esac
        [ -f "$cand/$rel" ] || continue
        # Exempt exactly while the file is the reference's own copy, byte for
        # byte. Anything else the candidate changed is theirs to answer for.
        if [ -f "/ref/$rel" ] && cmp -s -- "$cand/$rel" "/ref/$rel"; then
            exempt=$((exempt + 1))
            continue
        fi
        scanned=$((scanned + 1))
        # Comments are stripped first. The procedure records this exact mistake
        # twice -- a ban that "failed the oracle on a comment *inside assembly*"
        # -- and it recurred here: upstream's own aes-gcm-avx512 perlasm names
        # `vpternlogd` and `vpxord` in its prose header, which is why that file
        # is also on the reference list, but a candidate's file would have been
        # charged for writing a comment.
        #
        # The stripper is vendor/strip_comments.py, and it replaced a sed
        # one-liner that was silently doing nothing. Two defects, both measured:
        # `\*+` is BRE for a literal '*' followed by a literal '+', so the
        # block-comment pattern required the text to contain `*+` and matched no
        # comment delimiter ever written; and GNU sed's `N` exits without running
        # the rest of the script when there is no next line, so a single-line
        # file was never examined. Net effect: every C block comment reached the
        # scan intact, and a candidate who wrote `/* AVX-512 would need zmm0,
        # absent here */` was charged for a capability they had declined to use.
        # The replacement also keeps string literals, because inline asm lives in
        # a string and the old `;`-stripping truncated C lines at their first
        # semicolon -- a false negative in the same gate. See its docstring.
        if ! python3 /tests/vendor/strip_comments.py "$cand/$rel" \
                > /tmp/env_code_only 2>/dev/null; then
            # Fail closed: if the stripper cannot run, scan the file raw. That
            # over-reports rather than under-reports, which is the direction a
            # capability gate should fail in.
            cp "$cand/$rel" /tmp/env_code_only
        fi
        # AVX-512 registers and the encodings only AVX-512 defines, plus VAES
        # and VPCLMULQDQ -- the mnemonics (vaesenc/vaesdec/vaeskeygenassist,
        # vpclmulqdq) and the intrinsics (_mm256_aes*_epi128, _mm256_clmul*),
        # neither of which carries a zmm to be caught by register alone.
        if grep -InE '(^|[^a-z0-9_])(zmm[0-9]+|%zmm|_mm512_|_mm256_aes(enc|dec|enclast|declast)_epi128|_mm(256|512)_clmul[a-z0-9_]*|vaes[a-z0-9]*|vpclmul[a-z0-9]*|vpternlog[a-z0-9]*|vpxor[dq]|kmov[a-z0-9]*|vpshufbitqmb)' \
                /tmp/env_code_only >/tmp/env_hit.txt 2>/dev/null; then
            echo "  ENVELOPE: $rel uses a capability this host does not provide:"
            head -3 /tmp/env_hit.txt | sed 's/^/      /'
            hits=$((hits + 1))
        fi
    done < /tmp/env_code.txt
    echo "  candidate-changed code files scanned for out-of-envelope instructions: $scanned (reference's own copies exempt: $exempt), offending: $hits"
    if [ "$hits" -gt 0 ]; then
        bad=1
    elif [ "$scanned" -eq 0 ]; then
        if [ "$listed" -eq 0 ]; then
            echo "  envelope: 0 of 0 files changed by the candidate -- rsync transferred and deleted nothing, the delivered tree is byte-identical to this milestone's base; instruction scan skipped, vacuously passed"
        elif [ "$code" -eq 0 ]; then
            echo "  envelope: 0 of 0 code files changed by the candidate (rsync listed $listed item(s), none of them a code file under crypto/, third_party/ or gen/); instruction scan skipped, vacuously passed"
        else
            echo "  envelope: 0 of $code candidate-changed code files are outside the reference's own copies -- every changed code file is byte-identical to the one the ladder's reference restores; instruction scan skipped, vacuously passed"
        fi
    fi

    return "$bad"
}
