#!/usr/bin/env python3
"""Remove named kernel families' optimized backends from a StringZilla tree.

Usage:  strip.py <tree> <family> [<family> ...]

A "family" is one kernel directory, e.g. `find` or `utf8_uncased`. Stripping a
family means:

  1. delete every backend header in the family's directory except `serial.h`
     and `tables.h`;
  2. delete every `#include` line in the tree that named one of those headers;
  3. drop, from every file in the tree, each `#if SZ_USE_<TIER>` / `#elif
     SZ_USE_<TIER>` branch that references a symbol the deleted headers used to
     define -- keeping the `#else` fallback where the block has one.

The result is still a complete, correct program on every architecture: the
public API behaves exactly as before, on the portable path.

Removing the headers alone is not enough, and that is the point of step 3. Every
ISA header is included *unconditionally* and guards itself with `#if
SZ_USE_<TIER>`, and `SZ_USE_<TIER>` is auto-detected from the compiler's own
target macros. On a machine that can compile AVX2, `SZ_USE_HASWELL` is 1 no
matter what this tree contains, so the dispatch chains, the tests and the
benchmarks would still name symbols whose definitions are gone.

Step 3 matches on *symbols*, not on the tier macro alone: a `#if SZ_USE_HASWELL`
block that exercises a family this strip did not touch is left exactly where it
is, so removing `find`'s AVX2 backend does not also remove `sort`'s AVX2 tests.

Idempotent: re-running on an already-stripped tree changes nothing.
"""

import os
import re
import sys

# Backend headers a family may carry, and the macro tier each one belongs to.
BACKEND_TIERS = (
    "WESTMERE", "GOLDMONT", "HASWELL", "SKYLAKE", "ICELAKE",
    "NEON", "NEONAES", "NEONSHA", "SVE", "SVE2", "SVE2AES",
    "RVV", "RVVCRYPTO", "LASX", "POWERVSX", "V128", "V128RELAXED",
)
BACKEND_FILES = {t.lower(): t for t in BACKEND_TIERS}

TIER_ALT = "|".join(sorted(BACKEND_TIERS, key=len, reverse=True))
GUARD_RE = re.compile(r"^[ \t]*#[ \t]*(if|elif)[ \t]+(.*)$")
USE_TIER_RE = re.compile(r"SZ_USE_(%s)\b" % TIER_ALT)
# A symbol defined by a backend header, e.g. sz_find_haswell, szs_..._icelake.
SYM_RE = re.compile(r"\b(sz|szs)_[A-Za-z0-9_]*?_(" + "|".join(t.lower() for t in BACKEND_TIERS) + r")\b")
# group(1) = include root ("stringzilla" | "stringzillas")
# group(2) = family directory, group(3) = file stem, group(4) = extension
INCLUDE_RE = re.compile(
    r'^[ \t]*#include\s+"(stringzilla|stringzillas)/([a-z0-9_]+)/([a-z0-9_]+)\.(h|hpp)"')

SRC_EXTS = (".h", ".hpp", ".c", ".cpp", ".cu", ".cuh", ".inl")


def read(path):
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        return f.readlines()


def write(path, lines):
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.writelines(lines)


def code_only(lines):
    """The same lines with comments and string literals removed.

    Used for every decision this script makes about what a file *says*, so that
    a comment mentioning a symbol the strip removed is not mistaken for a use of
    it: `utf8_linebreaks/haswell.h` documents its Ice Lake twin by name in a doc
    comment, and matching on raw text would delete the whole AVX2 body of a
    family that is not being stripped.
    """
    out = []
    in_block = False
    for line in lines:
        text = line
        while True:
            if in_block:
                end = text.find("*/")
                if end < 0:
                    text = ""
                    break
                text = text[end + 2:]
                in_block = False
                continue
            start = text.find("/*")
            cut = text.find("//")
            if start >= 0 and (cut < 0 or start < cut):
                end = text.find("*/", start + 2)
                if end < 0:
                    text = text[:start]
                    in_block = True
                    break
                text = text[:start] + text[end + 2:]
                continue
            if cut >= 0:
                text = text[:cut]
            break
        out.append(text)
    return out


def symbols_in(path):
    """The backend-suffixed identifiers this header **defines**.

    Defined, not merely mentioned: a stripped header *calls* other families'
    symbols (`utf8_uncased/haswell.h` calls `sz_find_haswell`, which belongs to
    `find/haswell.h`), and treating a mention as a definition would make the
    collapse rule delete `find.h`'s AVX2 dispatch in a tree where `find` was
    never stripped.

    The rule is positional rather than syntactic: a symbol is defined here when
    it appears on a line that starts in the first column.  Everything this
    project defines at file scope -- `SZ_API_COMPTIME` functions, `SZ_HELPER_*`
    helpers, typedefs, macros -- starts at column 0; every *call* to another
    translation unit's kernel is indented inside a body.  Neither signatures
    that wrap across lines nor trailing comments can fool it.
    """
    found = set()
    for text in code_only(read(path)):
        if not text[:1].strip():
            continue  # indented: a call site, not a definition
        if text.lstrip().startswith("#include"):
            continue  # `#include "...haswell.h" // `sz_find_haswell`` is a mention
        found.update(m.group(0) for m in SYM_RE.finditer(text))
    return found


def uses_removed(code, idxs, removed_syms):
    for k in idxs:
        for s in removed_syms:
            if s in code[k]:
                return True
    return False


def collapsed(lines, removed_syms):
    """Drop branches guarded by a removed tier that use a removed symbol.

    Given

        #if SZ_USE_HASWELL              #if SZ_USE_HASWELL
        ... sz_find_haswell(...) ...     ... sz_sort_haswell(...) ...
        #else                            #else
        ... sz_find_serial(...) ...      ... sz_sort_serial(...) ...
        #endif                           #endif

    the left block goes and only the `#else` body survives, while the right one
    is left alone: `sort` was not stripped, so its AVX2 path still exists.
    A block with no `#else` that uses a removed symbol is deleted whole.

    Only a guard that *is* a `SZ_USE_<tier>` chain is rewritten. Any other
    guard (`#if !SZ_DYNAMIC_DISPATCH`, `#ifdef __cplusplus`, ...) is walked
    through, so the tier chains nested inside it are rewritten in place instead
    of the whole enclosing block being judged by its contents.
    """
    code = code_only(lines)
    out = []
    i = 0
    n = len(lines)
    while i < n:
        m = GUARD_RE.match(lines[i])
        if not m or m.group(1) != "if" or not USE_TIER_RE.search(m.group(2)):
            out.append(lines[i])
            i += 1
            continue

        # Walk the whole chain at this level: collect branch bodies and the
        # position of the `#else`.
        depth = 0
        j = i
        branches = []   # (guard_line_index, [body line indices])
        else_at = None
        end = None
        cur_start = i
        cur_body = []
        while j < n:
            t = lines[j].lstrip()
            if t.startswith("#if"):
                depth += 1
                if depth > 1:
                    cur_body.append(j)
            elif t.startswith("#endif"):
                depth -= 1
                if depth == 0:
                    branches.append((cur_start, cur_body))
                    end = j
                    break
                cur_body.append(j)
            elif depth == 1 and (t.startswith("#elif") or t.startswith("#else")):
                branches.append((cur_start, cur_body))
                if t.startswith("#else"):
                    else_at = j
                else:
                    cur_start = j
                cur_body = []
            else:
                if depth >= 1:
                    cur_body.append(j)
            j += 1
        if end is None:  # unterminated: leave the file alone
            out.append(lines[i])
            i += 1
            continue

        def body_lines(idxs):
            return [lines[k] for k in idxs]

        # Drop every branch that uses a removed symbol; keep the rest as-is.
        kept = [(g, b) for (g, b) in branches if not uses_removed(code, [g] + b, removed_syms)]
        if len(kept) == len(branches):
            out.extend(lines[i:end + 1])
            i = end + 1
            continue

        kept_else = (else_at is not None and
                     not uses_removed(code, list(range(else_at + 1, end)), removed_syms))

        if not kept and not kept_else:
            i = end + 1          # nothing survives: delete the whole chain
            continue

        if not kept:
            # Only the `#else` body survives; it becomes the code itself.
            out.extend(collapsed(lines[else_at + 1:end], removed_syms))
            i = end + 1
            continue

        # Something survives: re-emit it, recursing so that a tier chain nested
        # inside a surviving branch is rewritten too.  The first surviving guard
        # becomes the chain's `#if`; a leading `#elif` would not compile.
        for index, (g, b) in enumerate(kept):
            guard = lines[g]
            if index == 0 and guard.lstrip().startswith("#elif"):
                guard = re.sub(r"^([ \t]*)#elif", r"\1#if", guard, count=1)
            out.append(guard)
            out.extend(collapsed(body_lines(b), removed_syms))
        if kept_else:
            out.append("#else\n")
            out.extend(collapsed(lines[else_at + 1:end], removed_syms))
        out.append("#endif\n")
        i = end + 1
    return out


# Tier names as they appear in prose.  A comment explaining how the removed
# AVX-512 twin works is a description of the answer, so it goes with the code.
TIER_WORD_RE = re.compile(
    r"\b(westmere|goldmont|haswell|skylake|icelake|neon|sve2?|rvv|lasx|powervsx|v128relaxed|v128)\b",
    re.IGNORECASE)


def scrub_tier_comments(lines, tiers):
    """Drop comment lines that name a tier removed from this family.

    A line goes only when it is wholly a comment -- it starts with `*`, `/*` or
    `//` -- and it either closes any block it opens (`/*` and `*/` on the same
    line) or closes nothing at all.  That keeps every `/** ... */` block
    terminated, and never touches a line of code: a statement whose trailing
    comment names a tier keeps its code and its comment.
    """
    if not tiers:
        return lines
    pattern = re.compile(r"\b(%s)\b" % "|".join(sorted(tiers, key=len, reverse=True)), re.IGNORECASE)
    out = []
    for line in lines:
        stripped_line = line.strip()
        if not stripped_line.startswith(("*", "/*", "//")) or not pattern.search(line):
            out.append(line)
            continue
        opens, closes = "/*" in line, "*/" in line
        if opens != closes:
            out.append(line)   # part of a multi-line block: would leave it unterminated
            continue
        continue               # a whole comment on its own line: safe to drop
    return out


def collect_family(tree, family, keep_tiers=()):
    """Find one family's backend headers and the symbols they define.

    `keep_tiers` names capability tiers whose backends stay in the tree -- the
    ladder's pinned envelope.  A family outside the keep set loses all of them;
    a family inside it loses only those the grading host cannot execute, which
    is what makes a level's reachable oracle exactly the envelope rather than
    every architecture upstream happens to ship.
    """
    fam_dir = None
    for base in ("stringzilla", "stringzillas"):
        cand = os.path.join(tree, "include", base, family)
        if os.path.isdir(cand):
            fam_dir = cand
            break
    if fam_dir is None:
        raise SystemExit("strip.py: no such family directory for %r" % family)

    paths = []
    readme = os.path.join(fam_dir, "README.md")
    if os.path.isfile(readme):
        paths.append(readme)  # describes the backend ladder and its throughputs
    for name in sorted(BACKEND_FILES):
        if name in keep_tiers:
            continue
        for suffix in (".h", ".hpp"):
            path = os.path.join(fam_dir, name + suffix)
            if os.path.isfile(path):
                paths.append(path)
    syms = set()
    for path in paths:
        syms |= symbols_in(path)
    return paths, syms


def apply_strip(tree, removed_syms):
    """Remove dangling includes and collapse removed-tier branches, tree-wide."""
    rewritten = 0
    for root, _dirs, files in os.walk(tree):
        if ".git" in root.split(os.sep):
            continue
        for fname in sorted(files):
            if not fname.endswith(SRC_EXTS):
                continue
            path = os.path.join(root, fname)
            try:
                lines = read(path)
            except (UnicodeDecodeError, OSError):
                continue
            kept = []
            for index, line in enumerate(lines):
                # A pure-comment line that names a symbol this strip removed is
                # part of the map of what was taken out (`@sa sz_find_haswell,
                # sz_find_skylake, ...`, or a doc line describing the AVX-512
                # twin).  It goes with the code it describes.  Only whole
                # comment lines: a statement that merely carries such a mention
                # in a trailing comment is left intact.
                stripped_line = line.strip()
                is_comment = stripped_line.startswith(("*", "/*", "//"))
                if is_comment and any(s in line for s in removed_syms):
                    rewritten += 1
                    continue
                m = INCLUDE_RE.match(line)
                # Remove the include iff the file it names is no longer in the
                # tree.  Matching on the file stem alone is wrong: stripping the
                # `hash` family would then also delete `stringzillas/fingerprints/
                # haswell.hpp`, a file that still exists and is still needed.
                if m:
                    target = os.path.join(tree, "include", m.group(1), m.group(2),
                                          m.group(3) + "." + m.group(4))
                    if not os.path.isfile(target):
                        continue
                kept.append(line)
            if kept != lines:
                write(path, kept)
                rewritten += 1
                lines = kept
            fixed = collapsed(lines, removed_syms)
            if fixed != lines:
                write(path, fixed)
                rewritten += 1
    return rewritten


def main(argv):
    if len(argv) < 3:
        raise SystemExit(__doc__)
    tree = argv[1]
    if not os.path.isdir(os.path.join(tree, "include", "stringzilla")):
        raise SystemExit("strip.py: %s does not look like a StringZilla tree" % tree)

    # --keep-tiers names capability tiers whose backends survive for the
    # families listed after it.  Families listed under --strip lose every
    # backend; families listed under --keep lose only the tiers the host
    # cannot execute.
    keep_tiers, strip_families, keep_families = (), [], []
    rest = argv[2:]
    # Short form: `strip.py <tree> <family>...` strips every named family
    # outright.  Used by the environment and verifier Dockerfiles, which keep
    # no envelope tiers.
    if rest and not any(t.startswith("--") for t in rest):
        strip_families = list(rest)
        rest = []
    index = 0
    while index < len(rest):
        token = rest[index]
        if token == "--keep-tiers":
            keep_tiers = tuple(t.strip().lower() for t in rest[index + 1].split(",") if t.strip())
            index += 2
        elif token == "--strip":
            index += 1
            while index < len(rest) and not rest[index].startswith("--"):
                strip_families.append(rest[index])
                index += 1
        elif token == "--keep":
            index += 1
            while index < len(rest) and not rest[index].startswith("--"):
                keep_families.append(rest[index])
                index += 1
        else:
            raise SystemExit("strip.py: unexpected argument %r" % token)

    # The pure-Python mirror implements the same algorithms this ladder asks
    # for in another language (`test/sz_helpers.py` holds working UAX-29 and
    # case-folding references) and ctest never runs it.  The project's front
    # page carries worked pseudocode for one of these kernels with the verbatim
    # constants and lane layout.  Both go with the per-family READMEs, which
    # describe the backend ladder this task removes.
    for extra in ("test/sz_helpers.py", "test/szs_helpers.py", "README.md"):
        path = os.path.join(tree, extra)
        if os.path.isfile(path):
            os.remove(path)
    for root, dirs, files in os.walk(os.path.join(tree, "test")):
        for name in files:
            if name.endswith(".py"):
                os.remove(os.path.join(root, name))
    probe_dir = os.path.join(tree, "probes")
    if os.path.isdir(probe_dir):
        for name in sorted(os.listdir(probe_dir)):
            if name.endswith(".md"):
                os.remove(os.path.join(probe_dir, name))

    # Phase 1: every backend that is going, goes -- and every symbol it defined
    # is collected -- before a single byte is rewritten anywhere.  The order
    # matters: rewriting for one family can gut another family's backend files
    # (a `sort` kernel that calls a `memory` kernel loses that call when
    # `memory` is stripped), and a gutted file no longer says which symbols it
    # defined.  Collecting first makes the result order-independent.
    all_syms = set()
    removed = 0
    scrubbed_tiers = {}
    for family in strip_families + keep_families:
        tiers = keep_tiers if family in keep_families else ()
        keep_names = {t.lower() for t in tiers}
        scrubbed_tiers[family] = tuple(t.lower() for t in BACKEND_FILES if t not in keep_names)
        paths, syms = collect_family(tree, family, tiers)
        for path in paths:
            os.remove(path)
        all_syms |= syms
        removed += len(paths)
        print("stripping %-18s removing %2d backends (%5d symbols defined)" %
              (family, len(paths), len(syms)))
    if not all_syms:
        print("strip.py: nothing to do")
        return 0

    # Phase 2a: prose that names a removed tier, anywhere in the tree, per
    # family's own removed tiers.  The bindings, the probes and the C shims all
    # describe the same backends, so this is not scoped to the family directory.
    for root, dirs, files in os.walk(tree):
        if ".git" in root.split(os.sep):
            continue
        for name in sorted(files):
            if not name.endswith(SRC_EXTS + (".md",)):
                continue
            path = os.path.join(root, name)
            try:
                lines = read(path)
            except (UnicodeDecodeError, OSError):
                continue
            fixed = lines
            for tiers in scrubbed_tiers.values():
                fixed = scrub_tier_comments(fixed, tiers)
            if fixed != lines:
                write(path, fixed)

    # Phase 2b: one pass over the tree with the complete symbol set.
    rewritten = apply_strip(tree, all_syms)
    print("strip.py: %d files rewritten, %d symbols, %d backend headers removed" %
          (rewritten, len(all_syms), removed))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
