"""redis-cli: connecting, formatting, and the bulk modes.

The CLI is the tree's own client. How it parses a host argument, which family
it reaches for, and how it renders an address back to the user are all part of
the observable interface.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import srv  # noqa: E402

PORTS = 2
FAMILY = "behaviour"

# --bigkeys announces each new record holder as it walks the keyspace, so these
# lines come out in SCAN order -- which depends on the server's per-process
# hash seed and differs between two runs of the same binary. The summary that
# follows them is order-independent and is what gets scored.
PROGRESS = re.compile(rb"^\[\s*\d+\.\d+%\]")


def _cli(ctx, label, args, adv=False, stdin=b"", timeout=60, cwd=None,
         drop=None):
    """Run redis-cli once. stdin is always a closed pipe unless given, so an
    invocation that would drop into the interactive REPL sees EOF and exits.
    `drop` removes whole matching lines before the output is recorded."""
    rc, out = srv.run_tool(ctx.tree, "redis-cli", args, timeout=timeout,
                           stdin=stdin, cwd=cwd)
    if drop is not None:
        out = b"\n".join(l for l in out.split(b"\n") if not drop.search(l))
    ctx.emit("cli/%s/rc" % label, rc, adv=adv)
    ctx.emit_text("cli/%s/out" % label, out, adv=adv)
    return out


def run(ctx):
    port = ctx.port(0)
    dead = ctx.port(1)
    sock = os.path.join(ctx.workspace.base, "cli.sock")

    s = ctx.server("cli", port,
                   ctxmod.default_directives(port, "127.0.0.1 ::1",
                                             extra=["unixsocket %s" % sock]))
    s.start().wait_ready()

    H6 = ["-h", "::1", "-p", port]
    H4 = ["-h", "127.0.0.1", "-p", port]

    # -- reaching the server ----------------------------------------------
    _cli(ctx, "ping-v6", H6 + ["PING"])
    _cli(ctx, "ping-v4", H4 + ["PING"])
    _cli(ctx, "ping-hostname", ["-h", "localhost", "-p", port, "PING"], adv=True)
    _cli(ctx, "ping-default-host", ["-p", port, "PING"], adv=True)
    _cli(ctx, "ping-unix", ["-s", sock, "PING"])
    _cli(ctx, "ping-bracketed-host",
         ["-h", "[::1]", "-p", port, "PING"], adv=True)
    _cli(ctx, "ping-scoped-host",
         ["-h", "::1%lo", "-p", port, "PING"], adv=True)
    _cli(ctx, "ping-v4-mapped",
         ["-h", "::ffff:127.0.0.1", "-p", port, "PING"], adv=True)
    _cli(ctx, "connect-refused-v6", ["-h", "::1", "-p", dead, "PING"], adv=True)
    _cli(ctx, "connect-refused-v4",
         ["-h", "127.0.0.1", "-p", dead, "PING"], adv=True)
    _cli(ctx, "connect-unresolvable",
         ["-h", "no-such-host.invalid", "-p", port, "PING"], adv=True)
    _cli(ctx, "connect-bad-unix", ["-s", "/nonexistent.sock", "PING"], adv=True)
    _cli(ctx, "connect-timeout-opt",
         H6 + ["-t", "2", "PING"], adv=True)

    # -- reply rendering ---------------------------------------------------
    _cli(ctx, "set", H6 + ["SET", "ck", "cv"])
    _cli(ctx, "get", H6 + ["GET", "ck"])
    _cli(ctx, "get-missing", H6 + ["GET", "no-such-key"], adv=True)
    _cli(ctx, "error-reply", H6 + ["INCR", "ck"], adv=True)
    _cli(ctx, "integer-reply", H6 + ["DBSIZE"])
    _cli(ctx, "array-reply", H6 + ["MGET", "ck", "no-such-key"], adv=True)
    _cli(ctx, "nested-reply", H6 + ["COMMAND", "INFO", "GET"], adv=True)
    _cli(ctx, "binary-reply-raw",
         H6 + ["EVAL", "return '\\001\\002\\003'", "0"], adv=True)
    _cli(ctx, "binary-reply-no-raw",
         H6 + ["--no-raw", "EVAL", "return '\\001\\002\\003'", "0"], adv=True)
    _cli(ctx, "csv", H6 + ["--csv", "MGET", "ck", "no-such-key"], adv=True)
    _cli(ctx, "raw-flag", H6 + ["--raw", "GET", "ck"], adv=True)
    _cli(ctx, "select-db", H6 + ["-n", "3", "SET", "indb3", "v"])
    _cli(ctx, "select-db-read", H6 + ["-n", "3", "GET", "indb3"])
    _cli(ctx, "select-db-other", H6 + ["-n", "0", "GET", "indb3"], adv=True)
    _cli(ctx, "repeat", H6 + ["-r", "3", "-i", "0", "PING"], adv=True)
    _cli(ctx, "repeat-zero", H6 + ["-r", "0", "PING"], adv=True)
    _cli(ctx, "stdin-arg", H6 + ["-x", "SET", "fromstdin"],
         stdin=b"piped-value", adv=True)
    _cli(ctx, "stdin-arg-read", H6 + ["GET", "fromstdin"], adv=True)
    _cli(ctx, "unknown-command", H6 + ["NOSUCHCOMMAND"], adv=True)
    _cli(ctx, "no-command", H6, adv=True)

    # -- commands read from stdin -----------------------------------------
    _cli(ctx, "stdin-commands", H6,
         stdin=b"SET pipeline-key 1\nINCR pipeline-key\nGET pipeline-key\n",
         adv=True)

    # -- bulk modes --------------------------------------------------------
    _cli(ctx, "scan", H6 + ["--scan", "--pattern", "ck"], adv=True)

    # --bigkeys reports the largest key of each type; ties would be broken by
    # SCAN order, which depends on the per-process hash seed, so db 5 is seeded
    # with one unambiguous winner per type.
    with s.client() as c:
        c.cmd("SELECT", 5)
        c.cmd("FLUSHDB")
        c.cmd("SET", "big-string", "x" * 100)
        c.cmd("SET", "small-string", "x")
        c.cmd("RPUSH", "big-list", *[str(i) for i in range(10)])
        c.cmd("RPUSH", "small-list", "1")
        c.cmd("HMSET", "big-hash", *[x for i in range(20)
                                     for x in ("f%d" % i, str(i))])
        c.cmd("HSET", "small-hash", "f", "v")
        c.cmd("SADD", "big-set", *[str(i) for i in range(30)])
        c.cmd("SADD", "small-set", "one")
        c.cmd("ZADD", "big-zset", *[x for i in range(40)
                                    for x in (str(i), "m%d" % i)])
        c.cmd("ZADD", "small-zset", "1", "m")
    _cli(ctx, "bigkeys", H6 + ["-n", "5", "--bigkeys"], adv=True,
         drop=PROGRESS)
    _cli(ctx, "eval-file-missing",
         H6 + ["--eval", "/nonexistent.lua", ",", "1"], adv=True)
    lua = os.path.join(s.dir, "script.lua")
    srv.write_new(lua, "return {KEYS[1], ARGV[1]}\n", owner=False)
    _cli(ctx, "eval-file", H6 + ["--eval", lua, "kk", ",", "aa"], adv=True)

    pipe_payload = b"".join(
        b"*3\r\n$3\r\nSET\r\n$%d\r\npipe%d\r\n$1\r\n%d\r\n" % (
            len(b"pipe%d" % i), i, i % 10)
        for i in range(100))
    _cli(ctx, "pipe", H6 + ["--pipe"], stdin=pipe_payload, adv=True)
    _cli(ctx, "pipe-result", H6 + ["GET", "pipe99"], adv=True)

    rdbfile = os.path.join(s.dir, "from-cli.rdb")
    _cli(ctx, "rdb-dump", H6 + ["--rdb", rdbfile], adv=True)
    ctx.emit("cli/rdb-dump/file-present", os.path.exists(rdbfile), adv=True)

    # -- self-description ---------------------------------------------------
    _cli(ctx, "version", ["-v"])
    _cli(ctx, "help", ["--help"], adv=True)
    _cli(ctx, "bad-option", ["--no-such-option"], adv=True)
    _cli(ctx, "missing-port-value", ["-h", "::1", "-p"], adv=True)
    _cli(ctx, "non-numeric-port", ["-h", "::1", "-p", "notaport", "PING"],
         adv=True)
    _cli(ctx, "negative-port", ["-h", "::1", "-p", "-1", "PING"], adv=True)
    _cli(ctx, "auth-without-password", H6 + ["-a", "wrong", "PING"], adv=True)
    s.stop()
