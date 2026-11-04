#!/usr/bin/env python3
"""Non-scored similarity diagnostic: how much of what the candidate wrote was
already upstream's.

    similarity.py [--same-file|--whole-tree] <base-tree> <candidate-tree> <upstream-tree>

Prints one number: of the non-trivial lines the candidate ADDED relative to the
tree it inherited, the fraction that appears verbatim in upstream's own code.

Two scopes, because the tree layout makes them two different questions:

  --same-file  (the default) upstream's own version of the SAME file.  This is
               what a candidate reads who copies a kernel in place.
  --whole-tree upstream's whole `src/` directory.  This ALSO catches a kernel
               copied into a different file -- the route this task's layout
               invites, because the stripped tree keeps a family's implementation
               in the back-end's `implementation.cpp` while upstream keeps the
               kernels in separate `src/<isa>/avx2_*.cpp` files that the stripped
               tree does not contain.  A candidate who types upstream's kernel
               text into `implementation.cpp` reads near zero under --same-file
               and high here.

Neither number is a gate and neither is a clean bill of health.  Both are
verbatim-line measures, so a candidate who copies and cosmetically rewrites
reads near zero under either.  --whole-tree is the wider net; it is also the
noisier one on honest work, because ordinary C++ lines coincide with upstream's
by accident, so it is the weaker signal of the two and only ever an upper bound.

No gate is possible and none is intended: the oracle *is* upstream, so any
threshold would fail the reference solution.  Both numbers are reported beside
the reward so a reader can see the shape of a submission.
"""

import os
import sys

SKIP_PREFIX = ("#include", "#endif", "#if", "#else", "#pragma")
EXTS = (".cpp", ".h", ".inl", ".c")


def interesting(line):
    s = line.strip()
    if not s or s in ("{", "}", "};", ")", "("):
        return False
    if s.startswith("//"):
        return False
    if s.startswith(SKIP_PREFIX):
        return False
    return True


def lines_of(path):
    try:
        with open(path, "r", errors="replace") as f:
            return [l.rstrip("\n") for l in f]
    except OSError:
        return []


def upstream_tree_lines(up):
    """Every interesting line of upstream's own src/ tree."""
    tree = set()
    for root, dirs, files in os.walk(os.path.join(up, "src")):
        dirs[:] = [d for d in dirs if d not in ("build", ".git")]
        for name in files:
            if not name.endswith(EXTS):
                continue
            for l in lines_of(os.path.join(root, name)):
                if interesting(l):
                    tree.add(l)
    return tree


def main():
    args = sys.argv[1:]
    scope = "--same-file"
    if args and args[0].startswith("--"):
        scope = args.pop(0)
    if scope not in ("--same-file", "--whole-tree") or len(args) < 3:
        raise SystemExit(__doc__)
    base, cand, up = args[0], args[1], args[2]
    tree_lines = upstream_tree_lines(up) if scope == "--whole-tree" else None
    added = 0
    verbatim = 0
    for root, dirs, files in os.walk(os.path.join(cand, "src")):
        dirs[:] = [d for d in dirs if d not in ("build", ".git")]
        for name in files:
            if not name.endswith(EXTS):
                continue
            cpath = os.path.join(root, name)
            rel = os.path.relpath(cpath, cand)
            clines = lines_of(cpath)
            blines = set(lines_of(os.path.join(base, rel)))
            if tree_lines is not None:
                ulines = tree_lines
            else:
                upath = os.path.join(up, rel)
                if not os.path.exists(upath):
                    # a file the candidate invented: nothing upstream to match
                    new = [l for l in clines if interesting(l)]
                    added += len(new)
                    continue
                ulines = set(lines_of(upath))
            for l in clines:
                if not interesting(l):
                    continue
                if l in blines:
                    continue  # inherited, not added
                added += 1
                if l in ulines:
                    verbatim += 1
    print("%.4f" % (verbatim / added) if added else "0.0000")


if __name__ == "__main__":
    main()
