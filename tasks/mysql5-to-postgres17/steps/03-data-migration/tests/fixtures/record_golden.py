#!/usr/bin/env python3
"""Recorder for the fixtures this task takes from a *golden* tree.

Rule 5's exception clause: where a step's requirement changes what a response
or a table holds, the new expectation is re-recorded from that step's golden
tree (the workspace and PostgreSQL cluster the reference solution leaves --
the oracle run's steps/0N-*/artifacts), never typed in. The fixtures:

  fixture                      golden tree   what is re-derived
  behavior-overrides-04.json   step 04       status/location/sha256/length of
                                             each overridden id (`why` is prose)
  behavior-flips-04.json       step 04       the flipped ids, each anchor's
                                             head/tail digest at its recorded
                                             rung, and `body_bytes` (recorded
                                             and golden lengths); the measured
                                             `common_prefix/suffix` are not
                                             re-derived (informational, unread)
  runtime-baseline-04.json     step 04       `scenarios` and `error_scenarios`
                                             / `error_detail`: the scenarios in
                                             which the server refused a
                                             statement from a non-scorer role,
                                             read from the server log exactly as
                                             `wglib.pglog._evaluate` reads it
  behavior-overrides-06.json   step 06       as for -04, plus the overridden
                                             table digests
  behavior-partition-06.json   step 06       the requests and table snapshots
                                             the golden step-06 tree answers
                                             differently from the recording
  textvalues-repaired.json     step 06       every non-ASCII value of every
                                             text column textvalues-baseline.json
                                             records, with its row count, read
                                             with `wglib.encodinggate.read_column`
  mysql-intact-04.json         step 03       `wglib.mysqlkeep.record` over the
                                             handoff's /var/lib/mysql, /etc/mysql
  error-surface-markers.json   --            `wglib.httpdrv.ERROR_SURFACE` held to
                                             the keep rule over the pages in
                                             error-surfaces/ (each produced by the
                                             probe*.sh beside it)

A pass is the step's own scorer reading: the plan module's `_state()` (the
recorded tables resolved by `pgnames`), `wglib.deploy.bring_up`, and one
`collect.collect(..., strict=True, logmark=pglog.mark)`. Every record keeps
all of `runner.body_reading` (the anchors need the per-rung digests). With
`--textvalues` the text columns are read first, before the corpus writes
anything, which is when the step's scorer reads them.

Run inside the step's own verifier image (04, 05 or 06) as root, PYTHONPATH
=/tests, with the golden tree's /app, /var/lib/postgresql and /etc/postgresql
restored, once per fresh container:

    record_golden.py pass [--textvalues] --out /out/<n>.json

and then, anywhere (no container needed), against two passes:

    record_golden.py overrides  g1.json g2.json --baseline behavior-baseline.json \\
        --check behavior-overrides-0N.json
    record_golden.py flips      g1.json g2.json --baseline behavior-baseline.json \\
        --check behavior-flips-04.json
    record_golden.py runtime    g1.json g2.json --check runtime-baseline-04.json
    record_golden.py partition  g1.json g2.json --baseline behavior-baseline.json \\
        --flips behavior-flips-04.json --overrides behavior-overrides-04.json \\
        --check behavior-partition-06.json
        # behavior-overrides-04.json ships in steps 04 and 05: the partition is
        # what the repair moves, so step 04's carried override is not in it
    record_golden.py textvalues g1.json g2.json --check textvalues-repaired.json
    record_golden.py intact --datadir <step-03 artifacts>/var/lib/mysql \\
        --etcdir <step-03 artifacts>/etc/mysql --check mysql-intact-04.json
    record_golden.py markers --pages error-surfaces --check error-surface-markers.json

(`intact` and `markers` need PYTHONPATH pointing at a tests/ directory, for
wglib; `pass` for the step-04 fixtures runs in a step-04 or step-05 image with
the step-04 golden tree, and for the step-06 ones in the step-06 image with
the step-06 golden tree and `--textvalues`.)

`--check` exits 0 only when every re-derived field equals the fixture's; it
prints what it compared and what it treats as prose or provenance. These
fixtures are commented with authored explanations (`why`, `note`) and
measured context; those are not re-derived, and the scorer reads none of them.

Round-3 migration review, 2026-10-05: every fixture above re-derived IDENTICAL
in every field listed, from the oracle-r2 run's step-03/04/06 artifacts in the
step-04 and step-06 verifier images. See NOTES-authoring.md, round 3.
"""
import argparse
import json
import sys

REQUEST_FIELDS = ("status", "location", "sha256", "length")


def one_pass(plan_path, textvalues, columns_from):
    from wglib import collect, deploy, encodinggate, pglog, score
    plan = score.load_plan(plan_path)
    dep = deploy.bring_up(want_http=True)
    try:
        if not dep.apache.running or not dep.postgres.running:
            sys.exit("deployment did not come up: %s" % dep.summary())
        state = plan._state()
        text = None
        if textvalues:
            with open(columns_from, encoding="utf-8") as fh:
                wanted = [(c["table"], c["column"])
                          for c in json.load(fh)["columns"]]
            conn = state.conn
            found = encodinggate.catalog(conn)
            text = []
            for table, column in wanted:
                real = found.get((table.lower(), column.lower()))
                if real is None:
                    text.append({"table": table, "column": column,
                                 "missing": True})
                    continue
                values, ok = encodinggate.read_column(conn, real[0], real[1],
                                                      real[2])
                cur = conn.cursor()
                cur.execute('SELECT count(*), count(DISTINCT %s) FROM %s'
                            % (encodinggate.reads(real[1], real[2])[0],
                               encodinggate._quote(real[0])))
                rows, distinct = cur.fetchone()
                cur.close()
                conn.commit()
                text.append({"table": table, "column": column,
                             "data_type": real[2], "rows": int(rows),
                             "distinct": int(distinct),
                             "nonascii": len(values), "octets_utf8": ok,
                             "values": sorted([v, n] for v, n in values.items())})
        obs = collect.collect(state, strict=True, logmark=pglog.mark)
        slices = pglog.scenario_slices(None, obs["log_windows"])
        errored, detail = set(), {}
        for name, recs in slices.items():
            for r in recs:
                if (r.sev in pglog.ERROR_SEVERITIES and r.user
                        and r.user not in pglog.SCORER_ROLES):
                    errored.add(name)
                    detail.setdefault(name, " ".join(r.text.split())[:120])
    finally:
        deploy.tear_down()
    if obs.get("aborted") or obs.get("transport_errors"):
        sys.exit("pass incomplete: %r %r" % (obs.get("aborted"),
                                            obs.get("transport_errors")))
    return {"deployment": dep.summary(),
            "requests": obs["requests"],
            "tables_before": obs["tables_before"],
            "tables_after": obs["tables_after"],
            "scenarios": len(slices),
            "error_scenarios": sorted(errored),
            "error_detail": detail,
            "textvalues": text}


def _agree(passes, key):
    first = passes[0][key]
    for p in passes[1:]:
        if p[key] != first:
            sys.exit("the passes disagree on %s; nothing recorded" % key)
    return first


def _reqs(p):
    return {r["id"]: r for r in p["requests"]}


def _stable_requests(passes):
    by = [_reqs(p) for p in passes]
    out = {}
    for rid, r in by[0].items():
        if all(b.get(rid) == r for b in by[1:]):
            out[rid] = r
    return out


def _stable_tables(passes, key):
    out = {}
    for t, v in passes[0][key].items():
        if all(p[key].get(t) == v for p in passes[1:]):
            out[t] = v
    return out


def do_overrides(passes, fixture, baseline):
    got = _stable_requests(passes)
    base = {r["id"]: r for r in baseline["requests"]}
    bad = []
    n = 0
    for rid, want in sorted(fixture.get("requests", {}).items()):
        n += 1
        g = got.get(rid)
        if g is None:
            bad.append("%s: not stable across the passes" % rid)
            continue
        if any(want[f] != g[f] for f in REQUEST_FIELDS):
            bad.append("%s: want %r got %r" % (rid, {f: want[f] for f in REQUEST_FIELDS},
                                               {f: g[f] for f in REQUEST_FIELDS}))
        if all(base[rid][f] == g[f] for f in REQUEST_FIELDS):
            bad.append("%s: reproduces the baseline, so it is no override" % rid)
    for key in ("tables_before", "tables_after"):
        stable = _stable_tables(passes, key)
        for t, want in sorted(fixture.get(key, {}).items()):
            n += 1
            g = stable.get(t)
            if g is None or g.get("digest") != want["digest"] or g.get("rows") != want["rows"]:
                bad.append("%s/%s: want %s/%s got %r" % (key, t, want["digest"][:12],
                                                         want["rows"], g))
    return n, bad


def do_flips(passes, fixture, baseline=None):
    got = _stable_requests(passes)
    base = {r["id"]: r for r in (baseline or {}).get("requests", [])}
    bad = []
    n = 0
    for rid in fixture["requests"]:
        n += 1
        if rid not in got:
            bad.append("%s: not stable across the passes" % rid)
    for rid, spec in sorted(fixture.get("request_anchors", {}).items()):
        g = got.get(rid) or {}
        for end, rungs in (("head", "heads"), ("tail", "tails")):
            if end not in spec:
                continue
            n += 1
            have = (g.get(rungs) or {}).get(str(spec[end]["bytes"]))
            if have != spec[end]["sha256"]:
                bad.append("%s %s@%d: want %s got %s" % (rid, end, spec[end]["bytes"],
                                                        spec[end]["sha256"][:12],
                                                        (have or "none")[:12]))
        n += 1
        if g.get("error_surface"):
            bad.append("%s: golden body carries %r" % (rid, g["error_surface"]))
        if base and "body_bytes" in spec:
            n += 1
            want_bb = [base[rid]["length"], g.get("length")]
            if spec["body_bytes"] != want_bb:
                bad.append("%s body_bytes: want %r got %r"
                           % (rid, spec["body_bytes"], want_bb))
    for key in ("tables_before", "tables_after"):
        if fixture.get(key):
            bad.append("%s: flips of tables are not re-derived here" % key)
    return n, bad


def do_runtime(passes, fixture):
    got = {"scenarios": _agree(passes, "scenarios"),
           "error_scenarios": _agree(passes, "error_scenarios"),
           "error_detail": _agree(passes, "error_detail")}
    bad = ["%s: want %r got %r" % (k, fixture.get(k), got[k])
           for k in sorted(got) if fixture.get(k) != got[k]]
    return len(got), bad


def do_partition(passes, fixture, baseline, flips, overrides04):
    got = _stable_requests(passes)
    if len(got) != len(passes[0]["requests"]):
        sys.exit("unstable requests across passes: %r"
                 % sorted(set(_reqs(passes[0])) - set(got)))
    base = {r["id"]: r for r in baseline["requests"]}
    carried = set(flips.get("requests", [])) | set(overrides04.get("requests", {}))
    moved = [r["id"] for r in baseline["requests"]
             if r["id"] not in carried
             and any(base[r["id"]][f] != got[r["id"]][f] for f in REQUEST_FIELDS)]
    out = {"name": fixture.get("name"), "requests": sorted(moved)}
    for key in ("tables_before", "tables_after"):
        stable = _stable_tables(passes, key)
        out[key] = sorted(t for t, v in baseline[key].items()
                          if t in stable and stable[t] != v)
    bad = ["%s: want %r got %r" % (k, fixture.get(k), out[k])
           for k in ("requests", "tables_before", "tables_after")
           if sorted(fixture.get(k, [])) != out[k]]
    return 3, bad


def do_textvalues(passes, fixture):
    reads = [p["textvalues"] for p in passes]
    if any(r is None for r in reads):
        sys.exit("a pass without --textvalues")
    if any(r != reads[0] for r in reads[1:]):
        sys.exit("the passes disagree on the text values")
    got = {(c["table"], c["column"]): c for c in reads[0]}
    bad, n = [], 0
    for want in fixture["columns"]:
        key = (want["table"], want["column"])
        g = got.get(key)
        for field in ("values", "nonascii", "octets_utf8", "data_type",
                      "rows", "distinct"):
            n += 1
            if g is None or g.get(field) != want.get(field):
                bad.append("%s.%s %s: want %s got %s"
                           % (key[0], key[1], field,
                              json.dumps(want.get(field), ensure_ascii=False)[:200],
                              json.dumps(None if g is None else g.get(field),
                                         ensure_ascii=False)[:200]))
    # Two informational aggregates, read by no scorer, defined as the step-06
    # recorder defined them (which is not how record_mysql.py's baseline
    # recording defines its own): distinct values database-wide, and the
    # largest column's row count.
    for k, expr in (("distinct_values",
                     lambda cs: len({v for c in cs for v, _ in c["values"]})),
                    ("rows", lambda cs: max(c["rows"] for c in cs))):
        n += 1
        v = expr(reads[0])
        if fixture.get(k) != v:
            bad.append("%s: want %r got %r" % (k, fixture.get(k), v))
    return n, bad


def do_markers(pages_dir, fixture):
    """error-surface-markers.json against the pages it was derived from.

    The keep rule is the fixture's own note: a literal is a marker only if
    some deliberately broken page of this application carries it and no good
    page does. The broken pages, and the probe*.sh scripts that broke the
    deployment to produce them, ship in fixtures/error-surfaces/; the scorer's
    markers are `wglib.httpdrv.ERROR_SURFACE`. Re-derived here: that list is
    the fixture's `markers`; every marker is carried by at least one broken
    page and is not on clean.html; `missed` is exactly the broken pages
    carrying none. `provenance` (which broken pages carry each marker) is
    informational and is compared as a note only. The other good bodies (`golden:` and `mysql:`
    sides of step 04's four flipped ids) are not shipped; their golden side
    is checked by `flips` above, which fails a golden flipped body that
    carries any marker.
    """
    import os
    from wglib import httpdrv
    pages = {}
    for name in sorted(os.listdir(pages_dir)):
        if name.endswith(".html"):
            with open(os.path.join(pages_dir, name), "rb") as fh:
                pages[name] = fh.read()
    good = {"clean.html"}
    broken = sorted(n for n in pages if n not in good)
    markers = [m.decode("ascii") for m in httpdrv.ERROR_SURFACE]
    bad, n = [], 0
    n += 1
    if sorted(fixture["markers"]) != sorted(markers):
        bad.append("markers: fixture %r, scorer %r" % (fixture["markers"], markers))
    for m in markers:
        n += 2
        carriers = [p for p in broken if m.encode() in pages[p]]
        if carriers != sorted(fixture["provenance"].get(m, [])):
            # Informational: nothing reads `provenance`, and what the keep
            # rule needs of it -- at least one broken carrier -- is asked
            # below. Reported, not failed. (Round 3 found trace.html, whose
            # page prints getTraceAsString(), carrying `#0 /app/` without
            # being listed.)
            print("  note: provenance %r lists %r; the shipped pages carrying"
                  " it are %r" % (m, fixture["provenance"].get(m), carriers))
        on_good = [p for p in good if p in pages and m.encode() in pages[p]]
        if on_good or not carriers:
            bad.append("%r breaks the keep rule: good %r, broken %r"
                       % (m, on_good, carriers))
    n += 1
    missed = [p for p in broken
              if not any(m.encode() in pages[p] for m in markers)]
    if missed != sorted(fixture["missed"]):
        bad.append("missed: want %r got %r" % (fixture["missed"], missed))
    return n, bad


def do_intact(datadir, etcdir, fixture):
    """mysql-intact-04.json: `wglib.mysqlkeep.record`, the shipped recorder,
    over the step-03 golden handoff's /var/lib/mysql and /etc/mysql (a read
    of the directory tree and my.cnf; no server is started)."""
    import os
    from wglib import mysqlkeep
    got = mysqlkeep.record(datadir=datadir, etcdir=etcdir,
                           config=os.path.join(etcdir, "my.cnf"))
    bad = ["%s: want %r got %r" % (k, fixture.get(k), got.get(k))
           for k in sorted(set(fixture) | set(got)) if fixture.get(k) != got.get(k)]
    return len(set(fixture) | set(got)), bad


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pass")
    p.add_argument("--plan", default="/tests/plan.py")
    p.add_argument("--textvalues", action="store_true")
    p.add_argument("--columns-from",
                   default="/tests/fixtures/textvalues-baseline.json")
    p.add_argument("--out", required=True)
    it = sub.add_parser("intact")
    it.add_argument("--datadir", required=True)
    it.add_argument("--etcdir", required=True)
    it.add_argument("--check", required=True)
    mk = sub.add_parser("markers")
    mk.add_argument("--pages", default="/tests/fixtures/error-surfaces")
    mk.add_argument("--check", required=True)
    for name in ("overrides", "flips", "runtime", "partition", "textvalues"):
        m = sub.add_parser(name)
        m.add_argument("passes", nargs="+")
        m.add_argument("--check", required=True)
        if name in ("overrides", "partition"):
            m.add_argument("--baseline", required=True)
        if name == "flips":
            m.add_argument("--baseline")
        if name == "partition":
            m.add_argument("--flips", required=True)
            m.add_argument("--overrides", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "pass":
        doc = one_pass(a.plan, a.textvalues, a.columns_from)
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1, sort_keys=True, ensure_ascii=False)
        print("golden pass: %d requests, %d+%d tables, %d scenarios, refused in"
              " %r, textvalues %s, %s"
              % (len(doc["requests"]), len(doc["tables_before"]),
                 len(doc["tables_after"]), doc["scenarios"],
                 doc["error_scenarios"],
                 "read" if doc["textvalues"] is not None else "not read",
                 doc["deployment"]))
        return 0
    if a.cmd in ("markers", "intact"):
        with open(a.check, encoding="utf-8") as fh:
            fixture = json.load(fh)
        n, bad = (do_markers(a.pages, fixture) if a.cmd == "markers" else
                  do_intact(a.datadir, a.etcdir, fixture))
        for line in bad:
            print("  " + line)
        print("%s: %d re-derived field(s), %s"
              % (a.cmd, n, "IDENTICAL" if not bad else "%d DIFFERENT" % len(bad)))
        return 1 if bad else 0
    passes = []
    for path in a.passes:
        with open(path, encoding="utf-8") as fh:
            passes.append(json.load(fh))
    with open(a.check, encoding="utf-8") as fh:
        fixture = json.load(fh)
    load = lambda path: json.load(open(path, encoding="utf-8"))
    if a.cmd == "overrides":
        n, bad = do_overrides(passes, fixture, load(a.baseline))
    elif a.cmd == "flips":
        n, bad = do_flips(passes, fixture,
                          load(a.baseline) if a.baseline else None)
    elif a.cmd == "runtime":
        n, bad = do_runtime(passes, fixture)
    elif a.cmd == "partition":
        n, bad = do_partition(passes, fixture, load(a.baseline), load(a.flips),
                              load(a.overrides))
    else:
        n, bad = do_textvalues(passes, fixture)
    for line in bad:
        print("  " + line)
    print("%s: %d re-derived field(s), %s" % (a.cmd, n,
                                             "IDENTICAL" if not bad else
                                             "%d DIFFERENT" % len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
