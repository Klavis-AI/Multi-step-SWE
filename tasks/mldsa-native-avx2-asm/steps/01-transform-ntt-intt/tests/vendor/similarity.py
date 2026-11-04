#!/usr/bin/env python3
"""Non-scored similarity diagnostic: how much of what the candidate wrote is
verbatim upstream code?

    similarity.py <reference_tree> <candidate_tree> <upstream_tree>

For every file the candidate added or changed relative to this step's reference
tree, take the lines that were added, and report the fraction of them that
appears verbatim in the same file of the unstripped upstream tree.

Read it as: ~1.0 for a tree that copied upstream's implementation file into
place, ~0.0 for one that wrote its own.  Its limits, which are real:

  * it is a verbatim-line measure, so a cosmetically rewritten copy reads low
    and a launderer is indistinguishable from an honest reimplementer;
  * measured against this step's reference it reports *marginal* novelty, not
    the composition of the tree -- a candidate that copied everything at an
    earlier step reads ~0 here even if its tree is entirely upstream's;
  * it is a diagnostic, never a gate: the oracle for this task IS upstream, so
    any gate on it would fail the reference solution.
"""

import difflib
import os
import sys


def walk(root):
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", "build", "bld")]
        for f in filenames:
            p = os.path.join(dirpath, f)
            if os.path.isfile(p) and not os.path.islink(p):
                out[os.path.relpath(p, root)] = p
    return out


def main():
    ref, cand, up = sys.argv[1], sys.argv[2], sys.argv[3]
    refs, cands, ups = walk(ref), walk(cand), walk(up)
    added = 0
    verbatim = 0
    for rel, path in cands.items():
        rp = refs.get(rel)
        if rp is not None:
            with open(rp, "rb") as f:
                a = f.read()
            with open(path, "rb") as f:
                b = f.read()
            if a == b:
                continue
            old = a.decode("utf-8", "replace").splitlines()
        else:
            old = []
        try:
            with open(path, errors="replace") as f:
                new = f.read().splitlines()
        except OSError:
            continue
        up_path = ups.get(rel)
        have = set()
        if up_path:
            with open(up_path, errors="replace") as f:
                have = set(l.strip() for l in f.read().splitlines())
        for line in difflib.unified_diff(old, new, n=0, lineterm=""):
            if not line.startswith("+") or line.startswith("+++"):
                continue
            s = line[1:].strip()
            if not s or s in ("{", "}", "};", "#endif", "#else"):
                continue
            added += 1
            if line[1:].strip() in have:
                verbatim += 1
    print("%.4f" % (verbatim / added if added else 0.0))


if __name__ == "__main__":
    main()
