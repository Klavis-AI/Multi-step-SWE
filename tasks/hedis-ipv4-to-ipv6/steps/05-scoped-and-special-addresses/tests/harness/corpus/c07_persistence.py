"""Persistence: RDB, AOF, the check tools, and restart-from-disk.

Data has to survive a round trip through a file and back into a *different*
process. This is ordinary regression surface that a networking change should
leave untouched, which is exactly why it is scored.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import srv  # noqa: E402

PORTS = 2
FAMILY = "behaviour"


def _seed(c):
    c.cmd("SELECT", 9)
    c.cmd("FLUSHALL")
    c.cmd("MSET", "s1", "v1", "s2", "v2", "n1", "12345")
    c.cmd("SET", "big", "x" * 100000)
    c.cmd("SET", b"bin", b"\x00\xff\xfe")
    c.cmd("RPUSH", "l1", *[str(i) for i in range(300)])
    c.cmd("RPUSH", "l2", "a", "b", "c")
    c.cmd("HMSET", "h1", *[x for i in range(600) for x in ("f%d" % i, str(i))])
    c.cmd("HMSET", "h2", "a", "1", "b", "2")
    c.cmd("SADD", "si", *[str(i) for i in range(200)])
    c.cmd("SADD", "st", "alpha", "beta", "gamma")
    c.cmd("ZADD", "z1", *[x for i in range(200) for x in (str(i), "m%d" % i)])
    c.cmd("ZADD", "z2", "1.5", "a", "inf", "b", "-inf", "c")
    c.cmd("PFADD", "hll", *["e%d" % i for i in range(1000)])
    c.cmd("SETEX", "ttl1", 10000, "v")
    c.cmd("SELECT", 3)
    c.cmd("SET", "otherdb", "yes")
    c.cmd("SELECT", 9)


def _fingerprint(ctx, c, prefix):
    """Everything that must be identical after a disk round trip."""
    c.cmd("SELECT", 9)
    ctx.emit_reply("%s/dbsize" % prefix, c.cmd("DBSIZE"))
    ctx.emit_reply("%s/keys" % prefix, c.cmd("KEYS", "*"), sort=True)
    for k in ("s1", "s2", "n1", "bin"):
        ctx.emit_reply("%s/get-%s" % (prefix, k), c.cmd("GET", k))
    ctx.emit_reply("%s/strlen-big" % prefix, c.cmd("STRLEN", "big"))
    ctx.emit_reply("%s/lrange-l2" % prefix, c.cmd("LRANGE", "l2", 0, -1))
    ctx.emit_reply("%s/llen-l1" % prefix, c.cmd("LLEN", "l1"))
    ctx.emit_reply("%s/lindex-l1-299" % prefix, c.cmd("LINDEX", "l1", 299))
    ctx.emit_reply("%s/hlen-h1" % prefix, c.cmd("HLEN", "h1"))
    ctx.emit_reply("%s/hget-h1-f599" % prefix, c.cmd("HGET", "h1", "f599"))
    ctx.emit_reply("%s/hgetall-h2" % prefix, c.cmd("HGETALL", "h2"), sort=True)
    ctx.emit_reply("%s/scard-si" % prefix, c.cmd("SCARD", "si"))
    ctx.emit_reply("%s/smembers-st" % prefix, c.cmd("SMEMBERS", "st"), sort=True)
    ctx.emit_reply("%s/zcard-z1" % prefix, c.cmd("ZCARD", "z1"))
    ctx.emit_reply("%s/zrange-z2" % prefix,
                   c.cmd("ZRANGE", "z2", 0, -1, "WITHSCORES"))
    ctx.emit_reply("%s/pfcount" % prefix, c.cmd("PFCOUNT", "hll"))
    ctx.emit_reply("%s/ttl-positive" % prefix,
                   ["int", 1 if c.cmd("TTL", "ttl1")[1] > 0 else 0])
    for k in ("l1", "h1", "si", "z1", "st", "z2", "h2", "l2"):
        ctx.emit_reply("%s/encoding-%s" % (prefix, k),
                       c.cmd("OBJECT", "ENCODING", k))
    c.cmd("SELECT", 3)
    ctx.emit_reply("%s/otherdb" % prefix, c.cmd("GET", "otherdb"))
    c.cmd("SELECT", 9)


def _wait_flag(c, field, want, deadline=30.0):
    return srv.wait_info(c, "persistence", field, want, deadline)


def _aof(*commands):
    """A byte-exact AOF body: one RESP multibulk per command."""
    out = []
    for cmd in commands:
        out.append(b"*%d\r\n" % len(cmd))
        for arg in cmd:
            out.append(b"$%d\r\n%s\r\n" % (len(arg), arg))
    return b"".join(out)


def _write(datadir, name, body):
    # Into a server's directory, which the score uid owns: created without
    # following anything that stands there (`srv.write_new`).
    return srv.write_new(os.path.join(datadir, name), body)


def _magic(path):
    try:
        with srv.open_regular(path) as f:  # no link followed
            return f.read(9).decode("ascii", "backslashreplace")
    except OSError as e:
        return "open-failed: %s" % e.__class__.__name__


def run(ctx):
    port = ctx.port(0)
    port2 = ctx.port(1)
    tree = ctx.tree

    s = ctx.server("persist", port, ctxmod.default_directives(port,
                                                              extra=["dbfilename dump.rdb"]))
    s.start().wait_ready()
    datadir = s.dir

    with s.client() as c:
        _seed(c)
        _fingerprint(ctx, c, "live")

        # -- in-process reload --------------------------------------------
        ctx.emit_reply("rdb/debug-reload", c.cmd("DEBUG", "RELOAD"))
        _fingerprint(ctx, c, "reload")

        # -- explicit SAVE -------------------------------------------------
        ctx.emit_reply("rdb/save", c.cmd("SAVE"))
        ctx.emit("rdb/file-magic", _magic(os.path.join(datadir, "dump.rdb")))
        ctx.emit("rdb/file-present",
                 os.path.exists(os.path.join(datadir, "dump.rdb")))

        # -- background SAVE -----------------------------------------------
        ctx.emit_reply("rdb/bgsave", c.cmd("BGSAVE"))
        ctx.emit("rdb/bgsave-finished",
                 _wait_flag(c, "rdb_bgsave_in_progress", "0"))
        ctx.emit("rdb/last-bgsave-ok",
                 b"rdb_last_bgsave_status:ok" in c.cmd("INFO", "persistence")[1])
        ctx.emit_reply("rdb/bgsave-while-running", c.cmd("BGSAVE"), adv=True)

    # -- redis-check-rdb on a good file ------------------------------------
    rc, out = srv.run_tool(tree, "redis-check-rdb",
                           [os.path.join(datadir, "dump.rdb")], timeout=120)
    ctx.emit("rdb/check-rc", rc)
    ctx.emit_text("rdb/check-out", out)

    # -- restart into the same directory -----------------------------------
    s.stop()
    # Reuse the first server's directory so the RDB read back is the one just
    # written.
    s2 = ctx.server("persist2", port,
                    ctxmod.default_directives(port, extra=["dbfilename dump.rdb"]),
                    datadir=datadir)
    s2.start().wait_ready()
    with s2.client() as c:
        _fingerprint(ctx, c, "restart")

        # -- AOF -----------------------------------------------------------
        ctx.emit_reply("aof/enable", c.cmd("CONFIG", "SET", "appendonly", "yes"))
        ctx.emit("aof/rewrite-finished",
                 _wait_flag(c, "aof_rewrite_in_progress", "0"))
        ctx.emit("aof/enabled-flag", _wait_flag(c, "aof_enabled", "1"))
        c.cmd("SET", "after-aof", "1")
        c.cmd("DEL", "s2")
        ctx.emit_reply("aof/bgrewrite", c.cmd("BGREWRITEAOF"))
        ctx.emit("aof/bgrewrite-finished",
                 _wait_flag(c, "aof_rewrite_in_progress", "0"))
        time.sleep(0.3)
        ctx.emit("aof/file-present",
                 os.path.exists(os.path.join(datadir, "appendonly.aof")))
    s2.stop()

    aof = os.path.join(datadir, "appendonly.aof")
    rc, out = srv.run_tool(tree, "redis-check-aof", [aof], timeout=120)
    ctx.emit("aof/check-rc", rc)
    ctx.emit_text("aof/check-out", out)

    # -- restart with AOF loading ------------------------------------------
    s3 = ctx.server("persist3", port2,
                    ctxmod.default_directives(port2,
                                              extra=["appendonly yes",
                                                     "dbfilename dump.rdb"]),
                    datadir=datadir)
    s3.start().wait_ready()
    with s3.client() as c:
        _fingerprint(ctx, c, "aof-restart")
        ctx.emit_reply("aof/after-aof-key", c.cmd("GET", "after-aof"))
        ctx.emit_reply("aof/deleted-key", c.cmd("EXISTS", "s2"))
        # SHUTDOWN closes the connection instead of replying.
        try:
            c.cmd("SHUTDOWN", "NOSAVE")
            shutdown_reply = "replied"
        except Exception as e:  # noqa: BLE001 - recorded outcome
            shutdown_reply = type(e).__name__
    ctx.emit("srv/shutdown-reply", shutdown_reply, adv=True)
    end = time.time() + 15
    while time.time() < end and s3.proc.poll() is None:
        time.sleep(0.05)
    ctx.emit("srv/shutdown-exited", s3.proc.poll() is not None, adv=True)
    ctx.emit("srv/shutdown-rc", s3.proc.poll(), adv=True)
    s3.stop()

    # -- damaged AOFs whose exact bytes are ours ----------------------------
    # Cutting bytes off the server's own rewritten AOF is not reproducible: the
    # rewrite walks each dict in hash order, so which record lands last -- and
    # therefore what a short cut destroys -- changes between runs, and so does
    # every offset redis-check-aof prints. These files are assembled here, byte
    # for byte, so the tool's report is a property of the tool.
    records = [[b"SELECT", b"9"], [b"SET", b"k1", b"v1"],
               [b"RPUSH", b"l", b"a", b"b"], [b"SET", b"k2", b"v2"]]
    good = _aof(*records)
    # Everything up to the last record: where --fix must leave the file.
    prefix = _aof(*records[:-1])
    cut = good[:-9]  # the last record chopped part-way through an argument
    for label, body in (("handmade", good),
                        ("cut", cut),
                        ("trailing-garbage", good + b"not-a-command\r\n"),
                        ("empty", b"")):
        path = _write(datadir, "%s.aof" % label, body)
        rc, out = srv.run_tool(tree, "redis-check-aof", [path], timeout=120)
        ctx.emit("aof/check-%s-rc" % label, rc, adv=True)
        ctx.emit_text("aof/check-%s-out" % label, out, adv=True)

    fixme = _write(datadir, "fixme.aof", cut)
    rc, out = srv.run_tool(tree, "redis-check-aof", ["--fix", fixme],
                           timeout=120, stdin=b"y\n")
    ctx.emit("aof/check-fix-rc", rc, adv=True)
    ctx.emit_text("aof/check-fix-out", out, adv=True)
    with srv.open_regular(fixme) as f:  # raises like open(); no link
        fixed = f.read()
    ctx.emit("aof/fix-truncated-to-record-boundary", fixed == prefix, adv=True)
    rc, out = srv.run_tool(tree, "redis-check-aof", [fixme], timeout=120)
    ctx.emit("aof/check-after-fix-rc", rc, adv=True)
    ctx.emit_text("aof/check-after-fix-out", out, adv=True)

    # -- damaged RDBs, likewise byte-exact -----------------------------------
    # A server-written RDB has the same ordering problem as the AOF, so the
    # corruption probes use headers assembled here. `\xff` is the EOF opcode
    # and the eight zero bytes are the "checksum disabled" trailer, which makes
    # `minimal-v6` the smallest file the loader should accept.
    eof = b"\xff" + b"\x00" * 8
    for label, body in (("bad-magic", b"NOTREDIS7" + eof),
                        ("minimal-v6", b"REDIS0006" + eof),
                        ("future-version", b"REDIS0099" + eof),
                        ("header-only", b"REDIS0006"),
                        ("empty", b"")):
        path = _write(datadir, "%s.rdb" % label, body)
        rc, out = srv.run_tool(tree, "redis-check-rdb", [path], timeout=120)
        ctx.emit("rdb/check-%s-rc" % label, rc, adv=True)
        ctx.emit_text("rdb/check-%s-out" % label, out, adv=True)
    rc, out = srv.run_tool(tree, "redis-check-rdb", ["/nonexistent.rdb"],
                           timeout=60)
    ctx.emit("rdb/check-missing-rc", rc, adv=True)
    ctx.emit_text("rdb/check-missing-out", out, adv=True)
