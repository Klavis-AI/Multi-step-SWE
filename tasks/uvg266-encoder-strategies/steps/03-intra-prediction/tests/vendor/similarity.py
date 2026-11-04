#!/usr/bin/env python3
"""Diagnostic: how much of the candidate's added code is upstream's.

Of the lines the candidate added relative to this step's base, what fraction
appears verbatim in upstream's version of the same file.  Reads
~1.0 for the oracle's own patch and ~0.0 for a tree that changed nothing.

**This is a lower bound on copying, never a clean bill of health.** It is a
verbatim line measure: code that has been reformatted, renamed or rewritten in
equivalent form reads near zero, so a launderer is indistinguishable from an
honest reimplementer.  It is reported as a diagnostic and does not enter the
reward.

Usage: similarity.py <candidate tree> <base tree> <FUNCS file>
"""
import os
import re
import sys

# Upstream's optimized twins live here; the base tree has had them removed, so
# anything the candidate wrote for this step is compared against upstream's
# file of the same name.
ISA_DIRS = ("avx2", "sse41", "sse42", "sse2", "altivec")


def added_lines(cand_tree, base_tree, rel):
    cpath = os.path.join(cand_tree, rel)
    bpath = os.path.join(base_tree, rel)
    if not os.path.exists(cpath):
        return []
    try:
        clines = open(cpath, errors="surrogateescape").read().splitlines()
    except OSError:
        return []
    blines = []
    if os.path.exists(bpath):
        try:
            blines = open(bpath, errors="surrogateescape").read().splitlines()
        except OSError:
            blines = []
    import difflib
    return [ln[2:] for ln in difflib.unified_diff(blines, clines, n=0, lineterm="")
            if ln.startswith("+") and not ln.startswith("+++")]


def upstream_lines(upstream_root, rel):
    p = os.path.join(upstream_root, rel)
    if not os.path.exists(p):
        return set()
    try:
        return set(open(p, errors="surrogateescape").read().splitlines())
    except OSError:
        return set()


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    cand_tree, base_tree, funcs_file = argv[1], argv[2], argv[3]
    upstream_root = os.environ.get("UPSTREAM_ROOT", "/upstream")

    # The functions in scope name the family; each family's files are the ones
    # a candidate would have written.
    fams = set()
    for ln in open(funcs_file):
        ln = ln.strip()
        if ln:
            fams.add(ln.split("/")[0])
    stem = {"dct": "dct", "picture": "picture", "intra": "intra", "ipol": "ipol",
            "quant": "quant", "sao": "sao", "alf": "alf", "depquant": "depquant"}

    def norm(line):
        return re.sub(r"\s+", "", line)

    added, matched = 0, 0
    for fam in fams:
        name = stem.get(fam, fam)
        for isa in ISA_DIRS:
            rel = "src/strategies/%s/%s-%s.c" % (isa, name, isa)
            up = {norm(x) for x in upstream_lines(upstream_root, rel) if norm(x)}
            for ln in added_lines(cand_tree, base_tree, rel):
                s = norm(ln)
                if not s or s.startswith("//") or s in ("{", "}", "#endif"):
                    continue
                added += 1
                if s in up:
                    matched += 1
        rel = "src/strategies/strategies-%s.c" % name
        up = {norm(x) for x in upstream_lines(upstream_root, rel) if norm(x)}
        for ln in added_lines(cand_tree, base_tree, rel):
            s = norm(ln)
            if not s or s.startswith("//"):
                continue
            added += 1
            if s in up:
                matched += 1

    print("%.4f" % (matched / added) if added else "0.0")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
