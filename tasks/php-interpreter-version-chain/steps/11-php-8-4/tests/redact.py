#!/usr/bin/env python3
"""Replace test paths in a verifier report with their index in the scored list.

In shared mode the verifier runs inside the agent's own container, and Harbor only
empties /logs/verifier immediately before the *next* verifier -- so whatever this step's
verifier leaves there is readable by the agent for the whole of the next step's turn.
The per-test reports name every scored test, which is held-out material: the scored
subset is the thing the agent is not supposed to know.

An index is not.  The agent has the pool but has never seen the ordered list the index
refers to, so "entry 147 failed" identifies nothing; whoever holds the list file can map
it straight back.  Statuses and timings are kept, so the reports stay useful for
after-the-fact analysis without telling the next turn which tests are scored.

usage: redact.py <report.json> <list.txt>
"""
import json, sys

def main():
    report, listfile = sys.argv[1], sys.argv[2]
    try:
        with open(report, encoding="utf8") as fh:
            d = json.load(fh)
    except (OSError, ValueError):
        return 0
    try:
        with open(listfile, encoding="utf8") as fh:
            order = {p: i for i, p in enumerate(fh.read().split())}
    except OSError:
        order = {}
    for r in d.get("results", []):
        p = r.pop("path", None)
        r.pop("name", None)
        r["i"] = order.get(p, -1)
    with open(report, "w", encoding="utf8") as fh:
        json.dump(d, fh)
    return 0

if __name__ == "__main__":
    sys.exit(main())
