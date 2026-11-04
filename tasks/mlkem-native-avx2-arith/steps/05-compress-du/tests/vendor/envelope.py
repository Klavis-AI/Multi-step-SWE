#!/usr/bin/env python3
"""envelope.py <candidate-tree> <base-tree> <step>

Is the candidate's work inside THIS STEP'S SCOPE inside the ladder's capability
envelope?

The envelope (stated as a fairness rule in every instruction) is: x86-64 with
AVX2 or narrower -- 256-bit registers and no AVX-512 or wider -- one thread,
and the project's own build flags
(`make ... OPT=1 AUTO=1`, i.e. -mavx2 -mbmi2).  Upstream ships no wider backend
for any kernel in this ladder, so the reference is at the ceiling and a
candidate at the ceiling is its peer.  Without a check behind it that rule is
prose, not a constraint.

**Scoped to the step, never to the diff.**  The workspace is cumulative and the
verifier's base is the step's own reference level, so a candidate that
implemented earlier steps its own way shows a diff in this step's base that is
not this step's work.  Only this step's own files, its own dispatch seams, files
the candidate added, and build files the candidate changed are examined.

**Vacuous for a tree that changed nothing in this step's scope.**  An unchanged
tree is the tree it started from: there is no new code to be out of envelope,
and the runner is told so, with the counts, rather than passing silently.  A
tree that changed something is checked in full.

Exit status is 0 whenever the check ran to a verdict (including "violation");
non-zero only when the check could not run at all.  The verdict line
`ENVELOPE_VERDICT=clean|violation|skipped` is what the caller reads.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import strip  # the ladder's own step -> (files, toggles, seams) table

# --------------------------------------------------------------------- tokens
#
# Each entry is (label, compiled regex).  The ISA tokens are the ones that can
# only be produced by a machine wider than the envelope: AVX-512 register
# operands, 512-bit intrinsics, the EVEX-only register numbers, the mnemonics
# that exist on no earlier generation, and the compiler options that widen the
# compiler's reach.
#
# What is scanned is code.  Comments and string literals cannot execute and are
# blanked before the tokens run (blank_comments), which is the rule the oracle
# for every step is checked against; preprocessor directives are
# code and are *not* blanked, so `#if defined(__AVX512F__)` and its body are
# still examined.
ISA_TOKENS = [
    ("AVX-512 register operand (zmm)",
     re.compile(r"(?<![\w.])%?zmm\d*(?![\w])", re.I)),
    ("512-bit intrinsic / type",
     re.compile(r"\b_(mm512_[A-Za-z0-9_]+|m512[a-z]*)\b")),
    ("EVEX-only register number (ymm16-ymm31)",
     re.compile(r"(?<![\w.])%?ymm(1[6-9]|2[0-9]|3[01])(?![\w])")),
    ("AVX-512 mask-register intrinsic",
     re.compile(r"\b_mm(256)?_mask_[A-Za-z0-9_]+")),
    ("AVX-512-only mnemonic",
     re.compile(r"(?<![\w.])v(permi2|permt2|ternlog|scatter|compress|expand|"
                r"conflict|plzcnt|popcnt|getmant|reduce|range|fixupimm)"
                r"[bwdq]*\b", re.I)),
    ("ISA-widening compiler option",
     re.compile(r"-m(avx512[a-z0-9]*|avxvnni[a-z0-9]*|avxifma[a-z0-9]*|"
                r"amx[a-z0-9]*|arch=native|tune=native|cpu=native|"
                # x86 -march names whose baseline includes AVX-512
                r"arch=(skylake-avx512|cascadelake|cooperlake|icelake[a-z]*|"
                r"tigerlake|rocketlake|sapphirerapids|emeraldrapids|"
                r"graniterapids|granitecascade|sierraforest|znver[45]|"
                r"knl|knm))\b")),
    ("ISA-widening target attribute",
     re.compile(r"(#\s*pragma\s+GCC\s+target|__attribute__\s*\(\s*\(\s*target)"
                r"\s*\([^)]*\b(avx512|avxvnni|avxifma|amx)\w*")),
]

THREAD_TOKENS = [
    ("thread creation in a kernel",
     re.compile(r"\b(pthread_create|thrd_create|std::thread|"
                r"_beginthreadex|clone3?)\s*\(")),
    ("OpenMP in a kernel",
     re.compile(r"(#\s*pragma\s+omp|omp_get_thread_num|omp_set_num_threads|"
                r"#\s*include\s*[<\"](omp|parallel)\.h[>\"])")),
    ("thread-enabling compiler option",
     re.compile(r"-f(openmp|omp)|-pthread\b|--threads")),
]

# Tokens that are fine in prose but are read here as code because the files
# examined are the candidate's own implementation files.  Kept deliberately
# small: a false positive fails a candidate that did nothing wrong.
#
# Scanned extensions: the build only compiles/dispatches what is listed here,
# so a token in a file the build never reads cannot change what runs -- except
# for the makefiles, which are scanned on their own as build units below.
SOURCE_EXT = (".c", ".h", ".S", ".s", ".inc", ".asm", ".inl", ".c99")
BUILD_NAMES = ("makefile", "gnumakefile")
BUILD_EXT = (".mk",)

SKIP_DIRS = {".git", "build", "__pycache__", ".mypy_cache"}


def read(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except (OSError, IOError):
        return None


def tree_files(root):
    """Every regular file under root, tree-relative, minus build products."""
    out = set()
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            out.add(os.path.relpath(os.path.join(dirpath, name), root))
    return out


def seam_text(tree, step):
    """The candidate-visible text of this step's dispatch seams in meta.h."""
    path = os.path.join(tree, strip.X, "meta.h")
    text = read(path)
    if text is None:
        return None
    text = text.decode("utf-8", "replace")
    parts = []
    for seam in strip.STEPS[step][2]:
        try:
            start, end = strip.find_block(text, seam)
        except SystemExit:
            return None
        parts.append(text[start:end])
    return "\n".join(parts)


class Unit(object):
    """One file-ish thing in the step's scope.

    `rel` is the tree-relative path and decides how the file is read; `label`
    is what the runner prints, which is the same path except for the seam unit,
    whose comparison is scoped to one function block while the file it lives in
    is what gets scanned.
    """

    def __init__(self, kind, label, rel, cand, changed, added=False):
        self.kind = kind
        self.label = label
        self.rel = rel
        self.cand = cand          # absolute path in the candidate tree
        self.changed = changed
        self.added = added


def build_units(cand, base, step):
    units = []

    # 1. The files this step owns.  Absent from the step's base by
    #    construction (the stripper removes them at every lower level), so a
    #    tree that never touched this step leaves both sides missing and the
    #    unit is unchanged.
    seen = set()
    for rel in strip.STEPS[step][0]:
        seen.add(rel)
        cb, bb = read(os.path.join(cand, rel)), read(os.path.join(base, rel))
        units.append(Unit("owned", rel, rel, os.path.join(cand, rel), cb != bb))

    # 2. This step's dispatch seams -- the hook the frontend calls, and the
    #    only place meta.h is compared, so an earlier step's dispatch edit is
    #    not this step's change.
    cs, bs = seam_text(cand, step), seam_text(base, step)
    seen.add(os.path.join(strip.X, "meta.h"))
    units.append(Unit("seam", os.path.join(strip.X, "meta.h (seams)"),
                      os.path.join(strip.X, "meta.h"),
                      os.path.join(cand, strip.X, "meta.h"), cs != bs))

    # 3. Files the candidate added anywhere in the tree.  This is the route the
    #    instruction describes for a candidate: "hand-written assembly in a new
    #    file ... or faster portable C, whichever you judge better".  A file
    #    that is not in the step's base is the candidate's own writing, so it
    #    is this step's business; the envelope is a property of the machine, so
    #    out-of-envelope code fails wherever in the tree it was written.
    cand_files, base_files = tree_files(cand), tree_files(base)
    already = set()
    for rel in sorted(cand_files - base_files):
        if rel in seen:
            continue
        real = os.path.realpath(os.path.join(cand, rel))
        if real in already:
            continue
        already.add(real)
        units.append(Unit("added", rel, rel, os.path.join(cand, rel), True,
                          added=True))

    # 4. Build definitions the candidate changed.  The flags are part of the
    #    envelope, and a makefile edit is the only way to change them.  The
    #    tree hard-links each example's auto.mk back to test/mk/auto.mk, and
    #    the candidate's tree has the examples removed, so units are keyed by
    #    the pair of files actually compared rather than by either path.
    pairs = set()
    for rel in sorted(cand_files & base_files):
        name = os.path.basename(rel).lower()
        if not (name in BUILD_NAMES or name.endswith(BUILD_EXT)):
            continue
        key = (os.path.realpath(os.path.join(cand, rel)),
               os.path.realpath(os.path.join(base, rel)))
        if key in pairs:
            continue
        pairs.add(key)
        cb, bb = read(os.path.join(cand, rel)), read(os.path.join(base, rel))
        if cb != bb:
            units.append(Unit("build", rel, rel, os.path.join(cand, rel), True))

    return units


# A `#` that opens a line, followed by one of these words, is a preprocessor
# directive -- code that the preprocessor acts on, not prose.
CPP_DIRECTIVE = re.compile(
    r"#[ \t]*(define|undef|include|include_next|import|if|ifdef|ifndef|"
    r"elif|else|endif|pragma|error|warning|line)\b")

ASSEMBLY_EXT = (".s", ".asm")   # gas; `#` is its comment character


def _blank(chunk):
    return re.sub(r"[^\n]", " ", chunk)


def blank_comments(text, build=False, assembly=False):
    """Replace comment bodies and string literals with spaces.

    A comment cannot execute, so it is not an out-of-envelope construct: a
    candidate writing "this is AVX2, never zmm" is not reaching past the
    envelope.  Code inside `#if defined(__AVX512F__)` still is, and is reported
    -- en route to a machine the reference never had is still en route.

    What counts as a comment is decided per language, and getting it backwards
    fails honest trees on their own prose -- the failure mode this check exists
    to avoid.  (For example, a `# NOTE: do NOT add -march=native` in a
    changed .mk and a "# never use zmm here" in a new .S must not read as
    violations.)

      * a build file (make) -- `#` opens a comment, and nothing else does:
        `$(wildcard x/*.c)` is not a comment, `"` is not a string, and `\\#`
        is an escaped literal `#`, not a comment;
      * an assembly file -- gas' own comment character is `#`, so `#` opens a
        comment to end of line *unless* it opens the line and is followed by a
        preprocessor directive keyword, which is code (`#define K zmm0` is a
        violation, `# do not use zmm here` is not);
      * C -- `/* */`, `//` and string literals, and `#` only ever introduces a
        directive, so it is left alone.

    Single-quoted character constants are never blanked: in assembly `'` may be
    a prefix rather than a delimiter (`$'a`), and guessing wrong would blank
    code.
    """
    if build:
        # make: `#` is the only comment introducer there is.
        out = []
        for line in text.split("\n"):
            i = 0
            while i < len(line):
                if line[i] == "#" and (i == 0 or line[i - 1] != "\\"):
                    break
                i += 1
            if i < len(line):
                out.append(line[:i] + _blank(line[i:]))
            else:
                out.append(line)
        return "\n".join(out)

    out = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(_blank(text[i:j]))
            i = j
        elif ch == "/" and nxt == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(_blank(text[i:j]))
            i = j
        elif ch == '"':
            j = i + 1
            while j < n and text[j] != "\n":
                if text[j] == "\\":
                    j += 2
                elif text[j] == '"':
                    j += 1
                    break
                else:
                    j += 1
            out.append(_blank(text[i:j]))
            i = j
        elif assembly and ch == "#":
            line_start = text.rfind("\n", 0, i) + 1
            if (not text[line_start:i].strip()
                    and CPP_DIRECTIVE.match(text[i:])):
                out.append(ch)
                i += 1
                continue
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(_blank(text[i:j]))
            i = j
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def scan(unit):
    """Yield (label, line number, line) for every out-of-envelope token."""
    lower = unit.rel.lower()
    name = os.path.basename(lower)
    is_build = name in BUILD_NAMES or name.endswith(BUILD_EXT)
    is_source = lower.endswith(SOURCE_EXT)
    if not (is_build or is_source):
        return
    text = read(unit.cand)
    if text is None:
        return
    tokens = ISA_TOKENS + THREAD_TOKENS
    code = blank_comments(text.decode("utf-8", "replace"), build=is_build,
                          assembly=lower.endswith(ASSEMBLY_EXT))
    for lineno, line in enumerate(code.splitlines(), 1):
        for label, rx in tokens:
            if rx.search(line):
                yield (label, lineno, line.strip()[:120])


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: envelope.py <candidate-tree> <base-tree> "
                         "<step>\n")
        return 2
    cand, base, step = sys.argv[1], sys.argv[2], int(sys.argv[3])

    units = build_units(cand, base, step)
    scoped = units
    changed = [u for u in scoped if u.changed]
    added = [u for u in scoped if u.added]

    print("  envelope scope: %d step-owned file(s), %d dispatch seam(s), "
          "%d candidate-added file(s), %d changed build file(s)"
          % (len([u for u in scoped if u.kind == "owned"]),
             len([u for u in scoped if u.kind == "seam"]),
             len(added), len([u for u in scoped if u.kind == "build"])))

    if not scoped:
        # The scope is derived from the ladder's own table; an empty one means
        # the table or the build context is broken, not that the tree is clean.
        print("  envelope: EMPTY SCOPE -- the step's file list did not resolve; "
              "treating as a verifier fault")
        print("ENVELOPE_VERDICT=violation")
        return 0

    if not changed:
        print("  envelope: %d of %d scoped files unchanged by the candidate "
              "-- check skipped" % (len(scoped), len(scoped)))
        print("ENVELOPE_VERDICT=skipped")
        return 0

    print("  envelope: %d of %d scoped files changed by the candidate "
          "-- checking AVX2 / 256-bit / one thread / project flags"
          % (len(changed), len(scoped)))
    examined = 0
    hits = []
    for unit in changed:
        name = os.path.basename(unit.rel).lower()
        if not (name in BUILD_NAMES or name.endswith(BUILD_EXT)
                or unit.rel.lower().endswith(SOURCE_EXT)):
            continue
        examined += 1
        if unit.added:
            why = "added"
        else:
            why = "changed"
        for label, lineno, line in scan(unit):
            hits.append((unit.label, why, lineno, label, line))

    if hits:
        print("  envelope: %d out-of-envelope construct(s) in %d file(s) "
              "examined:" % (len(hits), examined))
        for label, why, lineno, tok, line in hits[:40]:
            print("    %s:%d: %s (%s)" % (label, lineno, tok, why))
            print("      | %s" % line)
        print("ENVELOPE_VERDICT=violation")
        return 0

    print("  envelope: 0 out-of-envelope constructs in %d changed file(s) "
          "examined -- ok" % examined)
    print("ENVELOPE_VERDICT=clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
