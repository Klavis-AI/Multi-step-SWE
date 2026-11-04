#!/usr/bin/env python3
"""Runs the sample cases and prints a per-group tally.

A small, self-contained program: it exists so the milestone can be checked from
inside the workspace, and it reads only the sample corpus that ships with it.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_public_lib import judge, load_cases   # noqa: E402


def main():
    cases = load_cases()
    only = set(filter(None, os.environ.get("PUBLIC_GROUPS", "").split()))
    if only:
        cases = [c for c in cases if c["group"] in only]
    if not cases:
        print("no cases selected")
        return
    tally, fails = {}, []
    for case in cases:
        ok, detail = judge(case)
        p, t = tally.get(case["group"], (0, 0))
        tally[case["group"]] = (p + (1 if ok else 0), t + 1)
        if not ok:
            fails.append(f"FAIL {case['group']} {case['id']}: {detail}")
    width = max(len(g) for g in tally)
    for g in sorted(tally):
        p, t = tally[g]
        print(f"{g:<{width}}  {p:4d}/{t:<4d}  {100.0*p/t:5.1f}%")
    total_p = sum(p for p, _ in tally.values())
    total_t = sum(t for _, t in tally.values())
    print(f"{'TOTAL':<{width}}  {total_p:4d}/{total_t:<4d}  {100.0*total_p/total_t:5.1f}%")
    if fails:
        print("\nfirst failures:")
        for line in fails[:40]:
            print("  " + line)


if __name__ == "__main__":
    main()
