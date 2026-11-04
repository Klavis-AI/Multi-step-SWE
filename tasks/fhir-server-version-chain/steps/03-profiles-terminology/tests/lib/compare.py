#!/usr/bin/env python3
"""Compare a normalised run against the recorded expectations."""
import json, sys


def compare_case(case, got, want):
    """-> (passed, [failed field names])"""
    fails = []
    for field in sorted(set(got) | set(want)):
        if field == "status" and case.get("status_in"):
            if got.get("status") not in case["status_in"]:
                fails.append("status")
            continue
        if field not in want:
            continue
        if got.get(field) != want.get(field):
            fails.append(field)
    return (not fails), fails


def score(cases, expected, actual):
    per, npass = {}, 0
    for c in cases:
        cid = c["id"]
        w = expected.get(cid)
        if w is None:
            continue
        g = actual.get(cid, {})
        ok, fails = compare_case(c, g, w)
        per[cid] = {"group": c.get("group"), "pass": ok, "failed_fields": fails,
                    "expected": {k: w.get(k) for k in fails},
                    "actual": {k: g.get(k) for k in fails}}
        npass += 1 if ok else 0
    total = len(per)
    return npass, total, per


def main():
    cases = json.load(open(sys.argv[1]))
    expected = json.load(open(sys.argv[2]))
    actual = json.load(open(sys.argv[3]))
    npass, total, per = score(cases, expected, actual)
    import collections
    g = collections.defaultdict(lambda: [0, 0])
    for cid, r in per.items():
        g[r["group"]][1] += 1
        if r["pass"]:
            g[r["group"]][0] += 1
    for k in sorted(g):
        print("%-22s %4d/%-4d" % (k, g[k][0], g[k][1]))
    print("TOTAL %d/%d = %.3f" % (npass, total, npass / total if total else 0))
    if len(sys.argv) > 4:
        json.dump(per, open(sys.argv[4], "w"), indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
