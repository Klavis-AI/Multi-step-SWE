#!/usr/bin/env bash
# Hold the candidate to the capability envelope this task states in every
# instruction, for THIS step.
#
#     check_envelope <base_tree> <candidate_tree> <ceiling_isa> <funcs_file>
#
# Why this exists
# ---------------
# The oracle is only a yardstick when both sides may use the same machine.
# Every instruction for this task states the same rule: this milestone is
# measured on AVX2 or narrower, and the build's instruction-set level must not
# rise above what src/CMakeLists.txt already configures for the AVX2 strategy
# files.  The ceiling is recorded per step in tests/ISA.stepNN.  A stated
# constraint with no check is not a constraint: nothing else in this verifier
# reads that file, and
# a tree that writes 512-bit code into this step's files would otherwise be
# measured against a 256-bit reference on a machine it brought itself.
#
# Correctness, and what this check is not
# ---------------------------------------
# Behaviour is the correctness half: build, byte-identical per-function output,
# no new suite failures, the same end-to-end bitstream.  This check answers a
# separate question -- which machine the candidate raced on -- and it is scoped
# so that it cannot answer it about a tree that changed nothing here.
#
# Scope, and why it is not the whole diff
# ---------------------------------------
# The workspace is cumulative and /base is this step's own base tree, so at a
# late step the diff also holds every family the candidate implemented in
# earlier steps.  Those are not this step's files, so they are not examined:
# the scope is the files THIS step owns -- the family's implementation files and
# dispatcher, anything the candidate added under src/, and anything the step's
# files reach through #include -- plus the build files, which are global by
# nature and are examined as flags wherever they change.  A candidate that
# changed none of them passes vacuously, and the gate says so in the log rather
# than passing silently.
set -u

check_envelope() {
    python3 -I - "$@" <<'PYEOF'
import os, re, sys

CODE_EXT = (".c", ".h", ".inc", ".hpp", ".cpp", ".cc", ".S", ".s", ".asm")
ISA_DIRS = ("generic", "avx2", "sse41", "sse42", "sse2", "altivec")

# Tokens that spell a capability wider than the envelope.  For ceiling avx2
# (256-bit) these are the AVX-512-only spellings, by category: vector and mask
# types, register names, mask intrinsics, EVEX-only mnemonics, compiler
# builtins, vector-extension types wider than 256 bits, and any target
# pragma/attribute naming the wider ISA (which is how a candidate could widen
# portable C without writing an intrinsic).
WIDE_TOKENS = re.compile(
    r"_mm512[a-z0-9_]*"
    r"|__m512[a-z0-9_]*"
    r"|\bzmm[0-9]*\b"
    r"|__mmask[0-9]+"
    r"|_k(?:andn?|or|xor|xnor|not|mov|unpack|shiftl|shiftr|add|test|ortest|concatu?)[bwdq]?"
    # AVX2's own masked gathers -- VGATHERPS/PD/DD/DQ in their 128- and
    # 256-bit forms (_mm_mask_i32gather_*, _mm256_mask_i64gather_*, ...) --
    # are inside the ceiling, so the lookahead exempts them.  AVX-512's are
    # unaffected: every _mm512 spelling still matches the first arm.
    r"|_mm(?:256|512)?_maskz?_(?!i(?:32|64)gather)[a-z0-9_]+"
    r"|vpermb|vpermi2[a-z0-9]*|vpermt2[a-z0-9]*|vpternlog[a-z0-9]*"
    r"|vpcompress[a-z0-9]*|vpexpand[a-z0-9]*|vpshld[a-z0-9]*|vpshrd[a-z0-9]*"
    r"|vpmultishiftqb|vpopcnt[a-z0-9]*|vpconflict[a-z0-9]*"
    r"|__builtin_ia32_[a-z0-9_]*512"
    r"|vector_size\s*\(\s*(?:64|128)\s*\)"
    r"|(?:pragma|__attribute__)[^\n]{0,120}(?:avx[-_]?512|avx10|target\s*\(\s*\"[^\"]*512)",
    re.I,
)

# Build flags that let a compiler widen portable C beyond the envelope.
# -mavx2, -march=haswell and -march=x86-64-v3 are AVX2-only and stay permitted.
WIDE_FLAGS = re.compile(
    r"-mavx512[a-z0-9]*|-mavx10[a-z0-9.]*|-mevex512"
    r"|-mprefer-vector-width\s*=\s*(?:512|1024)"
    r"|-march\s*=\s*(?:native|x86-64-v4|skylake-avx512|icelake[^ \t\"'#;]*"
    r"|tigerlake|cascadelake|cooperlake|sapphirerapids|emeraldrapids|graniterapids"
    r"|znver[45][^ \t\"'#;]*)",
    re.I,
)

# For a narrower ceiling, the wider tier is out of bounds as well.
SSE2_ONLY = re.compile(
    r"|\by?mm[0-9]*\b|-mavx2[a-z0-9]*|__m256[a-z0-9_]*|_mm256_[a-z0-9_]+"
    r"|vinserti128|vextracti128|vpbroadcast[a-z0-9]*|vzero(?:upper|all)"
    r"|vpgather[a-z0-9]*|vgather[a-z0-9]*|vperm[bq2][a-z0-9]*"
    r"|-march\s*=\s*(?:x86-64-v3|haswell|znver[123][^ \t\"'#;]*)",
    re.I,
)


def read(path):
    with open(path, "r", errors="replace") as fh:
        return fh.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def is_code(rel):
    return rel.endswith(CODE_EXT)


def is_build(rel):
    base = os.path.basename(rel)
    return base == "CMakeLists.txt" or base.endswith(".cmake")


def introduced(cand_tree, base_tree, rel):
    """Lines the candidate's file carries that the base's did not.

    The two trees sit at different ladder levels, so a file can differ without
    the candidate having written anything: at step 2 the base holds step 1's
    build lines and a no-op candidate does not.  What the envelope cares about
    is what the candidate added, and only added capability can widen the code.
    """
    ctext = read(os.path.join(cand_tree, rel))
    bpath = os.path.join(base_tree, rel)
    bset = set(read(bpath).splitlines()) if os.path.exists(bpath) else set()
    return "\n".join(l for l in ctext.splitlines()
                     if l.strip() and l not in bset)


def family_matches(rel, fams):
    parts = rel.split("/")
    if len(parts) == 4 and parts[:2] == ["src", "strategies"] and parts[2] in ISA_DIRS:
        for fam in fams:
            if parts[3].startswith(fam + "-") or parts[3].startswith(fam + "_"):
                return True
    if len(parts) == 3 and parts[:2] == ["src", "strategies"]:
        for fam in fams:
            if parts[2] in ("strategies-%s.c" % fam, "strategies-%s.h" % fam):
                return True
    return False


def walk_all(tree):
    for root, dirs, files in os.walk(tree):
        dirs[:] = [d for d in dirs if d not in ("build", "CMakeFiles", ".git")]
        for fn in files:
            yield os.path.relpath(os.path.join(root, fn), tree)


def walk_src(tree):
    for rel in walk_all(tree):
        if rel.startswith("src" + os.sep):
            yield rel


def includes_of(tree, rel):
    """Quoted includes of one file, resolved against the usual include roots."""
    out = []
    path = os.path.join(tree, rel)
    if not os.path.exists(path):
        return out
    for inc in re.findall(r'#\s*include\s+"([^"]+)"', read(path)):
        for cand in (os.path.normpath(os.path.join(os.path.dirname(rel), inc)),
                     os.path.normpath(os.path.join("src", inc)),
                     os.path.normpath(inc)):
            if os.path.exists(os.path.join(tree, cand)):
                out.append(cand)
                break
    return out


def reachable(tree, seeds, limit=512):
    seen, frontier = set(seeds), list(seeds)
    while frontier and len(seen) < limit:
        for nxt in includes_of(tree, frontier.pop()):
            if nxt not in seen:
                seen.add(nxt)
                frontier.append(nxt)
    return seen


def main(argv):
    if len(argv) != 5:
        sys.stderr.write("usage: check_envelope <base_tree> <cand_tree> "
                         "<ceiling_isa> <funcs_file>\n")
        return 2
    base, cand, ceiling, funcs = argv[1], argv[2], argv[3], argv[4]
    ceiling = ceiling.strip().lower()

    def emit(line):
        print(line)
        try:
            os.makedirs("/logs/verifier", exist_ok=True)
            with open("/logs/verifier/envelope.txt", "a") as fh:
                fh.write(line + "\n")
        except OSError:
            pass

    if ceiling not in ("avx2", "sse2"):
        emit("envelope: FATAL unknown ceiling '%s' -- cannot enforce it" % ceiling)
        return 1

    fams = sorted({ln.split("/")[0].strip() for ln in read(funcs).splitlines() if ln.strip()})
    if not fams or not all(re.match(r"^[a-z0-9_]+$", f) for f in fams):
        emit("envelope: FATAL FUNCS carries no '<family>/<function>' entries -- "
             "cannot derive this step's scope")
        return 1

    # ---- the step's scope -------------------------------------------------
    fam_cand = [r for r in walk_src(cand) if is_code(r) and family_matches(r, fams)]
    fam_base = [r for r in walk_src(base) if is_code(r) and family_matches(r, fams)]
    # Files the candidate added are its own work: any code it put under src/,
    # and any build file (CMakeLists.txt/*.cmake) anywhere, since build flags are
    # global by nature.
    added = [r for r in walk_all(cand)
             if (is_build(r) or (is_code(r) and r.startswith("src" + os.sep)))
             and not os.path.exists(os.path.join(base, r))]
    changed_build = [r for r in walk_all(cand)
                     if is_build(r) and os.path.exists(os.path.join(base, r))
                     and read(os.path.join(base, r)) != read(os.path.join(cand, r))]
    # headers (or sources) the step's own files pull in: changed ones and added
    # ones, so a wider helper cannot be smuggled in through an #include.
    reach = reachable(cand, set(fam_cand) | set(added))
    reached = [r for r in reach
               if r not in fam_cand and r not in added and is_code(r)
               and not os.path.exists(os.path.join(base, r))]

    scoped = sorted(set(fam_cand) | set(fam_base) | set(added) | set(changed_build)
                    | set(reached))

    if not scoped:
        emit("envelope: FATAL this step's family '%s' owns no file in either tree -- "
             "verifier/data mismatch, nothing examined" % ",".join(fams))
        return 1

    changed, unchanged, deleted = [], [], []
    for rel in scoped:
        b, c = os.path.join(base, rel), os.path.join(cand, rel)
        if not os.path.exists(c):
            deleted.append(rel)
        elif introduced(cand, base, rel).strip():
            changed.append(rel)
        else:
            unchanged.append(rel)

    emit("envelope: ceiling %s (%s), scoped to this step's family '%s'"
         % (ceiling, "256-bit vectors; wider is out of envelope" if ceiling == "avx2"
            else "128-bit vectors; wider is out of envelope", ",".join(fams)))

    if not changed and not deleted:
        emit("envelope: %d of %d scoped files unchanged by the candidate -- "
             "check skipped" % (len(unchanged), len(scoped)))
        return 0

    n_added = sum(1 for r in changed if not os.path.exists(os.path.join(base, r)))
    emit("envelope: %d of %d scoped files changed by the candidate -- checking those "
         "(%d added, %d modified, %d deleted)"
         % (len(changed) + len(deleted), len(scoped), n_added,
            len(changed) - n_added, len(deleted)))
    for rel in deleted:
        emit("  note: %s is absent from the candidate tree" % rel)

    pat = WIDE_TOKENS
    if ceiling == "sse2":
        pat = re.compile(WIDE_TOKENS.pattern + SSE2_ONLY.pattern, re.I)

    offenders, examined = {}, 0
    for rel in changed:
        text = introduced(cand, base, rel)
        if is_build(rel):
            # cmake comments are '#'-led; C-style stripping would miss them
            text = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
            pats = (pat, WIDE_FLAGS)
        else:
            pats = (pat,)
        text = strip_comments(text)
        hits = set()
        for p in pats:
            hits.update(m.group(0).strip().lower() for m in p.finditer(text))
        examined += 1
        if hits:
            offenders[rel] = sorted(hits)

    if offenders:
        emit("envelope: FAIL wider-than-%s code in this step's files:" % ceiling.upper())
        for rel in sorted(offenders):
            emit("  %s: %s" % (rel, ", ".join(offenders[rel][:8])))
        return 1

    emit("envelope: PASS (%d changed file(s) examined, nothing wider than %s)"
         % (examined, ceiling.upper()))
    return 0


sys.exit(main(sys.argv))
PYEOF
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    check_envelope "$@"
    exit $?
fi
