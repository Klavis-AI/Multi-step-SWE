#!/usr/bin/env python3
"""Drive [base]/[Type]/$validate for every validation case and compare with the
official validator's verdict."""
import json, os, re, sys, time, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client


def norm_path(p):
    """Drop array indices, ofType() casts and the [x] marker so two conformant
    validators can name the same element."""
    p = re.sub(r"\[\d+\]", "", p or "")
    p = re.sub(r"\.ofType\([A-Za-z]+\)", "", p)
    return p.replace("[x]", "")


def paths_match(w, g):
    """True when two element locations name the same place.

    One validator may name the containing element, and a choice element may be
    named either by its base (`Observation.value`) or by the concrete member
    (`Observation.valueQuantity`) -- at ANY depth, not only the last segment:
    `Observation.value.code` and `Observation.valueQuantity.code` are the same
    element, and requiring the same spelling would score a naming convention.
    """
    ws, gs = w.split("."), g.split(".")
    # Compare segment by segment over the common prefix: one validator may name
    # the containing element and the other a child of it, and either may spell a
    # choice element by its base or by the concrete member.  `Observation.value`
    # and `Observation.valueQuantity.value` are the same place named at two
    # depths with two spellings, and requiring one of them would score a naming
    # convention rather than whether the error was located.
    for a, b in zip(ws, gs):
        if a != b and not (a.startswith(b) or b.startswith(a)):
            return False
    return True


def xpath_to_path(loc):
    """A DSTU2 `issue.location` as a dotted element path.

    DSTU2's OperationOutcome has no `expression`; it names the element in
    `location`, "a simple XPath limited to element names, repetition indicators
    and the default child access" (`/f:Patient/f:name[1]/f:given`).  Namespace
    prefixes, predicates and a trailing attribute step are dropped, and so is a
    resource-type step below the root (`.../f:resource/f:Patient/...`): XML
    wraps an inline resource in an element named by its type, JSON does not.
    A location already written as a dotted path keeps its steps."""
    loc = re.sub(r"\[[^\]]*\]", "", (loc or "").strip())
    if "/" not in loc:
        return loc
    segs = []
    for seg in loc.split("/"):
        seg = seg.strip()
        if not seg or seg.startswith("@") or seg in (".", "*"):
            continue
        seg = seg.split(":", 1)[-1]
        if segs and seg[:1].isupper():
            continue
        segs.append(seg)
    return ".".join(segs)


def issue_paths(issue, dstu2):
    """The element locations one issue names: `expression`, plus `location`
    at DSTU2, whose OperationOutcome has no `expression`."""
    paths = list(issue.get("expression") or [])
    if dstu2:
        locs = issue.get("location")
        if isinstance(locs, list):
            paths += [xpath_to_path(l) for l in locs if isinstance(l, str)]
    return paths


def run(base, valdir, expected_path, out_path=None, timeout=120, instance_ids=None,
        field="invalid", deadline=None):
    """`instance_ids` maps a resource type to an id that exists on the server; when
    given, every fifth case is sent to the instance-level form of the operation.

    `field` names the expectation to compare against.  A differential case
    carries the verdict at its own release AND the verdict the same instance
    gets at R4, so the same corpus can be driven against a second base; that is
    what makes the case a differential rather than a coin flip, because a server
    that answers one way everywhere must fail one of the two sides."""
    exp = json.load(open(expected_path))
    c = Client(base, timeout=timeout)
    dstu2 = base.rstrip("/").endswith("/baseDstu2")
    instance_ids = instance_ids or {}
    rows, verdict_ok, path_ok, n_invalid = {}, 0, 0, 0
    for idx, case in enumerate(exp["cases"]):
        if deadline is not None and time.monotonic() > deadline:
            rows[case["id"]] = {"mutation": case["mutation"], "verdict_ok": False,
                                "path_ok": False, "expected_invalid": bool(case[field]),
                                "got_invalid": None, "status": 0,
                                "url": "not reached before this component's deadline"}
            continue
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
        want_invalid = bool(case[field])
        vok = is_oo and (got_invalid == want_invalid)
        verdict_ok += 1 if vok else 0
        pok = None
        if want_invalid:
            n_invalid += 1
            want = {norm_path(p) for p in (case.get("paths_%s" % field)
                                           if field != "invalid" else case["paths"]) or []}
            got = {norm_path(p) for i in issues if i.get("severity") in ("error", "fatal")
                   for p in issue_paths(i, dstu2)}
            # Two conformant validators may name the element itself or the element
            # that contains it, so a location matches when one path is the other
            # or its ancestor.  Nothing pins the granularity.
            pok = any(paths_match(w, g) for w in want for g in got)
            path_ok += 1 if pok else 0
        rows[case["id"]] = {"mutation": case["mutation"], "verdict_ok": vok,
                            "path_ok": pok, "expected_invalid": want_invalid,
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
