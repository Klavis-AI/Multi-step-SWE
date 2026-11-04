#!/usr/bin/env python3
"""Local conformance runner for the DSTU2 and STU3 service bases.

  python3 run_conformance.py --root http://127.0.0.1:8080
  python3 run_conformance.py --root ... --only dstu2 --verbose
  python3 run_conformance.py --root ... --validate
"""
import argparse, collections, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
from fhirhttp import Client
import runner, compare, valscore

WORK = os.path.dirname(os.path.dirname(HERE))
BASES = {"dstu2": "baseDstu2", "stu3": "baseDstu3"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="server root, e.g. http://127.0.0.1:8080")
    ap.add_argument("--only", choices=sorted(BASES), default=None)
    ap.add_argument("--skip-seed", action="store_true")
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    for tag, base_path in sorted(BASES.items()):
        if args.only and tag != args.only:
            continue
        base = "%s/%s" % (args.root.rstrip("/"), base_path)
        client = Client(base, timeout=120)
        seed = os.path.join(WORK, "testdata", "seed-" + tag)
        if not args.skip_seed and os.path.isdir(seed):
            t0 = time.time()
            ok, fail = runner.load_seed(client, seed)
            print("%s dataset: %d written, %d refused (%.1fs)" % (tag, ok, fail, time.time() - t0))
        cf = os.path.join(HERE, "%s-cases.json" % tag)
        ef = os.path.join(HERE, "%s-expected.json" % tag)
        if not args.validate and os.path.exists(cf):
            cases = json.load(open(cf))
            expected = json.load(open(ef))
            links = {}
            actual = runner.run(client, [c for c in cases if c.get("phase", "B") == "B"], base, links)
            actual.update(runner.run(client, [c for c in cases if c.get("phase", "B") == "C"], base, links))
            npass, total, per = compare.score(cases, expected, actual)
            g = collections.defaultdict(lambda: [0, 0])
            for cid, r in per.items():
                g[r["group"]][1] += 1
                g[r["group"]][0] += 1 if r["pass"] else 0
            for k in sorted(g):
                print("  %-22s %4d/%-4d" % (k, g[k][0], g[k][1]))
            print("%s cases: %d/%d" % (tag, npass, total))
            if args.verbose:
                for cid, r in sorted(per.items()):
                    if not r["pass"]:
                        c = next(x for x in cases if x["id"] == cid)
                        print("  MISS %s  %s %s" % (cid, c["method"], c["url"]))
                        print("       fields   %s" % r["failed_fields"])
                        print("       expected %s" % json.dumps(r["expected"])[:300])
                        print("       actual   %s" % json.dumps(r["actual"])[:300])
        for kind, label in (("valcases", "$validate"), ("valdiff", "$validate (differential)")):
            vd = os.path.join(HERE, "%s-%s" % (kind, tag))
            ve = os.path.join(HERE, "%sexpected-%s.json"
                              % ("val" if kind == "valcases" else "valdiff", tag))
            if not os.path.isdir(vd) or not os.path.exists(ve):
                continue
            if not (args.validate or not args.only):
                continue
            v, n, p, ni, rows = valscore.run(base, vd, ve)
            print("%s %s: %d/%d verdicts, %d/%d located" % (tag, label, v, n, p, ni))
            if args.verbose:
                for cid, r in sorted(rows.items()):
                    if not r["verdict_ok"]:
                        print("  MISS %-52s expected_invalid=%s got_invalid=%s"
                              % (cid, r["expected_invalid"], r["got_invalid"]))


main()
