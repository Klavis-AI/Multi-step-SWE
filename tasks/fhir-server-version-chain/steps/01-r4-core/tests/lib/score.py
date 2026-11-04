#!/usr/bin/env python3
"""Rung 1 scorer.  Prints SCORE=<0..1> as its last line and never raises."""
import json, os, random, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness as H
# Validator input and outcome live where only the scorer can write: the
# candidate's server runs beside it and shares /tmp.
SCRATCH = H.scratch_dir("fhir-score")
OO_PATH = os.path.join(SCRATCH, "output-oo.json")
import fhirhttp
from fhirhttp import Client
import runner, compare, valscore

# Author mode only: inside the shipped verifier the corpora are at
# /tests/data and nothing in the environment can point them elsewhere.
DATA = (os.environ.get("FHIRCHAIN_DATA") if H.AUTHOR_MODE else None) \
    or "/tests/data"
ROOT = (os.environ.get("FHIRCHAIN_ROOT") if H.AUTHOR_MODE else None) \
    or H.BASE_URL
BASE = ROOT + "/baseR4"

WEIGHTS = {"rest": 0.45, "validate": 0.25, "output": 0.13, "capability": 0.10, "persist": 0.07}
EXPECTED = tuple(sorted(WEIGHTS))

# The absolute deadline for the component now running; every case loop in the
# shared library honours it, so a candidate that is merely slow in one
# component cannot starve the others (fleet advisory 18 13).
DEADLINE = None
assert abs(sum(WEIGHTS.values()) - 1.0) < 1e-9, "component weights must sum to 1"


def pct(a, b):
    return (a / b) if b else 0.0


# --------------------------------------------------------------------------- #

def component_rest(client):
    cases = json.load(open(os.path.join(DATA, "hidden-corpus.json")))
    expected = json.load(open(os.path.join(DATA, "hidden-expected.json")))
    links = {}
    b = [c for c in cases if c.get("phase", "B") == "B"]
    c_ = [c for c in cases if c.get("phase", "B") == "C"]
    actual = runner.run(client, b, BASE, links)
    actual.update(runner.run(client, c_, BASE, links))
    npass, total, per = compare.score(cases, expected, actual)
    groups = {}
    for cid, r in per.items():
        g = groups.setdefault(r["group"], [0, 0])
        g[1] += 1
        g[0] += 1 if r["pass"] else 0
    for k in sorted(groups):
        H.log("  REST %-20s %4d/%-4d" % (k, groups[k][0], groups[k][1]))
    fails = [cid for cid, r in per.items() if not r["pass"]]
    for cid in fails[:40]:
        H.log("  REST miss %s %s exp=%s got=%s" % (cid, per[cid]["failed_fields"],
              json.dumps(per[cid]["expected"])[:140], json.dumps(per[cid]["actual"])[:140]))
    H.log("REST: %d/%d" % (npass, total))
    return pct(npass, total)


def component_validate(clean):
    ids = {}
    for c in clean:
        ids.setdefault(c["type"], c["id"])
    v, n, p, ni, rows = valscore.run(BASE, os.path.join(DATA, "valcases"),
                                     os.path.join(DATA, "valexpected.json"),
                                     instance_ids=ids)
    # Balanced across the two classes, so that answering "invalid" to everything
    # -- which the corpus's shape would otherwise reward, most cases being
    # mutants -- scores 0.5 on the verdict rather than 0.9.
    cls = {True: [0, 0], False: [0, 0]}
    for r in rows.values():
        cls[r["expected_invalid"]][1] += 1
        cls[r["expected_invalid"]][0] += 1 if r["verdict_ok"] else 0
    bal = sum(pct(a, b) for a, b in cls.values() if b) / sum(1 for a, b in cls.values() if b)
    H.log("VALIDATE: verdict %d/%d (valid %d/%d, invalid %d/%d, balanced %.3f), located %d/%d"
          % (v, n, cls[False][0], cls[False][1], cls[True][0], cls[True][1], bal, p, ni))
    miss = {}
    for cid, r in rows.items():
        if not r["verdict_ok"]:
            miss[r["mutation"]] = miss.get(r["mutation"], 0) + 1
    if miss:
        H.log("  verdict misses by mutation: %s" % sorted(miss.items(), key=lambda x: -x[1]))
    return 0.8 * bal + 0.2 * pct(p, ni)


OUTPUT_REQUESTS = [
    ("GET", "/metadata", None, {}),
    ("GET", "/metadata?_format=xml", None, {}),
    ("GET", "/Patient?_summary=count", None, {}),
    ("GET", "/Patient/does-not-exist-xyz", None, {}),
]


def _local_refs(node, out):
    if isinstance(node, list):
        for x in node:
            _local_refs(x, out)
    elif isinstance(node, dict):
        for k, v in node.items():
            if k == "reference" and isinstance(v, str) and "/" in v and not v.startswith("#"):
                seg = v.split("/")
                out.add("%s/%s" % (seg[-2], seg[-1]))
            else:
                _local_refs(v, out)


def component_output(client, clean):
    """Validate what the server returns with the official HL7 validator.

    Only instances the validator already reports clean are read back, so a
    finding here is about the server's own output, never about the sample data.
    """
    outdir = os.path.join(SCRATCH, "candidate-output")
    subprocess.run(["rm", "-rf", outdir], check=False)
    os.makedirs(outdir, exist_ok=True)
    clean_keys = {"%s/%s" % (c["type"], c["id"]) for c in clean}
    by_type = {}
    for c in clean:
        by_type.setdefault(c["type"], []).append(c["id"])

    # instances whose references all point at other clean instances: their
    # _include bundles are clean too
    self_contained = []
    for c in clean:
        try:
            doc = json.load(open(os.path.join(DATA, "seed", "%s.%s.json" % (c["type"], c["id"]))))
        except Exception:
            continue
        refs = set()
        _local_refs(doc, refs)
        if refs and refs <= clean_keys:
            self_contained.append(c)

    reqs = list(OUTPUT_REQUESTS)
    rnd = random.Random(4242)
    picks = list(clean)
    rnd.shuffle(picks)
    for c in picks:
        reqs.append(("GET", "/%s/%s" % (c["type"], c["id"]), None, {}))
    for c in picks[:40]:
        reqs.append(("GET", "/%s/%s" % (c["type"], c["id"]), None, {"Accept": "application/fhir+xml"}))
    for t in sorted(by_type):
        ids = ",".join(by_type[t][:8])
        reqs.append(("GET", "/%s?_id=%s&_count=20" % (t, ids), None, {}))
    for c in self_contained[:12]:
        reqs.append(("GET", "/%s?_id=%s&_include=*&_count=20" % (c["type"], c["id"]), None, {}))
    for c in picks[:12]:
        reqs.append(("GET", "/%s/%s/_history" % (c["type"], c["id"]), None, {}))
    tx = {"resourceType": "Bundle", "type": "transaction", "entry": [
        {"fullUrl": "urn:uuid:33333333-3333-4333-8333-333333333333",
         "resource": {"resourceType": "Organization", "name": "Output Check Org"},
         "request": {"method": "POST", "url": "Organization"}}]}
    reqs.append(("POST", "/", tx, {"Content-Type": "application/fhir+json"}))

    n = 0
    for method, url, body, hdrs in reqs:
        st, h, b = client.send(method, url, body, hdrs)
        if not b:
            continue
        ct = (h.get("Content-Type") or h.get("content-type") or "").lower()
        ext = "xml" if "xml" in ct else "json"
        if ext == "json":
            try:
                doc = json.loads(b.decode("utf-8"))
                if not isinstance(doc, dict) or not doc.get("resourceType"):
                    continue
            except Exception:
                continue
        with open(os.path.join(outdir, "out-%03d.%s" % (n, ext)), "wb") as f:
            f.write(b)
        n += 1
    if n == 0:
        H.log("OUTPUT: the server produced nothing to validate")
        return 0.0
    t0 = time.time()
    # A stale outcome from an earlier invocation must never be read as this
    # one's.
    try:
        os.unlink(OO_PATH)
    except OSError:
        pass
    # 6 GiB of heap and 16 GiB of address space: the JVM reserves far more
    # address space than it commits, and 16 is well below the container's 24, so
    # a runaway dies alone instead of taking the pod.
    rc, vout, verr = H.run_bounded(
        ["java", "-Xmx6g", "-XX:MaxMetaspaceSize=512m", "-XX:+ExitOnOutOfMemoryError",
         "-jar", (os.environ.get("FHIRCHAIN_VALIDATOR") if H.AUTHOR_MODE else None)
         or "/opt/validator_cli.jar",
         outdir, "-version", "4.0.1", "-tx", "n/a", "-output", OO_PATH],
        H.capped(1200), 16 * 1024 * 1024 * 1024)
    if rc is None:
        H.log("OUTPUT: the official validator timed out; scoring what it wrote")

    class _P:
        pass
    p = _P()
    p.returncode, p.stdout, p.stderr = rc, vout, verr
    H.log("OUTPUT: validator exit %s in %.0fs over %d files"
          % (p.returncode if p.returncode is not None else "timeout", time.time() - t0, n))
    try:
        doc = json.load(open(OO_PATH))
    except Exception:
        H.log("OUTPUT: no validator output\n" + (p.stdout or "")[-2500:])
        return 0.0
    ok = 0
    seen = 0
    for e in doc.get("entry", []):
        oo = e.get("resource", {})
        f = None
        for x in oo.get("extension", []):
            if x.get("url", "").endswith("operationoutcome-file"):
                f = x.get("valueString")
        errs = [i for i in oo.get("issue", []) if i.get("severity") in ("error", "fatal")]
        seen += 1
        if not errs:
            ok += 1
        elif seen <= 400:
            H.log("  OUTPUT invalid %s: %s" % (os.path.basename(f or "?"),
                  "; ".join((i.get("details") or {}).get("text", "")[:110] for i in errs[:2])))
    H.log("OUTPUT: %d/%d responses validate clean" % (ok, seen))
    return pct(ok, seen)


def seeded_types():
    """Every resource type the sample dataset contains: the instruction requires
    the CapabilityStatement to declare the types the server serves, and these are
    demonstrably served."""
    out = set()
    d = os.path.join(DATA, "seed")
    for fn in os.listdir(d):
        if fn.endswith(".json") and not fn.startswith("_"):
            out.add(fn.split(".", 1)[0])
    return sorted(out)


def component_capability(client):
    checks = []
    st, h, b = client.send("GET", "/metadata")
    try:
        cs = json.loads(b.decode("utf-8"))
    except Exception:
        cs = {}
    checks.append(("status 200", st == 200))
    checks.append(("resourceType CapabilityStatement", cs.get("resourceType") == "CapabilityStatement"))
    checks.append(("fhirVersion 4.0.1", cs.get("fhirVersion") == "4.0.1"))
    checks.append(("status is a code", isinstance(cs.get("status"), str) and cs.get("status") != ""))
    checks.append(("kind is a code", isinstance(cs.get("kind"), str) and cs.get("kind") != ""))
    fmts = cs.get("format") or []
    checks.append(("format lists JSON", any("json" in str(f) for f in fmts)))
    checks.append(("format lists XML", any("xml" in str(f) for f in fmts)))
    rest = (cs.get("rest") or [{}])[0]
    checks.append(("rest.mode server", rest.get("mode") == "server"))
    resources = rest.get("resource") or []
    declared = {r.get("type"): r for r in resources if isinstance(r, dict)}
    for t in seeded_types():
        checks.append(("declares %s" % t, t in declared))
    sysints = {i.get("code") for i in (rest.get("interaction") or []) if isinstance(i, dict)}
    checks.append(("declares transaction", "transaction" in sysints))
    checks.append(("declares batch", "batch" in sysints))

    # Every declared search parameter must actually be accepted by search, and a
    # parameter that is not declared must be refused.
    rnd = random.Random(99)
    pairs = []
    for t, r in declared.items():
        for sp in (r.get("searchParam") or []):
            if isinstance(sp, dict) and sp.get("name") and not sp["name"].startswith("_"):
                pairs.append((t, sp["name"]))
    rnd.shuffle(pairs)
    tried = pairs[:30]
    good = 0
    for t, name in tried:
        # `:missing=false` is a legal value for every parameter type, so a 200
        # here means the parameter is accepted, not that a value happened to parse
        st2, _, _ = client.send("GET", "/%s?%s:missing=false&_count=1" % (t, name))
        if st2 == 200:
            good += 1
    checks.append(("declared search parameters accepted (%d/%d)" % (good, len(tried)),
                   len(tried) > 0 and good >= 0.95 * len(tried)))
    st3, _, _ = client.send("GET", "/Patient?definitely-not-a-parameter=1")
    checks.append(("undeclared parameter refused", st3 == 400))
    # the declared metadata itself must be right, not just the names
    typed = 0
    defined = 0
    sample = pairs[:40]
    for t, name in sample:
        sp = next((x for x in (declared.get(t, {}).get("searchParam") or [])
                   if x.get("name") == name), {})
        if sp.get("type") in ("number", "date", "string", "token", "reference",
                              "composite", "quantity", "uri", "special"):
            typed += 1
        if isinstance(sp.get("definition"), str) and sp["definition"].startswith("http"):
            defined += 1
    checks.append(("declared search parameters carry a type (%d/%d)" % (typed, len(sample)),
                   len(sample) > 0 and typed == len(sample)))
    checks.append(("declared search parameters carry a definition (%d/%d)" % (defined, len(sample)),
                   len(sample) > 0 and defined == len(sample)))

    for name, ok in checks:
        if not ok:
            H.log("  CAPABILITY miss: %s" % name)
    n = sum(1 for _, ok in checks if ok)
    H.log("CAPABILITY: %d/%d" % (n, len(checks)))
    return pct(n, len(checks))


def component_persist(proc, client, clean):
    if proc is None:
        # Author mode: the server was started outside this process, so it cannot
        # be restarted here.  The checks still run against the live server, and
        # the real restart happens in the dry run and on the cluster.
        H.log("PERSIST: author mode -- the restart is skipped, the reads are not")
    """State written before a restart must come back, and all three configured
    directories must be in use."""
    probe = {"resourceType": "Organization", "id": "persist-probe-1",
             "name": "Persistence Probe", "active": True}
    st, _, _ = client.send("PUT", "/Organization/persist-probe-1", probe,
                           {"Content-Type": "application/fhir+json"})
    st2, _, _ = client.send("PUT", "/Organization/persist-probe-1",
                            dict(probe, name="Persistence Probe v2"),
                            {"Content-Type": "application/fhir+json"})
    if proc is not None:
        H.stop_server(proc)
        time.sleep(2)
    # In author mode the server was started elsewhere, with its own data
    # directories, so this check is about the harness rather than the candidate.
    dirs_used = (True if proc is None else
                 all(os.path.isdir(os.path.join(H.DATA, d)) and
                     os.listdir(os.path.join(H.DATA, d))
                     for d in ("index", "transaction", "resource")))
    if proc is None:
        proc2, ok = None, True          # author mode: the live server stays up
    else:
        proc2, ok = H.start_server()
    checks = [("restarts", ok), ("all three data directories used", dirs_used)]
    if ok:
        st3, _, b3 = client.send("GET", "/Organization/persist-probe-1")
        try:
            doc = json.loads(b3.decode("utf-8"))
        except Exception:
            doc = {}
        checks.append(("resource survives the restart", st3 == 200 and doc.get("name") == "Persistence Probe v2"))
        checks.append(("version survives the restart", (doc.get("meta") or {}).get("versionId") == "2"))
        st4, _, b4 = client.send("GET", "/Organization/persist-probe-1/_history")
        try:
            hist = json.loads(b4.decode("utf-8"))
        except Exception:
            hist = {}
        checks.append(("history survives the restart", hist.get("total") == 2 or len(hist.get("entry") or []) == 2))
        sample = [c for c in clean if c["type"] == "Patient"][:3]
        surv = 0
        for c in sample:
            s, _, _ = client.send("GET", "/%s/%s" % (c["type"], c["id"]))
            surv += 1 if s == 200 else 0
        checks.append(("seeded resources survive the restart (%d/%d)" % (surv, len(sample)),
                       len(sample) > 0 and surv == len(sample)))
    for name, ok2 in checks:
        if not ok2:
            H.log("  PERSIST miss: %s" % name)
    n = sum(1 for _, o in checks if o)
    H.log("PERSIST: %d/%d" % (n, len(checks)))
    return pct(n, len(checks)), proc2 if ok else None


# --------------------------------------------------------------------------- #

LAST_PROC = None


def score_running_server(proc=None):
    """Everything after the server is up, so the same code can be pointed at
    an already-running server while the task is being authored."""
    global LAST_PROC
    client = Client(BASE, timeout=120)
    clean = json.load(open(os.path.join(DATA, "cleanseed.json")))["clean"]
    t0 = time.time()
    # The seed load gets a share of the budget like a component would: it is
    # the first thing that talks to the candidate, so a slow server spends
    # the whole step here otherwise.
    global DEADLINE
    DEADLINE = H.component_deadline(len(EXPECTED) + 1)
    fhirhttp.set_deadline(DEADLINE)
    ok, fail = runner.load_seed(client, os.path.join(DATA, "seed"), deadline=DEADLINE)
    H.log("SEED: %d written, %d refused (%.0fs)" % (ok, fail, time.time() - t0))

    comps = {}

    def one(name, fn):
        """Run a component fail-closed.  A component that raises scores 0 for
        its own weight and leaves the others alone; a component that is
        simply absent from `comps` would otherwise drop out of the sum and
        hand its weight to the components that did run."""
        global DEADLINE
        DEADLINE = H.component_deadline(len(EXPECTED) - len(comps))
        fhirhttp.set_deadline(DEADLINE)
        t = time.time()
        try:
            comps[name] = fn()
        except Exception:
            H.log("COMPONENT %s raised; scoring it 0:\n%s"
                  % (name, traceback.format_exc()))
            comps[name] = 0.0
        H.log("  (%s %.0fs; %.0fs of the step budget left)"
              % (name, time.time() - t, H.budget_left()))

    one("rest", lambda: component_rest(client))
    if not H.process_identity_ok():
        return 0.0
    one("validate", lambda: component_validate(clean))
    if not H.process_identity_ok():
        return 0.0
    one("capability", lambda: component_capability(client))
    one("output", lambda: component_output(client, clean))
    if not H.process_identity_ok():
        return 0.0

    def _persist():
        nonlocal proc
        v, proc = component_persist(proc, client, clean)
        return v
    one("persist", _persist)

    # Every weighted component must have produced a number.  Naming them is
    # what keeps a component that never ran from silently handing its weight
    # to the ones that did (fleet advisory 17 part 5 / the rung-2 finding).
    for name in EXPECTED:
        if name not in comps:
            H.log("COMPONENT %s produced no result at all -- treating it as 0"
                  % name)
            comps[name] = 0.0
    score = sum(WEIGHTS[k] * comps[k] for k in WEIGHTS)
    H.log("COMPONENTS: " + json.dumps({k: round(comps[k], 4) for k in sorted(comps)}))
    H.log("WEIGHTS:    " + json.dumps(WEIGHTS))
    LAST_PROC = proc
    return score


def main():
    score = 0.0
    proc = None
    try:
        if not os.path.isdir(H.APP):
            H.log("GATE: /app is missing")
            return 0.0
        foreign = H.foreign_sources()
        if foreign:
            H.log("GATE: the tree contains sources in another compiled language: %s"
                  % ", ".join(foreign[:10]))
            return 0.0
        # Fleet advisory 12: the candidate runs in this container, beside the
        # expectations.  Assert -- as the uid it will run as -- that it cannot
        # read them, rather than trusting the image's chmod to have worked.
        probes = [os.path.join(DATA, f) for f in
                  ("hidden-expected.json", "valexpected.json", "cleanseed.json")]
        ok, readable = H.expectations_unreachable(probes)
        if ok is None:
            H.log("GUARD: not running as root, so the candidate cannot be confined "
                  "(author mode)")
        elif ok:
            H.log("GUARD: the candidate's uid cannot read any of %d expectation files"
                  % len(probes))
        else:
            H.log("GUARD FAILED: the candidate's uid can still read %d expectation "
                  "file(s): %s" % (len(readable), ", ".join(readable[:4])))
            return 0.0
        removed = H.scrub_prebuilt()
        H.log("GATE: removed %d pre-built files before building" % removed)
        if not H.build(H.capped(900)):
            return 0.0
        # The candidate is confined; assert it can still use its own workspace,
        # as that uid, before blaming it for not starting.
        wok, wbad = H.workspace_usable(
            ["/app/src", "/root/.fhir/packages"],
            [os.path.join(H.DATA, x) for x in ("index", "transaction", "resource")])
        if wok is None:
            H.log("GUARD: workspace probe skipped (author mode)")
        elif wok:
            H.log("GUARD: the candidate's uid can execute its binary, read what the "
                  "instruction names, and write its data directories")
        else:
            H.log("GUARD FAILED: the confinement broke the candidate's own workspace: "
                  "%s" % "; ".join(wbad[:4]))
            return 0.0
        if not os.path.isfile(H.BIN):
            H.log("GATE: %s was not produced by the build" % H.BIN)
            return 0.0
        subprocess.run(["rm", "-rf", H.DATA], check=False)
        proc, up = H.start_server()
        if not up:
            H.log("GATE: the server did not answer GET /baseR4/metadata")
            H.log(open("/tmp/server-%d.log" % H.PORT, errors="replace").read()[-3000:])
            return 0.0
        if not H.process_identity_ok():
            return 0.0

        score = score_running_server(proc)
        proc = LAST_PROC
    except Exception:
        H.log("VERIFIER EXCEPTION:\n" + traceback.format_exc())
    finally:
        H.stop_server(proc)
    return score


# Belt and braces with test.sh: the scorer may be run directly.
os.umask(0o022)

if __name__ == "__main__":
    s = main()
    print("SCORE=%.6f" % max(0.0, min(1.0, s)), flush=True)
