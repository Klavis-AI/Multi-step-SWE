#!/usr/bin/env python3
"""Score Bulk Data export, SMART scopes and `$everything`.

Every expected answer here is COMPUTED, not recorded: the patient compartment
comes from the published `CompartmentDefinition`, the export contents from the
resources the server was seeded with, and the SMART status codes from the scope
grammar in `scopes-and-launch-context.html`.  Nothing in this module compares
against another server's behaviour.
"""
import base64, hashlib, hmac, json, os, re, sys, time, urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client


# ---------------------------------------------------------------- compartment
def compartment_params(compdef):
    """-> {resource type: [search parameter codes that put it in a patient's
    compartment]} from the published CompartmentDefinition."""
    out = {}
    for r in compdef.get("resource") or []:
        if r.get("param"):
            out[r["code"]] = list(r["param"])
    return out


def _refs(doc, path):
    """Every `reference` string a dotted element path reaches.

    Search-parameter expressions carry FHIRPath function calls -- most often
    `.where(resolve() is Patient)` -- which narrow the result rather than
    navigate.  Dropping them widens the path to every reference at that element,
    and membership is then decided by the reference itself pointing at the
    patient, which is the same answer.
    """
    segs = []
    for seg in path.split("."):
        if "(" in seg:          # a function call narrows rather than navigates
            break
        segs.append(seg)
    path = ".".join(segs)
    if not path:
        return []
    cur = [doc]
    for seg in path.split("."):
        nxt = []
        for c in cur:
            if not isinstance(c, dict):
                continue
            v = c.get(seg)
            if v is None:
                for k in c:
                    if k.startswith(seg) and len(k) > len(seg) and k[len(seg)].isupper():
                        v = c[k]
                        break
            if v is None:
                continue
            nxt.extend(v if isinstance(v, list) else [v])
        cur = nxt
    out = []
    for c in cur:
        if isinstance(c, dict) and isinstance(c.get("reference"), str):
            out.append(c["reference"])
        elif isinstance(c, str):
            out.append(c)
    return out


def in_compartment(doc, pid, params, sp_expr):
    rt = doc.get("resourceType")
    if rt == "Patient":
        return doc.get("id") == pid
    for code in params.get(rt, []):
        expr = sp_expr.get((rt, code))
        for e in (expr or "").split("|"):
            e = e.strip()
            if not e.startswith(rt + "."):
                continue
            for ref in _refs(doc, e[len(rt) + 1:]):
                if ref == "Patient/" + pid or ref.endswith("/Patient/" + pid):
                    return True
    return False


# ---------------------------------------------------------------- the scorer
# async-bulk.html: on the kick-off "A client SHOULD provide this header [Accept]
# ... If omitted, the server MAY return an error", so every kick-off sends it.
KICKOFF = {"Prefer": "respond-async", "Accept": "application/fhir+json"}


def run(root, datadir, log=print, deadline=None):
    units = {}

    def U(name, ok, note=""):
        units[name] = (bool(ok), 1.0)
        if not ok:
            log("  BULK miss %-40s %s" % (name, note))

    base = root.rstrip("/") + "/baseR4"
    c = Client(base, timeout=120)
    seeddir = os.path.join(datadir, "seed-bulk")
    compdef = json.load(open(os.path.join(datadir, "compartment-patient.json")))
    params = compartment_params(compdef)
    sp_expr = {tuple(k.split("|")): v
               for k, v in json.load(open(os.path.join(datadir, "bulk-sp.json"))).items()}
    # The tokens are MINTED HERE, from the key the agent also has and the scope
    # matrix, because the `iss` claim has to name the server under test: a token
    # minted at authoring time carries whatever base URL the authoring server
    # had, and every request with it is then a 401 about the port rather than
    # about the scope.
    jwks = json.load(open(os.path.join(datadir, "jwks.json")))
    k = jwks["keys"][0]
    key = base64.urlsafe_b64decode(k["k"] + "=" * (-len(k["k"]) % 4))

    def b64(d):
        return base64.urlsafe_b64encode(d).decode().rstrip("=")

    # The `iss` a token must carry is the base URL the SERVER believes it has,
    # which need not be the host name the grader dialled it on; the server states
    # it in its own CapabilityStatement.
    st, _, body = c.send("GET", "/metadata", None, {})
    try:
        impl = (json.loads(body.decode("utf-8")).get("implementation") or {}).get("url")
    except Exception:
        impl = None
    issuer = (impl or root).rstrip("/")
    for suffix in ("/baseR4", "/baseDstu2", "/baseDstu3", "/baseR4B", "/baseR5", "/baseR6"):
        if issuer.endswith(suffix):
            issuer = issuer[: -len(suffix)]
            break

    def mint(scope, patient=None, iss=None, exp=None, kid=None, break_sig=False):
        h = {"alg": "HS256", "typ": "JWT", "kid": kid or k["kid"]}
        p = {"iss": iss if iss is not None else issuer,
             "scope": scope,
             "exp": exp if exp is not None else int(time.time()) + 3600}
        if patient:
            p["patient"] = patient
        si = "%s.%s" % (b64(json.dumps(h).encode()), b64(json.dumps(p).encode()))
        sig = b64(hmac.new(key, si.encode(), hashlib.sha256).digest())
        if break_sig:
            sig = ("A" * 4 + sig[4:]) if not sig.startswith("AAAA") else ("B" * 4 + sig[4:])
        return "%s.%s" % (si, sig)

    tokens = json.load(open(os.path.join(datadir, "smart-tokens.json")))
    for name, t in tokens.items():
        t["token"] = mint(t["scope"], t.get("patient"),
                          iss=t.get("iss"),
                          exp=(int(time.time()) - 60) if t.get("expired") else None,
                          kid=t.get("kid"),
                          break_sig=bool(t.get("break_sig")))

    # ---- the data this rung is scored over, written through the API
    docs = []
    for fn in sorted(os.listdir(seeddir)):
        if not fn.endswith(".json"):
            continue
        d = json.load(open(os.path.join(seeddir, fn), encoding="utf-8-sig"))
        st, _, _ = c.send("PUT", "/%s/%s" % (d["resourceType"], d["id"]), d,
                          {"Content-Type": "application/fhir+json",
                           "Prefer": "return=minimal"})
        if st in (200, 201):
            docs.append(d)
    U("seed", len(docs) > 20, "%d of %d written" % (len(docs), len(os.listdir(seeddir))))

    # ---- the discovery document
    st, _, body = c.send("GET", "/.well-known/smart-configuration", None, {})
    try:
        cfg = json.loads(body.decode("utf-8"))
    except Exception:
        cfg = {}
    U("smart-config-status", st == 200, "status %s" % st)
    for k in ("issuer", "jwks_uri", "authorization_endpoint", "token_endpoint",
              "capabilities", "code_challenge_methods_supported"):
        U("smart-config-" + k, k in cfg, "missing %s" % k)
    U("smart-config-not-a-resource", "resourceType" not in cfg,
      "the discovery document is not a FHIR resource")

    # ---- the token matrix
    made = []
    for name, t in sorted(tokens.items()):
        hdr = {"Authorization": "Bearer " + t["token"]}
        for label, method, path, want in t["expect"]:
            st, h, body = c.send(method, path, {"resourceType": "Observation",
                                                "status": "final",
                                                "code": {"text": "scope probe"}}
                                 if method in ("POST", "PUT") else None,
                                 dict(hdr, **({"Content-Type": "application/fhir+json"}
                                              if method in ("POST", "PUT") else {})))
            if method == "POST" and st == 201:
                loc = (h.get("Location") or h.get("location") or "")
                rid = loc.split("/_history")[0].rstrip("/").rsplit("/", 1)[-1]
                if rid:
                    made.append("Observation/" + rid)
            U("smart-%s-%s" % (name, label), st == want,
              "%s %s -> %s, wanted %s" % (method, path, st, want))
    for ref in made:
        c.send("DELETE", "/" + ref, None, {})
    st, _, _ = c.send("GET", "/Patient", None, {})
    U("smart-open-without-a-token", st == 200,
      "a request with no Authorization header -> %s" % st)

    # ---- $export
    def start(path, qs=""):
        st, h, _ = c.send("GET", path + (("?" + qs) if qs else ""), None, KICKOFF)
        loc = h.get("Content-Location") or h.get("content-location")
        return st, loc

    def rel_of(url):
        """The server states its own BASE_URL, which need not be the host name
        the grader dialled it on (`localhost` against `127.0.0.1`).  Only the
        path is portable."""
        if not url:
            return url
        p = urllib.parse.urlsplit(url)
        return p.path + (("?" + p.query) if p.query else "")

    def manifest(loc, tries=40):
        rel = rel_of(loc)
        rc = Client(root.rstrip("/"), timeout=120)
        for _ in range(tries):
            st, _, body = rc.send("GET", rel, None, {})
            if st == 200:
                return json.loads(body.decode("utf-8"))
            if st != 202:
                return None
            time.sleep(0.5)
        return None

    st, loc = start("/$export")
    U("export-accepted", st == 202, "status %s" % st)
    U("export-content-location", bool(loc) and loc.startswith("http"),
      "Content-Location %r" % loc)
    m = manifest(loc) if loc else None
    U("export-manifest", isinstance(m, dict), "no manifest")
    if isinstance(m, dict):
        for k in ("transactionTime", "request", "requiresAccessToken", "output"):
            U("export-manifest-" + k, k in m, "missing %s" % k)
        U("export-manifest-not-a-resource", "resourceType" not in m,
          "the manifest is not a FHIR resource")
        want = {}
        for d in docs:
            want[d["resourceType"]] = want.get(d["resourceType"], 0) + 1
        got = {o["type"]: o for o in m.get("output") or []}
        U("export-types", set(got) == set(want),
          "types %s, wanted %s" % (sorted(got), sorted(want)))
        U("export-no-empty-files", all(o.get("url") for o in m.get("output") or []),
          "an output entry with no url")
        # the NDJSON itself
        if got:
            t0 = sorted(got)[0]
            url = got[t0]["url"]
            rel = rel_of(url)
            rc = Client(root.rstrip("/"), timeout=120)
            st, h, body = rc.send("GET", rel, None, {}, raw=True)
            ct = (h.get("Content-Type") or h.get("content-type") or "")
            U("ndjson-status", st == 200, "status %s" % st)
            U("ndjson-content-type", "ndjson" in ct, "Content-Type %r" % ct)
            lines = [l for l in (body or b"").decode("utf-8", "replace").split("\n") if l.strip()]
            U("ndjson-lines", len(lines) == want.get(t0, -1),
              "%d lines for %d %s" % (len(lines), want.get(t0, -1), t0))
            ok = True
            for l in lines:
                try:
                    d = json.loads(l)
                except Exception:
                    ok = False
                    break
                if d.get("resourceType") != t0:
                    ok = False
                    break
            U("ndjson-parses", ok, "a line is not a %s resource" % t0)

    # parameters and errors
    st, loc = start("/$export", "_type=Patient")
    m = manifest(loc) if loc else None
    U("export-type-filter",
      isinstance(m, dict) and {o["type"] for o in m.get("output") or []} <= {"Patient"},
      "output %s" % (sorted(o["type"] for o in (m or {}).get("output") or [])))
    st, _, _ = c.send("GET", "/$export?_outputFormat=csv", None, KICKOFF)
    U("export-bad-outputformat", st == 400, "status %s" % st)
    st, _, _ = c.send("GET", "/$export?_type=NoSuchType", None, KICKOFF)
    U("export-unknown-type", st == 400, "status %s" % st)
    st, _, _ = c.send("GET", "/$export", None, {"Accept": "application/fhir+json"})
    U("export-needs-prefer", st == 400, "without Prefer -> %s" % st)
    rc = Client(root.rstrip("/"), timeout=120)
    st, _, _ = rc.send("GET", "/bulk/no-such-job", None, {})
    U("export-unknown-job", st == 404, "status %s" % st)
    st, loc = start("/Patient/$export")
    if loc:
        rel = rel_of(loc)
        manifest(loc)
        st, _, _ = rc.send("DELETE", rel, None, {})
        U("export-cancel", st == 202, "DELETE -> %s" % st)
        st, _, _ = rc.send("GET", rel, None, {})
        U("export-cancelled-is-gone", st == 404, "GET after DELETE -> %s" % st)

    # ---- $everything
    pids = [d["id"] for d in docs if d["resourceType"] == "Patient"]
    for pid in pids[:6]:
        st, _, body = c.send("GET", "/Patient/%s/$everything" % pid, None, {})
        try:
            bn = json.loads(body.decode("utf-8"))
        except Exception:
            bn = {}
        got = {"%s/%s" % (e["resource"]["resourceType"], e["resource"]["id"])
               for e in bn.get("entry") or [] if isinstance(e.get("resource"), dict)}
        want = {"%s/%s" % (d["resourceType"], d["id"]) for d in docs
                if in_compartment(d, pid, params, sp_expr)}
        U("everything-%s" % pid, st == 200 and got == want,
          "status %s; missing %s; extra %s" % (st, sorted(want - got)[:4],
                                               sorted(got - want)[:4]))
    st, _, _ = c.send("GET", "/Patient/no-such-patient/$everything", None, {})
    U("everything-unknown", st == 404, "status %s" % st)
    st, _, _ = c.send("GET", "/Patient/$everything", None, {})
    U("everything-type-level", st in (400, 404), "status %s" % st)

    # Everything this component wrote comes back out: the carried-forward R4
    # corpus runs after it against the same store, and 88 extra resources turn
    # up there as extra ids -- a miss in a component that is not at fault.
    for d in docs:
        c.send("DELETE", "/%s/%s" % (d["resourceType"], d["id"]), None, {})

    log("BULK/SMART/EVERYTHING: %d/%d"
        % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


if __name__ == "__main__":
    run(sys.argv[1], sys.argv[2])
