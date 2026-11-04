#!/usr/bin/env python3
"""Recorder for the three corpus-driven fixtures in this directory.

  fixture                   what is recorded
  behavior-baseline.json    the corpus (61 scenarios, 413 requests) over the
                            untouched tree: one record per response and a
                            digest of every comparable table before and after
  strict-probes.json        `wglib.strictgate.observe` over the same untouched
                            deployment, taken right after the corpus pass --
                            the order step 01's plan scores them in
  behavior-partition.json   (step 01) the requests and table snapshots that
                            move when nothing but my.cnf's `sql_mode` changes

The instrument is the scorer's own: `wglib.collect.collect()` drives
`wglib.corpus` through `wglib.runner.Recorder` and snapshots the tables with
`runner.db_snapshot`, the session id, the echoed lesson-source panes and the
row order of the one bare-JSON-array route normalised by
`wglib.httpdrv.normalize`; `wglib.strictgate.observe` drives its probes. This
script adds only what turns readings into a recording:

  * every pass runs in its own freshly restored container, because both the
    corpus and the probes write to the database they read;
  * the deployment is brought up exactly as the scorer brings it up
    (`wglib.deploy.bring_up`, the image's frozen clock, the MySQL connection
    from `deploy.connect_mysql`), and a transport error aborts the pass
    (`strict=True`) instead of being recorded;
  * `--analyse` runs `ANALYZE TABLE` on every base table first -- the planner
    perturbation of NOTES §20.9 -- so a response whose bytes depend on the
    query plan disagrees with the plain passes and is dropped by name;
  * the table snapshots use the exclusion set in force when the fixture was
    taken, `dbstate.RANDOM_TABLES` (jf_session) alone. `jf_logs` and
    `jf_stats` joined `dbstate.EXCLUDED_TABLES` later (NOTES §24.18), the
    scorer no longer compares their digests, and `datagate.seed_tables` still
    reads their recorded row counts -- so the recording keeps them;
  * `--sql-mode` rewrites my.cnf's `sql_mode` line before mysqld starts and
    changes nothing else (the instrument of NOTES §19.1); `partition` names
    what then differs from the baseline recording.

A request record keeps the eight fields the fixture carries (`adversarial id
length location method path sha256 status`); the scorer compares
`collect.REQUEST_FIELDS` of them. The anchor digests and error-surface reading
`runner.body_reading` also takes are scoring-side readings for flipped ids.
A strict-probe recording keeps the keys the fixture carries; the instrument
has since grown `sql_mode_loaded`, `sql_mode_scan`, `logged_sql_mode` and
`init_connect`, which the checks read from the observation side only.

The untouched baseline is the agent's starting state: /app, /var/lib/mysql
and /etc/mysql of a container of the environment image, or -- the same thing,
because a no-op changes nothing -- the nop run's steps/01-strict-mode/
artifacts/{workspace,var/lib/mysql,etc/mysql}. Pinned procedure (host side;
IMG is a step 01-03 verifier image built from that step's tests/, ART the
baseline artifacts):

    run() {  # run <name> <args to `record_corpus.py pass`>
      n=$1; shift
      docker run -d --name rec-$n --entrypoint sleep $IMG infinity
      docker exec rec-$n mkdir -p /app /var/lib/mysql /etc/mysql /out
      docker cp $ART/workspace/.     rec-$n:/app
      docker cp $ART/var/lib/mysql/. rec-$n:/var/lib/mysql
      docker cp $ART/etc/mysql/.     rec-$n:/etc/mysql
      docker exec -e PYTHONPATH=/tests -e PYTHONPYCACHEPREFIX=/tmp/pyc rec-$n \\
        python3 /tests/fixtures/record_corpus.py pass "$@" --out /out/$n.json
      docker cp rec-$n:/out/$n.json . && docker rm -f rec-$n
    }
    run plain1 --probes; run plain2 --probes; run analysed --analyse
    run strict --sql-mode STRICT_TRANS_TABLES,NO_ENGINE_SUBSTITUTION
    F=steps/01-strict-mode/tests/fixtures
    python3 record_corpus.py baseline plain1.json plain2.json analysed.json \\
        --check $F/behavior-baseline.json             # or --out <file>
    python3 record_corpus.py probes plain1.json plain2.json \\
        --check $F/strict-probes.json
    python3 record_corpus.py partition --baseline $F/behavior-baseline.json \\
        strict.json --check $F/behavior-partition.json

The merge commands need no container and import nothing from wglib. `--check`
exits 0 only when every key of the fixture other than its image name
(provenance: the image the authors recorded in) equals the re-derived reading
-- for strict-probes.json in the form the scorer reads it (`canon_probes`) --
and also says whether the file `--out` would write is byte-identical.

Round-3 migration review, 2026-10-05, re-derived from the nop-r2 run's step-01
artifacts in the step-01 verifier image: behavior-baseline.json and
behavior-partition.json byte-identical; strict-probes.json identical in every
key, with `sql_mode_mentions` equal as a set (the recording lists it in the
recording host's directory order) and `config_sql_mode`'s evidence lines
differing only by the "[mysqld] " label (see `canon_probes`). See
NOTES-authoring.md, round 3.
"""
import argparse
import json
import re
import sys

DATABASE = "webgoatphp"
MY_CNF = "/etc/mysql/my.cnf"
KEEP = ("adversarial", "id", "length", "location", "method", "path",
        "sha256", "status")
PROBE_KEYS = ("config_sql_mode", "root_over_socket", "schema_differences",
              "seen", "sql_mode", "sql_mode_mentions", "stored")
SCHEMA = "/tests/fixtures/schema-baseline.json"
TOOLS = "/tests/wglib"


def _set_sql_mode(mode):
    with open(MY_CNF, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    pat = re.compile(r"^(\s*sql[_-]mode\s*=\s*)(.*)$", re.I)
    hits = [i for i, l in enumerate(lines) if pat.match(l)]
    if len(hits) != 1:
        sys.exit("expected exactly one sql_mode line in %s, found %d"
                 % (MY_CNF, len(hits)))
    i = hits[0]
    lines[i] = pat.sub(lambda m: m.group(1) + mode, lines[i])
    with open(MY_CNF, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


def one_pass(analyse, probes, sql_mode):
    from wglib import collect, dbstate, deploy, strictgate
    # The exclusion set the fixture was recorded under (see the docstring).
    dbstate.EXCLUDED_TABLES = dbstate.RANDOM_TABLES
    if sql_mode:
        _set_sql_mode(sql_mode)
    dep = deploy.bring_up(want_http=True)
    try:
        if not (dep.mysql.running and dep.apache.running):
            sys.exit("deployment did not come up: %s" % dep.summary())
        conn, note = deploy.connect_mysql(DATABASE)
        try:
            cur = conn.cursor()
            cur.execute("SELECT @@GLOBAL.sql_mode")
            mode = cur.fetchone()[0]
            mode = mode.decode() if isinstance(mode, bytes) else mode
            if analyse:
                cur.execute("SELECT table_name FROM information_schema.tables"
                            " WHERE table_schema=%s AND table_type='BASE TABLE'"
                            " ORDER BY table_name", (DATABASE,))
                names = [r[0].decode() if isinstance(r[0], bytes) else r[0]
                         for r in cur.fetchall()]
                for t in names:
                    cur.execute("ANALYZE TABLE `%s`" % t.replace("`", "``"))
                    cur.fetchall()
            cur.close()
            state = dbstate.MySQLState(conn, DATABASE)
            obs = collect.collect(state, strict=True)
            seen = None
            if probes:
                pconn, _ = deploy.connect_mysql(DATABASE)
                try:
                    with open(SCHEMA, encoding="utf-8") as fh:
                        schema = json.load(fh)
                    seen = strictgate.observe(strictgate.Env(pconn),
                                              schema=schema, database=DATABASE,
                                              roots=("/app",), tools=TOOLS)
                finally:
                    pconn.close()
        finally:
            conn.close()
    finally:
        deploy.tear_down()
    if obs.get("aborted") or obs.get("transport_errors"):
        sys.exit("pass incomplete: %r %r" % (obs.get("aborted"),
                                            obs.get("transport_errors")))
    return {"analysed": bool(analyse),
            "server_sql_mode": mode,
            "connection": note,
            "deployment": dep.summary(),
            "requests": [{k: r[k] for k in KEEP} for r in obs["requests"]],
            "tables_before": obs["tables_before"],
            "tables_after": obs["tables_after"],
            "session_ids_seen": obs["session_ids_seen"],
            "probes": seen}


def merge_baseline(passes, image):
    order = [r["id"] for r in passes[0]["requests"]]
    by_pass = [{r["id"]: r for r in p["requests"]} for p in passes]
    requests, dropped_requests = [], []
    for rid in order:
        recs = [bp.get(rid) for bp in by_pass]
        if all(r == recs[0] for r in recs):
            requests.append(recs[0])
        else:
            dropped_requests.append(rid)
    for bp in by_pass[1:]:
        dropped_requests.extend(sorted(set(bp) - set(order)))
    out = {"requests": requests}
    dropped_tables = []
    for key in ("tables_before", "tables_after"):
        keep = {}
        for t in sorted(set().union(*(p[key] for p in passes))):
            vals = [p[key].get(t) for p in passes]
            if vals[0] is not None and all(v == vals[0] for v in vals):
                keep[t] = vals[0]
            else:
                dropped_tables.append("%s/%s" % (key, t))
        out[key] = keep
    out.update({"dropped_requests": dropped_requests,
                "dropped_tables": dropped_tables,
                "passes": len(passes),
                "perturbed_passes": sum(1 for p in passes if p["analysed"]),
                "image": image})
    return out


def merge_probes(passes, image):
    readings = []
    for p in passes:
        if p.get("probes") is None:
            sys.exit("a pass without --probes cannot record strict-probes.json")
        readings.append({k: p["probes"].get(k) for k in PROBE_KEYS})
    bad = sorted(k for k in PROBE_KEYS
                 if any(r[k] != readings[0][k] for r in readings[1:]))
    if bad:
        sys.exit("the passes disagree on %s; nothing recorded" % ", ".join(bad))
    out = dict(readings[0])
    out.update({"_corpus_first": True, "_image": image,
                "_passes": len(passes)})
    return out


def canon_probes(doc):
    """The strict-probe reading in the form the scorer reads it.

    Two fields are not compared as stored, and neither is scored as stored:
    `sql_mode_mentions` is a list in os.walk order -- the recording host's
    directory order -- and `strictgate.no_relaxation` reads it as a set
    (`before = {key ...}`), so it is compared sorted; and the recorded
    `config_sql_mode` is never read at all (`check_mode` reads the observed
    side only), while its evidence lines gained a "[group] " label in the
    instrument after the fixture was taken, so the label is dropped. Every
    other key is compared exactly.
    """
    out = dict(doc)
    out["sql_mode_mentions"] = sorted(map(list, doc.get("sql_mode_mentions") or []))
    value, lines = (doc.get("config_sql_mode") or [None, []])
    out["config_sql_mode"] = [value, [re.sub(r"^(\S+:\d+ )\[[^\]]*\] ", r"\1", l)
                                      for l in lines]]
    return out


def derive_partition(baseline, passes):
    p = passes[0]
    if any(q["requests"] != p["requests"] or q["tables_after"] != p["tables_after"]
           or q["tables_before"] != p["tables_before"] for q in passes[1:]):
        sys.exit("the perturbed passes disagree with each other")
    want = {r["id"]: r for r in baseline["requests"]}
    got = {r["id"]: r for r in p["requests"]}
    out = {"_explain": False, "_sql_mode": p["server_sql_mode"],
           "name": "strict-mode",
           "requests": [rid for rid in (r["id"] for r in baseline["requests"])
                        if got.get(rid) != want[rid]]}
    for key in ("tables_before", "tables_after"):
        out[key] = sorted(t for t, v in baseline[key].items()
                          if p[key].get(t) != v)
    return out


def dumps(doc, newline=True):
    return json.dumps(doc, indent=1, sort_keys=True) + ("\n" if newline else "")


def _report(want, got, raw, newline, skip):
    diff = sorted(k for k in set(want) | set(got)
                  if k not in skip and want.get(k) != got.get(k))
    if diff:
        print("DIFFERENT in %s" % ", ".join(diff))
        for k in diff:
            w, g = want.get(k), got.get(k)
            if isinstance(w, list) and isinstance(g, list) and w and isinstance(w[0], dict) and "id" in w[0]:
                wi, gi = {r["id"]: r for r in w}, {r["id"]: r for r in g}
                for rid in sorted(set(wi) | set(gi)):
                    if wi.get(rid) != gi.get(rid):
                        print("  %s %s: want %r got %r" % (k, rid, wi.get(rid), gi.get(rid)))
            elif isinstance(w, dict) and isinstance(g, dict):
                for t in sorted(set(w) | set(g)):
                    if w.get(t) != g.get(t):
                        print("  %s/%s: want %r got %r" % (k, t, w.get(t), g.get(t)))
            else:
                print("  %s: want %r got %r" % (k, w, g))
        return 1
    print("IDENTICAL (every key%s); file bytes %s"
          % ((" but %s" % "/".join(sorted(skip))) if skip else "",
             "identical" if dumps(got, newline).encode() == raw else "differ"))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pass")
    p.add_argument("--analyse", action="store_true")
    p.add_argument("--probes", action="store_true")
    p.add_argument("--sql-mode")
    p.add_argument("--out", required=True)
    for name in ("baseline", "probes", "partition"):
        m = sub.add_parser(name)
        m.add_argument("passes", nargs="+")
        if name == "partition":
            m.add_argument("--baseline", required=True)
        else:
            m.add_argument("--image", default="unspecified")
        g = m.add_mutually_exclusive_group(required=True)
        g.add_argument("--out")
        g.add_argument("--check")
    a = ap.parse_args(argv)
    if a.cmd == "pass":
        doc = one_pass(a.analyse, a.probes, a.sql_mode)
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1, sort_keys=True)
        print("pass (%s, sql_mode=%s): %d requests, %d+%d tables, %d session"
              " ids, probes %s, %s"
              % ("analysed" if a.analyse else "plain", doc["server_sql_mode"],
                 len(doc["requests"]), len(doc["tables_before"]),
                 len(doc["tables_after"]), doc["session_ids_seen"],
                 "taken" if doc["probes"] is not None else "not taken",
                 doc["deployment"]))
        return 0
    passes = []
    for path in a.passes:
        with open(path, encoding="utf-8") as fh:
            passes.append(json.load(fh))
    want = raw = None
    if a.check:
        with open(a.check, "rb") as fh:
            raw = fh.read()
        want = json.loads(raw)
    if a.cmd == "baseline":
        got = merge_baseline(passes, want["image"] if want else a.image)
        skip, newline = {"image"}, True
        print("stable: %d/%d requests, %d+%d table snapshots, dropped %r %r"
              % (len(got["requests"]), len(passes[0]["requests"]),
                 len(got["tables_before"]), len(got["tables_after"]),
                 got["dropped_requests"], got["dropped_tables"]))
    elif a.cmd == "probes":
        got = merge_probes(passes, want["_image"] if want else a.image)
        skip, newline = {"_image"}, True
    else:
        with open(a.baseline, encoding="utf-8") as fh:
            got = derive_partition(json.load(fh), passes)
        skip, newline = set(), False
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write(dumps(got, newline))
        return 0
    if a.cmd == "probes":
        exact = sorted(k for k in PROBE_KEYS if want.get(k) != got.get(k))
        if exact:
            print("as stored, differs in %s; comparing in the form the scorer"
                  " reads (canon_probes)" % ", ".join(exact))
        want, got = canon_probes(want), canon_probes(got)
    return _report(want, got, raw, newline, skip)


if __name__ == "__main__":
    sys.exit(main())
