#!/usr/bin/env python3
"""Recorder for the five statically-derived fixtures in this directory.

Each fixture below is a reading of a source tree by an instrument that ships
in `wglib`, so re-deriving it needs the tree and the instrument and nothing
else (sql-baseline.json also asks the database the tree's schema lives in
which functions and collations exist):

  fixture                 instrument                         tree read
  sql-baseline.json       wglib.sqlgate.record(roots, conn)  step-03 golden tree
                                                              + its PostgreSQL
  drivers-baseline.json   wglib.mysqlgone.record(roots)      untouched baseline
  inventory-step05.json   wglib.retire.record_inventory      step-04 golden tree
  drivers-step05.json     wglib.mysqlgone.record(drop=gone)  step-04 golden tree
  wiring-step05.json      wglib.retire.record_wiring(drop=gone)
                          + record_wiring_live(gone=gone)    step-04 golden tree

where `gone` is inventory-step05.json's `gone` list. "Golden tree" is the
workspace the reference solution leaves after that step (the oracle run's
steps/0N-*/artifacts/workspace); the baseline is the untouched tree (the nop
run's steps/01-strict-mode/artifacts/workspace).

Run inside a step 01-03 verifier image as root (the PHP scanners drop to
`nobody` themselves), with PYTHONPATH=/tests:

    record_static.py --baseline /base --tree03 /t03 --tree04 /t04 \\
        --check /tests/fixtures      # or --out <dir> to write the files

`--tree03` needs its PostgreSQL cluster restored to /var/lib/postgresql and
/etc/postgresql; the script starts it with wglib.deploy. `--check` compares
each fixture's own keys with the re-derived reading and exits 0 only when all
five are identical. Round-2 migration review, 2026-10-05: all five came back
IDENTICAL (drivers-baseline.json predates the `vocabulary` key the instrument
now also emits; every key the fixture has is identical).
"""
import argparse
import json
import os
import sys


def derive(base, t03, t04):
    from wglib import deploy, mysqlgone, retire, sqlgate
    svc = deploy.start_postgres()
    if not svc.running:
        sys.exit("postgres did not start: %s" % svc.detail)
    conn, _ = deploy.connect_postgres(deploy.APP_DB)
    conn.autocommit = True
    out = {"sql-baseline.json": sqlgate.record([t03], conn),
           "drivers-baseline.json": mysqlgone.record([base])}
    inv = retire.record_inventory([t04])
    gone = inv["gone"]
    out["inventory-step05.json"] = inv
    out["drivers-step05.json"] = mysqlgone.record([t04], drop=gone)
    wiring = retire.record_wiring([t04], drop=gone)
    wiring["live"] = retire.record_wiring_live([t04], gone=gone)
    out["wiring-step05.json"] = wiring
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--tree03", required=True)
    ap.add_argument("--tree04", required=True)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--out")
    g.add_argument("--check")
    a = ap.parse_args(argv)
    got = derive(a.baseline, a.tree03, a.tree04)
    if a.out:
        for name, doc in got.items():
            with open(os.path.join(a.out, name), "w", encoding="utf-8") as fh:
                json.dump(doc, fh, indent=1, sort_keys=True, ensure_ascii=False)
                fh.write("\n")
        return 0
    bad = 0
    for name, doc in sorted(got.items()):
        with open(os.path.join(a.check, name), encoding="utf-8") as fh:
            want = json.load(fh)
        diff = sorted(k for k in want if want[k] != doc.get(k))
        extra = sorted(set(doc) - set(want))
        print("%-24s %s%s" % (name, "IDENTICAL" if not diff else
                              "DIFFERENT in %s" % ", ".join(diff),
                              (" (instrument also emits %s)" % ", ".join(extra))
                              if extra else ""))
        bad += bool(diff)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
