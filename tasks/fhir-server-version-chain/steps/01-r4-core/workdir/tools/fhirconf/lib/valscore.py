#!/usr/bin/env python3
"""Drive [base]/[Type]/$validate for every validation case and compare with the
official validator's verdict."""
import json, os, re, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client


def norm_path(p):
    """Drop array indices, ofType() casts and the [x] marker so two conformant
    validators can name the same element."""
    p = re.sub(r"\[\d+\]", "", p or "")
    p = re.sub(r"\.ofType\([A-Za-z]+\)", "", p)
    return p.replace("[x]", "")


def paths_match(w, g):
    """True when two element locations name the same place.  One validator may
    name the containing element, and a choice element may be named either by its
    base (`Observation.value`) or by the concrete member (`Observation.valueQuantity`)."""
    if w == g or w.startswith(g + ".") or g.startswith(w + "."):
        return True
    ws, gs = w.split("."), g.split(".")
    if len(ws) == len(gs) and ws[:-1] == gs[:-1]:
        a, b = ws[-1], gs[-1]
        return a.startswith(b) or b.startswith(a)
    return False


def run(base, valdir, expected_path, out_path=None, timeout=30, instance_ids=None):
    """`instance_ids` maps a resource type to an id that exists on the server; when
    given, every fifth case is sent to the instance-level form of the operation."""
    exp = json.load(open(expected_path))
    c = Client(base, timeout=timeout)
    instance_ids = instance_ids or {}
    rows, verdict_ok, path_ok, n_invalid = {}, 0, 0, 0
    for idx, case in enumerate(exp["cases"]):
        body = open(os.path.join(valdir, case["id"] + ".json"), "rb").read()
        url = "/%s/$validate" % case["type"]
        if idx % 5 == 0 and case["type"] in instance_ids:
            url = "/%s/%s/$validate" % (case["type"], instance_ids[case["type"]])
        st, h, b = c.send("POST", url, body, {"Content-Type": "application/fhir+json"})
        try:
            oo = json.loads(b.decode("utf-8"))
        except Exception:
            oo = None
        issues = oo.get("issue", []) if isinstance(oo, dict) else []
        is_oo = isinstance(oo, dict) and oo.get("resourceType") == "OperationOutcome"
        got_invalid = is_oo and any(i.get("severity") in ("error", "fatal") for i in issues)
        vok = is_oo and (got_invalid == case["invalid"])
        verdict_ok += 1 if vok else 0
        pok = None
        if case["invalid"]:
            n_invalid += 1
            want = {norm_path(p) for p in case["paths"]}
            got = {norm_path(p) for i in issues if i.get("severity") in ("error", "fatal")
                   for p in (i.get("expression") or [])}
            # Two conformant validators may name the element itself or the element
            # that contains it, so a location matches when one path is the other
            # or its ancestor.  Nothing pins the granularity.
            pok = any(paths_match(w, g) for w in want for g in got)
            path_ok += 1 if pok else 0
        rows[case["id"]] = {"mutation": case["mutation"], "verdict_ok": vok,
                            "path_ok": pok, "expected_invalid": case["invalid"],
                            "got_invalid": got_invalid, "status": st, "url": url}
    if out_path:
        json.dump(rows, open(out_path, "w"), indent=1, sort_keys=True)
    return verdict_ok, len(exp["cases"]), path_ok, n_invalid, rows


if __name__ == "__main__":
    v, n, p, ni, rows = run(sys.argv[1], sys.argv[2], sys.argv[3],
                            sys.argv[4] if len(sys.argv) > 4 else None)
    print("verdict agreement: %d/%d = %.3f" % (v, n, v / n))
    print("error located:     %d/%d = %.3f" % (p, ni, p / ni if ni else 0))
    bad = collections.Counter()
    for k, r in rows.items():
        if not r["verdict_ok"]:
            bad[r["mutation"]] += 1
    print("verdict misses by mutation:", bad.most_common())
    badp = collections.Counter()
    for k, r in rows.items():
        if r["path_ok"] is False:
            badp[r["mutation"]] += 1
    print("path misses by mutation:", badp.most_common())
