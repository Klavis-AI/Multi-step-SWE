#!/usr/bin/env python3
"""Local conformance runner.

Loads the sample dataset into a running FHIR service base, replays the request
cases in rest-cases.json and reports how many answers match the recorded ones.
The recorded answers come from a reference FHIR server; the cases here are a
small sample, so a clean run here is necessary but not sufficient.

  python3 run_conformance.py --base http://127.0.0.1:8080/baseR4
  python3 run_conformance.py --base ... --skip-seed --only search-token
  python3 run_conformance.py --base ... --validate      # $validate cases only
  python3 run_conformance.py --base ... --verbose
"""
import argparse, collections, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
from fhirhttp import Client
import runner, compare, valscore

SEED = os.path.join(os.path.dirname(os.path.dirname(HERE)), "testdata", "seed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True, help="FHIR service base, e.g. http://127.0.0.1:8080/baseR4")
    ap.add_argument("--seed-dir", default=SEED)
    ap.add_argument("--skip-seed", action="store_true")
    ap.add_argument("--only", default=None, help="one case group, e.g. search-date")
    ap.add_argument("--validate", action="store_true", help="run only the $validate cases")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    client = Client(args.base, timeout=120)
    if not args.validate:
        if not args.skip_seed:
            t0 = time.time()
            ok, fail = runner.load_seed(client, args.seed_dir)
            print("dataset: %d resources written, %d refused (%.1fs)" % (ok, fail, time.time() - t0))
            if fail:
                print("  (every sample resource must be accepted before the request cases mean anything)")
        cases = json.load(open(os.path.join(HERE, "rest-cases.json")))
        expected = json.load(open(os.path.join(HERE, "rest-expected.json")))
        if args.only:
            cases = [c for c in cases if c.get("group") == args.only]
        links = {}
        actual = runner.run(client, [c for c in cases if c.get("phase", "B") == "B"], args.base, links)
        actual.update(runner.run(client, [c for c in cases if c.get("phase", "B") == "C"], args.base, links))
        npass, total, per = compare.score(cases, expected, actual)
        g = collections.defaultdict(lambda: [0, 0])
        for cid, r in per.items():
            g[r["group"]][1] += 1
            g[r["group"]][0] += 1 if r["pass"] else 0
        for k in sorted(g):
            print("  %-22s %4d/%-4d" % (k, g[k][0], g[k][1]))
        print("REST cases: %d/%d" % (npass, total))
        if args.verbose:
            for cid, r in sorted(per.items()):
                if not r["pass"]:
                    c = next(x for x in cases if x["id"] == cid)
                    print("  MISS %s  %s %s" % (cid, c["method"], c["url"]))
                    print("       fields   %s" % r["failed_fields"])
                    print("       expected %s" % json.dumps(r["expected"])[:400])
                    print("       actual   %s" % json.dumps(r["actual"])[:400])

    if args.validate or not args.only:
        vdir = os.path.join(HERE, "valcases")
        vexp = os.path.join(HERE, "valexpected.json")
        if os.path.isdir(vdir):
            v, n, p, ni, rows = valscore.run(args.base, vdir, vexp)
            print("$validate verdicts: %d/%d   error located: %d/%d" % (v, n, p, ni))
            if args.verbose:
                for cid, r in sorted(rows.items()):
                    if not r["verdict_ok"]:
                        print("  MISS %-52s expected_invalid=%s got_invalid=%s"
                              % (cid, r["expected_invalid"], r["got_invalid"]))


main()
