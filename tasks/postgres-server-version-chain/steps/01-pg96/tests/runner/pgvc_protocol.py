#!/usr/bin/env python3
"""Wire-protocol probes driven through psycopg and a raw socket.

Each probe returns a short string.  The same probes are run against the
reference server to record the expected strings; they also run against the
candidate and the strings are compared.
"""
import json, socket, struct, subprocess, sys, threading, time

import psycopg
from psycopg import sql as _sql

PROBES = {}


def probe(name, minver=0):
    def deco(fn):
        PROBES[name] = (fn, minver)
        return fn
    return deco


def conn(ctx, **kw):
    return psycopg.connect(host=ctx["host"], port=ctx["port"], user=ctx["user"],
                           dbname=ctx.get("db", "postgres"), autocommit=True,
                           connect_timeout=15, **kw)


def r(v):
    return json.dumps(v, sort_keys=True, default=str)


# ----------------------------------------------------------------- startup
@probe("startup_parameter_status")
def p_startup(ctx):
    with conn(ctx) as c:
        got = {}
        for k in (b"server_encoding", b"client_encoding", b"DateStyle", b"IntervalStyle",
                  b"integer_datetimes", b"standard_conforming_strings", b"TimeZone",
                  b"is_superuser", b"session_authorization", b"application_name"):
            v = c.pgconn.parameter_status(k)
            got[k.decode()] = v.decode() if v else None
        return r(got)


@probe("startup_server_version_major")
def p_version(ctx):
    with conn(ctx) as c:
        v = c.pgconn.parameter_status(b"server_version").decode()
        num = c.execute("SELECT current_setting('server_version_num')::int").fetchone()[0]
        return r({"major_from_num": num // 10000, "banner_major": v.split(".")[0]})


@probe("startup_backend_key_present")
def p_key(ctx):
    with conn(ctx) as c:
        return r({"pid_positive": c.pgconn.backend_pid > 0})


@probe("ssl_request_refused")
def p_ssl(ctx):
    s = socket.create_connection((ctx["host"], ctx["port"]), 10)
    s.sendall(struct.pack("!ii", 8, 80877103))
    b = s.recv(1)
    s.close()
    return r({"reply": b.decode("latin1")})


@probe("gssenc_request_refused")
def p_gss(ctx):
    s = socket.create_connection((ctx["host"], ctx["port"]), 10)
    s.sendall(struct.pack("!ii", 8, 80877104))
    b = s.recv(1)
    s.close()
    return r({"reply": b.decode("latin1")})


@probe("bad_protocol_major_rejected")
def p_badproto(ctx):
    s = socket.create_connection((ctx["host"], ctx["port"]), 10)
    payload = b"user\x00postgres\x00database\x00postgres\x00\x00"
    body = struct.pack("!i", 0x00020000) + payload
    s.sendall(struct.pack("!i", len(body) + 4) + body)
    data = s.recv(4096)
    s.close()
    return r({"type": chr(data[0]) if data else ""})


# ------------------------------------------------------------ simple query
@probe("simple_query_multi")
def p_simple(ctx):
    with conn(ctx) as c:
        cur = c.execute("SELECT 1 AS a; SELECT 'x' AS b")
        out = [cur.fetchall()]
        while cur.nextset():
            out.append(cur.fetchall())
        return r(out)


@probe("empty_query")
def p_empty(ctx):
    with conn(ctx) as c:
        cur = c.execute("")
        return r({"status": cur.pgresult.status if cur.pgresult else None,
                  "rowcount": cur.rowcount})


@probe("error_fields_syntax")
def p_errsyntax(ctx):
    with conn(ctx) as c:
        try:
            c.execute("SELEC 1")
            return r({"raised": False})
        except psycopg.Error as e:
            d = e.diag
            return r({"sqlstate": d.sqlstate, "message": d.message_primary,
                      "position": d.statement_position, "severity": d.severity_nonlocalized})


@probe("error_fields_undefined_table")
def p_errtable(ctx):
    with conn(ctx) as c:
        try:
            c.execute("SELECT * FROM no_such_table_pgvc")
            return r({"raised": False})
        except psycopg.Error as e:
            return r({"sqlstate": e.diag.sqlstate, "message": e.diag.message_primary})


@probe("error_fields_constraint")
def p_errcons(ctx):
    with conn(ctx) as c:
        c.execute("DROP TABLE IF EXISTS pr_ec")
        c.execute("CREATE TABLE pr_ec(i int primary key)")
        c.execute("INSERT INTO pr_ec VALUES (1)")
        try:
            c.execute("INSERT INTO pr_ec VALUES (1)")
            out = {"raised": False}
        except psycopg.Error as e:
            out = {"sqlstate": e.diag.sqlstate, "message": e.diag.message_primary,
                   "detail": e.diag.message_detail}
        c.execute("DROP TABLE pr_ec")
        return r(out)


@probe("notice_captured")
def p_notice(ctx):
    got = []
    with conn(ctx) as c:
        c.add_notice_handler(lambda d: got.append((d.severity_nonlocalized, d.message_primary)))
        c.execute("DROP TABLE IF EXISTS pr_no_such_table_xyz")
        time.sleep(0.2)
        return r(sorted(got))


# ---------------------------------------------------------- extended query
@probe("extended_params_text")
def p_ext_text(ctx):
    with conn(ctx) as c:
        cur = c.cursor(binary=False)
        cur.execute("SELECT %s::int + %s::int, %s::text", (40, 2, "hi"))
        return r(cur.fetchall())


@probe("extended_params_binary")
def p_ext_bin(ctx):
    with conn(ctx) as c:
        cur = c.cursor(binary=True)
        cur.execute("SELECT %s::int8, %s::float8, %s::bool, %s::text, %s::numeric",
                    (2**40, 1.5, True, "z", "12345.6789"))
        return r(cur.fetchall())


@probe("binary_result_types")
def p_bin_types(ctx):
    with conn(ctx) as c:
        cur = c.cursor(binary=True)
        cur.execute("SELECT 1::int2, 2::int4, 3::int8, 4.5::float4, 6.25::float8, true, "
                    "'abc'::text, '\\x0102'::bytea, '2020-01-02'::date, "
                    "'2020-01-02 03:04:05'::timestamp, '1 day'::interval, "
                    "'123.456'::numeric, ARRAY[1,2,3], '{\"a\":1}'::jsonb, "
                    "'11111111-2222-3333-4444-555555555555'::uuid")
        row = cur.fetchone()
        return r([str(x) for x in row])


@probe("describe_statement_types")
def p_describe(ctx):
    with conn(ctx) as c:
        cur = c.cursor()
        cur.execute("SELECT %s::int AS a, %s::text AS b", (1, "x"))
        return r([(d.name, d.type_code) for d in cur.description])


@probe("prepared_reuse")
def p_prep(ctx):
    with conn(ctx) as c:
        cur = c.cursor()
        out = []
        for i in range(6):
            cur.execute("SELECT %s::int * 2", (i,), prepare=True)
            out.append(cur.fetchone()[0])
        return r(out)


@probe("portal_chunks")
def p_portal(ctx):
    with conn(ctx) as c:
        c.autocommit = False
        with c.cursor(name="pr_portal") as cur:
            cur.itersize = 2
            cur.execute("SELECT g FROM generate_series(1,7) g ORDER BY g")
            out = [row[0] for row in cur]
        c.rollback()
        return r(out)


@probe("rowdescription_metadata")
def p_rowdesc(ctx):
    with conn(ctx) as c:
        cur = c.execute("SELECT 1::int4 AS a, 'x'::varchar(9) AS b, 2.5::numeric(8,3) AS cc")
        return r([(d.name, d.type_code, d.internal_size, d.precision, d.scale)
                  for d in cur.description])


@probe("transaction_status")
def p_txstat(ctx):
    with conn(ctx) as c:
        c.autocommit = False
        a = c.info.transaction_status
        c.execute("SELECT 1")
        b = c.info.transaction_status
        try:
            c.execute("SELECT 1/0")
        except psycopg.Error:
            pass
        d = c.info.transaction_status
        c.rollback()
        e = c.info.transaction_status
        return r([int(a), int(b), int(d), int(e)])


@probe("cancel_request")
def p_cancel(ctx):
    with conn(ctx) as c:
        c.autocommit = False
        done = {}

        def run():
            try:
                c.execute("SELECT count(*) FROM generate_series(1, 40000000)")
                done["ok"] = True
            except psycopg.Error as e:
                done["sqlstate"] = e.diag.sqlstate

        t = threading.Thread(target=run)
        t.start()
        time.sleep(0.7)
        c.cancel()
        t.join(timeout=30)
        try:
            c.rollback()
        except Exception:
            pass
        return r({"sqlstate": done.get("sqlstate")})


# --------------------------------------------------------------------- COPY
@probe("copy_out_text")
def p_copyout(ctx):
    with conn(ctx) as c:
        c.execute("DROP TABLE IF EXISTS pr_copy")
        c.execute("CREATE TABLE pr_copy(i int, t text)")
        c.execute("INSERT INTO pr_copy VALUES (1,'a'),(2,'b with\ttab'),(3,NULL)")
        out = []
        with c.cursor().copy("COPY pr_copy TO STDOUT") as cp:
            for block in cp:
                out.append(bytes(block).decode())
        c.execute("DROP TABLE pr_copy")
        return r("".join(out))


@probe("copy_out_csv_header")
def p_copycsv(ctx):
    with conn(ctx) as c:
        c.execute("DROP TABLE IF EXISTS pr_copy2")
        c.execute("CREATE TABLE pr_copy2(i int, t text)")
        c.execute("INSERT INTO pr_copy2 VALUES (1,'a,b'),(2,'q\"q')")
        out = []
        with c.cursor().copy("COPY pr_copy2 TO STDOUT WITH (FORMAT csv, HEADER)") as cp:
            for block in cp:
                out.append(bytes(block).decode())
        c.execute("DROP TABLE pr_copy2")
        return r("".join(out))


@probe("copy_in_text")
def p_copyin(ctx):
    with conn(ctx) as c:
        c.execute("DROP TABLE IF EXISTS pr_copy3")
        c.execute("CREATE TABLE pr_copy3(i int, t text)")
        with c.cursor().copy("COPY pr_copy3 FROM STDIN") as cp:
            cp.write("1\talpha\n2\t\\N\n3\tgamma\n")
        rows = c.execute("SELECT i, t FROM pr_copy3 ORDER BY i").fetchall()
        c.execute("DROP TABLE pr_copy3")
        return r(rows)


@probe("copy_in_error")
def p_copyinerr(ctx):
    with conn(ctx) as c:
        c.execute("DROP TABLE IF EXISTS pr_copy4")
        c.execute("CREATE TABLE pr_copy4(i int)")
        try:
            with c.cursor().copy("COPY pr_copy4 FROM STDIN") as cp:
                cp.write("notanint\n")
            out = {"raised": False}
        except psycopg.Error as e:
            out = {"sqlstate": e.diag.sqlstate}
        c.execute("DROP TABLE pr_copy4")
        return r(out)


# --------------------------------------------------------- version-gated
@probe("pipeline_mode", minver=14)
def p_pipeline(ctx):
    with conn(ctx) as c:
        out = []
        with c.pipeline():
            c1 = c.execute("SELECT 1")
            c2 = c.execute("SELECT 2")
            c3 = c.execute("SELECT 3")
        out = [c1.fetchone()[0], c2.fetchone()[0], c3.fetchone()[0]]
        return r(out)


def run_all(ctx, names):
    out = {}
    for name in names:
        if name not in PROBES:
            out[name] = "EXC:UnknownProbe"
            continue
        fn, minver = PROBES[name]
        try:
            out[name] = fn(ctx)
        except Exception as exc:                       # a probe failure is a result too
            out[name] = "EXC:%s: %s" % (type(exc).__name__, str(exc)[:200])
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, required=True)
    ap.add_argument("--user", default="postgres")
    ap.add_argument("--db", default="postgres")
    ap.add_argument("--sockdir", default="")
    ap.add_argument("--names", required=True, help="comma separated probe names")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    ctx = {"host": a.host, "port": a.port, "user": a.user, "db": a.db, "sockdir": a.sockdir}
    names = [n for n in a.names.split(",") if n]
    res = run_all(ctx, names)
    open(a.out, "w").write(json.dumps(res, indent=1, sort_keys=True))
    return 0




# ------------------------------------------------- appended version-gated probes
@probe("old_client_protocol30")
def p_oldclient(ctx):
    """A PostgreSQL 9.6 psql -- a protocol 3.0 only client -- must still connect."""
    import subprocess as sp
    cmd = ["/usr/lib/postgresql/9.6/bin/psql", "-X", "-q", "-A", "-t",
           "-h", ctx["host"], "-p", str(ctx["port"]), "-U", ctx["user"],
           "-d", ctx.get("db", "postgres"),
           "-c", "SELECT 1+1 AS two, 'abc' AS t"]
    p = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=60)
    return r({"rc": p.returncode, "out": p.stdout.decode().strip()})


@probe("negotiate_protocol_default")
def p_neg_default(ctx):
    """psql 18 defaults to protocol 3.0, which every release must accept."""
    import subprocess as sp
    cmd = ["/usr/lib/postgresql/18/bin/psql", "-X", "-q", "-A", "-t",
           "-d", "host=%s port=%d user=%s dbname=%s" % (ctx["host"], ctx["port"], ctx["user"],
                                                        ctx.get("db", "postgres")),
           "-c", "\\conninfo"]
    p = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=60)
    out = [l.strip() for l in p.stdout.decode().splitlines() if "Protocol" in l]
    return r({"rc": p.returncode, "protocol": out})


@probe("negotiate_protocol_latest", minver=18)
def p_neg_latest(ctx):
    """With max_protocol_version=latest a PostgreSQL 18 server negotiates 3.2."""
    import subprocess as sp
    cmd = ["/usr/lib/postgresql/18/bin/psql", "-X", "-q", "-A", "-t",
           "-d", "host=%s port=%d user=%s dbname=%s max_protocol_version=latest"
                 % (ctx["host"], ctx["port"], ctx["user"], ctx.get("db", "postgres")),
           "-c", "\\conninfo"]
    p = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=60)
    out = [l.strip() for l in p.stdout.decode().splitlines() if "Protocol" in l]
    return r({"rc": p.returncode, "protocol": out})


@probe("negotiate_protocol_future")
def p_neg_future(ctx):
    """An unknown 3.x minor: the server either negotiates down or refuses."""
    s = socket.create_connection((ctx["host"], ctx["port"]), 10)
    payload = (b"user\x00" + ctx["user"].encode() + b"\x00database\x00"
               + ctx.get("db", "postgres").encode() + b"\x00\x00")
    body = struct.pack("!i", 0x00030009) + payload
    s.sendall(struct.pack("!i", len(body) + 4) + body)
    # drain briefly: how the server packetises its reply is not a protocol fact
    s.settimeout(0.6)
    data = b""
    try:
        while len(data) < 8192:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    s.close()
    kinds = []
    i = 0
    while i + 5 <= len(data) and len(kinds) < 4:
        t = chr(data[i])
        ln = struct.unpack("!i", data[i+1:i+5])[0]
        kinds.append(t)
        i += 1 + ln
    return r({"first_types": kinds[:3], "has_0A000": b"0A000" in data})


@probe("bad_protocol_major_message", minver=6)
def p_badproto_msg(ctx):
    s = socket.create_connection((ctx["host"], ctx["port"]), 10)
    payload = b"user\x00" + ctx["user"].encode() + b"\x00database\x00" \
              + ctx.get("db", "postgres").encode() + b"\x00\x00"
    body = struct.pack("!i", 0x00020000) + payload
    s.sendall(struct.pack("!i", len(body) + 4) + body)
    data = s.recv(4096)
    s.close()
    return r({"text": data.decode("latin1", "replace").strip("\x00")})


@probe("startup_unknown_pq_param")
def p_pq_param(ctx):
    """A `_pq_.` protocol extension the server does not know."""
    s = socket.create_connection((ctx["host"], ctx["port"]), 10)
    payload = (b"user\x00" + ctx["user"].encode() + b"\x00database\x00"
               + ctx.get("db", "postgres").encode()
               + b"\x00_pq_.pgvc_not_a_real_option\x001\x00\x00")
    body = struct.pack("!i", 0x00030000) + payload
    s.sendall(struct.pack("!i", len(body) + 4) + body)
    data = s.recv(4096)
    s.close()
    return r({"first_type": chr(data[0]) if data else ""})


@probe("copy_binary_out")
def p_copybin(ctx):
    with conn(ctx) as c:
        try:
            with c.cursor().copy("COPY (SELECT 1) TO STDOUT WITH (FORMAT binary)") as cp:
                n = sum(len(bytes(b)) for b in cp)
            return r({"bytes_gt_0": n > 0})
        except psycopg.Error as e:
            return r({"sqlstate": e.diag.sqlstate})




# ----------------------------------------------------- authentication methods
def _pw_connect(ctx, user, password, timeout=20):
    import subprocess as sp, os as _os
    env = dict(_os.environ, PGPASSWORD=password, PGCONNECT_TIMEOUT="10")
    cmd = ["/usr/lib/postgresql/18/bin/psql", "-X", "-q", "-A", "-t",
           "-h", ctx["host"], "-p", str(ctx["port"]), "-U", user,
           "-d", ctx.get("db", "postgres"), "-c", "SELECT 1"]
    p = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=timeout, env=env)
    out = p.stdout.decode()
    state = "ok" if p.returncode == 0 and out.strip() == "1" else "fail"
    detail = ""
    for key in ("password authentication failed", "no pg_hba.conf entry",
                "pg_hba.conf rejects", "role \"", "does not exist"):
        if key in out:
            detail = key
            break
    return {"result": state, "detail": detail}


def _make_role(ctx, name, password, encryption=None):
    with conn(ctx) as c:
        num = c.execute("SELECT current_setting('server_version_num')::int").fetchone()[0]
        c.execute('DROP ROLE IF EXISTS %s' % name)
        if encryption:
            # password_encryption is a boolean before PostgreSQL 10 and the
            # method name from 10 on
            if num < 100000:
                c.execute("SET password_encryption = %s" % ("on" if encryption == "md5" else "on"))
            else:
                c.execute("SET password_encryption = '%s'" % encryption)
        c.execute("CREATE ROLE %s LOGIN PASSWORD '%s'" % (name, password))


@probe("auth_md5_roundtrip")
def p_auth_md5(ctx):
    _make_role(ctx, "pgvc_md5", "s3cr3t", "md5")
    good = _pw_connect(ctx, "pgvc_md5", "s3cr3t")
    bad = _pw_connect(ctx, "pgvc_md5", "wrong")
    with conn(ctx) as c:
        pw = c.execute("SELECT left(rolpassword, 3) FROM pg_authid "
                       "WHERE rolname = 'pgvc_md5'").fetchone()[0]
    return r({"good": good, "bad": bad, "verifier_prefix": pw})


@probe("auth_scram_roundtrip", minver=2)
def p_auth_scram(ctx):
    _make_role(ctx, "pgvc_scram", "s3cr3t", "scram-sha-256")
    good = _pw_connect(ctx, "pgvc_scram", "s3cr3t")
    bad = _pw_connect(ctx, "pgvc_scram", "wrong")
    with conn(ctx) as c:
        pw = c.execute("SELECT left(rolpassword, 14) FROM pg_authid "
                       "WHERE rolname = 'pgvc_scram'").fetchone()[0]
    return r({"good": good, "bad": bad, "verifier_prefix": pw})


@probe("auth_reject")
def p_auth_reject(ctx):
    _make_role(ctx, "pgvc_reject", "s3cr3t", None)
    return r(_pw_connect(ctx, "pgvc_reject", "s3cr3t"))


@probe("unix_socket_connect")
def p_unix(ctx):
    import subprocess as sp
    sd = ctx.get("sockdir")
    if not sd:
        return r({"skipped": True})
    cmd = ["/usr/lib/postgresql/18/bin/psql", "-X", "-q", "-A", "-t",
           "-h", sd, "-p", str(ctx["port"]), "-U", ctx["user"],
           "-d", ctx.get("db", "postgres"), "-c", "SELECT 'over-unix-socket'"]
    p = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT, timeout=30)
    return r({"rc": p.returncode, "out": p.stdout.decode().strip()[:60]})


# NOTE: every probe must be defined ABOVE this line, or it is not registered
# by the time main() reads the name list.
if __name__ == "__main__":
    sys.exit(main())
