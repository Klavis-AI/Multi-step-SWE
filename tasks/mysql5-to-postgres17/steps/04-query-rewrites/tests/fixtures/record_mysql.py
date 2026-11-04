#!/usr/bin/env python3
"""Recorder for the three MySQL-catalogue fixtures in this directory.

  fixture                    what is recorded                 deployment read
  schema-baseline.json       information_schema: every base   untouched baseline
                             table's engine, collation,
                             columns in ordinal order, primary
                             key, indexes and foreign keys
  textvalues-baseline.json   every distinct non-ASCII value   untouched baseline
                             of every text column of every
                             comparable table, with its row
                             count
  schema-semantics.json      the answers MySQL gives to the   the tree step 02 is
                             probes `wglib.semantics.         handed (step 01's
                             build_plan(schema-baseline)`     reference: the same
                             derives, asked through           schema, strict
                             `semantics.MySQLExecutor`        `sql_mode`)

Nothing about the deployment is changed except by the semantics probes, which
the module itself confines to a clone table, a rolled-back transaction and a
restored AUTO_INCREMENT. Every reading is taken twice (`--passes`, default 2)
and kept only where both agree; a disagreeing table, column or probe is
dropped and named.

How each reading is taken (these are the definitions of the fixtures' fields):

  schema    TABLES (TABLE_TYPE 'BASE TABLE'): ENGINE, TABLE_COLLATION.
            COLUMNS by ORDINAL_POSITION: COLUMN_NAME, DATA_TYPE, COLUMN_TYPE,
            IS_NULLABLE, COLUMN_DEFAULT, CHARACTER_SET_NAME, COLLATION_NAME,
            CHARACTER_MAXIMUM_LENGTH, NUMERIC_PRECISION, NUMERIC_SCALE, and
            'auto_increment' in EXTRA. STATISTICS: index 'PRIMARY' gives the
            primary key in SEQ_IN_INDEX order; every other index is
            {name, unique = NON_UNIQUE == 0, parts [{column, prefix =
            SUB_PART}]}, indexes sorted by name. KEY_COLUMN_USAGE rows with a
            REFERENCED_TABLE_NAME give the foreign keys, sorted by name.
  text      the char/varchar/*text columns of `dbstate.MySQLState.tables()`
            (comparable tables: `dbstate.EXCLUDED_TABLES` filtered out),
            sorted by (table, column). Values are fetched as raw bytes over a
            `SET NAMES binary` connection, grouped with `GROUP BY BINARY` (so
            NFC and NFD spellings stay two values), and decoded from the
            column's declared charset by `dbstate.MySQLState._decode` (MySQL
            latin1 = `wglib.mysqllatin1`). `distinct` counts every group,
            `nonascii` the groups with a non-ASCII value, `values` lists those
            as [value, rows] sorted by value.
  semantics `semantics.run_plan(MySQLExecutor(conn), build_plan(baseline))`
            with `baseline` the schema-baseline.json beside this script.

Run inside a step 01-03 verifier image as root, PYTHONPATH=/tests, with the
deployment's /var/lib/mysql and /etc/mysql restored (`wglib.deploy.start_mysql`
starts it): for `capture`, the untouched baseline (the environment image's, or
the nop run's steps/01-strict-mode/artifacts -- a no-op changes nothing); for
`capture-semantics`, the oracle run's steps/01-strict-mode/artifacts. Then:

    record_mysql.py capture --out /out/catalogue.json
    record_mysql.py capture-semantics --out /out/semantics.json
    # anywhere, no container needed:
    record_mysql.py schema     catalogue.json --check schema-baseline.json
    record_mysql.py textvalues catalogue.json --check textvalues-baseline.json
    record_mysql.py semantics  semantics.json --check schema-semantics.json

(`--out <file>` instead of `--check` writes the fixture.) `--check` exits 0
only when every key other than `image` (provenance: the image the authors
recorded in) equals the re-derived reading, and says whether the file `--out`
would write is byte-identical.

Round-3 migration review, 2026-10-05: all three re-derived byte-identical --
schema-baseline.json and textvalues-baseline.json from the nop-r2 run's
step-01 artifacts, schema-semantics.json from the oracle-r2 run's step-01
artifacts -- in the step-01 verifier image. See NOTES-authoring.md, round 3.
"""
import argparse
import json
import sys

DATABASE = "webgoatphp"
TEXT_TYPES = ("char", "varchar", "tinytext", "text", "mediumtext", "longtext")
SCHEMA_FIXTURE = "/tests/fixtures/schema-baseline.json"


def _s(v):
    return v.decode("utf-8") if isinstance(v, (bytes, bytearray)) else v


def _i(v):
    return None if v is None else int(v)


def _open():
    from wglib import deploy
    svc = deploy.start_mysql()
    if not svc.running:
        sys.exit("mysql did not start: %s" % svc.detail)
    conn, _ = deploy.connect_mysql(DATABASE)
    return conn


def _q(conn, sql, args=()):
    cur = conn.cursor()
    cur.execute(sql, args)
    rows = cur.fetchall()
    cur.close()
    return rows


def read_schema(conn):
    tables = {}
    for name, engine, coll in _q(conn,
            "SELECT TABLE_NAME, ENGINE, TABLE_COLLATION FROM"
            " information_schema.TABLES WHERE TABLE_SCHEMA=%s AND"
            " TABLE_TYPE='BASE TABLE' ORDER BY TABLE_NAME", (DATABASE,)):
        name = _s(name)
        cols = []
        for r in _q(conn,
                "SELECT COLUMN_NAME, DATA_TYPE, COLUMN_TYPE, IS_NULLABLE,"
                " COLUMN_DEFAULT, CHARACTER_SET_NAME, COLLATION_NAME,"
                " CHARACTER_MAXIMUM_LENGTH, NUMERIC_PRECISION, NUMERIC_SCALE,"
                " EXTRA FROM information_schema.COLUMNS WHERE TABLE_SCHEMA=%s"
                " AND TABLE_NAME=%s ORDER BY ORDINAL_POSITION",
                (DATABASE, name)):
            r = [_s(v) for v in r]
            cols.append({"name": r[0], "type": r[1], "column_type": r[2],
                         "nullable": r[3] == "YES", "default": r[4],
                         "charset": r[5], "collation": r[6],
                         "length": _i(r[7]), "precision": _i(r[8]),
                         "scale": _i(r[9]),
                         "auto_increment": "auto_increment" in (r[10] or "")})
        pk, idx = [], {}
        for iname, nonuniq, seq, col, sub in _q(conn,
                "SELECT INDEX_NAME, NON_UNIQUE, SEQ_IN_INDEX, COLUMN_NAME,"
                " SUB_PART FROM information_schema.STATISTICS WHERE"
                " TABLE_SCHEMA=%s AND TABLE_NAME=%s ORDER BY INDEX_NAME,"
                " SEQ_IN_INDEX", (DATABASE, name)):
            iname, col = _s(iname), _s(col)
            if iname == "PRIMARY":
                pk.append((int(seq), col))
                continue
            ent = idx.setdefault(iname, {"name": iname,
                                         "unique": int(nonuniq) == 0,
                                         "parts": []})
            ent["parts"].append((int(seq), {"column": col, "prefix": _i(sub)}))
        indexes = []
        for iname in sorted(idx):
            ent = idx[iname]
            ent["parts"] = [p for _, p in sorted(ent["parts"],
                                                 key=lambda x: x[0])]
            indexes.append(ent)
        fks = {}
        for cname, col, rtab, rcol, pos in _q(conn,
                "SELECT CONSTRAINT_NAME, COLUMN_NAME, REFERENCED_TABLE_NAME,"
                " REFERENCED_COLUMN_NAME, ORDINAL_POSITION FROM"
                " information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA=%s"
                " AND TABLE_NAME=%s AND REFERENCED_TABLE_NAME IS NOT NULL",
                (DATABASE, name)):
            cname = _s(cname)
            ent = fks.setdefault(cname, {"name": cname, "ref_table": _s(rtab),
                                         "cols": []})
            ent["cols"].append((int(pos), _s(col), _s(rcol)))
        foreign_keys = []
        for cname in sorted(fks):
            ent = fks[cname]
            cs = sorted(ent.pop("cols"))
            ent["columns"] = [c for _, c, _ in cs]
            ent["ref_columns"] = [r for _, _, r in cs]
            foreign_keys.append(ent)
        tables[name] = {"engine": _s(engine), "collation": _s(coll),
                        "columns": cols,
                        "primary_key": [c for _, c in sorted(pk)],
                        "indexes": indexes, "foreign_keys": foreign_keys}
    return tables


def read_text(conn):
    from wglib import dbstate
    state = dbstate.MySQLState(conn, DATABASE)
    out = []
    for table in state.tables():
        for r in _q(conn,
                "SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_SET_NAME,"
                " COLLATION_NAME FROM information_schema.COLUMNS WHERE"
                " TABLE_SCHEMA=%s AND TABLE_NAME=%s", (DATABASE, table)):
            col, dtype, cs, coll = [_s(v) for v in r]
            if dtype not in TEXT_TYPES:
                continue
            groups = _q(conn, "SELECT `%s`, COUNT(*) FROM `%s` GROUP BY BINARY"
                        " `%s`" % (col, table, col))
            values = []
            for v, n in groups:
                if v is None:
                    continue
                text = state._decode(v, cs)
                if any(ord(ch) > 127 for ch in text):
                    values.append([text, int(n)])
            values.sort(key=lambda x: x[0])
            out.append({"table": table, "column": col, "charset": cs,
                        "collation": coll, "distinct": len(groups),
                        "nonascii": len(values), "values": values})
    out.sort(key=lambda c: (c["table"], c["column"]))
    return out


def server_version(conn):
    """The release number, e.g. 5.7.44. `@@version` carries a `-log` suffix
    whenever the general query log is on, which `deploy.start_mysql` turns on
    and the agent image's server does not; the suffix is a logging flag, not
    part of the version, so it is not recorded."""
    import re
    return re.match(r"[0-9.]+", _s(_q(conn, "SELECT @@version")[0][0])).group(0)


def capture(passes):
    conn = _open()
    try:
        return {"server": server_version(conn),
                "readings": [{"schema": read_schema(conn),
                              "text": read_text(conn)} for _ in range(passes)]}
    finally:
        conn.close()


def capture_semantics(passes):
    from wglib import semantics
    with open(SCHEMA_FIXTURE, encoding="utf-8") as fh:
        baseline = json.load(fh)
    conn = _open()
    try:
        mode = _s(_q(conn, "SELECT @@SESSION.sql_mode")[0][0])
        plan = semantics.build_plan(baseline)
        runs = [semantics.run_plan(semantics.MySQLExecutor(conn), plan)
                for _ in range(passes)]
        return {"server": server_version(conn), "sql_mode": mode,
                "probes": len(plan), "runs": runs}
    finally:
        conn.close()


def merge_schema(cap, image):
    reads = [r["schema"] for r in cap["readings"]]
    tables, dropped = {}, []
    for t in sorted(set().union(*reads)):
        vals = [r.get(t) for r in reads]
        if vals[0] is not None and all(v == vals[0] for v in vals):
            tables[t] = vals[0]
        else:
            dropped.append(t)
    return {"database": DATABASE, "dropped_tables": dropped, "image": image,
            "passes": len(reads), "server": cap["server"], "tables": tables}


def merge_text(cap, image):
    reads = [{(c["table"], c["column"]): c for c in r["text"]}
             for r in cap["readings"]]
    columns, dropped = [], []
    for key in sorted(set().union(*reads)):
        vals = [r.get(key) for r in reads]
        if vals[0] is not None and all(v == vals[0] for v in vals):
            columns.append(vals[0])
        else:
            dropped.append("%s.%s" % key)
    return {"columns": columns, "database": DATABASE,
            "distinct_values": sum(c["nonascii"] for c in columns),
            "dropped_columns": dropped, "image": image,
            "passes": len(reads),
            "rows": sum(n for c in columns for _, n in c["values"])}


def merge_semantics(cap, image):
    runs = cap["runs"]
    answers, dropped = {}, []
    for pid in sorted(set().union(*runs)):
        vals = [r.get(pid) for r in runs]
        if vals[0] is not None and all(v == vals[0] for v in vals):
            answers[pid] = vals[0]
        else:
            dropped.append(pid)
    return {"answers": answers, "dropped": dropped, "image": image,
            "passes": len(runs), "probes": cap["probes"],
            "server": cap["server"], "sql_mode": cap["sql_mode"]}


def dumps(doc, ascii_only):
    return json.dumps(doc, indent=1, sort_keys=True,
                      ensure_ascii=ascii_only) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("capture", "capture-semantics"):
        c = sub.add_parser(name)
        c.add_argument("--passes", type=int, default=2)
        c.add_argument("--out", required=True)
    for name in ("schema", "textvalues", "semantics"):
        m = sub.add_parser(name)
        m.add_argument("capture")
        m.add_argument("--image", default="unspecified")
        g = m.add_mutually_exclusive_group(required=True)
        g.add_argument("--out")
        g.add_argument("--check")
    a = ap.parse_args(argv)
    if a.cmd in ("capture", "capture-semantics"):
        doc = (capture if a.cmd == "capture" else capture_semantics)(a.passes)
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1, sort_keys=True, ensure_ascii=False)
        print("%s: %d pass(es) against MySQL %s" % (a.cmd, a.passes,
                                                    doc["server"]))
        return 0
    with open(a.capture, encoding="utf-8") as fh:
        cap = json.load(fh)
    want = raw = None
    if a.check:
        with open(a.check, "rb") as fh:
            raw = fh.read()
        want = json.loads(raw)
    image = want["image"] if want else a.image
    merge = {"schema": merge_schema, "textvalues": merge_text,
             "semantics": merge_semantics}[a.cmd]
    got = merge(cap, image)
    ascii_only = a.cmd != "textvalues"
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write(dumps(got, ascii_only))
        return 0
    diff = sorted(k for k in set(want) | set(got)
                  if k != "image" and want.get(k) != got.get(k))
    if diff:
        print("DIFFERENT in %s" % ", ".join(diff))
        for k in diff:
            w, g = want.get(k), got.get(k)
            if isinstance(w, dict) and isinstance(g, dict):
                for t in sorted(set(w) | set(g)):
                    if w.get(t) != g.get(t):
                        print("  %s/%s:\n    want %s\n    got  %s"
                              % (k, t, json.dumps(w.get(t), ensure_ascii=False)[:600],
                                 json.dumps(g.get(t), ensure_ascii=False)[:600]))
            elif isinstance(w, list) and isinstance(g, list):
                wk = {(c.get("table"), c.get("column")): c for c in w} if w and isinstance(w[0], dict) else None
                gk = {(c.get("table"), c.get("column")): c for c in g} if g and isinstance(g[0], dict) else None
                if wk is not None and gk is not None:
                    for key in sorted(set(wk) | set(gk), key=str):
                        if wk.get(key) != gk.get(key):
                            print("  %s %s:\n    want %s\n    got  %s"
                                  % (k, key, json.dumps(wk.get(key), ensure_ascii=False)[:600],
                                     json.dumps(gk.get(key), ensure_ascii=False)[:600]))
                else:
                    print("  %s: want %r got %r" % (k, w, g))
            else:
                print("  %s: want %r got %r" % (k, w, g))
        return 1
    print("IDENTICAL (every key but image); file bytes %s"
          % ("identical" if dumps(got, ascii_only).encode() == raw else "differ"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
