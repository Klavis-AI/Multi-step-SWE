#!/usr/bin/env python3
"""Recorder for fixtures/logvalues-06.json -- the repaired `jf_logs` values.

Why this fixture exists. `textvalues-baseline.json` and
`textvalues-repaired.json` were taken through `dbstate.EXCLUDED_TABLES`, so
the damaged values in `jf_logs` (five serialized PHP structures carrying
`Anmeldung fÃ¼r MÃ¼ller`) are in neither, and step 06's "No value in the
database carries a double-encoding signature any more" had no check behind it
for that table (round-1 review F6-F1; NOTES "round 2"). `jf_logs` holds the
same 1,500 rows before and after any browsing -- `jf::Log()` has no caller in
the pinned tree (`dbstate.py`, VOLATILE_TABLES note) -- so its rows can be
recorded and compared by `ID`.

What it records. Every row of `jf_logs` is read twice from each of two
databases and the two reads of each must agree:

  before   the untouched baseline's MySQL 5.7 (`webgoatphp`, root over the
           socket), read through `dbstate.MySQLState`, i.e. each column decoded
           by its own declared charset -- the instrument every corpus family
           reads MySQL with;
  after    the golden step-06 tree's PostgreSQL 17 (`webgoatphp`, the
           superuser over the socket), the tree `solution/solve.sh` leaves.

The fixture keeps exactly the (row, text column) cells whose value differs
between the two, with both values. The recorder refuses to write a fixture in
which a kept `before` value does not carry a double-encoding signature, a kept
`after` value still does (both by `wglib.mojibake`), or any other cell differs
-- so the file is "what the repair changed in this table", derived, never
typed.

How to run it (both servers in one container: the step 01-03 verifier image is
the one that carries MySQL 5.7, PostgreSQL 17, PyMySQL and psycopg2). The
recording in this directory was made on 2026-10-05 with:

    BASE=<an untouched baseline: the nop run's steps/01-strict-mode/artifacts>
    GOLD=<the oracle run's steps/06-encoding-repair/artifacts>
    docker build -t wg-v03 steps/03-data-migration/tests
    docker run -d --name rec --entrypoint sleep wg-v03 infinity
    docker exec rec mkdir -p /var/lib/mysql /etc/mysql /var/lib/postgresql \\
        /etc/postgresql /rec
    docker cp $BASE/var/lib/mysql/. rec:/var/lib/mysql
    docker cp $BASE/etc/mysql/. rec:/etc/mysql
    docker cp $GOLD/var/lib/postgresql/. rec:/var/lib/postgresql
    docker cp $GOLD/etc/postgresql/. rec:/etc/postgresql
    docker cp steps/06-encoding-repair/tests/fixtures/record_logvalues.py rec:/rec/
    docker exec -e PYTHONPATH=/tests -e PYTHONPYCACHEPREFIX=/tmp/pyc rec \\
        python3 /rec/record_logvalues.py --out /rec/logvalues-06.json
    docker cp rec:/rec/logvalues-06.json steps/06-encoding-repair/tests/fixtures/

`--check <path>` re-derives and compares against an existing fixture instead
of writing one (exit 0 when identical, 1 otherwise).
"""

import argparse
import json
import sys

TABLE = "jf_logs"
KEY = "ID"


def _mysql_rows():
    from wglib import dbstate, deploy
    svc = deploy.start_mysql()
    if not svc.running:
        sys.exit("mysql did not start: %s" % svc.detail)
    conn, note = deploy.connect_mysql(deploy.APP_DB)
    state = dbstate.MySQLState(conn, deploy.APP_DB)
    cols = [c[0] if isinstance(c, (tuple, list)) else c
            for c in state.columns(TABLE)]
    cur = conn.cursor()
    cur.execute("SELECT %s FROM `%s` ORDER BY `%s`"
                % (", ".join("`%s`" % c for c in cols), TABLE, KEY))
    charsets = dict((c[0], c[1]) for c in state.columns(TABLE)
                    if isinstance(c, (tuple, list)))
    out = {}
    for row in cur.fetchall():
        rec = {}
        for name, raw in zip(cols, row):
            if isinstance(raw, (bytes, bytearray)):
                cs = (charsets.get(name) or "utf8").lower()
                codec = {"utf8": "utf-8", "utf8mb4": "utf-8",
                         "latin1": "cp1252", "binary": None}.get(cs, cs)
                raw = raw.decode(codec) if codec else raw.hex()
            rec[name] = raw
        out[int(rec[KEY])] = rec
    cur.close()
    conn.commit()
    return out, "baseline MySQL %s (%s)" % (deploy.APP_DB, note)


def _postgres_rows():
    from wglib import deploy
    svc = deploy.start_postgres()
    if not svc.running:
        sys.exit("postgres did not start: %s" % svc.detail)
    conn, note = deploy.connect_postgres(deploy.APP_DB)
    cur = conn.cursor()
    cur.execute("SELECT * FROM %s ORDER BY id" % TABLE)
    names = [d[0] for d in cur.description]
    out = {}
    for row in cur.fetchall():
        rec = dict(zip(names, row))
        out[int(rec["id"])] = rec
    cur.close()
    conn.close()
    return out, "golden step-06 PostgreSQL %s (%s)" % (deploy.APP_DB, note)


def _text_columns(before):
    any_row = next(iter(before.values()))
    return sorted(k for k, v in any_row.items() if isinstance(v, str))


def record():
    from wglib import mojibake
    b1, b_note = _mysql_rows()
    b2, _ = _mysql_rows()
    if b1 != b2:
        sys.exit("the two reads of the baseline disagree")
    a1, a_note = _postgres_rows()
    a2, _ = _postgres_rows()
    if a1 != a2:
        sys.exit("the two reads of the golden tree disagree")
    if sorted(b1) != sorted(a1):
        sys.exit("the two databases hold different jf_logs ids: %d vs %d"
                 % (len(b1), len(a1)))
    changed, problems = [], []
    for col in _text_columns(b1):
        for rid in sorted(b1):
            before = b1[rid][col]
            after = a1[rid].get(col.lower())
            if before == after:
                continue
            if not mojibake.is_mojibake(before):
                problems.append("%s %d: changed value carries no signature"
                                % (col, rid))
            if after is None or mojibake.is_mojibake(after):
                problems.append("%s %d: golden value still damaged" % (col, rid))
            changed.append({"id": rid, "column": col,
                            "before": before, "after": after})
    if problems:
        sys.exit("refusing to record:\n  " + "\n  ".join(problems))
    if not changed:
        sys.exit("nothing in %s differs: wrong pair of databases?" % TABLE)
    return {
        "table": TABLE,
        "key": KEY,
        "rows_compared": len(b1),
        "passes": 2,
        "before": b_note,
        "after": a_note,
        "recorder": "tests/fixtures/record_logvalues.py (see its docstring)",
        "note": ("Cells of jf_logs whose value the golden step-06 repair "
                 "changed, before and after. Must-flip: the tree step 06 is "
                 "handed holds every `before`; a repaired tree holds each "
                 "`after`, compared with PHP serialize() string-length "
                 "prefixes normalised (plan.py `_log_values`)."),
        "cells": changed,
    }


def main(argv=None):
    ap = argparse.ArgumentParser()
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--out")
    g.add_argument("--check")
    a = ap.parse_args(argv)
    doc = record()
    text = json.dumps(doc, indent=1, sort_keys=True, ensure_ascii=False) + "\n"
    if a.out:
        with open(a.out, "w", encoding="utf-8") as fh:
            fh.write(text)
        print("wrote %s: %d cell(s)" % (a.out, len(doc["cells"])))
        return 0
    with open(a.check, encoding="utf-8") as fh:
        old = fh.read()
    same = (json.loads(old)["cells"] == doc["cells"])
    print("re-derived %d cell(s); %s" % (len(doc["cells"]),
                                         "IDENTICAL" if same else "DIFFERENT"))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
