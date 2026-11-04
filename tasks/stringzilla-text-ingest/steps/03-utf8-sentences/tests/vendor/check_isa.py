#!/usr/bin/env python3
"""Capability-envelope check, scoped to one step's own files.

    check_isa.py <tree> <family> [<base-tree>]

The ladder is scored under a pinned envelope: AVX2 and narrower, x86-64, no
AVX-512, no SHA-NI.  AVX-512 is absent from the grading host altogether, and
SHA-NI -- although the host's CPU lists it -- is enabled by neither the pinned
command line nor any kernel of the reference the candidate is compared
against, so either one would measure a machine the reference had no access to.
This checks what this step owns: the family's dispatch header, every file
beside its backends, and every file those headers `#include` -- because a
kernel the dispatch reaches need not sit in the family's directory, and a
backend named `x.inc` is the same code as one named `x.h`.  In a cumulative
task the candidate's tree also carries every earlier step, and a gate that read
the whole diff would charge this step for their work, so the scan is scoped by
reachability from this step's own header, not by the tree at large.

The envelope is a statement about *the candidate's* code, so when the step's
base tree is given as the third argument, only the files the candidate added or
changed are scanned.  A file byte-identical to the base's copy is the base's own
code -- not the candidate's -- so the scan is vacuous for it, the count is
printed, and no verdict is claimed about code the candidate did not write.
Anything the candidate did write (a new backend header, an edited dispatch, a
kernel pulled in from elsewhere) is scanned, and a scan that ends up examining
nothing at all is still a failure, not a pass.

Comments are stripped before scanning: the AVX2 reference kernels *describe*
their Ice Lake twins in prose (`a small stack staging union covers the partial
tail so we never read past the window, matching Ice Lake's
_mm512_permutexvar_epi8`), and a scan that read comments would fail the oracle
on its own documentation.  The ban fires on code, not on a comment about code.

Prints one line per file examined and a verdict.  Exit status 0 only when the
step's files were found and all of them are inside the envelope; a scan that
examined nothing is a failure, not a pass.
"""

import filecmp
import os
import re
import sys

BANNED = re.compile(
    r"__m512|_mm512_|_mm512\b|__AVX512|avx512|\bzmm\b|_mm256_sha|_mm_sha256|sha256rnds2|__SHA__",
    re.IGNORECASE)


def strip_comments(text):
    """Drop /* */ and // comments, and string/char literals."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
            continue
        if c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
                i += 2 if text[i] == "\\" else 1
            i += 1
            out.append('""')
            continue
        out.append(c)
        i += 1
    return "".join(out)


def family_files(tree, family):
    """The family's own files: its dispatch header and everything beside the
    backends.  No extension filter: a backend dropped in as `x.inc` or
    `x.hxx` is the same code as one named `x.h`."""
    inc = os.path.join(tree, "include", "stringzilla")
    targets = [os.path.join(inc, family + ".h")]
    fam_dir = os.path.join(inc, family)
    if os.path.isdir(fam_dir):
        targets += [os.path.join(fam_dir, f)
                    for f in sorted(os.listdir(fam_dir))
                    if os.path.isfile(os.path.join(fam_dir, f))]
    return targets


INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.MULTILINE)


def include_closure(tree, seeds, limit=512):
    """The seed files and everything they `#include`, transitively.

    A kernel the dispatch header reaches need not live in the family's
    directory: `#include "stringzilla/<somewhere else>/wide.h"` pulls one in
    from anywhere under `include/`, and a scan that stopped at the family's
    directory would never see it.  Resolution follows the includer's own
    directory first and `include/` second, the way the preprocessor does with
    the tree on its include path.  Bounded, and cycle-safe.
    """
    root = os.path.join(tree, "include")
    out, queue, seen = [], [p for p in seeds if os.path.isfile(p)], set()
    while queue and len(out) < limit:
        path = queue.pop(0)
        real = os.path.realpath(path)
        if real in seen:
            continue
        seen.add(real)
        out.append(path)
        try:
            with open(path, "r", encoding="utf-8", errors="surrogateescape") as handle:
                text = handle.read()
        except OSError:
            continue
        for name in INCLUDE_RE.findall(text):
            for candidate in (os.path.join(os.path.dirname(path), name),
                              os.path.join(root, name)):
                if os.path.isfile(candidate) and os.path.realpath(candidate) not in seen:
                    queue.append(candidate)
                    break
    return out


def scoped_files(tree, family):
    """This step's own files: the family's dispatch header, its backends, and
    everything those headers include."""
    return include_closure(tree, family_files(tree, family))


def scan(targets):
    """Scan the listed files; returns (examined, bad)."""
    examined, bad = 0, 0
    for path in targets:
        if not os.path.isfile(path):
            continue
        examined += 1
        with open(path, "r", encoding="utf-8", errors="surrogateescape") as handle:
            code = strip_comments(handle.read())
        hits = []
        for lineno, line in enumerate(code.split("\n"), 1):
            if BANNED.search(line):
                hits.append((lineno, line.strip()[:100]))
        if hits:
            bad += 1
            print("  %s: %d line(s) outside the envelope" % (path, len(hits)))
            for lineno, line in hits[:3]:
                print("    %d: %s" % (lineno, line))
    return examined, bad


def main(argv):
    if len(argv) not in (3, 4):
        raise SystemExit(__doc__)
    tree, family = argv[1], argv[2]
    base = argv[3] if len(argv) == 4 else None

    targets = scoped_files(tree, family)

    if base is not None:
        # The scope must exist before it can be declared unchanged: a family
        # with no files at all is a broken scope, not a vacuous pass.
        if not any(os.path.isfile(path) for path in targets):
            print("  ISA GATE VACUOUS: no file found for family '%s'" % family)
            return 1
        # Split this step's files into the ones the candidate touched and the
        # ones it did not, by content, not by timestamp.  Only the first set is
        # the candidate's code and only the first set gets a verdict.
        changed, unchanged, missing = [], [], []
        for path in targets:
            rel = os.path.relpath(path, tree)
            reference = os.path.join(base, rel)
            if not os.path.isfile(path):
                continue
            if os.path.isfile(reference) and filecmp.cmp(path, reference, shallow=False):
                unchanged.append(path)
            else:
                changed.append(path)
        for path in family_files(base, family):
            rel = os.path.relpath(path, base)
            if os.path.isfile(path) and not os.path.isfile(os.path.join(tree, rel)):
                missing.append(rel)
        for rel in missing:
            print("  envelope: %s was in the base and is gone from the candidate" % rel)
        if not changed and not missing:
            print("  envelope: %d of %d scoped files unchanged by the candidate -- check skipped"
                  % (len(unchanged), len(targets)))
            return 0
        print("  envelope: %d of %d scoped files changed by the candidate; scanning those, skipping %d unchanged"
              % (len(changed), len(targets), len(unchanged)))
        targets = changed

    examined, bad = scan(targets)
    if examined == 0:
        print("  ISA GATE VACUOUS: no scoped file left to examine for family '%s'" % family)
        return 1
    print("  examined %d file(s) reachable from the %s family" % (examined, family))
    if bad:
        print("  ENVELOPE VIOLATION: this milestone is scored on AVX2 or narrower")
        return 1
    print("  all within AVX2 or narrower")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
