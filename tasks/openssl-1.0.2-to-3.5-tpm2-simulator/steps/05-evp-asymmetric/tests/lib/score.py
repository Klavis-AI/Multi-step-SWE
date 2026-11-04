#!/usr/bin/env python3
"""Compare a replay against the baseline recording, over a chosen set of families.

    score.py <expected> <actual> [--families a,b,c] [--report FILE] [--quiet]

Case ids are <family>/<kind>/<nnn>; --families selects which families count, so
one recording serves every step and a step scores only what its instruction
actually states.  Without --families, everything counts.

A selected case scores 1 if the actual line is byte-identical to the expected
one and 0 otherwise -- including when it is absent, which is what a crashed or
truncated replay looks like.  Absent is never a pass: a missing expectation is
a failure, not a skip.  Lines in the actual file that were not selected are
ignored, so an agent cannot raise its score by emitting extra output.

Prints "reward=<0-1>" on the last line.  --report writes the full breakdown as
JSON: per-family totals and every mismatch, expected beside actual, which is
what makes a failing run diagnosable without re-running it.
"""
import collections
import json
import sys


def load(path):
    """{case-id: rest-of-line}, last occurrence winning."""
    out = {}
    with open(path, errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            tok = line.split(None, 1)
            if not tok or tok[0].startswith("#"):
                continue
            out[tok[0]] = tok[1] if len(tok) > 1 else ""
    return out


def family(cid):
    return cid.split("/", 1)[0]


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    exp, act = load(argv[1]), load(argv[2])
    want = None
    if "--families" in argv:
        want = set(argv[argv.index("--families") + 1].split(","))
        unknown = want - {family(c) for c in exp}
        if unknown:
            sys.stderr.write("score: no such family: %s\n"
                             % ",".join(sorted(unknown)))
            return 2
    quiet = "--quiet" in argv

    per = collections.defaultdict(lambda: [0, 0])      # family -> [hit, total]
    misses = []
    for cid in sorted(exp):
        fam = family(cid)
        if want is not None and fam not in want:
            continue
        per[fam][1] += 1
        got = act.get(cid)
        if got == exp[cid]:
            per[fam][0] += 1
        else:
            misses.append({"case": cid, "expected": exp[cid],
                           "actual": got if got is not None else "<absent>"})

    hits = sum(h for h, _ in per.values())
    tots = sum(t for _, t in per.values())
    if tots == 0:
        sys.stderr.write("score: no cases selected -- refusing to call that "
                         "a pass\n")
        return 2
    reward = hits / tots

    if not quiet:
        for fam in sorted(per):
            h, t = per[fam]
            print("  %-12s %4d/%-4d  %s" % (fam, h, t,
                                            "ok" if h == t else "MISMATCH"))
        for m in misses[:25]:
            print("  ! %s\n      expected %s\n      actual   %s"
                  % (m["case"], m["expected"], m["actual"]))
        if len(misses) > 25:
            print("  ... and %d more" % (len(misses) - 25))
    if "--report" in argv:
        with open(argv[argv.index("--report") + 1], "w") as fh:
            json.dump({"reward": reward, "matched": hits, "total": tots,
                       "families": {f: {"matched": v[0], "total": v[1]}
                                    for f, v in per.items()},
                       "mismatches": misses}, fh, indent=1)
    print("reward=%.6f (%d/%d)" % (reward, hits, tots))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
