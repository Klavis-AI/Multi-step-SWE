#!/usr/bin/env python3
"""Rung 5 scorer: Bulk Data export, SMART on FHIR scopes and `$everything`,
beside everything the earlier milestones asked for.  Prints SCORE=<0..1> as its
last line and never raises."""
import json, os, random, shutil, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness as H
# Validator input and outcome live where only the scorer can write: the
# candidate's server runs beside it and shares /tmp.
SCRATCH = H.scratch_dir("fhir-score")
import fhirhttp
from fhirhttp import Client
import runner, compare, valscore, fmlscore, profscore, termscore, subscore, bulkscore

# The two overrides exist so the same code can be pointed at an already-running
# server while the task is being authored; inside the verifier neither is set.
# Author mode only, like ROOT: inside the shipped verifier the corpora are
# at /tests/data and nothing in the environment can point them elsewhere.
DATA = (os.environ.get("FHIRCHAIN_DATA") if H.AUTHOR_MODE else None) \
    or "/tests/data"
# The author-mode override points the scorer at a server started outside
# the harness.  It is readable only when this module is imported from the
# task tree; inside the shipped verifier H.AUTHOR_MODE is False and the
# environment cannot redirect the run.
ROOT = (os.environ.get("FHIRCHAIN_ROOT") if H.AUTHOR_MODE else None) \
    or H.BASE_URL
VALIDATOR = (os.environ.get("FHIRCHAIN_VALIDATOR") if H.AUTHOR_MODE else None) \
    or "/opt/validator_cli.jar"

# The FHIR REST surface is very nearly version-invariant: read, vread, history,
# the Bundle envelope, conditional interactions, the search grammar and the shape
# of the conformance resource are the same from DSTU2 to R6, because a release
# changes the resource definitions rather than the protocol.  So "this case was
# sent to /baseDstu3" is not evidence that the case is about STU3, and grouping
# by which corpus a case came from would let the carry-forward pay for the
# milestone.
#
# Every scored unit therefore carries a ROLE, decided at authoring time by
# measurement: the whole scored set was replayed against the PREVIOUS rung's
# reference plus the cheapest change that makes this rung's new surface answer at
# all -- for this rung, the two lines that add R4B and R5 to the version table.
# A unit whose answer MOVED is a milestone unit; a unit whose answer was
# identical is carried forward.  Measuring against the previous reference alone
# would let two lines of version table earn the whole milestone weight.
# Roles are mixed across components; components are not weighted against each
# other except to keep one area from drowning the others.
ROLE_WEIGHTS = H.ROLE_WEIGHTS
COMPONENT_WEIGHTS = {"bulk": 0.34, "subscription": 0.10, "r4b": 0.07, "r5": 0.07,
                     "valdiff45": 0.08, "output": 0.05, "profile": 0.05,
                     "terminology": 0.05, "capability": 0.04, "derived": 0.04,
                     "dstu2": 0.04, "stu3": 0.04, "r4": 0.03}
EXPECTED = tuple(sorted(COMPONENT_WEIGHTS))

VERSIONS = [("dstu2", "baseDstu2"), ("stu3", "baseDstu3")]
NEWVER = [("r4b", "baseR4B"), ("r5", "baseR5")]

ROLES = {}

# The absolute deadline for the component now running; every case loop in the
# shared library honours it.
DEADLINE = None


def role_of(comp, cid):
    """A unit with no recorded role counts as carried forward, never as
    milestone, so that an omission can only lower the score.  The key is the
    component and the unit's prefix together, because the generated corpora
    share their case numbering across releases."""
    return ROLES.get("%s|%s" % (comp, cid.split("/")[0]), "regression")


def pct(a, b):
    return (a / b) if b else 0.0


def aggregate(results):
    return H.aggregate(results, COMPONENT_WEIGHTS, EXPECTED, role_of, H.log)


def run_corpus(tag, base_path, label):
    """Load that version's dataset and replay its held-out corpus."""
    base = "%s/%s" % (ROOT, base_path)
    client = Client(base, timeout=120)
    t0 = time.time()
    ok, fail = runner.load_seed(client, os.path.join(DATA, "seed-" + tag), deadline=DEADLINE)
    H.log("SEED %s: %d written, %d refused (%.0fs)" % (tag, ok, fail, time.time() - t0))
    cases = json.load(open(os.path.join(DATA, "%s-corpus.json" % tag)))
    expected = json.load(open(os.path.join(DATA, "%s-expected.json" % tag)))
    links = {}
    b = [c for c in cases if c.get("phase", "B") == "B"]
    c_ = [c for c in cases if c.get("phase", "B") == "C"]
    actual = runner.run(client, b, base, links, deadline=DEADLINE)
    actual.update(runner.run(client, c_, base, links, deadline=DEADLINE))
    npass, total, per = compare.score(cases, expected, actual)
    groups = {}
    for cid, r in per.items():
        g = groups.setdefault(r["group"], [0, 0])
        g[1] += 1
        g[0] += 1 if r["pass"] else 0
    for k in sorted(groups):
        H.log("  %s %-20s %4d/%-4d" % (label, k, groups[k][0], groups[k][1]))
    fails = [cid for cid, r in per.items() if not r["pass"]]
    for cid in fails[:30]:
        H.log("  %s miss %s %s exp=%s got=%s" % (label, cid, per[cid]["failed_fields"],
              json.dumps(per[cid]["expected"])[:120], json.dumps(per[cid]["actual"])[:120]))
    H.log("%s: %d/%d" % (label, npass, total))
    return {cid: (bool(r["pass"]), 1.0) for cid, r in per.items()}


def component_cross():
    """Cases that span service bases: expectations come from the published
    packages and from the official validator, computed at authoring time."""
    cases = json.load(open(os.path.join(DATA, "cross-corpus.json")))
    expected = json.load(open(os.path.join(DATA, "cross-expected.json")))
    client = Client(ROOT, timeout=120)
    links = {}
    b = [c for c in cases if c.get("phase", "B") == "B"]
    c_ = [c for c in cases if c.get("phase", "B") == "C"]
    actual = runner.run(client, b, ROOT, links, deadline=DEADLINE)
    actual.update(runner.run(client, c_, ROOT, links, deadline=DEADLINE))
    npass, total, per = compare.score(cases, expected, actual)
    for cid, r in sorted(per.items()):
        if not r["pass"]:
            H.log("  CROSS miss %-26s %s exp=%s got=%s" % (cid, r["failed_fields"],
                  json.dumps(r["expected"])[:110], json.dumps(r["actual"])[:110]))
    H.log("CROSS: %d/%d" % (npass, total))
    return {cid: (bool(r["pass"]), 1.0) for cid, r in per.items()}


def component_r4_regression():
    """The earlier milestone's behaviour has to keep working: a fixed stratified
    sample of its held-out corpus, replayed against /baseR4."""
    base = "%s/baseR4" % ROOT
    client = Client(base, timeout=120)
    ok, fail = runner.load_seed(client, os.path.join(DATA, "seed-r4"), deadline=DEADLINE)
    H.log("SEED r4: %d written, %d refused" % (ok, fail))
    cases = json.load(open(os.path.join(DATA, "r4-corpus.json")))
    expected = json.load(open(os.path.join(DATA, "r4-expected.json")))
    links = {}
    b = [c for c in cases if c.get("phase", "B") == "B"]
    c_ = [c for c in cases if c.get("phase", "B") == "C"]
    actual = runner.run(client, b, base, links, deadline=DEADLINE)
    actual.update(runner.run(client, c_, base, links, deadline=DEADLINE))
    npass, total, per = compare.score(cases, expected, actual)
    for cid, r in list(per.items())[:200]:
        if not r["pass"]:
            H.log("  R4 miss %s %s" % (cid, r["failed_fields"]))
    H.log("R4 REGRESSION: %d/%d" % (npass, total))
    return {cid: (bool(r["pass"]), 1.0) for cid, r in per.items()}


def component_validate():
    """-> (verdict units, located units).

    Most cases in a mutation corpus are invalid, so a plain pass rate pays a
    server that answers "invalid" to everything about 0.9.  Each case is
    weighted by the inverse frequency of its own class instead, which makes the
    two classes count equally and that strategy worth 0.5.
    """
    verdicts, located = {}, {}
    for tag, base_path in VERSIONS:
        d = os.path.join(DATA, "valcases-" + tag)
        e = os.path.join(DATA, "valexpected-%s.json" % tag)
        if not os.path.isdir(d):
            continue
        v, n, p, ni, rows = valscore.run("%s/%s" % (ROOT, base_path), d, e, deadline=DEADLINE)
        n_inv = sum(1 for r in rows.values() if r["expected_invalid"])
        n_val = len(rows) - n_inv
        H.log("VALIDATE %s: verdict %d/%d (valid %d, invalid %d), located %d/%d"
              % (tag, v, n, n_val, n_inv, p, ni))
        for cid, r in rows.items():
            w = (1.0 / n_inv if r["expected_invalid"] else 1.0 / n_val) if (n_inv and n_val) else 1.0
            verdicts[cid] = (bool(r["verdict_ok"]), w)
            if r["path_ok"] is not None:
                located[cid + "/loc"] = (bool(r["path_ok"]), 1.0)
    return verdicts, located


def component_bulk():
    """Bulk Data export, SMART scopes and `$everything`.

    Every expected answer is computed here from the published
    `CompartmentDefinition`, the seed this component writes, and the SMART scope
    grammar -- none of it is another server's recorded behaviour."""
    units = bulkscore.run(ROOT, DATA, log=H.log, deadline=DEADLINE)
    return units


def component_subscription():
    """Topic-based subscriptions at both new bases.

    Every scenario contains writes that must notify AND writes that must not, so
    neither a server that notifies on everything nor one that notifies on
    nothing earns a scenario.  The receiver is hosted here, so delivery is
    measured rather than assumed."""
    units = subscore.run(ROOT, os.path.join(DATA, "sub-corpus.json"),
                         log=H.log, deadline=DEADLINE)
    H.log("SUBSCRIPTIONS: %d/%d"
          % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


def component_valdiff45():
    """The same two-sided differential as the earlier rung, for the releases this
    one adds: instances built from what R4 and R4B/R5 disagree about, validated
    at the new base AND at `/baseR4`, with both verdicts required."""
    units = {}
    for tag, base_path in NEWVER:
        d = os.path.join(DATA, "valdiff-" + tag)
        e = os.path.join(DATA, "valdiffexpected-%s.json" % tag)
        if not os.path.isdir(d) or not os.path.exists(e):
            continue
        v, n, p, ni, rows = valscore.run("%s/%s" % (ROOT, base_path), d, e,
                                         deadline=DEADLINE)
        v4, n4, _, _, rows4 = valscore.run("%s/baseR4" % ROOT, d, e,
                                           field="invalid_at_r4", deadline=DEADLINE)
        both = 0
        for cid, r in rows.items():
            ok = bool(r["verdict_ok"]) and bool(rows4.get(cid, {}).get("verdict_ok"))
            both += 1 if ok else 0
            # Keyed by family: the releases share case ids, and a bare id let
            # the later family overwrite the earlier one's unit.
            units["%s:%s" % (tag, cid)] = (ok, 1.0)
        H.log("VALDIFF45 %s: %d/%d at %s, %d/%d at baseR4, both %d/%d"
              % (tag, v, n, base_path, v4, n4, both, len(rows)))
    return units


def component_derived():
    """Checks whose questions and answers are both computed here, at scoring
    time, from the dataset that was just loaded -- never shipped.

    Every other component compares against a recorded answer, and a recorded
    answer is a value that can be looked up rather than computed.  These cannot
    be: the ids are drawn from the seed in this process, the requests are built
    from them, and what counts as correct is derived from the same instances.
    A server that answered by replaying stored expectations has nothing to
    replay here.
    """
    units = {}
    for tag, base_path in VERSIONS + [("r4", "baseR4")]:
        seed_dir = os.path.join(DATA, "seed-" + tag)
        if not os.path.isdir(seed_dir):
            continue
        client = Client("%s/%s" % (ROOT, base_path), timeout=120)
        # derived: the seed may not be loaded yet -- a base whose corpus
        # component is not weighted at this rung never loads it, and these
        # questions are about the data, so they load it themselves.  A second
        # load of the same instances is a no-op.
        runner.load_seed(client, seed_dir, deadline=DEADLINE)
        by_type = {}
        for fn in sorted(os.listdir(seed_dir)):
            if not fn.endswith(".json") or fn.startswith("_"):
                continue
            try:
                d = json.load(open(os.path.join(seed_dir, fn), encoding="utf-8-sig"))
            except Exception:
                continue
            if d.get("resourceType") and d.get("id"):
                by_type.setdefault(d["resourceType"], []).append(d)

        def unit(name, ok, why=""):
            units["drv-%s-%s" % (tag, name)] = (bool(ok), 1.0)
            if not ok:
                H.log("  DERIVED %s %s %s" % (tag, name, why))

        types = sorted(t for t, v in by_type.items() if len(v) >= 3)[:12]
        for t in types:
            docs = by_type[t]
            ids = [d["id"] for d in docs[:5]]
            st, _, b = client.send("GET", "/%s?_id=%s&_count=50" % (t, ",".join(ids)))
            got = set()
            total = None
            try:
                bundle = json.loads(b.decode("utf-8"))
                total = bundle.get("total")
                for e in bundle.get("entry") or []:
                    r = e.get("resource") or {}
                    if r.get("id"):
                        got.add(r["id"])
            except Exception:
                pass
            unit("id-set-%s" % t, st == 200 and got == set(ids),
                 "asked for %s, got %s" % (sorted(ids), sorted(got)))
            unit("id-total-%s" % t, total == len(ids),
                 "total %s for %d ids" % (total, len(ids)))
            # read each one back and require it to be the instance that was written
            one = docs[0]
            st2, _, b2 = client.send("GET", "/%s/%s" % (t, one["id"]))
            same = False
            try:
                doc = json.loads(b2.decode("utf-8"))
                same = (doc.get("resourceType") == t and doc.get("id") == one["id"]
                        and all(doc.get(k) == v for k, v in one.items()
                                if k not in ("meta", "text", "id", "resourceType")
                                and not isinstance(v, (dict, list))))
            except Exception:
                pass
            unit("read-back-%s" % t, st2 == 200 and same)
            # a page smaller than the result set must carry a next link
            # this type has at least three instances, so a page of two must be
            # full and must carry a link to the next one
            st3, _, b3 = client.send("GET", "/%s?_count=2" % t)
            ok3, why3 = False, ""
            try:
                bundle = json.loads(b3.decode("utf-8"))
                n = len(bundle.get("entry") or [])
                has_next = any(l.get("relation") == "next" for l in bundle.get("link") or [])
                ok3 = (n == 2 and has_next)
                why3 = "%d entries, next=%s" % (n, has_next)
            except Exception as e:
                why3 = str(e)
            unit("page-%s" % t, st3 == 200 and ok3, why3)
    H.log("DERIVED: %d/%d" % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


def component_profile():
    """Validation against the published profiles.  Every case here is one whose
    verdict moves between "no profile" and "with the profile", so a server that
    ignores the parameter gets none of them."""
    units, located = {}, {}
    for ig in ("uscore", "ips"):
        d = os.path.join(DATA, "profcases-" + ig)
        e = os.path.join(DATA, "profexpected-%s.json" % ig)
        if not os.path.isdir(d) or not os.path.exists(e):
            continue
        u, l = profscore.run("%s/baseR4" % ROOT, d, e, log=H.log, deadline=DEADLINE)
        units.update(u)
        located.update(l)
    H.log("PROFILE: %d/%d verdicts, %d/%d located"
          % (sum(1 for v, _ in units.values() if v), len(units),
             sum(1 for v, _ in located.values() if v), len(located)))
    return units, located


def component_terminology():
    """The five terminology operations, against the answers the published
    `CodeSystem`, `ValueSet` and `ConceptMap` resources give."""
    units = termscore.run("%s/baseR4" % ROOT, DATA, log=H.log, deadline=DEADLINE)
    H.log("TERMINOLOGY: %d/%d" % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


def component_capability():
    """What each base declares about itself, checked against that release's own
    definitions and against what the server actually does.

    The declared resource types are the sharpest part: a server that answers all
    three bases from one set of definitions declares the same types at each of
    them, and the three releases define 94, 117 and 146."""
    units = {}
    for tag, base_path, version, conf in (("dstu2", "baseDstu2", "1.0.2", "Conformance"),
                                          ("stu3", "baseDstu3", "3.0.2", "CapabilityStatement"),
                                          ("r4", "baseR4", "4.0.1", "CapabilityStatement")):
        tf = os.path.join(DATA, "types-%s.json" % tag)
        if not os.path.exists(tf):
            continue
        want_types = set(json.load(open(tf)))
        client = Client("%s/%s" % (ROOT, base_path), timeout=120)
        st, h, b = client.send("GET", "/metadata")
        try:
            cs = json.loads(b.decode("utf-8"))
        except Exception:
            cs = {}
        def unit(name, ok):
            units["cap-%s-%s" % (tag, name)] = (bool(ok), 1.0)
            if not ok:
                H.log("  CAPABILITY %s %s" % (tag, name))
        unit("status-200", st == 200)
        unit("resourceType", cs.get("resourceType") == conf)
        unit("fhirVersion", cs.get("fhirVersion") == version)
        fmts = [str(f) for f in (cs.get("format") or [])]
        unit("format-json", any("json" in f for f in fmts))
        unit("format-xml", any("xml" in f for f in fmts))
        rest = (cs.get("rest") or [{}])[0]
        unit("rest-mode-server", rest.get("mode") == "server")
        declared = {r.get("type"): r for r in (rest.get("resource") or []) if isinstance(r, dict)}
        extra = sorted(set(declared) - want_types)
        missing = sorted(want_types - set(declared))
        unit("declares-no-foreign-type", not extra)
        unit("declares-every-type", not missing)
        if extra:
            H.log("  CAPABILITY %s declares %d types this release does not define: %s"
                  % (tag, len(extra), ", ".join(extra[:8])))
        if missing:
            H.log("  CAPABILITY %s omits %d types this release defines: %s"
                  % (tag, len(missing), ", ".join(missing[:8])))
        # a declared parameter has to be accepted, and an undeclared one refused
        # `_has` takes a structured name -- `_has:Type:parameter:code` -- so
        # `:missing` is not a modifier it can carry, and it is probed by the
        # reverse-chaining cases rather than here.
        pairs = [(t, sp["name"]) for t, r in sorted(declared.items())
                 for sp in (r.get("searchParam") or [])
                 if isinstance(sp, dict) and sp.get("name") and sp["name"] != "_has"]
        rnd = random.Random(99)
        rnd.shuffle(pairs)
        good, tried = 0, pairs[:24]
        for t, name in tried:
            st2, _, _ = client.send("GET", "/%s?%s:missing=false&_count=1" % (t, name))
            good += 1 if st2 == 200 else 0
        unit("declared-parameters-accepted", tried and good >= 0.95 * len(tried))
        st3, _, _ = client.send("GET", "/Patient?definitely-not-a-parameter=1")
        unit("undeclared-parameter-refused", st3 == 400)
        # The parameter SET a release publishes for a type is its own: R4's
        # Observation has parameters STU3's does not and DSTU2 spells others
        # differently, so this is where one set of definitions serving three
        # bases shows.  Both halves come from the package: what may be declared,
        # and what must be.
        pf = os.path.join(DATA, "params-%s.json" % tag)
        if os.path.exists(pf):
            pdef = json.load(open(pf))
            allowed, required = pdef["allowed"], pdef["required"]
            rnd2 = random.Random(7)
            sample = sorted(t for t in required if t in declared)
            rnd2.shuffle(sample)
            for t in sample[:34]:
                names = {sp.get("name") for sp in (declared[t].get("searchParam") or [])
                         if isinstance(sp, dict) and sp.get("name")
                         and not str(sp.get("name")).startswith("_")}
                miss = sorted(set(required[t]) - names)
                foreign = sorted(names - set(allowed.get(t, [])))
                ok = not miss and not foreign
                units["cap-%s-params-%s" % (tag, t)] = (ok, 1.0)
                if not ok:
                    H.log("  CAPABILITY %s %s: missing %s, not defined by this release %s"
                          % (tag, t, miss[:6], foreign[:6]))
    H.log("CAPABILITY: %d/%d" % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


OUTPUT_VERSIONS = [("r4b", "baseR4B", "4.3.0"),
                   ("r5", "baseR5", "5.0.0"),
                   ("r4", "baseR4", "4.0.1")]


def component_output():
    """Validate what the server itself returns, with the official HL7 validator,
    at each base's own FHIR version.

    This is the one component whose oracle runs here rather than at authoring
    time, and it is the sharpest version check in the milestone: a server that
    answers /baseDstu3 with R4-shaped resources produces output the validator
    rejects at 3.0.2, however well it replays a recorded corpus.  Only instances
    the validator already reports clean are read back, so a finding is about the
    server's output and never about the sample data.
    """
    units = {}
    for tag, base_path, version in OUTPUT_VERSIONS:
        outdir_src = os.path.join(DATA, "outseed-" + tag)
        if not os.path.isdir(outdir_src):
            continue
        client = Client("%s/%s" % (ROOT, base_path), timeout=120)
        # These instances are the official examples that the validator reports
        # clean at this version, with the narrative removed: the examples
        # published for the older releases carry an XHTML namespace the current
        # validator rejects on every one of them, which is about the sample data
        # and not about the server.  They are written here rather than taken
        # from the seed so that nothing about this check depends on the
        # recorded corpora.
        clean = []
        for fn in sorted(os.listdir(outdir_src)):
            if not fn.endswith(".json"):
                continue
            doc = json.load(open(os.path.join(outdir_src, fn), encoding="utf-8-sig"))
            st, _, _ = client.send("PUT", "/%s/%s" % (doc["resourceType"], doc["id"]), doc,
                                   {"Content-Type": "application/fhir+json",
                                    "Prefer": "return=minimal"})
            if st in (200, 201):
                clean.append({"type": doc["resourceType"], "id": doc["id"]})
        H.log("OUTPUT %s: wrote %d validator-clean instances" % (tag, len(clean)))
        if not clean:
            continue
        outdir = os.path.join(SCRATCH, "candidate-output-" + tag)
        subprocess.run(["rm", "-rf", outdir], check=False)
        os.makedirs(outdir, exist_ok=True)
        rnd = random.Random(4242)
        picks = list(clean)
        rnd.shuffle(picks)
        reqs = [("GET", "/metadata", None, {}),
                ("GET", "/metadata?_format=xml", None, {}),
                ("GET", "/Patient/does-not-exist-xyz", None, {})]
        for c in picks[:34]:
            reqs.append(("GET", "/%s/%s" % (c["type"], c["id"]), None, {}))
        for c in picks[:14]:
            reqs.append(("GET", "/%s/%s" % (c["type"], c["id"]), None,
                         {"Accept": "application/fhir+xml" if tag != "dstu2"
                                    else "application/xml+fhir"}))
        for c in picks[:8]:
            reqs.append(("GET", "/%s?_id=%s&_count=10" % (c["type"], c["id"]), None, {}))
        for c in picks[:6]:
            reqs.append(("GET", "/%s/%s/_history" % (c["type"], c["id"]), None, {}))
        names, n = [], 0
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
            name = "out-%s-%03d.%s" % (tag, n, ext)
            open(os.path.join(outdir, name), "wb").write(b)
            names.append(name)
            n += 1
        if not n:
            H.log("OUTPUT %s: the server produced nothing to validate" % tag)
            continue
        t0 = time.time()
        oo_path = os.path.join(SCRATCH, "output-oo-%s.json" % tag)
        # A stale outcome from an earlier invocation must never be read as this
        # one's: if the validator fails, the units score 0 rather than being
        # graded against the previous run's answers.
        try:
            os.unlink(oo_path)
        except OSError:
            pass
        rc, vout, verr = H.run_bounded(
            # 4 GiB of heap and 14 GiB of address space: measured, not guessed.
            # The validator will not start under 10 GiB of address space even
            # with a 4 GiB heap -- the JVM reserves far more than it commits --
            # and 14 is still well below the container's 24, so a runaway dies
            # alone instead of taking the pod.
            ["java", "-Xmx4g", "-XX:MaxMetaspaceSize=512m",
             "-XX:+ExitOnOutOfMemoryError", "-jar", VALIDATOR,
             outdir, "-version", version, "-tx", "n/a", "-output", oo_path],
            H.capped(600), 14 * 1024 * 1024 * 1024)
        H.log("OUTPUT %s: validator exit %s in %.0fs over %d files"
              % (tag, rc, time.time() - t0, n))
        if not os.path.exists(oo_path):
            H.log("OUTPUT %s: the validator produced no outcome (rc=%s):\n%s"
                  % (tag, rc, (vout or "")[-1500:]))
            for name in names:
                units["out-" + name] = (False, 1.0)
            continue
        try:
            doc = json.load(open(oo_path))
        except Exception:
            H.log("OUTPUT %s: the validator outcome was not readable" % tag)
            continue
        outcomes = ([doc] if doc.get("resourceType") == "OperationOutcome"
                    else [e.get("resource", {}) for e in doc.get("entry", [])])
        seen = set()
        for oo in outcomes:
            src = ""
            for e in oo.get("extension", []):
                if e.get("url", "").endswith("operationoutcome-file"):
                    src = os.path.basename(e.get("valueString", ""))
            # Structural findings only.  A code that is not in a bound value set
            # is a terminology finding -- DSTU2's own media types
            # `application/json+fhir` and `application/xml+fhir` are not
            # registered IANA types and no release's value set contains them --
            # and terminology is not what this milestone asks for.
            errs = [i for i in oo.get("issue", [])
                    if i.get("severity") in ("error", "fatal")
                    and i.get("code") not in ("code-invalid", "not-found")]
            if not src:
                continue
            seen.add(src)
            units["out-" + src] = (not errs, 1.0)
            if errs:
                H.log("  OUTPUT %s invalid %s: %s" % (tag, src,
                      "; ".join((i.get("details", {}) or {}).get("text", "")[:90] for i in errs[:2])))
        for name in names:
            units.setdefault("out-" + name, (False, 1.0))
    tot = len(units)
    good = sum(1 for v, _ in units.values() if v)
    H.log("OUTPUT: %d/%d responses validate clean" % (good, tot))
    return units


def component_valdiff():
    """Instances built from what the two releases actually disagree about -- an
    element one defines and the other does not, a cardinality that changed, a
    type that changed -- kept only where the official validator's verdict really
    does differ between the versions.  A structural mutant is invalid at every
    version and says nothing about which definitions a server used; these
    cannot be answered without the right ones."""
    units = {}
    for tag, base_path in VERSIONS:
        d = os.path.join(DATA, "valdiff-" + tag)
        e = os.path.join(DATA, "valdiffexpected-%s.json" % tag)
        if not os.path.isdir(d) or not os.path.exists(e):
            continue
        v, n, p, ni, rows = valscore.run("%s/%s" % (ROOT, base_path), d, e, deadline=DEADLINE)
        # The same instances at R4, against the verdict the official validator
        # gave there.  Each case was kept BECAUSE the two verdicts differ, so a
        # unit that requires both sides cannot be answered by a server that says
        # "invalid" to everything -- which, scored at one base only, took 94.6%
        # of this component (the modal baseline in rec/modal.py).
        v4, n4, _, _, rows4 = valscore.run("%s/baseR4" % ROOT, d, e,
                                           field="invalid_at_r4", deadline=DEADLINE)
        H.log("VALDIFF %s: verdict %d/%d at %s, %d/%d at baseR4"
              % (tag, v, n, base_path, v4, n4))
        miss = [cid for cid, r in rows.items() if not r["verdict_ok"]]
        for cid in miss[:20]:
            H.log("  VALDIFF miss %s (%s) expected invalid=%s, got invalid=%s"
                  % (cid, rows[cid]["mutation"], rows[cid]["expected_invalid"],
                     rows[cid]["got_invalid"]))
        both = 0
        for cid, r in rows.items():
            ok = bool(r["verdict_ok"]) and bool(rows4.get(cid, {}).get("verdict_ok"))
            both += 1 if ok else 0
            # Keyed by family: the releases share case ids, and a bare id let
            # the later family overwrite the earlier one's unit.
            units["%s:%s" % (tag, cid)] = (ok, 1.0)
        H.log("VALDIFF %s: both sides right %d/%d" % (tag, both, len(rows)))
    return units


def component_fml():
    """The official FHIR Mapping Language test cases, both halves: the suite's
    own transforms run end to end, and every `.map` in the suite converted to a
    StructureMap and compared with the official validator's conversion."""
    ok, n, rows = fmlscore.run("%s/baseR4" % ROOT, os.path.join(DATA, "fml"), log=H.log)
    units = {"fml-" + k.replace("/", "_"): (bool(v["ok"]), 1.0) for k, v in rows.items()}
    client = Client("%s/baseR4" % ROOT, timeout=120)
    st, _, _ = client.send("POST", "/StructureMap/$transform?source=http://example.org/no-such-map",
                           {"resourceType": "Patient"})
    units["fml-unknown-map"] = (st == 404, 1.0)
    if st != 404:
        H.log("  FML unknown map -> %s, expected 404" % st)
    st, _, _ = client.send("POST", "/StructureMap", "this is not a map",
                           {"Content-Type": "text/fhir-mapping"})
    units["fml-unparsable"] = (st == 400, 1.0)
    if st != 400:
        H.log("  FML unparsable map -> %s, expected 400" % st)
    H.log("FML: %d/%d" % (sum(1 for v, _ in units.values() if v), len(units)))
    return units


def main():
    score = 0.0
    proc = None
    try:
        if not os.path.isdir(H.APP):
            H.log("GATE: /app is missing")
            return 0.0
        foreign = H.foreign_sources()
        if foreign:
            H.log("GATE: sources in another compiled language: %s" % ", ".join(foreign[:10]))
            return 0.0
        # Fleet advisory 12: the candidate runs in this container, beside the
        # expectations.  Assert -- as the uid it will run as -- that it cannot
        # read them, rather than trusting the image's chmod to have worked.
        probes = [os.path.join(DATA, f) for f in
                  ("dstu2-expected.json", "stu3-expected.json", "cross-expected.json",
                   "r4-expected.json", "valexpected-dstu2.json",
                   "valdiffexpected-stu3.json", "types-r4.json", "roles.json")]
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
        H.log("GATE: removed %d pre-built files before building" % H.scrub_prebuilt())
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
        # The key SMART tokens are signed with lives at the path the instruction
        # names, in the candidate's own workspace.  The verifier restores only
        # /app/src, so it puts the same key there before starting the server: it
        # is not an expectation -- the agent is given it too -- and without it a
        # correct server has nothing to verify a signature against.
        try:
            src = os.path.join(DATA, "jwks.json")
            if os.path.exists(src):
                H.place_file(src, "testdata/smart", "jwks.json")
        except OSError as e:
            H.log("could not place the SMART key: %s" % e)
        proc, up = H.start_server(wait_path="/baseDstu2/metadata")
        if not up:
            H.log("GATE: the server did not answer GET /baseDstu2/metadata")
            H.log(open("/tmp/server-%d.log" % H.PORT, errors="replace").read()[-3000:])
            return 0.0
        if not H.process_identity_ok():
            return 0.0
        # A 200 from a base says nothing about which version answered, and this
        # milestone is about several versions at once: each base must return its
        # own conformance resource at its own fhirVersion.
        probe = Client(ROOT, timeout=H.REQUEST_TIMEOUT_SEC)
        for base_path, want_type, want_ver in (("baseDstu2", "Conformance", "1.0.2"),
                                               ("baseDstu3", "CapabilityStatement", "3.0.2"),
                                               ("baseR4", "CapabilityStatement", "4.0.1")):
            st, _, b = probe.send("GET", "/%s/metadata" % base_path)
            try:
                doc = json.loads(b.decode("utf-8"))
            except Exception:
                doc = {}
            if st != 200 or doc.get("resourceType") != want_type or doc.get("fhirVersion") != want_ver:
                H.log("GATE: /%s/metadata answered %s %s/%s, expected 200 %s/%s"
                      % (base_path, st, doc.get("resourceType"), doc.get("fhirVersion"),
                         want_type, want_ver))
                return 0.0
        H.log("GATE: all three service bases answer with their own conformance resource")
        return score_running_server()
    except Exception:
        H.log("VERIFIER EXCEPTION:\n" + traceback.format_exc())
    finally:
        H.stop_server(proc)
    return score


def score_running_server():
    """Everything after the server is up, so the same code can be pointed at an
    already-running server while the task is being authored."""
    global ROLES
    rp = os.path.join(DATA, "roles.json")
    ROLES = json.load(open(rp)) if os.path.exists(rp) else {}
    H.log("ROLES: %d units labelled (%d milestone, %d regression)"
          % (len(ROLES), sum(1 for v in ROLES.values() if v == "milestone"),
             sum(1 for v in ROLES.values() if v == "regression")))

    plan = [
        ("bulk",     component_bulk),
        ("subscription", component_subscription),
        ("r4b",      lambda: run_corpus("r4b", "baseR4B", "R4B")),
        ("r5",       lambda: run_corpus("r5", "baseR5", "R5")),
        ("valdiff45", component_valdiff45),
        ("profile|_profloc", component_profile),
        ("terminology", component_terminology),
        ("dstu2",    lambda: run_corpus("dstu2", "baseDstu2", "DSTU2")),
        ("stu3",     lambda: run_corpus("stu3", "baseDstu3", "STU3")),
        ("r4",       component_r4_regression),
        ("derived",  component_derived),
        ("capability", component_capability),
        ("output",   component_output),
    ]

    comps = {}
    for i, (name, fn) in enumerate(plan):
        # Each component gets an equal share of what is left of the step's wall
        # clock, so a candidate that is merely SLOW in one component cannot
        # starve the ones after it -- which, in this rung, are the carried-
        # forward ones a correct candidate should pass (fleet advisory 18 13).
        global DEADLINE
        DEADLINE = H.component_deadline(len(plan) - i)
        fhirhttp.set_deadline(DEADLINE)
        t = time.time()
        try:
            v = fn()
        except Exception:
            H.log("COMPONENT %s raised; scoring it 0:\n%s" % (name, traceback.format_exc()))
            v = (({}, {}) if "|" in name else {})
        if "|" in name:
            a, b = name.split("|")
            comps[a], comps[b] = v
            if b.startswith("_"):
                comps.pop(b)      # collected but not weighted at this rung
        else:
            comps[name] = v
        H.log("  (%s %.0fs; %.0fs of the step budget left)"
              % (name, time.time() - t, H.budget_left()))
        if i % 4 == 3 and not H.process_identity_ok():
            return 0.0
    if not H.process_identity_ok():
        return 0.0

    # Author mode only: the per-unit results, which is how roles are assigned by
    # measurement and how a blind run's misses are classified.  Gated on
    # AUTHOR_MODE like every other override, so the shipped verifier writes
    # nothing anywhere the candidate could read.
    dump = os.environ.get("FHIRCHAIN_DUMP_UNITS") if H.AUTHOR_MODE else None
    if dump:
        json.dump({c: {u: bool(ok) for u, (ok, _w) in units.items()}
                   for c, units in comps.items()}, open(dump, "w"), indent=1,
                  sort_keys=True)
        H.log("wrote per-unit results to %s" % dump)

    score, table, role_score = aggregate(comps)
    for k in sorted(table):
        H.log("  %-24s %s" % (k, table[k]))
    H.log("ROLES:      " + json.dumps({k: round(v, 4) for k, v in role_score.items()}))
    H.log("ROLEWEIGHT: " + json.dumps(ROLE_WEIGHTS))
    H.log("COMPONENTS: " + json.dumps(
        {k: "%d/%d" % (sum(1 for v, _ in u.values() if v), len(u)) for k, u in sorted(comps.items())}))
    return score



# Belt and braces with test.sh: the scorer may be run directly.
os.umask(0o022)

if __name__ == "__main__":
    s = main()
    print("SCORE=%.6f" % max(0.0, min(1.0, s)), flush=True)
