#!/usr/bin/env python3
"""Turn the per-build conformance results into this milestone's reward.

The score is T1 x T2: T1 is the pass fraction of the groups this release newly
changed (milestone.json), and T2 is the mean of the pass fraction of each source
-- the release's own test suite and the programs written for this task -- over
everything else, so neither source can carry the milestone on its own. A program
that was never run counts as a failure, whatever the reason: a build that
failed, a deadline, a crash.

The score is 0 unless the liveness canaries failed the way a correct
interpreter makes them fail; see below.
"""
import argparse, glob, json, os


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--results", required=True, help="directory of results-*.json")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    with open(os.path.join(a.corpus, "expected.json")) as f:
        exp = json.load(f)

    tests = {}
    deadline_hit = False
    for p in sorted(glob.glob(os.path.join(a.results, "results-*.json"))):
        with open(p) as f:
            d = json.load(f)
        deadline_hit = deadline_hit or d.get("deadline_hit", False)
        tests.update(d["tests"])

    missing = 0
    for name, e in exp.items():
        if name not in tests:
            tests[name] = {"pass": False, "group": e.get("group", "?"),
                           "build": e.get("build", "default"),
                           "got_err1": "<not run>"}
            missing += 1

    groups, sources, builds = {}, {}, {}
    for name, r in tests.items():
        for d, k in ((groups, r["group"]), (sources, r["group"].split(":")[0]),
                     (builds, r.get("build", "default"))):
            t = d.setdefault(k, [0, 0])
            t[1] += 1
            if r["pass"]:
                t[0] += 1

    # reward = T1 x T2. T1 is this version's own milestone -- the groups this
    # release newly changed, named in milestone.json beside this script (so the
    # byte-identical scorer knows which version it is grading). T2 is
    # preservation: everything else, which includes the earlier-version
    # behaviour this release inherits and must keep working. A previous-version
    # tree passes T2 but fails T1, so its reward is 0. Step 1 has no earlier
    # version (empty t1_groups): T1 = 1 and the score is the plain source mean.
    # Inside T2 the suite source and the dual source count equally, the dual
    # source without the T1 groups. Partial credit and "a program that never ran
    # is a failure" hold inside each factor, and the liveness zero below still
    # overrides the product.
    try:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               "milestone.json")) as f:
            t1_groups = set(json.load(f).get("t1_groups", []))
    except (OSError, ValueError):
        t1_groups = set()        # no milestone.json -> plain source mean
    def source_mean(items):
        src = {}
        for _n, r in items:
            t = src.setdefault(r["group"].split(":")[0], [0, 0])
            t[1] += 1
            if r["pass"]:
                t[0] += 1
        parts = [v[0] / v[1] for v in src.values() if v[1]]
        return (sum(parts) / len(parts)) if parts else 0.0
    if t1_groups:
        t1_items = [(n, r) for n, r in tests.items() if r["group"] in t1_groups]
        t2_items = [(n, r) for n, r in tests.items() if r["group"] not in t1_groups]
        t1_tot = len(t1_items)
        t1 = (sum(1 for _n, r in t1_items if r["pass"]) / t1_tot) if t1_tot else 1.0
        t2 = source_mean(t2_items)
    else:
        t1, t2 = 1.0, source_mean(list(tests.items()))
    score = t1 * t2

    # Liveness. The suite reports a failure through 'assert', 'error' and
    # 'os.exit', which the candidate implements: an interpreter whose 'assert'
    # returns its argument passes every chunk that is scored as "ran clean"
    # while implementing none of the semantics. The canary programs are the
    # ones a correct interpreter has to FAIL: each has a non-zero recorded exit
    # status. The gate asks only whether the candidate FAILED them -- a non-zero
    # exit -- not whether the message matched, so a near-correct interpreter is
    # not zeroed for one imperfect diagnostic (that is scored in the dual:canary
    # group like any other program). An interpreter that cannot fail at all --
    # exits 0 where a canary must error -- zeroes the milestone.
    canaries = sorted(n for n in tests if n.startswith("d_canary_"))
    dead = [n for n in canaries
            if tests[n].get("got_status", 0) == 0
            or tests[n].get("want_status", 1) == 0]
    if not canaries:
        print("LIVENESS: no canary programs in this corpus")
        score = 0.0
    elif dead:
        print("LIVENESS FAILED: %d of %d canary programs did not fail as they must: %s"
              % (len(dead), len(canaries), " ".join(dead)))
        score = 0.0
    else:
        print("LIVENESS: %d/%d canary programs failed as expected"
              % (len(canaries), len(canaries)))

    passed = sum(1 for r in tests.values() if r["pass"])
    summary = {"total": len(tests), "passed": passed, "score": score,
               "t1": t1, "t2": t2, "t1_groups": sorted(t1_groups),
               "canaries": canaries, "canaries_dead": dead,
               "deadline_hit": deadline_hit, "never_ran": missing,
               "sources": sources, "groups": groups, "builds": builds,
               "tests": tests}
    with open(a.out, "w") as f:
        json.dump(summary, f, indent=1, sort_keys=True)

    print("conformance: %d/%d" % (passed, len(tests)))
    for src in sorted(sources):
        print("  [%s] %d/%d" % (src, sources[src][0], sources[src][1]))
    for b in sorted(builds):
        print("  build %-10s %d/%d" % (b, builds[b][0], builds[b][1]))
    for g in sorted(groups):
        print("  %-26s %d/%d" % (g, groups[g][0], groups[g][1]))
    if missing:
        print("NOT RUN: %d programs never ran and are scored as failures" % missing)
    print("SCORE %.6f" % score)
    print("REWARD %.6f" % score)


main()
