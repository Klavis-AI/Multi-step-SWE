#!/usr/bin/env python3
"""Compatibility checks: dumps produced by earlier releases restore, and a
protocol-3.0 client from an old psql connects and gets the same answers."""
import json, os, pathlib, subprocess, sys

PSQL18 = "/usr/lib/postgresql/18/bin/psql"
PSQL96 = "/usr/lib/postgresql/9.6/bin/psql"


def psql(binary, port, db, args, inp=None, timeout=180, host="127.0.0.1"):
    cmd = [binary, "-X", "-q", "-h", host, "-p", str(port), "-U", "postgres", "-d", db] + args
    env = dict(os.environ, PGCONNECT_TIMEOUT="15", LC_ALL="C", TZ="UTC")
    p = subprocess.run(cmd, input=(inp.encode() if inp else None), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout, env=env)
    return p.returncode, p.stdout.decode("utf-8", "replace")


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--dumps", required=True, help="directory of <name>.sql dumps")
    ap.add_argument("--queries", required=True, help="JSON: name -> list of verification SQL")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    queries = json.loads(pathlib.Path(a.queries).read_text())
    out = {}
    for name in sorted(queries):
        dumpfile = pathlib.Path(a.dumps) / (name + ".sql")
        db = "compat_" + name.replace("-", "_")
        psql(PSQL18, a.port, "postgres", ["-c", "DROP DATABASE IF EXISTS %s" % db], host=a.host)
        rc, o = psql(PSQL18, a.port, "postgres", ["-c", "CREATE DATABASE %s" % db], host=a.host)
        if rc != 0:
            out[name + "/restore"] = "CREATE DATABASE failed"
            continue
        rc, o = psql(PSQL18, a.port, db, ["-v", "ON_ERROR_STOP=0", "-f", str(dumpfile)], host=a.host)
        errs = [l for l in o.splitlines() if l.startswith("ERROR") or l.startswith("psql:")]
        out[name + "/restore"] = "errors=%d" % len(errs)
        out[name + "/restore_detail"] = "\n".join(errs[:12])
        for i, q in enumerate(queries[name]):
            rc, o = psql(PSQL18, a.port, db, ["-A", "-t", "-c", q], host=a.host)
            out["%s/q%d" % (name, i)] = o.strip()
        # the same verification queries through a protocol-3.0 client from 9.6
        for i, q in enumerate(queries[name][:2]):
            rc, o = psql(PSQL96, a.port, db, ["-A", "-t", "-c", q], host=a.host)
            out["%s/old_client_q%d" % (name, i)] = o.strip()
    pathlib.Path(a.out).write_text(json.dumps(out, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
