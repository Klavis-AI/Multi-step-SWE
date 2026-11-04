#!/usr/bin/env python3
"""Check version negotiation on the media type.

Every expected answer comes from `versions.html` and from the two releases'
StructureDefinitions: the parameter carries major.minor, the response echoes the
parameter it answered with, a release this server does not hold makes the media
type unacceptable, and the representation the answer is rendered in is the one
the named release defines -- so an element that release does not define is not
in it, and a resource type it does not define has no representation at all.
"""
import json, os, sys, time, urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client

BASES = {"1.0": "baseDstu2", "3.0": "baseDstu3", "4.0": "baseR4",
         "4.3": "baseR4B", "5.0": "baseR5", "6.0": "baseR6"}


def run(root, datadir, log=print, deadline=None):
    units = {}

    def U(name, ok, note=""):
        units[name] = (bool(ok), 1.0)
        if not ok:
            log("  NEG miss %-44s %s" % (name, note))

    spec = json.load(open(os.path.join(datadir, "negotiate.json")))
    root = root.rstrip("/")

    # the instances the rendering cases read back, written at their own base
    written = 0
    for base, docs in sorted((spec.get("seed") or {}).items()):
        c = Client("%s/%s" % (root, base), timeout=120)
        for d in docs:
            st, _, _ = c.send("PUT", "/%s/%s" % (d["resourceType"], d["id"]), d,
                              {"Content-Type": "application/fhir+json",
                               "Prefer": "return=minimal"})
            if st in (200, 201):
                written += 1
    U("seed", written == sum(len(v) for v in (spec.get("seed") or {}).values()),
      "%d of %d written" % (written, sum(len(v) for v in (spec.get("seed") or {}).values())))

    for case in spec["cases"]:
        if deadline is not None and time.monotonic() > deadline:
            U(case["id"], False, "not reached before the deadline")
            continue
        c = Client("%s/%s" % (root, case["base"]), timeout=120)
        hdrs = dict(case.get("headers") or {})
        body = case.get("body")
        if body is not None and "Content-Type" not in hdrs:
            hdrs["Content-Type"] = "application/fhir+json"
        st, h, raw = c.send(case.get("method", "GET"), case["url"], body, hdrs, raw=True)
        want = case["expect"]
        ok = st == want["status"]
        note = "status %s, wanted %s" % (st, want["status"])
        ct = (h.get("Content-Type") or h.get("content-type") or "")
        if ok and "content_type_has" in want:
            ok = want["content_type_has"] in ct
            note = "Content-Type %r, wanted %r in it" % (ct, want["content_type_has"])
        if ok and "content_type_lacks" in want:
            ok = want["content_type_lacks"] not in ct
            note = "Content-Type %r must not carry %r" % (ct, want["content_type_lacks"])
        if ok and ("absent" in want or "present" in want):
            try:
                doc = json.loads((raw or b"").decode("utf-8"))
            except Exception:
                doc = {}
            for k in want.get("absent", []):
                if k in doc:
                    ok = False
                    note = "%s must not appear in the %s representation" % (k, case.get("as"))
            for k in want.get("present", []):
                if k not in doc:
                    ok = False
                    note = "%s must appear in the %s representation" % (k, case.get("as"))
        U(case["id"], ok, note)

    # Everything this check seeded comes back out: the REST corpora and the
    # bulk export run against the same stores and would find these instances as
    # extra ids.
    for base, docs in sorted((spec.get("seed") or {}).items()):
        c = Client("%s/%s" % (root, base), timeout=60)
        for d in docs:
            c.send("DELETE", "/%s/%s" % (d["resourceType"], d["id"]), None, {})

    log("NEGOTIATE: %d/%d" % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


if __name__ == "__main__":
    run(sys.argv[1], sys.argv[2])
