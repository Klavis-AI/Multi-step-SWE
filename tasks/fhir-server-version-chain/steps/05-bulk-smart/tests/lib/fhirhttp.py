#!/usr/bin/env python3
"""Replay engine and response normaliser shared by the recorder and the scorer.

The normaliser is applied identically to the reference server's response and to
the candidate's, so every volatile field is removed at recording time.
"""
import json, os, re, sys, urllib.request, urllib.error, urllib.parse, socket, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

VOLATILE_META = ("lastUpdated", "source")


_DEADLINE = None


def set_deadline(t):
    """`t` is an absolute time.monotonic() value, or None for no limit."""
    global _DEADLINE
    _DEADLINE = t


class Client:
    def __init__(self, base, timeout=60):
        self.base = base.rstrip("/")
        self.timeout = timeout

    def send(self, method, url, body=None, headers=None, raw=False):
        # One wall-clock deadline for the component now running.  Checking it
        # here rather than in each component's loop means a component that
        # issues requests directly -- the derived questions, the capability
        # questions, the terminology operations -- is bounded too, and a
        # candidate that answers slowly cannot spend another component's budget
        # (fleet advisory 18 13).
        if _DEADLINE is not None and time.monotonic() > _DEADLINE:
            return 0, {}, b""
        # percent-encode anything the URL grammar cannot carry literally
        url = "".join(ch if ord(ch) < 128 else urllib.parse.quote(ch) for ch in url)
        full = self.base + url
        data = None
        h = dict(headers or {})
        if body is not None:
            if isinstance(body, (dict, list)):
                data = json.dumps(body).encode()
                h.setdefault("Content-Type", "application/fhir+json")
            else:
                data = body.encode() if isinstance(body, str) else body
        req = urllib.request.Request(full, data=data, method=method)
        for k, v in h.items():
            req.add_header(k, v)
        # The deadline holds for a request already in flight as well: a request
        # started just before it may not run on for the full per-request
        # allowance, or the budget bounds nothing.
        tmo = self.timeout
        if _DEADLINE is not None:
            tmo = max(1.0, min(tmo, _DEADLINE - time.monotonic()))
        # And the allowance is for the whole exchange.  A socket timeout bounds
        # each read, not their sum, so a server that sends a byte at a time
        # never trips it; the exchange runs in a worker that is abandoned at
        # the limit and ends when the server is stopped.
        box = {}

        def work():
            try:
                box["r"] = self._exchange(req, tmo)
            except BaseException as e:      # re-raised below, as before
                box["e"] = e
        t = threading.Thread(target=work, daemon=True)
        t.start()
        t.join(tmo + 5)
        if t.is_alive():
            return 0, {}, b"no complete response within %d s" % int(tmo)
        if "e" in box:
            raise box["e"]
        return box["r"]

    @staticmethod
    def _exchange(req, tmo):
        try:
            with urllib.request.urlopen(req, timeout=tmo) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()
        except (urllib.error.URLError, socket.timeout, ConnectionError) as e:
            return 0, {}, str(e).encode()


_ENT = {"&apos;": "'", "&quot;": '"', "&#39;": "'", "&#34;": '"', "&#x27;": "'", "&#x22;": '"'}


def canon_div(s):
    """Narrative XHTML: how a character reference is written is a formatting
    choice no specification pins, so both sides are compared after normalising
    entities and inter-tag whitespace."""
    if not isinstance(s, str):
        return s
    for k, v in _ENT.items():
        s = s.replace(k, v)
    s = re.sub(r">\s+<", "><", s)
    return re.sub(r"\s+", " ", s).strip()


def xml_pairs(text):
    """A FHIR XML document reduced to its element paths and values.  Two
    conformant serialisations differ in whitespace and attribute order, never in
    this list."""
    try:
        import xml.etree.ElementTree as ET
        root = ET.fromstring(text)
    except Exception:
        return None
    out = []

    def strip_ns(t):
        return t.split("}", 1)[1] if "}" in t else t

    DROP = ("meta.lastUpdated", "meta.versionId", "Bundle.id@", "Bundle.timestamp",
            "issue.diagnostics", "issue.details.text", "issue.code@", "text.div")

    def walk(node, path):
        p = path + "." + strip_ns(node.tag) if path else strip_ns(node.tag)
        for k, v in sorted(node.attrib.items()):
            if strip_ns(k) == "xmlns":
                continue
            item = "%s@%s=%s" % (p, strip_ns(k), v)
            if not any(dr in item for dr in DROP):
                out.append(item)
        kids = list(node)
        if not kids and (node.text or "").strip():
            item = "%s=%s" % (p, " ".join((node.text or "").split()))
            if not any(dr in item for dr in DROP):
                out.append(item)
        seen = {}
        for kid in kids:
            t = strip_ns(kid.tag)
            seen[t] = seen.get(t, -1) + 1
            walk(kid, "%s.%s[%d]" % (p, t, seen[t]) if False else p)
    walk(root, "")
    return sorted(out)


def oo_projection(pairs):
    """An OperationOutcome is scored on what it reports, not on how it is
    dressed.  Narrative, `issue.expression`, `details.coding` and the rest are
    all allowed by the definitions and vary between conformant servers, so an
    XML OperationOutcome is reduced to whether it carries an error issue."""
    if not pairs or not all(p.startswith("OperationOutcome") for p in pairs):
        return pairs
    sev = sorted({p for p in pairs if p.startswith("OperationOutcome.issue.severity@value=")})
    if any(p.endswith(("=error", "=fatal")) for p in sev):
        return ["OperationOutcome.issue.severity@value=error"]
    return sev


def strip_volatile(node, base):
    """Recursively remove fields no two conformant servers must agree on."""
    if isinstance(node, list):
        return [strip_volatile(x, base) for x in node]
    if not isinstance(node, dict):
        if isinstance(node, str) and base and base in node:
            return node.replace(base, "$BASE")
        return node
    out = {}
    for k, v in node.items():
        if k == "meta" and isinstance(v, dict):
            m = {mk: mv for mk, mv in v.items() if mk not in VOLATILE_META}
            if m:
                out[k] = strip_volatile(m, base)
            continue
        if k == "div" and isinstance(v, str):
            out[k] = canon_div(v)
            continue
        out[k] = strip_volatile(v, base)
    return out


def canon_resource(res, base):
    """Canonical form of a single resource for byte-level comparison."""
    if not isinstance(res, dict):
        return res
    r = strip_volatile(res, base)
    rt = r.get("resourceType")
    if rt == "OperationOutcome":
        # Servers are free to word diagnostics and to pick among permitted
        # issue codes, so only the severities are comparable.
        return {"resourceType": "OperationOutcome",
                "severities": sorted(i.get("severity", "") for i in r.get("issue", []))}
    if rt == "Bundle":
        r.pop("id", None)
        r.pop("timestamp", None)
        r.pop("meta", None)
        r["link"] = sorted(l.get("relation", "") for l in r.get("link", []) or [])
        for e in r.get("entry", []) or []:
            if isinstance(e, dict):
                if isinstance(e.get("resource"), dict):
                    e["resource"] = canon_resource(e["resource"], base)
                rsp = e.get("response")
                if isinstance(rsp, dict):
                    rsp.pop("lastModified", None)
    return r


def entry_ids(bundle, mode=None):
    out = []
    for e in bundle.get("entry", []) or []:
        if not isinstance(e, dict):
            continue
        m = (e.get("search") or {}).get("mode")
        if mode is not None and m != mode:
            continue
        if mode is None and m not in (None, "match"):
            continue
        res = e.get("resource")
        if isinstance(res, dict) and res.get("resourceType"):
            out.append("%s/%s" % (res["resourceType"], res.get("id")))
    return out


def normalise(status, headers, body_bytes, base, case):
    """Reduce one exchange to the fields this case compares."""
    want = case.get("compare", ["status"])
    out = {"status": status}
    try:
        body = json.loads(body_bytes.decode("utf-8")) if body_bytes else None
    except Exception:
        body = None
    ctype = headers.get("Content-Type", "") or headers.get("content-type", "")

    if "status" in want:
        pass
    if "etag" in want:
        # http.html treats versionId as opaque, so only its presence and shape
        # are comparable, never the value a particular server happens to pick.
        et = (headers.get("ETag") or headers.get("etag") or "").strip()
        out["etag"] = bool(re.match(r'^W/".+"$', et))
    if "last_modified" in want:
        out["last_modified"] = bool(headers.get("Last-Modified") or headers.get("last-modified"))
    if "location" in want:
        loc = headers.get("Location") or headers.get("location") or ""
        m = re.search(r"/([A-Za-z]+)/([^/]+)(?:/_history/([^/]+))?$", loc)
        out["location"] = {"type": m.group(1), "versioned": bool(m.group(3))} if m else None
    if "ctype" in want:
        out["ctype"] = ctype.split(";")[0].strip().lower()
    if "xml" in want or "xml_paths" in want:
        txt = body_bytes.decode("utf-8", "replace") if body_bytes else ""
        m = re.search(r"<([A-Za-z]+)\b[^>]*xmlns=\"http://hl7\.org/fhir\"", txt)
        if "xml" in want:
            out["xml_root"] = m.group(1) if m else None
        if "xml_paths" in want:
            out["xml_paths"] = oo_projection(xml_pairs(txt))
    if "oo" in want:
        out["oo"] = bool(isinstance(body, dict) and body.get("resourceType") == "OperationOutcome"
                         and any(i.get("severity") in ("error", "fatal") for i in body.get("issue", [])))
    if "fhirversion" in want:
        out["fhirversion"] = body.get("fhirVersion") if isinstance(body, dict) else None
    if "rtype" in want:
        out["rtype"] = body.get("resourceType") if isinstance(body, dict) else None
    if isinstance(body, dict) and body.get("resourceType") == "Bundle":
        if "ids" in want:
            ids = entry_ids(body)
            out["ids"] = ids if case.get("ordered") else sorted(ids)
        if "include_ids" in want:
            out["include_ids"] = sorted(entry_ids(body, "include"))
        if "total" in want:
            out["total"] = body.get("total")
        if "has_total" in want:
            out["has_total"] = "total" in body
        if "btype" in want:
            out["btype"] = body.get("type")
        if "entry_status" in want:
            out["entry_status"] = [(((e.get("response") or {}).get("status") or "").split() or [""])[0]
                                   for e in body.get("entry", []) or []]
        if "count" in want:
            out["count"] = len(body.get("entry", []) or [])
        if "entry_keys" in want:
            ents = body.get("entry", []) or []
            res = (ents[0] or {}).get("resource") if ents else None
            out["entry_keys"] = sorted(res.keys()) if isinstance(res, dict) else None
    if "body" in want:
        out["body"] = canon_resource(body, base) if isinstance(body, dict) else None
    if "vid" in want:
        v = ((body or {}).get("meta") or {}).get("versionId") if isinstance(body, dict) else None
        out["vid"] = bool(v)
    if "sorted" in want and isinstance(body, dict) and body.get("resourceType") == "Bundle":
        import gen
        ok = True
        for spec in case.get("sort", []):
            keys = []
            for e in body.get("entry", []) or []:
                if (e.get("search") or {}).get("mode") not in (None, "match"):
                    continue
                r = e.get("resource") or {}
                vs = [v for v in gen.evaluate(r, spec["expr"]) if isinstance(v, str)]
                if vs:
                    keys.append(min(vs))
            seq = keys[::-1] if spec.get("desc") else keys
            if any(seq[i] > seq[i + 1] for i in range(len(seq) - 1)):
                ok = False
        out["sorted"] = ok
    if "keys" in want:
        out["keys"] = sorted(body.keys()) if isinstance(body, dict) else None
    return out
