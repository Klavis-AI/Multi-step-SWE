#!/usr/bin/env bash
# Hold the candidate to the instruction-set envelope this stage is scored on.
#
#     check_isa <base_tree> <candidate_tree> <max_isa> <funcs_file>
#
# Why this exists
# ---------------
# The oracle is only a yardstick when both sides may use the same machine. The
# grading host reports `sse sse2 ssse3 sse4_1 sse4_2 avx avx2` and no AVX-512,
# so code that needs AVX-512 will not execute there at all: it is not a faster
# answer, it is an answer that cannot be measured. The envelope is therefore
# SSE through AVX2 for both sides, and it is enforced rather than stated.
#
# Scope, and why it is not the whole diff
# ---------------------------------------
# The workspace is cumulative and /base is the TASK's reference tree, so
# `diff /base $SRC` at a late step also contains every earlier stage the
# candidate implemented itself. Checking that diff wholesale would fail step 06
# for 256-bit code written legitimately at step 01. So the check works from the
# symbols: each scoped function name is a dispatch member or a transform
# pointer, the candidate's own dispatch says which symbol it installs for it,
# and only the files defining those symbols are inspected.
#
# The resolution reads the right-hand side of the install, not a bare symbol:
# `decode_cb32 = pick_kernel();` and `decode_cb32 = cond ? fast : slow;` install
# code for this stage exactly as `decode_cb32 = fast;` does, so every identifier
# on the right-hand side is followed to the file that defines it.
#
# Build flags are the exception: they are global by nature, so an added flag
# that lets a compiler widen portable C is an offence wherever it appears.
#
# Comments are not code
# ---------------------
# Both arms read code, and a comment is not code: a CMake `#` note or a C
# `/* ... */` block that merely *names* a flag or a 512-bit intrinsic is not a
# use of it.  `;` ends a statement and is not treated as a comment marker.
#
# The check counts what it examined and reports it, so an empty result cannot
# read like a clean pass.
#
# Vacuity, for a tree that wrote nothing
# -------------------------------------
# The envelope is a rule about code the CANDIDATE wrote.  /base is the task's
# cumulative reference tree, so at step k a no-op candidate differs from it --
# it lacks the earlier milestones -- while writing nothing at all.  Failing
# that tree here would report a tree that changed nothing as having changed
# behaviour, which is a verdict on the envelope rather than on the candidate's
# code.  The check therefore asks first whether the candidate wrote any line of
# code at all (`added`, computed below): when it wrote none, there is nothing
# for either arm to inspect, and the check says so, prints the scope it
# resolved, and returns satisfied.  One added line anywhere puts both arms in
# force.
set -u

# 256-bit-and-wider machinery.  AVX2 is permitted, so plain `ymm` and
# `_mm256_*` are NOT here -- only what is above the ceiling.  `vzeroall` and
# `vzeroupper` are AVX-era instructions *inside* the envelope, so they are not
# here either.
#
# The mask-register arm requires the `%` of AT&T assembly (`%k1`, or `%%k1`
# in extended asm).  A bare `k1` is an ordinary C identifier -- a constant or
# a loop index named `k1` in the candidate's own file is not AVX-512.  AVX-512
# use that reaches the assembler is still caught without it: `zmm*`,
# `_mm512_*`, `__m512*` and the AVX-512-only mnemonics below cover intrinsics
# and both asm syntaxes, and a mask-only kernel buys nothing to measure.
_WIDE='\<zmm[0-9]+\>|%k[0-7]\>|_mm512_[a-z0-9_]+|__m512[a-z_]*|vpermi2[bwdq]|vpermt2[bwdq]|vpternlog|vpbroadcastm|vgatherpf|vscatter|vcompress|vexpand|vpmovm2|vpmov[a-z0-9]*_mem'
_FLAGS='\-mavx512[a-z0-9_]*|\-march=native|\-march=x86-64-v4|\-mprefer-vector-width=512'

# The scope/comment pass.  It reads the diff on stdin, so it is held in a
# variable and run with `-c` rather than from a heredoc (a heredoc would eat
# the very stdin the diff arrives on).
read -r -d '' _SCOPE_PY <<'PYEOF' || true
import os, re, sys

def strip(line, inblock, cmake):
    """Remove comments from one line.  Returns (code, block_state).

    Quoted strings are copied through untouched: a path like
    "coding/*_ssse3.cpp" carries `/*` that is not a comment.
    """
    out, i, n = [], 0, len(line)
    while i < n:
        c = line[i]
        if inblock:
            j = line.find("*/", i)
            if j < 0:
                return "".join(out), True
            i, inblock = j + 2, False
            continue
        if c == '"' or c == "'":
            j = i + 1
            while j < n:
                if line[j] == "\\":
                    j += 2
                    continue
                if line[j] == c:
                    j += 1
                    break
                j += 1
            out.append(line[i:j])
            i = j
            continue
        if line.startswith("/*", i):
            inblock = True               # a /*...*/ block may span lines
            i += 2
            continue
        if line.startswith("//", i) or (cmake and c == "#"):
            break                        # line comment: the rest is not code
        out.append(c)
        i += 1
    return "".join(out), inblock


def strip_lines(lines, cmake=False):
    out, inblock = [], False
    for ln in lines:
        code, inblock = strip(ln, inblock, cmake)
        out.append(code)
    return out


cand, funcs = sys.argv[1], sys.argv[2]
# the pattern is written for `grep -oE`, where \< and \> are word boundaries;
# Python's re knows neither (it would read them as literal `<` and `>`), so
# translate rather than silently matching nothing.
wide = re.compile(sys.argv[3].replace(r"\<", r"\b").replace(r"\>", r"\b"))

# ---- added lines (build-flag arm) ---------------------------------------
cmake, inblock, added = False, False, []
for raw in sys.stdin:
    if raw.startswith("+++ "):
        name = raw[4:].split("\t")[0].strip()
        cmake = name.endswith("CMakeLists.txt") or name.endswith(".cmake")
        inblock = False
        continue
    if not raw.startswith("+"):
        continue
    code, inblock = strip(raw[1:].rstrip("\n"), inblock, cmake)
    if code.strip():
        added.append(code.lower())

# ---- scope: what this step's dispatch installs --------------------------
names = []
if funcs and os.path.exists(funcs):
    for line in open(funcs):
        t = line.split()
        if t:
            names.append(t[0])
if not names:                       # fall back to every dispatch member
    names = ["decode_cb32", "encode_cb32", "rev_vert_step", "rev_horz_ana",
             "rev_horz_syn", "irv_vert_step", "irv_vert_times_K",
             "irv_horz_ana", "irv_horz_syn", "rct_forward", "rct_backward",
             "ict_forward", "ict_backward", "rev_convert",
             "irv_convert_to_integer", "irv_convert_to_float",
             "tx_to_cb32", "tx_to_cb64", "tx_from_cb32", "tx_from_cb64"]

seams = ["src/core/codestream/ojph_codeblock_fun.cpp",
         "src/core/transform/ojph_transform.cpp",
         "src/core/transform/ojph_colour.cpp"]
syms = set()
for rel in seams:
    p = os.path.join(cand, rel)
    if not os.path.exists(p):
        continue
    try:
        src = "\n".join(strip_lines(open(p, errors="replace").read().split("\n")))
    except OSError:
        continue
    for fn in names:
        # `decode_cb32 = ojph_decode_codeblock_avx2;` and the `if (level)`
        # body that installs it -- collect every identifier the right-hand
        # side of this member's install names, so a ternary, a helper call or
        # an assignment split over lines resolves the same as a bare symbol.
        for m in re.finditer(r"\b%s\s*=\s*([^;]*);" % re.escape(fn), src):
            for ident in re.findall(r"[A-Za-z_]\w*", m.group(1)):
                if ident not in ("NULL", "nullptr", "true", "false") \
                        and not ident.isupper():     # flags/macros, not files
                    syms.add(ident)

files, hits = [], set()
for root, _dirs, fs in os.walk(cand):
    if "/.git" in root or "/build" in root:
        continue
    for f in fs:
        if not f.endswith((".cpp", ".h", ".hpp", ".c", ".inc", ".S")):
            continue
        p = os.path.join(root, f)
        try:
            body = open(p, errors="replace").read()
        except OSError:
            continue
        if not any(re.search(r"\b%s\b" % re.escape(s), body) for s in syms):
            continue
        rel = os.path.relpath(p, cand)
        files.append(rel)
        code = "\n".join(strip_lines(body.split("\n")))
        for m in wide.finditer(code):
            hits.add("    %s  (%s)" % (m.group(0), rel))

print("@@ADDED")
print("\n".join(added))
print("@@FILES")
print("\n".join(sorted(files)))
print("@@HITS")
print("\n".join(sorted(hits)))
PYEOF

check_isa() {
    local base="$1" cand="$2" max="$3" funcs="${4:-}"
    local files hits added flags scanned total section

    # `max` is what this step declares as its envelope.  There is one ceiling
    # for the whole ladder (AVX2 -- the grading host executes nothing wider),
    # so a step that declares something else would be measured against a
    # different machine than the one it was told about.
    if [ "$max" != "avx2" ]; then
        echo "  ISA check FAILED: this step declares envelope '$max', but the"
        echo "  ladder is scored at avx2; the declaration and the check disagree."
        return 1
    fi

    # One pass, three readings: the candidate's added lines with comments
    # removed (the global build-flag rule), the files this step's dispatch
    # installs (the scope), and the 512-bit tokens those files contain once
    # their comments are removed (the hits).
    local dump
    dump=$(diff -ruN "$base" "$cand" 2>/dev/null | python3 -c "$_SCOPE_PY" \
               "$cand" "$funcs" "$_WIDE")

    # A scope pass that produced nothing at all (no python3, a crash) must not
    # read as the no-op skip below: that is the difference between "examined
    # everything, found nothing" and "examined nothing".  The three markers are
    # printed on every successful pass, empty sections included.
    case "$dump" in
        *"@@ADDED"*"@@FILES"*"@@HITS"*) : ;;
        *) echo "  ISA check FAILED: the scope pass produced no result"
           echo "  (python3 missing, or it failed); nothing was examined."
           return 1 ;;
    esac

    section=$(printf '%s\n' "$dump" | awk '
        /^@@/ { s = substr($0, 3); next }
        { if (s != "") print s "\t" $0 }')
    added=$(printf '%s\n' "$section" | awk -F'\t' '$1 == "ADDED" { print $2 }')
    files=$(printf '%s\n' "$section" | awk -F'\t' '$1 == "FILES" { print $2 }')
    hits=$(printf '%s\n' "$section" | awk -F'\t' '$1 == "HITS"  { print substr($0, index($0, "\t") + 1) }')

    # Is there anything of the candidate's to inspect?  `added` holds every
    # line of code this tree carries that the base does not, comments removed:
    # when it is empty the candidate wrote nothing at all.  That is the no-op
    # -- at step k it lacks the earlier milestones, so it is a subset of the
    # base rather than a modification of it -- and failing it here would
    # report a tree that changed nothing as having changed behaviour.  Say
    # what was resolved and pass; the moment a line is added anywhere, both
    # arms below run.  Deleting lines cannot widen anything, so an
    # empty `added` is sound.
    total=$(printf '%s\n' "$files" | grep -c . || true)
    if [ -z "$added" ]; then
        echo "  envelope: candidate added no line of code anywhere -- check skipped ($total scoped file(s) resolved)"
        return 0
    fi

    flags=$(printf "%s\n" "$added" | grep -oE -e "$_FLAGS" 2>/dev/null | sort -u | head -6 || true)

    scanned="$total"

    if [ -n "$flags" ]; then
        echo "  ISA check FAILED: an added build flag widens the build past AVX2."
        echo "  The grading host executes SSE through AVX2 only; 512-bit code"
        echo "  cannot run there and is not a faster answer."
        printf '%s\n' "$flags" | sed 's/^/    /'
        echo "  (from a build line the candidate added, wherever it appears)"
    fi
    if [ -n "$hits" ]; then
        echo "  ISA check FAILED: wider than AVX2 in this stage's implementation."
        echo "  The grading host executes SSE through AVX2 only; 512-bit code"
        echo "  cannot run there and is not a faster answer."
        printf '%s\n' "$hits"
    fi
    if [ -n "$hits" ] || [ -n "$flags" ]; then
        return 1
    fi

    if [ "$scanned" -eq 0 ]; then
        echo "  ISA check FAILED: no implementation file found for this stage's"
        echo "  functions (nothing was examined, so nothing was cleared)."
        return 1
    fi
    echo "  ISA check: AVX2 envelope respected ($scanned stage file(s) inspected)"
    return 0
}
