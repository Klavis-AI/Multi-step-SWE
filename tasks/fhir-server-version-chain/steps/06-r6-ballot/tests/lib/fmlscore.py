#!/usr/bin/env python3
"""Score the FHIR Mapping Language component.

Two halves, both with expectations from the official HL7 validator 6.10.4 or
from FHIR/fhir-test-cases itself -- never written by hand:

  * conversion -- POST the `.map` source with `Content-Type: text/fhir-mapping`,
    read the stored `StructureMap` back as JSON and compare it with the resource
    the official validator produces from the same source (`convert`).  Only the
    fields the instruction names are compared: url, name, structure, group,
    input, rule, source, target, dependent.
  * transform -- run the map over the suite's own source instance through
    `StructureMap/$transform` and compare with the suite's expected output.
"""
import json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client


def _one(v):
    return v if isinstance(v, list) else ([] if v is None else [v])


WILDCARD = "\x00*"


UNITS = ("year", "month", "week", "day", "hour", "minute", "second", "millisecond")
_UNIT_RE = re.compile(r"(\d)\s+(" + "|".join(u + "s?" for u in UNITS) + r")\b")


def canon_expr(v):
    """FHIRPath writes a time-valued quantity either way -- `5 days` and
    `5 'days'` are the same literal -- and whitespace inside an expression is
    not significant.  Both are normalised so the comparison is about the
    expression rather than about how it was spelled."""
    v = _UNIT_RE.sub(lambda m: "%s '%s'" % (m.group(1), m.group(2)), v)
    # FHIRPath writes a string literal with either quote; the oracle re-renders
    # every one of them single-quoted.
    v = re.sub(r'"((?:[^"\\]|\\.)*)"', lambda m: "'%s'" % m.group(1), v)
    v = re.sub(r"\s*,\s*", ",", v)
    return re.sub(r"\s+", " ", v).strip()


def proj_param(p):
    """A transform parameter carries its value in a choice element.  The oracle
    renders `create('Reference')` and `reference(src)` with an EMPTY parameter
    object -- it loses the argument -- so such a parameter is a wildcard here
    rather than an assertion that the argument is absent."""
    for k, v in sorted(p.items()):
        if k.startswith("value"):
            return canon_expr(str(v))
    return WILDCARD


def norm_transform(t):
    """`mapping-language.html` writes a plain assignment several ways -- the
    short form `src.a -> tgt.b;`, `copy`, and `create` with no argument all mean
    the same thing -- and the variable a renderer invents for the short form is
    its own choice.  Both are folded here, so what is compared is the map the
    source text describes rather than one renderer's spelling of it."""
    tr = t.get("transform", "")
    if tr in ("", "copy", "create") and not _one(t.get("parameter")):
        tr = "assign"
    return tr


def proj_rule(r):
    return {
        "name": r.get("name", ""),
        "source": [{"context": s.get("context", ""), "element": s.get("element", ""),
                    "condition": canon_expr(s.get("condition", "")),
                    "check": canon_expr(s.get("check", "")),
                    "listMode": s.get("listMode", "")}
                   for s in _one(r.get("source"))],
        "target": [{"context": t.get("context", ""), "element": t.get("element", ""),
                    "transform": norm_transform(t),
                    "listMode": sorted(_one(t.get("listMode"))),
                    "parameter": [proj_param(p) for p in _one(t.get("parameter"))]}
                   for t in _one(r.get("target"))],
        "dependent": [d.get("name", "") for d in _one(r.get("dependent"))],
        "rule": [proj_rule(x) for x in _one(r.get("rule"))],
    }


def proj_map(m):
    return {
        "url": m.get("url", ""),
        "name": m.get("name", ""),
        "structure": sorted([(s.get("url", ""), s.get("mode", ""), s.get("alias", ""))
                             for s in _one(m.get("structure"))]),
        "group": [{"name": g.get("name", ""),
                   "extends": g.get("extends", ""),
                   "typeMode": "" if g.get("typeMode") in (None, "none") else g["typeMode"],
                   "input": [(i.get("name", ""), i.get("type", ""), i.get("mode", ""))
                             for i in _one(g.get("input"))],
                   "rule": [proj_rule(r) for r in _one(g.get("rule"))]}
                  for g in _one(m.get("group"))],
    }


def diff(a, b, path=""):
    """-> a short list of the first places two projections differ."""
    out = []
    if type(a) is not type(b):
        return ["%s: %s vs %s" % (path or ".", type(a).__name__, type(b).__name__)]
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            out += diff(a.get(k), b.get(k), "%s.%s" % (path, k))
    elif isinstance(a, list):
        if len(a) != len(b):
            return ["%s: %d vs %d entries" % (path, len(a), len(b))]
        for i, (x, y) in enumerate(zip(a, b)):
            out += diff(x, y, "%s[%d]" % (path, i))
    elif a != b and a != WILDCARD:
        out.append("%s: %r vs %r" % (path, a, b))
    return out[:6]


def strip_server(obj):
    """Drop what the server owns rather than the map: id, meta, narrative."""
    if isinstance(obj, dict):
        return {k: strip_server(v) for k, v in obj.items()
                if k not in ("id", "meta", "text", "implicitRules", "language")}
    if isinstance(obj, list):
        return [strip_server(v) for v in obj]
    return obj


def norm_out(obj):
    return strip_server(obj)


# R4 `http.html` leaves the body of a create to the server unless the client
# asks: "In the absence of the header, servers may choose whether to return the
# full resource or not".  So the create asks for the representation, and when a
# server still answers without one, the stored map is read back from Location.
CREATE = {"Content-Type": "text/fhir-mapping", "Prefer": "return=representation"}


def _stored_map(c, h, b):
    """The StructureMap a create stored: the response body when it is one, else
    the resource read back from the `Location` the create answered with."""
    try:
        got = json.loads(b.decode("utf-8")) if b else None
    except Exception:
        got = None
    if isinstance(got, dict) and got.get("resourceType") == "StructureMap":
        return got
    loc = next((v for k, v in (h or {}).items()
                if k.lower() in ("location", "content-location")), None)
    if isinstance(loc, str) and "StructureMap/" in loc:
        st2, _, b2 = c.send("GET", "/" + loc[loc.index("StructureMap/"):], None,
                            {"Accept": "application/fhir+json"})
        if st2 == 200:
            try:
                back = json.loads(b2.decode("utf-8"))
            except Exception:
                back = None
            if isinstance(back, dict) and back.get("resourceType") == "StructureMap":
                return back
    return None


def run(base, fmldir, log=print, timeout=120):
    man = json.load(open(os.path.join(fmldir, "manifest.json")))
    c = Client(base, timeout=timeout)
    ok = n = 0
    rows = {}

    for t in man.get("convert", []):
        n += 1
        src = open(os.path.join(fmldir, t["map"]), "rb").read()
        want = json.load(open(os.path.join(fmldir, t["expected"])))
        st, h, b = c.send("POST", "/StructureMap", src, CREATE)
        good, why = False, "POST %s" % st
        if st in (200, 201):
            got = _stored_map(c, h, b)
            if got is not None:
                d = diff(proj_map(want), proj_map(got))
                good, why = (not d), ("; ".join(d) if d else "")
            else:
                why = "no StructureMap in the create body or at its Location"
        ok += 1 if good else 0
        rows[t["map"]] = {"kind": "convert", "ok": good, "why": why}
        if not good:
            log("  FML convert %-34s %s" % (t["map"], why[:110]))

    for t in man.get("transform", []):
        n += 1
        src = open(os.path.join(fmldir, t["map"]), "rb").read()
        st, h, b = c.send("POST", "/StructureMap", src, CREATE)
        good, why = False, "POST map %s" % st
        if st in (200, 201):
            url = (_stored_map(c, h, b) or {}).get("url")
            if not url:
                url = t.get("url")
            body = open(os.path.join(fmldir, t["source"]), "rb").read()
            st2, h2, b2 = c.send("POST", "/StructureMap/$transform?source=" + (url or ""),
                                 body, {"Content-Type": "application/fhir+json"})
            if st2 == 200:
                try:
                    got = json.loads(b2.decode("utf-8"))
                except Exception:
                    got = None
                want = json.load(open(os.path.join(fmldir, t["output"])))
                d = diff(norm_out(want), norm_out(got))
                good, why = (not d), ("; ".join(d) if d else "")
            else:
                why = "$transform %s" % st2
        ok += 1 if good else 0
        rows[t["map"] + "/transform"] = {"kind": "transform", "ok": good, "why": why}
        if not good:
            log("  FML transform %-32s %s" % (t["map"], why[:110]))

    return ok, n, rows


if __name__ == "__main__":
    o, n, rows = run(sys.argv[1], sys.argv[2])
    print("FML: %d/%d" % (o, n))
