#!/usr/bin/env python3
"""Similarity diagnostic.

    novelty.py <base-tree> <candidate-tree> <upstream-tree>

Prints one JSON object on stdout.  `verbatim_fraction` is the share of the
source lines the candidate ADDED that also appear, verbatim and in the same
file, in upstream's own version of that file.  `added_lines` and
`verbatim_lines` are the raw counts so the fraction can be judged rather than
trusted.

WHAT THIS IS AND IS NOT.  The oracle for this task IS upstream, so no gate is
possible here -- any similarity threshold would fail the reference solution.
This is a diagnostic, and it must be read as one:

  * It is a **verbatim-line** measure.  Code that has been cosmetically rewritten
    -- renamed locals, reordered statements, reformatted -- reads near zero and
    is indistinguishable from an honest reimplementation.
  * It is therefore a **lower bound on copying, never a clean bill of health**.
  * A step's figure measures the candidate's *cumulative* change against the
    ladder's base tree -- the verifier passes the fully stripped L0 tree, not
    this step's own base -- so the raw counts grow step by step and a tree that
    copied a lot during an earlier step and nothing here still reads high.
"""
import json
import subprocess
import sys


def git_diff_names(base: str, cand: str):
    out = subprocess.run(
        ["diff", "-rq", base, cand], capture_output=True, text=True, check=False
    ).stdout
    names = []
    for line in out.splitlines():
        if line.startswith("Files ") and line.endswith(" differ"):
            names.append(line.split(" and ", 1)[1][: -len(" differ")])
        elif line.startswith("Only in "):
            d, f = line[len("Only in ") :].split(": ", 1)
            names.append(d.rstrip("/") + "/" + f)
    return names


def norm(path: str, root: str) -> str:
    if path.startswith(root.rstrip("/") + "/"):
        return path[len(root.rstrip("/")) + 1 :]
    return path


def added_lines(base_file: str, cand_file: str):
    """Lines present in cand but not in base, as a multiset."""
    import collections

    def read(p):
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as fh:
                return [l.rstrip("\n") for l in fh]
        except OSError:
            return []

    b = collections.Counter(read(base_file))
    c = collections.Counter(read(cand_file))
    out = []
    for line, n in c.items():
        extra = n - b.get(line, 0)
        if extra > 0:
            out.extend([line] * extra)
    return out


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: novelty.py <base> <candidate> <upstream>", file=sys.stderr)
        return 2
    base, cand, upstream = sys.argv[1], sys.argv[2], sys.argv[3]

    added = 0
    verbatim = 0
    for path in git_diff_names(base, cand):
        rel = norm(path, cand)
        cand_file = cand.rstrip("/") + "/" + rel
        base_file = base.rstrip("/") + "/" + rel
        up_file = upstream.rstrip("/") + "/" + rel
        try:
            with open(up_file, "r", encoding="utf-8", errors="replace") as fh:
                up_lines = {l.rstrip("\n") for l in fh}
        except OSError:
            up_lines = set()
        for line in added_lines(base_file, cand_file):
            if not line.strip():
                continue
            added += 1
            if line in up_lines:
                verbatim += 1

    frac = (verbatim / added) if added else 0.0
    print(
        json.dumps(
            {
                "verbatim_fraction": round(frac, 4),
                "added_lines": added,
                "verbatim_lines": verbatim,
            }
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
