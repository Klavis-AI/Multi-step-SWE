#!/usr/bin/env python3
"""Combine the per-set .phpt results into this step's reward.

Each argument is `name=weight:report.json:list.txt[:factor]`, factor T1 or T2 (default T1).

  * reward = T1 x T2.  T1 is this release's own behaviour:
    the weighted mean of its sets (the tests new or changed at this release and the
    hidden variants / identity checks).  T2 is preservation: the earlier corpora at this
    release's expectations.  A previous release that keeps the old behaviour earns T2
    but not T1, and a tree that breaks what earlier releases did loses T2.
  * a set whose list file is empty or absent does not exist for this step; a factor
    with no set at all is absent and does not enter the product (step 1 has no T2);
  * a set that has tests but produced no report — it crashed or ran out of time —
    scores 0 for its weight. Dropping its weight instead would *raise* the reward of a
    candidate whose interpreter is too slow to finish, which is not the contract the
    instruction states.
"""
import json
import os
import sys


def count_list(path):
    if not path or not os.path.exists(path):
        return 0
    n = 0
    with open(path, encoding="utf8") as fh:
        for line in fh:
            line = line.strip()
            if line and not line.startswith("#"):
                n += 1
    return n


def main():
    factors = {}          # factor -> [weight sum, weighted score]
    lines = []
    for spec in sys.argv[1:]:
        name, rest = spec.split("=", 1)
        parts = rest.split(":")
        weight = float(parts[0])
        path = parts[1]
        listpath = parts[2] if len(parts) > 2 else None
        factor = parts[3] if len(parts) > 3 else "T1"
        declared = count_list(listpath)
        if declared == 0:
            lines.append("%-12s not part of this step (%s)" % (name, factor))
            continue
        acc = factors.setdefault(factor, [0.0, 0.0])
        acc[0] += weight
        try:
            with open(path, encoding="utf8") as fh:
                d = json.load(fh)
        except (OSError, ValueError):
            lines.append("%-12s   0/%-4d = 0.0000  (%s weight %.2f)  no report: crashed or timed out"
                         % (name, declared, factor, weight))
            continue
        n = d.get("total", 0)
        p = d.get("passed", 0)
        denom = max(n, declared)
        frac = p / float(denom) if denom else 0.0
        acc[1] += weight * frac
        lines.append("%-12s %4d/%-4d = %.4f  (%s weight %.2f)" % (name, p, denom, frac, factor, weight))
    reward = 1.0 if factors else 0.0
    parts = []
    for factor in sorted(factors):
        wsum, wscore = factors[factor]
        value = wscore / wsum if wsum > 0 else 0.0
        reward *= value
        parts.append("%s %.4f" % (factor, value))
    lines.append("product: %s" % " x ".join(parts) if parts else "product: no scored set")
    for line in lines:
        print(line)
    print("reward %.6f" % reward)
    sys.stdout.flush()
    try:
        os.makedirs("/logs/verifier", exist_ok=True)
        with open("/logs/verifier/score-breakdown.txt", "w", encoding="utf8") as fh:
            fh.write("\n".join(lines) + "\nreward %.6f\n" % reward)
        with open("/logs/verifier/reward.value", "w", encoding="utf8") as fh:
            fh.write("%.6f\n" % reward)
    except OSError:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
