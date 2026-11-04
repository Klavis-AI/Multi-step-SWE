#!/usr/bin/env python3
"""Non-scored verbatim-line similarity between a candidate's tree and upstream.

    similarity.py <upstream-dir> <base-dir> <candidate-dir>

Only the files this ladder's stages live in are compared. The measure is
deliberately crude:

  added     = lines the candidate's tree has that its *starting* tree (the
              ladder's base, /base) does not
  verbatim  = how many of those appear somewhere in upstream's copy of the same
              file
  reported  = verbatim / added, or 0.0 when the candidate added nothing

Reading ~1.0 means the candidate moved upstream's implementation in; reading
low means it did not copy it *verbatim*, which is a weaker statement than
"did not copy it". Neither reading is a gate -- the oracle's own patch is
upstream's code and must read ~1.0 -- and a cosmetic rewrite moves a launderer
down towards the noise.

A per-step figure is marginal novelty against this step's base, not the
composition of the tree: a candidate whose tree is 40% upstream code by weight
can still read 0.02 here if that code arrived in an earlier step. Numeric
because Harbor parses every key under `rewards` as a float.
"""

import pathlib
import sys

FILES = [
    "vortex-buffer/src/bit/count_ones.rs",
    "vortex-buffer/src/bit/pack.rs",
    "vortex-buffer/src/bit/select.rs",
    "vortex-array/src/arrays/filter/execute/simd_compress/mod.rs",
    "vortex-array/src/arrays/filter/execute/simd_compress/x86.rs",
    "vortex-array/src/arrays/filter/execute/simd_compress/neon.rs",
    "vortex-array/src/arrays/fixed_width/take/mod.rs",
    "vortex-array/src/arrays/fixed_width/take/avx2/mod.rs",
    "vortex-array/src/arrays/fixed_width/take/avx2/gather.rs",
    "vortex-array/src/arrays/bool/compute/filter.rs",
    "vortex-mask/src/intersect_by_rank.rs",
]


def norm(line):
    return " ".join(line.split())


def lines_of(path):
    try:
        return [norm(l) for l in path.read_text().splitlines()]
    except OSError:
        return None


def main(argv):
    if len(argv) < 4:
        return 2
    up = pathlib.Path(argv[1])
    base = pathlib.Path(argv[2])
    cand = pathlib.Path(argv[3])
    added = verbatim = 0
    for rel in FILES:
        u = lines_of(up / rel)
        c = lines_of(cand / rel)
        if u is None or c is None:
            continue
        b = lines_of(base / rel) or []
        upstream = {l for l in u if l}
        base_set = {l for l in b if l}
        for line in c:
            if not line or line in base_set:
                continue
            added += 1
            if line in upstream:
                verbatim += 1
    print("0.0" if added == 0 else "%.4f" % (verbatim / added))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
