#!/usr/bin/env python3
"""End-to-end guard: compare the sws_scale pipeline against the task's base.

    e2e.py <base-dumpdir> <candidate-dumpdir> <E2E_TOL.stepN>

Per configuration, prints the largest per-sample difference between the base's
output and the candidate's, and fails if it exceeds the declared budget.

**The budget is declared, not zero**, because libswscale's optimized kernels
are not bit-identical to its C reference: measured on this ladder, the fully
restored tree differs from the fully stripped one by at most 1 code value on
eight of the ten scored configurations and by 3 on the unscaled YUV-to-RGB
path, which is upstream's own SSSE3 kernel. checkasm encodes the same allowance per function -- that is what
its per-function differential asserts, and the milestone's *scored* correctness
gate is exactly that. An equality gate here would fail the reference solution.

What the guard is for is the failure the per-function tests cannot see: wrong
output that comes from glue, dispatch or state rather than from a kernel. A
kernel that is wrong rather than differently rounded moves values by far more
than a few counts, so the budget separates the two cleanly.
"""
import os
import sys


def read_budgets(path):
    out = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, _, val = line.partition(" ")
        out[name] = int(val)
    return out


def main(argv):
    base, cand, tolfile = argv[1], argv[2], argv[3]
    budgets = read_budgets(tolfile)
    rc = 0
    for name in sorted(budgets):
        bp, cp = os.path.join(base, name + ".raw"), os.path.join(cand, name + ".raw")
        if not os.path.exists(cp):
            print("  %-32s MISSING candidate dump" % name)
            rc = 1
            continue
        a = open(bp, "rb").read()
        b = open(cp, "rb").read()
        if len(a) != len(b):
            print("  %-32s SIZE %d vs %d" % (name, len(a), len(b)))
            rc = 1
            continue
        worst = 0
        n = 0
        for x, y in zip(a, b):
            d = x - y
            if d < 0:
                d = -d
            if d:
                n += 1
                if d > worst:
                    worst = d
        ok = "ok" if worst <= budgets[name] else "OVER BUDGET"
        print("  %-32s max|base-candidate|=%d budget=%d differing=%d/%d  %s"
              % (name, worst, budgets[name], n, len(a), ok))
        if worst > budgets[name]:
            rc = 1
    if not budgets:
        print("  NO CONFIGURATIONS -- the gate examined nothing")
        rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
