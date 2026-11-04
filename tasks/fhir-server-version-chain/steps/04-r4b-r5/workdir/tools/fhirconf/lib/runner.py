#!/usr/bin/env python3
"""Drive a corpus against a FHIR service base and normalise every exchange."""
import json, os, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fhirhttp import Client, normalise


def _refs(node, out):
    if isinstance(node, list):
        for x in node:
            _refs(x, out)
    elif isinstance(node, dict):
        for k, v in node.items():
            if k == "reference" and isinstance(v, str) and "/" in v and not v.startswith("#"):
                tail = v.split("/")
                if len(tail) >= 2:
                    out.add("%s/%s" % (tail[-2], tail[-1]))
            else:
                _refs(v, out)


def seed_order(seed_dir):
    """Targets before the resources that point at them.

    A server that resolves references while indexing cannot index a reference
    whose target does not exist yet, so the load order is topological over the
    reference graph, with cycles broken in file-name order.  The answers were
    recorded with this same function, so the reference server and the server
    being checked see the same sequence."""
    docs = {}
    for fn in sorted(os.listdir(seed_dir)):
        if fn.startswith("_") or not fn.endswith(".json"):
            continue
        d = json.load(open(os.path.join(seed_dir, fn), encoding="utf-8-sig"))
        docs["%s/%s" % (d["resourceType"], d["id"])] = (fn, d)
    deps = {}
    for key, (fn, d) in docs.items():
        s = set()
        _refs(d, s)
        deps[key] = {t for t in s if t in docs and t != key}
    order, state = [], {}
    def visit(k):
        st = state.get(k)
        if st == 2:
            return
        if st == 1:                      # a cycle: break it here
            return
        state[k] = 1
        for t in sorted(deps.get(k, ())):
            visit(t)
        state[k] = 2
        order.append(k)
    for k in sorted(docs):
        visit(k)
    return [docs[k] for k in order]


def load_seed(client, seed_dir, log=None, passes=1):
    ok = fail = 0
    ordered = seed_order(seed_dir)
    for p in range(passes):
        ok, fail = _load_once(client, ordered, log if p == passes - 1 else None)
    return ok, fail


def _load_once(client, ordered, log=None):
    ok = fail = 0
    for fn, doc in ordered:
        d = doc
        st, _, body = client.send("PUT", "/%s/%s" % (d["resourceType"], d["id"]), d,
                                  {"Content-Type": "application/fhir+json",
                                   "Prefer": "return=minimal"})
        if st in (200, 201):
            ok += 1
        else:
            fail += 1
            if log is not None:
                log.append((fn, st, body[:200].decode("utf-8", "replace")))
    return ok, fail


VID_RE = re.compile(r"@VID:([A-Za-z0-9_.\-]+)")
ETAG_RE = re.compile(r"@ETAG:([A-Za-z0-9_.\-]+)")


def _subst(text, vids, etags):
    """Resolve the opaque-version placeholders a case may carry."""
    if not isinstance(text, str):
        return text
    text = VID_RE.sub(lambda m: vids.get(m.group(1), "MISSING"), text)
    text = ETAG_RE.sub(lambda m: etags.get(m.group(1), "MISSING"), text)
    return text


def run(client, cases, base, follow_state=None):
    """-> {case_id: normalised}.  `follow_state` carries 'next' links between cases."""
    out = {}
    links = follow_state if follow_state is not None else {}
    vids, etags = {}, {}
    for case in cases:
        url = case["url"]
        if url.startswith("@FOLLOW:"):
            src = url[len("@FOLLOW:"):]
            nxt = links.get(src)
            if not nxt:
                out[case["id"]] = {"status": 0, "error": "no next link recorded for " + src}
                continue
            url = nxt[len(base):] if nxt.startswith(base) else nxt
        url = _subst(url, vids, etags)
        hdrs_in = {k: _subst(v, vids, etags) for k, v in (case.get("headers") or {}).items()}
        st, hdrs, body = client.send(case["method"], url, case.get("body"), hdrs_in)
        raw_etag = (hdrs.get("ETag") or hdrs.get("etag") or "").strip()
        if raw_etag:
            etags[case["id"]] = raw_etag
            m = re.match(r'^W/"(.+)"$', raw_etag)
            if m:
                vids[case["id"]] = m.group(1)
        out[case["id"]] = normalise(st, hdrs, body, base, case)
        try:
            doc = json.loads(body.decode("utf-8"))
            if isinstance(doc, dict) and doc.get("resourceType") == "Bundle":
                for l in doc.get("link", []) or []:
                    if l.get("relation") == "next":
                        links[case["id"]] = l.get("url")
        except Exception:
            pass
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--seed-dir", required=True)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip-seed", action="store_true")
    args = ap.parse_args()

    cases = json.load(open(args.corpus))
    client = Client(args.base)
    t0 = time.time()
    if not args.skip_seed:
        log = []
        ok, fail = load_seed(client, args.seed_dir, log)
        print("seed loaded: %d ok, %d failed (%.1fs)" % (ok, fail, time.time() - t0), file=sys.stderr)
        for row in log[:15]:
            print("  seed failure:", row, file=sys.stderr)
        if log:
            json.dump(log, open(args.out + ".seedfail.json", "w"), indent=1)
    links = {}
    b = [c for c in cases if c.get("phase", "B") == "B"]
    c_ = [c for c in cases if c.get("phase", "B") == "C"]
    t1 = time.time()
    res = run(client, b, args.base, links)
    print("phase B: %d cases in %.1fs" % (len(b), time.time() - t1), file=sys.stderr)
    t2 = time.time()
    res.update(run(client, c_, args.base, links))
    print("phase C: %d cases in %.1fs" % (len(c_), time.time() - t2), file=sys.stderr)
    json.dump(res, open(args.out, "w"), indent=1, sort_keys=True)
    print("total %.1fs -> %s" % (time.time() - t0, args.out), file=sys.stderr)


if __name__ == "__main__":
    main()
