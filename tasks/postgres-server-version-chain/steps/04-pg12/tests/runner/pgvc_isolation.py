#!/usr/bin/env python3
"""Concurrent-session runner in the shape of PostgreSQL's isolationtester.

A spec has setup SQL, teardown SQL, named sessions each with named steps, and a
list of permutations.  A step that does not finish within `block_ms` is recorded
as waiting; it is collected when a later step releases it.  The transcript is
what gets diffed, and the expected transcripts are recorded by running the same
specs against the official postgres image.
"""
import json, pathlib, queue, sys, threading, time

import psycopg

BLOCK_MS = 1500


class Sess:
    def __init__(self, ctx, name):
        self.name = name
        self.conn = psycopg.connect(host=ctx["host"], port=ctx["port"], user=ctx["user"],
                                    dbname=ctx.get("db", "postgres"), autocommit=True,
                                    connect_timeout=15)
        self.q = queue.Queue()
        self.res = queue.Queue()
        self.pending = None
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def loop(self):
        while True:
            item = self.q.get()
            if item is None:
                return
            step, sql = item
            out = []
            try:
                cur = self.conn.cursor()
                cur.execute(sql)
                if cur.description:
                    cols = [d.name for d in cur.description]
                    rows = cur.fetchall()
                    out.append("%s" % "|".join(cols))
                    for row in rows:
                        out.append("|".join("" if v is None else str(v) for v in row))
                self.res.put((step, "ok", "\n".join(out)))
            except psycopg.Error as e:
                self.res.put((step, "error", "ERROR:%s" % (e.diag.sqlstate or "?")))
            except Exception as e:
                self.res.put((step, "error", "EXC:%s" % type(e).__name__))

    def send(self, step, sql):
        self.pending = step
        self.q.put((step, sql))

    def collect(self, timeout):
        try:
            step, kind, text = self.res.get(timeout=timeout)
            self.pending = None
            return step, kind, text
        except queue.Empty:
            return None

    def close(self):
        try:
            self.q.put(None)
            self.conn.close()
        except Exception:
            pass


def run_permutation(ctx, spec, perm, transcript):
    admin = psycopg.connect(host=ctx["host"], port=ctx["port"], user=ctx["user"],
                            dbname=ctx.get("db", "postgres"), autocommit=True, connect_timeout=15)
    for sql in spec.get("setup", []):
        try:
            admin.execute(sql)
        except psycopg.Error as e:
            transcript.append("setup failed: %s" % (e.diag.sqlstate or "?"))
    sessions = {}
    for sname, sdef in spec["sessions"].items():
        s = Sess(ctx, sname)
        sessions[sname] = s
        for sql in sdef.get("setup", []):
            s.send("_setup", sql)
            s.collect(20)
    order = []
    for stepname in perm:
        owner = None
        for sname, sdef in spec["sessions"].items():
            if stepname in sdef["steps"]:
                owner = sname
        if owner is None:
            transcript.append("unknown step %s" % stepname)
            continue
        s = sessions[owner]
        s.send(stepname, spec["sessions"][owner]["steps"][stepname])
        got = s.collect(BLOCK_MS / 1000.0)
        if got is None:
            transcript.append("step %s: <waiting ...>" % stepname)
            order.append((owner, stepname))
            continue
        transcript.append("step %s: %s" % (stepname, got[2] if got[2] else "ok"))
        # a completed step may release someone who was waiting
        for oname, ostep in list(order):
            og = sessions[oname].collect(BLOCK_MS / 1000.0)
            if og is not None:
                transcript.append("step %s: <... completed> %s" % (ostep, og[2] if og[2] else "ok"))
                order.remove((oname, ostep))
    for oname, ostep in list(order):
        og = sessions[oname].collect(5)
        if og is not None:
            transcript.append("step %s: <... completed> %s" % (ostep, og[2] if og[2] else "ok"))
        else:
            transcript.append("step %s: <still waiting>" % ostep)
    for sname, sdef in spec["sessions"].items():
        for sql in sdef.get("teardown", []):
            sessions[sname].send("_td", sql)
            sessions[sname].collect(10)
    for s in sessions.values():
        s.close()
    for sql in spec.get("teardown", []):
        try:
            admin.execute(sql)
        except psycopg.Error:
            pass
    admin.close()


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--specs", required=True)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--user", default="postgres")
    ap.add_argument("--db", default="postgres")
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    ctx = {"host": a.host, "port": a.port, "user": a.user, "db": a.db}
    specs = json.loads(pathlib.Path(a.specs).read_text())
    want = set(x for x in a.only.split(",") if x)
    out = {}
    for name, spec in sorted(specs.items()):
        for i, perm in enumerate(spec["permutations"]):
            key = "%s#%d" % (name, i)
            if want and key not in want:
                continue
            tr = []
            try:
                run_permutation(ctx, spec, perm, tr)
            except Exception as exc:
                tr.append("RUNNER-EXC:%s" % type(exc).__name__)
            out[key] = "\n".join(tr)
    pathlib.Path(a.out).write_text(json.dumps(out, indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
