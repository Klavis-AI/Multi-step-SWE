#!/usr/bin/env python3
"""The capability boundary of a candidate tree, measured against this step's base.

    envelope.py owned --base <dir> --cand <dir> --funcs <file> --out <file>
    envelope.py isa   --base <dir> --cand <dir> --funcs <file> --ceiling <isa>

Both modes answer a question about code the CANDIDATE wrote, never about the
tree it started from.  The verifier's base is the pin with this step's and every
later step's families removed, and its dispatchers fall through to the
*portable* reference -- so the base answers `nw_striped_16` in 32 bits and with
no vectorized code behind it.  That is the base, not a behaviour the candidate
changed; a check that reads it as one reports a tree that changed nothing as
having changed something.

`owned` asks: which of this step's scoped entry points does the candidate
answer with code of its own?  An entry point is the candidate's own when a line
the candidate ADDED to the tree BINDS it -- the line installs or defines the
name, rather than merely mentioning or using it:

  * the name or its `_pointer` written to the LEFT of the line's first plain
    assignment, however the left-hand side is spelled
    (`parasail_sw_striped_16_pointer = parasail_sw_striped_avx2_256_16;`,
    `*(&parasail_sw_striped_16_pointer) = ...;`), which is how the dispatch
    layer installs an implementation;
  * a `#define` whose macro name is the name or its `_pointer`;
  * a public wrapper that no longer routes through its own pointer at all --
    its body no longer mentions `parasail_<name>_pointer`, so the added line
    names the kernel it calls instead.  Without this the wrapper-body route
    would claim nothing and a kernel installed that way would never be asked
    for its width.  (Testing the symbol rather than one call spelling keeps a
    `(*parasail_x_pointer)(...)` respelling -- which still routes -- from
    reading as a reimplementation.)

Comments are stripped and string literals are blanked before the comparison,
so a comment that documents an entry point does not claim it, and a driver
that merely LISTS the names -- or calls an entry point it left on the portable
path, or takes its address after an `=` -- does not claim it either.  Claiming
those would fail an honest partial candidate for an entry point the
instruction says is unimplemented, not a width failure.  Only lines that
differ from the base count, so reviewing an entry point the candidate did not
touch does not claim it either.  A tree that changed nothing in this step's
scope owns nothing.

`isa` asks the other envelope question: is any of the code carrying this
step's entry points written above the step's instruction-set ceiling?  The
files examined are exactly the changed-or-added files that name one of the
step's entry points, plus the changed files that mention a symbol one of those
names' dispatchers installs for it.  That second half is what reaches the
route the instruction invites -- optimizing the portable C itself (`src/sw.c`
for `sw_*`), which names no entry point and is reached by no dispatcher
assignment *among the changed files* -- because the dispatcher that states the
binding (`parasail_sw_striped_16_pointer = parasail_sw;`) is the base's and
the candidate never touched it.  The resolution follows every `parasail_*`
token to the right of the assignment (so a cast or address-of resolves the
same as the bare spelling) and every symbol a scoped name's public wrapper
body calls; and a changed file that declares itself the family reference
(`#define ENAME parasail_<family>`) is carried for that family whatever the
dispatcher currently routes.  The cumulative /app tree also carries every
earlier step's kernels; they name earlier steps' entry points, not this
step's, so the three name-keyed rules leave them alone by construction rather
than by a hand-written list.  (The shape-keyed rule below can carry one of
them -- an earlier step's kernel file differs from this step's base -- which
costs it a scan of its added lines and nothing else: a banned token is a
violation wherever in the candidate's own code it sits, and an honest tree has
none.)

A file can carry a step's entry point without naming one, and the three rules
above all decide by name: a kernel whose symbol is built by preprocessor token
pasting -- `#define SW_NM(s, w) parasail_sw_##s##_avx2_256_##w` -- names the
entry point and the kernel alike only after the preprocessor has run, so a
textual search sees neither, and the file the candidate actually installed goes
unscanned.  That is not an exotic style here: upstream's own `sg_helper.h`
expands all eleven semi-global wrapper variants from `CONCAT(A,B,C,D) A ## B ##
C ## D`, so validating this check against the oracle cannot reveal the hole.
A changed file that is shaped like such a kernel -- an intrinsic header, a
vector type, a `parasail_` name being pasted, or an above-ceiling token itself
-- is therefore carried as well, and scanned on the lines the candidate ADDED.
Only the added lines, because the widened rule decides by what a file is made
of rather than by what it names, and what a file was made of before the
candidate touched it is not the candidate's code; an honest AVX2 tree cannot
contain one of the banned tokens on any line, so the widened rule cannot fail
one, while a token-pasted kernel above the ceiling is written on added lines by
construction.

The ISA scan reads strings and is textual on purpose: the instruction names
the string `avx512` as a failure token on its own.  The ownership scan is the
one that blanks strings, because there a string is a mention, not a binding.

Exit codes: 0 checked and clean, 1 a finding, 2 could not run (a run that
could not complete is not a clean one).
"""

import argparse
import difflib
import os
import re
import sys

# Directories that are not source the candidate authored.  `build` is also
# stripped out of the workspace by the task's artifact filter, so nothing here
# can depend on skipping it; skipping it only keeps a leftover in-tree build
# directory (a shared library full of symbol names) from being read as source.
SKIP_DIRS = {"build", "builddir", "__pycache__", ".git"}

# Intrinsic tokens per capability level, as (regex, human name).  A ceiling of
# `avx2` bans everything above it, i.e. the avx512 set.  The ladder's steps all
# carry `avx2` in tests/ISA.stepN; the entry exists so a step that pins a lower
# ceiling is still enforced rather than silently unenforced.
LEVEL_TOKENS = {
    "avx512": [
        (re.compile(r"_mm512", re.I), "_mm512 intrinsic"),
        (re.compile(r"__m512", re.I), "__m512 type"),
        (re.compile(r"__mmask", re.I), "__mmask type"),
        (re.compile(r"zmm", re.I), "zmm register"),
        (re.compile(r"avx512", re.I), "avx512"),
    ],
    "avx2": [
        (re.compile(r"_mm256", re.I), "_mm256 intrinsic"),
        (re.compile(r"__m256", re.I), "__m256 type"),
        (re.compile(r"\bymm\b", re.I), "ymm register"),
        (re.compile(r"avx2", re.I), "avx2"),
    ],
}
ORDER = ["sse2", "sse41", "avx2", "avx512"]

# What a file that implements this ladder's kernels is made of, when it names
# none of them.  Used only to decide whether a changed file is scanned (see
# mode_isa); the ceiling itself is still the banned-token list above.
KERNEL_MARKERS = [
    # intrinsic headers -- every ISA's intrinsics, avx512 included, arrive
    # through immintrin.h
    re.compile(r"(xmmintrin|emmintrin|pmmintrin|tmmintrin|smmintrin|"
               r"immintrin|arm_neon|altivec)\.h", re.I),
    # vector types and intrinsic prefixes
    re.compile(r"__m\d+|_mm[0-9_]*", re.I),
    # a `parasail_` symbol being assembled by the preprocessor
    re.compile(r"parasail_[A-Za-z0-9_]*\s*##"),
]


def die(msg):
    sys.stderr.write("envelope.py: %s\n" % msg)
    raise SystemExit(2)


# --------------------------------------------------------------------- text ---

def strip_comments(text):
    """Blank out /* ... */ and // ... while leaving strings and line structure
    intact, so line numbers and added-line detection stay meaningful and a
    comment can never be mistaken for code (an intrinsics ban that fires on a
    comment is the failure this is here to avoid)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"' or ch == "'":
            q = ch
            out.append(ch)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and text.startswith("//", i):
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if ch == "/" and text.startswith("/*", i):
            i += 2
            while i < n and not text.startswith("*/", i):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            i = min(i + 2, n)
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def strip_strings(text):
    """Blank out the *contents* of string and character literals, keeping the
    quotes and the line structure.  The ownership scan uses this on top of
    strip_comments: `"parasail_sw_scan_16"` in a driver's name table is a
    mention, not the candidate answering that entry point, and reading it as a
    binding fails an honest partial tree on an entry point it left alone."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"' or ch == "'":
            q = ch
            out.append(ch)
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(" " + ("\n" if text[i + 1] == "\n" else " "))
                    i += 2
                    continue
                if text[i] == q:
                    out.append(text[i])
                    i += 1
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def read_text(path):
    """Comment-stripped text, or None for a file that is not source."""
    try:
        with open(path, "rb") as fh:
            head = fh.read(8192)
            if b"\x00" in head:
                return None                  # not source: never scanned
            data = head + fh.read()
    except OSError:
        return None
    return strip_comments(data.decode("utf-8", "replace"))


def rel_files(root):
    if not os.path.isdir(root):
        die("no such tree: %s" % root)
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames
                             if d not in SKIP_DIRS and not d.startswith(".git"))
        for name in filenames:
            path = os.path.join(dirpath, name)
            if os.path.isfile(path) and not os.path.islink(path):
                out[os.path.relpath(path, root)] = path
    return out


def added_line_numbers(base_text, cand_text):
    """1-based line numbers of the lines the candidate added or rewrote, from
    stripped text, or None when the file is new and every line is the
    candidate's."""
    if base_text is None:
        return None
    a = base_text.splitlines()
    b = cand_text.splitlines()
    out = set()
    for tag, _i1, _i2, j1, j2 in difflib.SequenceMatcher(
            None, a, b, autojunk=False).get_opcodes():
        if tag in ("insert", "replace"):
            out.update(range(j1 + 1, j2 + 1))
    return out


def added_lines(base_text, cand_text):
    """The lines the candidate added or rewrote, from stripped text."""
    b = cand_text.splitlines()
    nums = added_line_numbers(base_text, cand_text)
    if nums is None:
        return b
    return [b[i - 1] for i in sorted(nums)]


def read_names(funcs):
    names = []
    with open(funcs, errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if line:
                names.append(line)
    if not names:
        die("no scoped entry points in %s" % funcs)
    return names


def name_pattern(names):
    # Longest first: inside one step's scope no name is a prefix of another, so
    # this only guarantees the longest match wins if one ever is.
    alts = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))
    return re.compile(r"parasail_(%s)" % alts)


def ownership_patterns(names):
    """The line shapes that BIND a scoped name: the name (or its `_pointer`)
    written to the LEFT of an assignment on an added line -- however the
    left-hand side is spelled, `x = k`, `*(&x) = k` -- and a `#define` of
    either.  A name that only appears *after* the assignment (or on a line
    with no assignment at all) is a use, not a binding: a call site
    (`result = parasail_sw_striped_16(...)`), a function-pointer table, and a
    string in a driver's name list all leave the entry point where it was, and
    the instruction says such a name is unimplemented, not a width failure.

    The assignment operator is a plain `=` (not `==`, `!=`, `<=`, `>=`, and
    not a compound `+=` etc.), so a guard such as
    `if (parasail_x_pointer == NULL)` binds nothing."""
    alts = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))
    name = re.compile(r"(?<![A-Za-z0-9_])parasail_(%s)(?:_pointer)?(?![A-Za-z0-9_])"
                      % alts)
    define = re.compile(
        r"^\s*#\s*define\s+parasail_(%s)(?:_pointer)?\b" % alts)
    return name, define


PLAIN_ASSIGN = re.compile(r"(?<![=!<>+\-*/%&|^])=(?!=)")


def pointer_symbol_pattern(name):
    """`parasail_<name>_pointer` as a whole word -- the symbol a wrapper routes
    through.  Used to ask whether a wrapper body still answers through its own
    pointer at all, spelled however the candidate spells it."""
    return re.compile(r"(?<![A-Za-z0-9_])parasail_%s_pointer(?![A-Za-z0-9_])"
                      % re.escape(name))


def wrapper_definitions(text, names):
    """(name, body) for every definition of the public wrapper `parasail_<name>(`
    in this text.  A prototype is not a definition: the parameter list has to be
    followed by `{`, not by `;`.  The dispatcher function
    (`parasail_<name>_dispatcher`) is not the public wrapper -- the name there is
    followed by `_dispatcher`, not by `(` -- and `_sat` and other spellings are
    other names."""
    alts = "|".join(re.escape(n) for n in sorted(names, key=len, reverse=True))
    rx = re.compile(r"(?m)^[A-Za-z_][A-Za-z0-9_ \t]*\**[ \t]*parasail_(%s)[ \t]*\("
                    % alts)
    out = []
    for m in rx.finditer(text):
        i = text.index("(", m.end() - 1)
        depth = 0
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    i += 1
                    break
            i += 1
        j = i
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        if j >= len(text) or text[j] != "{":
            continue                      # a prototype or a call, not a body
        k = j
        depth = 0
        while k < len(text):
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
                if depth == 0:
                    k += 1
                    break
            k += 1
        out.append((m.group(1), text[j:k]))
    return out


# -------------------------------------------------------------------- modes ---

def changed_files(base, cand):
    """(rel -> base_path_or_None, rel -> cand_path) for files that differ."""
    bfiles = rel_files(base)
    cfiles = rel_files(cand)
    changed = []
    for rel in sorted(cfiles):
        cpath = cfiles[rel]
        bpath = bfiles.get(rel)
        ctext = read_text(cpath)
        if ctext is None:
            continue                      # not source: never scanned, never owned
        btext = read_text(bpath) if bpath else None
        if btext is not None and btext == ctext:
            continue
        changed.append((rel, bpath, cpath, btext, ctext))
    return changed


def mode_owned(args):
    names = read_names(args.funcs)
    name_rx, define = ownership_patterns(names)
    pointer_rx = {}
    changed = changed_files(args.base, args.cand)
    owned = {}
    for rel, _bpath, _cpath, btext, ctext in changed:
        # Strings are mentions, not bindings: blank them before looking for
        # one.  (The ISA scan deliberately does not.)
        btext = strip_strings(btext) if btext is not None else None
        ctext = strip_strings(ctext)
        for line in added_lines(btext, ctext):
            # Everything to the left of the line's first plain assignment is
            # being written to; a name there is bound, a name after it is used.
            a = PLAIN_ASSIGN.search(line)
            if a is not None:
                for m in name_rx.finditer(line[:a.start()]):
                    owned.setdefault(m.group(1), []).append(rel)
            for m in define.finditer(line):
                owned.setdefault(m.group(1), []).append(rel)
        # The wrapper-body route: the candidate stopped routing this entry
        # point through its own pointer, so the added line names the kernel it
        # calls instead and the line shapes above see nothing.  The wrapper's
        # own body is the binding then -- the tree it started from routes
        # every wrapper through the name's pointer, so a definition whose body
        # no longer mentions that pointer at all is answering the entry point
        # itself.  (Testing the symbol, not a call spelling, keeps a
        # `(*parasail_x_pointer)(...)` respelling from reading as a
        # reimplementation.)
        for name, body in wrapper_definitions(ctext, names):
            if name not in pointer_rx:
                pointer_rx[name] = pointer_symbol_pattern(name)
            if not pointer_rx[name].search(body):
                owned.setdefault(name, []).append(rel)

    with open(args.out, "w") as fh:
        for name in sorted(owned):
            fh.write(name + "\n")
    print("envelope: %d file(s) in the candidate tree differ from this step's "
          "base" % len(changed))
    for name in sorted(owned)[:25]:
        where = sorted(set(owned[name]))
        shown = ", ".join(where[:3]) + (" ..." if len(where) > 3 else "")
        print("  %s: bound by the candidate in %s" % (name, shown))
    if len(owned) > 25:
        print("  ... and %d more" % (len(owned) - 25))
    print("envelope scan: %d of %d scoped entry points are answered by the "
          "candidate's own code" % (len(owned), len(names)))
    return 0


def mode_isa(args):
    ceiling = args.ceiling.strip().lower()
    if ceiling not in ORDER:
        die("unknown instruction-set ceiling %r (known: %s)"
            % (ceiling, ", ".join(ORDER)))
    banned = []
    for level in ORDER[ORDER.index(ceiling) + 1:]:
        banned.extend(LEVEL_TOKENS.get(level, []))
    if not banned:
        die("no banned token set above ceiling %r" % ceiling)

    names = read_names(args.funcs)
    pattern = name_pattern(names)
    changed = changed_files(args.base, args.cand)

    # The step's files are reached the way the dispatch layer reaches them: the
    # files that name one of the step's entry points, plus the files that
    # define a symbol one of those entry points' dispatchers *installs* for it.
    # The installation is the dispatcher layer's own statement of which symbol
    # answers which entry point -- `parasail_nw_striped_16_pointer = ...;` --
    # so following it names a step's kernels exactly, without charging a step
    # for the earlier steps' kernels the cumulative tree also carries.  Every
    # `parasail_*` token to the right of the `=` counts, so a cast or an
    # address-of (`= (parasail_function_t *)parasail_x_kernel;`) resolves to
    # the same symbol as the bare spelling; and the body of a scoped name's
    # public wrapper counts too, so a kernel called from a rewritten wrapper
    # body is reached as well.
    install = re.compile(r"(?<![A-Za-z0-9_])(parasail_[A-Za-z0-9_]+)\s*=(?!=)")
    token = re.compile(r"parasail_[A-Za-z0-9_]+")
    # A reference implementation declares itself the family's body:
    # `#define ENAME parasail_sw_scan`.  The file carrying it is where the
    # portable-C route does its work for every name of that family.
    ename = re.compile(r"(?m)^\s*#\s*define\s+ENAME\s+(parasail_[A-Za-z0-9_]+)\b")
    by_rel = {rel: ctext for rel, _bp, _cp, _bt, ctext in changed}
    by_base = {rel: btext for rel, _bp, _cp, btext, _ctext in changed}
    named = [rel for rel, ctext in by_rel.items() if pattern.search(ctext)]

    def installed_symbols(text):
        """Symbols a scoped name's installation or wrapper body names."""
        out = set()
        for line in text.splitlines():
            m = install.search(line)
            if m and pattern.search(m.group(1)):
                out |= set(token.findall(line[m.end():]))
        for _name, body in wrapper_definitions(text, names):
            out |= set(token.findall(body))
        return out

    symbols = set()
    for ctext in by_rel.values():
        symbols |= installed_symbols(ctext)
    # The candidate's own dispatch layer, changed or not: on the route the
    # instruction invites -- optimizing the portable C -- the file that gets
    # the work names no entry point and no assignment to the name's pointer
    # survives in it, because the binding (`parasail_sw_striped_16_pointer =
    # parasail_sw;`) is a line of the BASE's dispatcher that the candidate had
    # no reason to touch.  Reading the dispatchers resolves the symbol the
    # name is actually answered by, and the carriers rule below (a *changed*
    # file that mentions it) then reaches the reference source and scans it.
    # Scope is unchanged: `symbols` is still keyed on THIS step's names, so an
    # earlier step's kernels stay out of it.
    src = os.path.join(args.cand, "src")
    if os.path.isdir(src):
        for rel in sorted(os.listdir(src)):
            if not rel.endswith("_dispatch.c"):
                continue
            text = read_text(os.path.join(src, rel)) or ""
            symbols |= installed_symbols(text)

    sym_rx = None
    if symbols:
        sym_rx = re.compile(r"(?<![A-Za-z0-9_])(?:%s)(?![A-Za-z0-9_])"
                            % "|".join(re.escape(s) for s in
                                       sorted(symbols, key=len, reverse=True)))

    def mentions_symbol(text):
        return sym_rx is not None and sym_rx.search(text) is not None

    def family_reference(text):
        """A changed file that declares itself the family reference
        (`#define ENAME parasail_<family>`) for one of this step's names,
        wherever the dispatcher happens to route that name today."""
        for sym in ename.findall(text):
            fam = sym[len("parasail_"):]
            if any(n == fam or n.startswith(fam + "_") for n in names):
                return True
        return False

    def kernel_shaped(text):
        """A changed file that is itself an implementation of this ladder's
        kernels without naming one: an intrinsic header, a vector type, a
        `parasail_` name being pasted together, or an above-ceiling token."""
        if any(rx.search(text) for rx, _what in banned):
            return True
        return any(rx.search(text) for rx in KERNEL_MARKERS)

    # rel -> None for a carrier whose whole text is in scope, or the set of the
    # added line numbers of one reached by the name-independent kernel rule.
    # The three name-keyed rules put the whole file in scope, because what
    # matters there is that the file IS the implementation; the widened rule
    # decides by what a file is made of, so it is held to the lines the
    # candidate added -- what the file was made of before it is not the
    # candidate's code, and no honest AVX2 file carries a banned token.
    carriers = {}
    for rel, ctext in by_rel.items():
        if rel in named or mentions_symbol(ctext) or family_reference(ctext):
            carriers[rel] = None
            continue
        if kernel_shaped(ctext):
            carriers[rel] = added_line_numbers(by_base.get(rel), ctext)
    if not changed:
        print("envelope: 0 of this step's files changed by the candidate -- "
              "ISA ceiling '%s' not applicable to an unchanged tree" % ceiling)
        return 0
    if not carriers:
        print("envelope: %d changed file(s), none of them carry this step's "
              "entry points -- ISA ceiling '%s' not applicable" % (len(changed), ceiling))
        return 0

    findings = []
    scanned = 0
    for rel in sorted(carriers):
        ctext = by_rel[rel]
        wanted = carriers[rel]
        for num, line in enumerate(ctext.splitlines(), 1):
            if wanted is not None and num not in wanted:
                continue
            scanned += 1
            for rx, what in banned:
                m = rx.search(line)
                if m:
                    findings.append((rel, num, m.group(0), what))
    nudged = sum(1 for rel in carriers if carriers[rel] is not None)
    print("envelope: %d changed file(s); %d of them carry this step's entry "
          "points; %d line(s) scanned for code above the '%s' ceiling"
          % (len(changed), len(carriers), scanned, ceiling))
    if nudged:
        print("  %d of those are reached only by the name-independent kernel "
              "rule (token-pasted symbol names) and are scanned on the lines "
              "the candidate added" % nudged)
    if findings:
        for rel, num, tok, what in findings[:40]:
            print("  %s:%d: %s (%s) -- above this step's '%s' ceiling"
                  % (rel, num, tok, what, ceiling))
        if len(findings) > 40:
            print("  ... and %d more" % (len(findings) - 40))
        return 1
    print("envelope: no code above the '%s' ceiling in %d file(s)"
          % (ceiling, len(carriers)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="mode", required=True)
    for mode in ("owned", "isa"):
        p = sub.add_parser(mode)
        p.add_argument("--base", required=True)
        p.add_argument("--cand", required=True)
        p.add_argument("--funcs", required=True)
        p.add_argument("--out")
        p.add_argument("--ceiling")
    args = ap.parse_args()
    if args.mode == "owned":
        if not args.out:
            die("owned needs --out")
        return mode_owned(args)
    if not args.ceiling:
        die("isa needs --ceiling")
    return mode_isa(args)


if __name__ == "__main__":
    sys.exit(main())
