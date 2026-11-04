#!/usr/bin/env python3
"""Check profile validation.

Every case is an instance the official validator answers one way against the
base resource and the other way against the profile, so three requests are made
per case and a case passes only when all three answers are right:

  base      POST [base]/[Type]/$validate                 -> the base verdict
  explicit  POST [base]/[Type]/$validate?profile=<url>   -> the profile verdict
  implicit  POST [base]/[Type]/$validate with meta.profile put back
                                                         -> the profile verdict

A server that ignores the `profile` parameter fails `explicit`; one that never
resolves `meta.profile` fails `implicit`; and -- the reason a case needs all
three -- a server that simply reports an error whenever a profile is mentioned
fails `base`, which is the only thing that stops a constant answer from passing
every case (a profile only ever ADDS constraints, so every case in the set is
"valid at the base, invalid under the profile" and a one-sided check would be
vacuous).
"""
import copy, json, os, sys, time, urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client
from valscore import norm_path, paths_match


def _verdict(c, url, body):
    st, h, b = c.send("POST", url, body, {"Content-Type": "application/fhir+json"})
    try:
        oo = json.loads(b.decode("utf-8"))
    except Exception:
        oo = None
    issues = oo.get("issue", []) if isinstance(oo, dict) else []
    is_oo = isinstance(oo, dict) and oo.get("resourceType") == "OperationOutcome"
    invalid = is_oo and any(i.get("severity") in ("error", "fatal") for i in issues)
    paths = {norm_path(p) for i in issues if i.get("severity") in ("error", "fatal")
             for p in (i.get("expression") or [])}
    return is_oo, invalid, paths


def run(base, casedir, expected_path, log=print, timeout=60, deadline=None):
    exp = json.load(open(expected_path))
    c = Client(base, timeout=timeout)
    units, located, misses = {}, {}, 0
    for case in exp["cases"]:
        if deadline is not None and time.monotonic() > deadline:
            units[case["id"]] = (False, 1.0)
            continue
        raw = open(os.path.join(casedir, case["id"] + ".json"), "rb").read()
        doc = json.loads(raw.decode("utf-8"))
        claimed = copy.deepcopy(doc)
        claimed.setdefault("meta", {})["profile"] = [case["profile"]]
        want_base = bool(case["invalid_without_profile"])
        want_prof = bool(case["invalid"])

        ok_b, inv_b, _ = _verdict(c, "/%s/$validate" % case["type"], raw)
        url = "/%s/$validate?profile=%s" % (case["type"],
                                            urllib.parse.quote(case["profile"], safe=""))
        ok_e, inv_e, paths_e = _verdict(c, url, raw)
        ok_i, inv_i, _ = _verdict(c, "/%s/$validate" % case["type"],
                                  json.dumps(claimed).encode("utf-8"))

        parts = {"base": ok_b and inv_b == want_base,
                 "explicit": ok_e and inv_e == want_prof,
                 "implicit": ok_i and inv_i == want_prof}
        ok = all(parts.values())
        units[case["id"]] = (ok, 1.0)
        if want_prof:
            want = {norm_path(p) for p in case["paths"]}
            located[case["id"] + "/loc"] = (
                any(paths_match(w, g) for w in want for g in paths_e), 1.0)
        if not ok and misses < 20:
            log("  PROFILE miss %-46s %s (base wanted invalid=%s, profile wanted "
                "invalid=%s)" % (case["id"][:46],
                                 ",".join(k for k, v in sorted(parts.items()) if not v),
                                 want_base, want_prof))
            misses += 1
    return units, located


if __name__ == "__main__":
    u, l = run(sys.argv[1], sys.argv[2], sys.argv[3])
    print("PROFILE: %d/%d verdicts, %d/%d located"
          % (sum(1 for v, _ in u.values() if v), len(u),
             sum(1 for v, _ in l.values() if v), len(l)))
