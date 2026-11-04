#!/usr/bin/env python3
"""fhirconf — the local conformance runner for this milestone.

  run_conformance.py --root http://127.0.0.1:8080 [--only <base>] [--validate]
                     [--profiles] [--terminology] [--verbose]
"""
import argparse, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "lib"))
import runner, compare, valscore, profscore, termscore          # noqa: E402
from fhirhttp import Client                                     # noqa: E402

BASES = {"dstu2": "baseDstu2", "stu3": "baseDstu3", "r4": "baseR4"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="server root, e.g. http://127.0.0.1:8080")
    ap.add_argument("--only", choices=sorted(BASES), default=None)
    ap.add_argument("--skip-seed", action="store_true")
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--profiles", action="store_true")
    ap.add_argument("--terminology", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()
    rest_only = args.validate or args.profiles or args.terminology

    for tag, base_path in sorted(BASES.items()):
        if args.only and args.only != tag:
            continue
        base = "%s/%s" % (args.root.rstrip("/"), base_path)
        c = Client(base, timeout=120)
        seed = os.path.join(HERE, "..", "..", "testdata", "seed-" + tag)
        seed = os.path.normpath(seed)
        cases_p = os.path.join(HERE, "%s-cases.json" % tag)
        if not args.skip_seed and os.path.isdir(seed):
            ok, fail = runner.load_seed(c, seed)
            print("%s dataset: %d written, %d refused" % (tag, ok, fail))
        if os.path.exists(cases_p) and not rest_only:
            cases = json.load(open(cases_p))
            exp = json.load(open(os.path.join(HERE, "%s-expected.json" % tag)))
            links = {}
            act = runner.run(c, [x for x in cases if x.get("phase", "B") == "B"], base, links)
            act.update(runner.run(c, [x for x in cases if x.get("phase", "B") == "C"], base, links))
            n, total, per = compare.score(cases, exp, act)
            print("%s cases: %d/%d" % (tag, n, total))
            if args.verbose:
                for cid, r in sorted(per.items()):
                    if not r["pass"]:
                        print("  MISS %s %s" % (cid, r["failed_fields"]))
        for kind, expf, label in (("valcases", "valexpected-%s.json", "$validate"),
                                  ("valdiff", "valdiffexpected-%s.json",
                                   "$validate (differential)")):
            vd = os.path.join(HERE, "%s-%s" % (kind, tag))
            ve = os.path.join(HERE, expf % tag)
            if not os.path.isdir(vd) or not os.path.exists(ve) or not args.validate:
                continue
            v, n, p, ni, rows = valscore.run(base, vd, ve)
            print("%s %s: %d/%d verdicts, %d/%d located" % (tag, label, v, n, p, ni))

    r4 = "%s/baseR4" % args.root.rstrip("/")
    if args.profiles:
        for ig in ("uscore", "ips"):
            d = os.path.join(HERE, "profcases-" + ig)
            e = os.path.join(HERE, "profexpected-%s.json" % ig)
            if not os.path.isdir(d) or not os.path.exists(e):
                continue
            u, l = profscore.run(r4, d, e, log=print if args.verbose else lambda *a: None)
            print("%s profiles: %d/%d verdicts, %d/%d located"
                  % (ig, sum(1 for x, _ in u.values() if x), len(u),
                     sum(1 for x, _ in l.values() if x), len(l)))
    if args.terminology and os.path.exists(os.path.join(HERE, "term-corpus.json")):
        u = termscore.run(r4, HERE, log=print if args.verbose else lambda *a: None)
        print("terminology: %d/%d" % (sum(1 for x, _ in u.values() if x), len(u)))


main()
