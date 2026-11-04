#!/usr/bin/env python3
"""Record the behavior suite against a tree, twice, and keep only what is stable.

    record.py <suite.json> <lss-binary> <repo-root> <out-dir> [--jobs N]
              [--covdir DIR] [--only PREFIX]

Every scenario runs twice in independent scratch directories. An exchange whose
two runs disagree after normalization is unstable; it is written to
`unstable.json` and excluded from the expectation set rather than being pinned
to whichever answer happened to come first.

The `expect/` directory beside this file is what it wrote, against an `lss`
built from the untouched baseline: `record.py suite.json <lss> <repo> expect/`.
"""
import concurrent.futures
import json
import os
import shutil
import sys

import harness


def main(argv):
    suite_path, lss, repo, out = argv[1:5]
    jobs = 4
    covdir = None
    only = None
    rest = argv[5:]
    while rest:
        flag = rest.pop(0)
        if flag == "--jobs":
            jobs = int(rest.pop(0))
        elif flag == "--covdir":
            covdir = rest.pop(0)
        elif flag == "--only":
            only = rest.pop(0)
        else:
            sys.exit("unknown flag %s" % flag)

    configs, scenarios = harness.load_suite(suite_path)
    if only:
        wanted = tuple(only.split(","))
        scenarios = [s for s in scenarios if s["id"].startswith(wanted)]

    def run_pass(tag):
        results = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {
                pool.submit(harness.run_scenario, scn, configs, lss, repo,
                            covdir): scn["id"]
                for scn in scenarios
            }
            for fut in concurrent.futures.as_completed(futures):
                results[futures[fut]] = fut.result()
        return results

    first = run_pass("a")
    second = run_pass("b")

    stable, unstable = {}, {}
    for scn in scenarios:
        sid = scn["id"]
        a, b = first.get(sid, {}), second.get(sid, {})
        keep = {}
        for key in sorted(set(a) | set(b)):
            if key in a and key in b and a[key] == b[key]:
                keep[key] = a[key]
            else:
                unstable.setdefault(sid, {})[key] = {"a": a.get(key),
                                                     "b": b.get(key)}
        stable[sid] = keep

    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    for sid, data in sorted(stable.items()):
        with open(os.path.join(out, "%s.json" % sid), "w") as fh:
            json.dump(data, fh, indent=2, sort_keys=True)
            fh.write("\n")
    with open(os.path.join(out, "unstable.json"), "w") as fh:
        json.dump(unstable, fh, indent=2, sort_keys=True)
        fh.write("\n")

    total = sum(len(v) for v in stable.values())
    bad = sum(len(v) for v in unstable.values())
    print("scenarios=%d  stable=%d  unstable=%d" % (len(scenarios), total, bad))
    for sid, items in sorted(unstable.items()):
        for key in sorted(items):
            print("  UNSTABLE %s/%s" % (sid, key))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
