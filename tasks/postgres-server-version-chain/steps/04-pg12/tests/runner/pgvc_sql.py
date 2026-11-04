#!/usr/bin/env python3
"""Replay a curated SQL corpus through upstream psql and capture byte-exact output.

This is glue only: it never contains an expected value.  The same module is used
against the reference server to *record* expected output and against the
candidate server to *produce* results that are diffed against those recordings.

The psql invocation deliberately mirrors pg_regress:  `psql -X -a -q` reading the
case from stdin, stdout and stderr merged into one stream.
"""
import argparse, concurrent.futures, json, os, pathlib, subprocess, sys, time

DEF_PSQL = "/usr/lib/postgresql/18/bin/psql"


def psql_env():
    e = dict(os.environ)
    e.update(PGCONNECT_TIMEOUT="20", PGAPPNAME="pgvc", LC_ALL="C", LANG="C", TZ="UTC")
    e.pop("PGOPTIONS", None)
    e.pop("PGDATABASE", None)
    return e


def run_sql_text(args, dbname, text, timeout):
    cmd = [args.psql, "-X", "-a", "-q", "-h", args.host, "-p", str(args.port),
           "-U", args.user, "-d", dbname]
    try:
        p = subprocess.run(cmd, input=text.encode(), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout, env=psql_env())
        return p.returncode, p.stdout.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as exc:
        got = exc.stdout.decode("utf-8", "replace") if exc.stdout else ""
        return 124, got + "\npgvc: TIMEOUT\n"


def admin(args, text, timeout=120):
    return run_sql_text(args, args.admin_db, text, timeout)


def wait_ready(args, deadline_s):
    end = time.time() + deadline_s
    while time.time() < end:
        rc, out = run_sql_text(args, args.admin_db, "SELECT 1;\n", 20)
        if rc == 0 and "(1 row)" in out:
            return True
        time.sleep(1.0)
    return False


def load_manifest(args):
    man = json.loads(pathlib.Path(args.manifest).read_text())
    cases = sorted(man["cases"], key=lambda c: c["name"])
    return man, cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases-dir", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5432)
    ap.add_argument("--user", default="postgres")
    ap.add_argument("--admin-db", default="postgres")
    ap.add_argument("--test-db", default="regression")
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--psql", default=DEF_PSQL)
    ap.add_argument("--case-timeout", type=int, default=60)
    ap.add_argument("--ready-timeout", type=int, default=60)
    args = ap.parse_args()

    outdir = pathlib.Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)
    cdir = pathlib.Path(args.cases_dir)
    man, cases = load_manifest(args)

    status = {"ready": False, "createdb": False, "cases": {}}
    if not wait_ready(args, args.ready_timeout):
        (outdir / "_runner.json").write_text(json.dumps(status))
        print("pgvc: server not reachable", file=sys.stderr)
        return 0
    status["ready"] = True

    # pg_regress creates its own database; do the same.
    rc, out = admin(args, "DROP DATABASE IF EXISTS %s;\n" % args.test_db)
    rc, out = admin(args, "CREATE DATABASE %s;\n" % args.test_db)
    status["createdb"] = (rc == 0 and "ERROR" not in out)
    status["createdb_output"] = out[-2000:]

    par = [c for c in cases if not c.get("serial")]
    ser = [c for c in cases if c.get("serial")]

    def one(case):
        text = (cdir / (case["name"] + ".sql")).read_text()
        t0 = time.time()
        rc, out = run_sql_text(args, args.test_db, text, args.case_timeout)
        (outdir / (case["name"] + ".out")).write_bytes(out.encode())
        return case["name"], rc, round(time.time() - t0, 3)

    results = {}
    if par:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.workers)) as ex:
            for name, rc, dt in ex.map(one, par):
                results[name] = {"rc": rc, "sec": dt}
    for case in ser:
        name, rc, dt = one(case)
        results[name] = {"rc": rc, "sec": dt}

    status["cases"] = results
    (outdir / "_runner.json").write_text(json.dumps(status, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
