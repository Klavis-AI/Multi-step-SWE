#!/usr/bin/env python3
"""Check that the candidate changed nothing outside the seam it was given.

    fence.py <base-tree> <candidate-tree>

Every file in the union of the two trees that is not under the seam prefix must
be byte-identical to the base's copy, or absent from the candidate because the
base also does not have it. A file the candidate added outside the seam is
reported, and so is one it removed or edited.

Why the whole tree and not just `libswscale/`. Both correctness signals are
produced by programs built from elsewhere: the differential assertions come from
`tests/checkasm/`, whose framework is `tests/checkasm/ext/`, and the portable C
in `libswscale/*.c` is the reference every per-function ratio is divided by.
With those outside the fence a candidate could edit the reference it is compared
against, or the harness that compares it, and no gate would notice.

The seam is the directory the ladder's oracle lives in, which is also where
FFmpeg puts every x86 kernel and where `libswscale/x86/Makefile` registers new
ones. A candidate that needs a new source file adds it there and lists it in
that Makefile; nothing else in the tree has to move.
"""
import os
import pathlib
import sys

SEAM = ("libswscale/x86/",)

# Build output is not source and is excluded from the workspace artifact
# anyway; skip any directory that holds or is a build tree.
BUILD_MARKERS = ("config.mak", "ffbuild/config.mak", "Makefile")


def walk(root):
    out = set()
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root)
        rel = "" if rel == "." else rel
        dirnames[:] = [d for d in dirnames
                       if d not in (".git", "build", "ffbuild_build")]
        if "config.mak" in filenames or "config.asm" in filenames:
            dirnames[:] = []
            continue
        for f in filenames:
            out.add(os.path.join(rel, f) if rel else f)
    return out


def same(a, b):
    try:
        return a.read_bytes() == b.read_bytes()
    except OSError:
        return False


def main(argv):
    base, cand = (pathlib.Path(p) for p in argv[1:3])
    bad = []
    for f in sorted(walk(cand)):
        if f.startswith(SEAM):
            continue
        b, c = base / f, cand / f
        if not b.exists():
            bad.append(("added", f))
            continue
        if not same(b, c):
            bad.append(("modified", f))
    for f in sorted(walk(base)):
        if f.startswith(SEAM):
            continue
        if not (cand / f).exists():
            bad.append(("removed", f))
    for kind, f in bad:
        print("%-9s %s" % (kind, f))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
