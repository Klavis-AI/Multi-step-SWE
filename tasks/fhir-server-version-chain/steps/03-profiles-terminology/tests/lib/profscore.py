#!/usr/bin/env python3
"""Score profile validation.

Every case is an instance the official validator answers one way against the
base resource and the other way against the profile, so three requests are made
per case and the unit requires all of them:

  base      POST [base]/[Type]/$validate                 -> the base verdict
  explicit  POST [base]/[Type]/$validate?profile=<url>   -> the profile verdict
  implicit  POST [base]/[Type]/$validate with meta.profile put back
                                                         -> the profile verdict

A server that ignores the `profile` parameter fails `explicit`; one that never
resolves `meta.profile` fails `implicit`; and -- the reason the unit is a
conjunction -- a server that simply reports an error whenever a profile is
mentioned fails `base`, which is the only defence against a constant answer
taking the whole component (a profile only ever ADDS constraints, so every case
in the set is "valid at the base, invalid under the profile" and a one-sided
unit would be vacuous).

Answering "invalid" whenever a profile is named still passes all three for such
a case, so each case also names a partner: an unmutated published example the
same validator finds conforming both at the base and under its own profile (the
same profile where the IG has one, else the same type, else another of the
IG's). A case's unit, and its location unit, are earned only when the
partner's three verdicts are right too.
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


def _three(c, casedir, cid, rtype, profile):
    raw = open(os.path.join(casedir, cid + ".json"), "rb").read()
    claimed = json.loads(raw.decode("utf-8"))
    claimed.setdefault("meta", {})["profile"] = [profile]
    b = _verdict(c, "/%s/$validate" % rtype, raw)
    e = _verdict(c, "/%s/$validate?profile=%s" % (rtype, urllib.parse.quote(profile, safe="")), raw)
    i = _verdict(c, "/%s/$validate" % rtype, json.dumps(claimed).encode("utf-8"))
    return raw, claimed, b, e, i


def run(base, casedir, expected_path, log=print, timeout=120, deadline=None):
    exp = json.load(open(expected_path))
    c = Client(base, timeout=timeout)
    units, located, misses = {}, {}, 0
    partners, pdone = exp.get("partners", {}), {}

    def partner_ok(pid):
        if pid not in pdone:
            p = partners[pid]
            _, _, b, e, i = _three(c, casedir, pid, p["type"], p["profile"])
            pdone[pid] = (b[0] and b[1] == bool(p["invalid_without_profile"])
                          and e[0] and e[1] == bool(p["invalid"])
                          and i[0] and i[1] == bool(p["invalid"]))
            if not pdone[pid]:
                log("  PROFILE partner miss %s (conforming example; base/explicit/implicit "
                    "invalid=%s/%s/%s)" % (pid[:48], b[1], e[1], i[1]))
        return pdone[pid]

    for case in exp["cases"]:
        if deadline is not None and time.monotonic() > deadline:
            units[case["id"]] = (False, 1.0)
            if case.get("invalid"):
                located[case["id"] + "/loc"] = (False, 1.0)
            continue
        pair = partner_ok(case["partner"]) if case.get("partner") else True
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
        ok = all(parts.values()) and pair
        units[case["id"]] = (ok, 1.0)
        if want_prof:
            want = {norm_path(p) for p in case["paths"]}
            hit = any(paths_match(w, g) for w in want for g in paths_e)
            located[case["id"] + "/loc"] = (hit and pair, 1.0)
            if not hit and misses < 20:
                log("  PROFLOC miss %-42s wanted one of %s, named %s"
                    % (case["id"][:42], sorted(want)[:3], sorted(paths_e)[:3]))
                misses += 1
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
