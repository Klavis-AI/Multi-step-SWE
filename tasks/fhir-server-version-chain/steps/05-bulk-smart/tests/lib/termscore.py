#!/usr/bin/env python3
"""Score the terminology component: drive each operation and compare with the
answer the published resources give."""
import json, os, sys, urllib.parse, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client


def _params(doc):
    out = {}
    for p in (doc.get("parameter") or []):
        n = p.get("name")
        for k, v in p.items():
            if k.startswith("value"):
                out.setdefault(n, []).append(v)
        if p.get("part"):
            out.setdefault(n + "#parts", []).append(p["part"])
    return out


def run(base, datadir, log=print, deadline=None):
    cases = json.load(open(os.path.join(datadir, "term-corpus.json")))
    exp = json.load(open(os.path.join(datadir, "term-expected.json")))
    c = Client(base, timeout=120)
    units, misses = {}, 0
    for case in cases:
        want = exp.get(case["id"])
        if want is None:
            continue
        if deadline is not None and time.monotonic() > deadline:
            units[case["id"]] = (False, 1.0)
            continue
        op = case["op"]
        q = None
        if op == "expand":
            q = "/ValueSet/$expand?url=" + urllib.parse.quote(case["url"], safe="")
        elif op == "validate-code":
            q = ("/ValueSet/$validate-code?url=" + urllib.parse.quote(case["url"], safe="") +
                 "&system=" + urllib.parse.quote(case["system"] or "", safe="") +
                 "&code=" + urllib.parse.quote(case["code"], safe=""))
        elif op == "lookup":
            q = ("/CodeSystem/$lookup?system=" + urllib.parse.quote(case["system"], safe="") +
                 "&code=" + urllib.parse.quote(case["code"], safe=""))
        elif op == "subsumes":
            q = ("/CodeSystem/$subsumes?system=" + urllib.parse.quote(case["system"], safe="") +
                 "&codeA=" + urllib.parse.quote(case["codeA"], safe="") +
                 "&codeB=" + urllib.parse.quote(case["codeB"], safe=""))
        elif op == "translate":
            q = ("/ConceptMap/$translate?url=" + urllib.parse.quote(case["url"], safe="") +
                 "&system=" + urllib.parse.quote(case.get("system") or "", safe="") +
                 "&code=" + urllib.parse.quote(case["code"], safe=""))
        st, h, b = c.send("GET", q)
        ok, why = False, "status %s" % st
        try:
            doc = json.loads(b.decode("utf-8"))
        except Exception:
            doc = None
        if st == 200 and isinstance(doc, dict):
            if op == "expand":
                got = sorted((x.get("system"), x.get("code"))
                             for x in (doc.get("expansion") or {}).get("contains") or [])
                wantset = sorted(tuple(x) for x in want["codes"])
                ok = got == wantset
                why = "" if ok else ("%d codes vs %d" % (len(got), len(wantset)))
            else:
                ps = _params(doc)
                if op == "validate-code":
                    ok = ps.get("result", [None])[0] is want["result"]
                    why = "" if ok else "result %s" % ps.get("result")
                elif op == "lookup":
                    ok = (ps.get("display", [None])[0] == want["display"])
                    why = "" if ok else "display %r" % ps.get("display")
                elif op == "subsumes":
                    ok = ps.get("outcome", [None])[0] == want["outcome"]
                    why = "" if ok else "outcome %s" % ps.get("outcome")
                elif op == "translate":
                    got = sorted(pt.get("valueCoding", {}).get("code")
                                 for parts in ps.get("match#parts", [])
                                 for pt in parts if pt.get("name") == "concept")
                    ok = (ps.get("result", [None])[0] is True and
                          set(want["targets"]) <= set(got))
                    why = "" if ok else "targets %s" % got[:4]
        units[case["id"]] = (ok, 1.0)
        if not ok and misses < 20:
            log("  TERM miss %-44s %s %s" % (case["id"][:44], op, why[:60]))
            misses += 1
    return units


if __name__ == "__main__":
    u = run(sys.argv[1], sys.argv[2])
    print("TERMINOLOGY: %d/%d" % (sum(1 for v, _ in u.values() if v), len(u)))
