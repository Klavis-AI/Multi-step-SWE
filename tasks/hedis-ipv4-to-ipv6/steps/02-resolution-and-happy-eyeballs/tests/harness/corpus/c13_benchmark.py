"""redis-benchmark: the third client in the tree, and its own default host.

`redis-benchmark` resolves a host, connects, and -- when it cannot -- prints the
address back out. It also carries its own hard-coded default host, separate from
the one `redis-cli` carries, and prints that default in its usage text. All
three of those are address handling, and none of them is reachable from the
server-side scenarios.

Every run is checked against the server it claimed to drive: a benchmark that
printed a rate without connecting would otherwise read as a success.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import norm  # noqa: E402
import srv  # noqa: E402

PORTS = 2
FAMILY = "behaviour"

# The measurements in a benchmark report: a rate, an elapsed time and a latency
# histogram. A rate is printed as a fixed-point number, as `inf` when the run
# was too quick to time, and as `nan` when nothing has been measured yet, so all
# three spellings are one alternation. Nothing here touches a host, a port or a
# test name.
NUM = r"(?:[\d.]+|-?inf|-?nan)"
RATE = re.compile(r"^%s requests per second$" % NUM)
QRATE = re.compile(r"^(\S+): %s requests per second$" % NUM)
# The live counter `showThroughput` overwrites with a carriage return; each
# update is a progress reading and none of them is an outcome.
PROGRESS = re.compile(r"^\S+: %s$" % NUM)
PERCENTILE = re.compile(r"^[\d.]+% <= [\d.]+ milliseconds$")
ELAPSED = re.compile(r"in [\d.]+ seconds")
CSV_ROW = re.compile(r'^("[^"]*"),"%s"$' % NUM)


def _clean(out):
    """A benchmark report with its measurements removed and its text kept."""
    text = norm.normalize_text(out).replace("\r", "\n")
    text = ELAPSED.sub("in <T> seconds", text)
    kept = []
    for line in text.splitlines():
        if PROGRESS.match(line) or PERCENTILE.match(line):
            continue
        line = QRATE.sub(r"\1: <RPS> requests per second", line)
        line = RATE.sub("<RPS> requests per second", line)
        line = CSV_ROW.sub(r'\1,"<RPS>"', line)
        kept.append(line)
    return "\n".join(kept)


def _bench(ctx, label, args, port_in_use=None, timeout=60, adv=True,
           dbsize=None):
    """Run the benchmark and record what it printed and what it did.

    `dbsize` is the client the run is checked through: a benchmark that fails to
    connect still exits, and the only difference between "connected and wrote"
    and "printed something and gave up" is on the server.
    """
    rc, out = srv.run_benchmark(ctx.tree, args, timeout=timeout)
    ctx.emit("bench/%s/rc" % label, rc, adv=adv)
    ctx.emit_text("bench/%s/out" % label, _clean(out), adv=adv)
    if dbsize is not None:
        ctx.emit_reply("bench/%s/dbsize" % label, dbsize.cmd("DBSIZE"), adv=adv)
        dbsize.cmd("FLUSHALL")


def run(ctx):
    port = ctx.port(0)
    closed = ctx.port(1)          # deliberately never listened on
    sock = os.path.join(ctx.workspace.base, "bench.sock")
    s = ctx.server("bench", port,
                   ctxmod.default_directives(port,
                                             extra=["unixsocket %s" % sock]))
    s.start().wait_ready()
    c = s.client()
    c.cmd("SELECT", 0)
    c.cmd("FLUSHALL")

    # -- the host forms ----------------------------------------------------
    # `-n 10 -c 2 -t set` writes exactly one key, so the server can confirm the
    # run reached it.
    common = ["-p", port, "-n", 10, "-c", 2, "-t", "set", "-q"]
    for label, host in (("v6-literal", "::1"),
                        ("bracketed", "[::1]"),
                        ("scoped", "::1%lo"),
                        ("expanded", "0:0:0:0:0:0:0:1"),
                        ("v4-mapped", "::ffff:127.0.0.1"),
                        ("v4-literal", "127.0.0.1"),
                        ("hostname", "localhost"),
                        ("unspecified", "::"),
                        ("unresolvable", "no-such-host.invalid")):
        _bench(ctx, "host-%s" % label, ["-h", host] + common, dbsize=c)

    # The default host is a compiled-in constant, not a resolution: with no
    # `-h` at all the benchmark goes wherever the tree tells it to.
    _bench(ctx, "host-default", common, dbsize=c)

    # -- reporting -----------------------------------------------------------
    _bench(ctx, "report-full", ["-h", "::1", "-p", port, "-n", 20, "-c", 2,
                                "-t", "set,get"], adv=False, dbsize=c)
    _bench(ctx, "report-csv", ["-h", "::1", "-p", port, "-n", 20, "-c", 2,
                               "-t", "set,get", "--csv"], adv=False, dbsize=c)
    _bench(ctx, "report-pipelined", ["-h", "::1", "-p", port, "-n", 100, "-c",
                                     4, "-P", 8, "-t", "set", "-q"], adv=False,
           dbsize=c)
    # 200 draws over a 4-key space: the final DBSIZE is 4 unless the run never
    # reached the server, which is the distinction being recorded.
    _bench(ctx, "report-keyspace", ["-h", "::1", "-p", port, "-n", 200, "-c", 2,
                                    "-r", 4, "-t", "set", "-q"], adv=False,
           dbsize=c)
    _bench(ctx, "report-payload", ["-h", "::1", "-p", port, "-n", 20, "-c", 2,
                                   "-d", 64, "-t", "set", "-q"], adv=False,
           dbsize=c)
    _bench(ctx, "report-dbnum", ["-h", "::1", "-p", port, "-n", 20, "-c", 2,
                                 "--dbnum", 3, "-t", "set", "-q"], adv=True)
    with s.client() as c3:
        c3.cmd("SELECT", 3)
        ctx.emit_reply("bench/report-dbnum/db3-size", c3.cmd("DBSIZE"))
        c3.cmd("FLUSHALL")

    # -- where it fails, and what it says about the address ------------------
    _bench(ctx, "closed-port", ["-h", "::1", "-p", closed, "-n", 1, "-c", 1,
                                "-t", "ping"])
    _bench(ctx, "closed-port-bracketed", ["-h", "[::1]", "-p", closed, "-n", 1,
                                          "-c", 1, "-t", "ping"])
    _bench(ctx, "closed-port-v4", ["-h", "127.0.0.1", "-p", closed, "-n", 1,
                                   "-c", 1, "-t", "ping"])
    _bench(ctx, "missing-socket", ["-s", "/nonexistent/redis.sock", "-n", 1,
                                   "-c", 1, "-t", "ping"])

    # -- argument handling ---------------------------------------------------
    _bench(ctx, "usage", ["--nosuchoption"])
    _bench(ctx, "bad-port", ["-h", "::1", "-p", "notaport", "-n", 1, "-c", 1,
                             "-t", "ping"])
    # Not `-c 0`: with no clients at all, whether the run finishes or aborts is
    # decided by whether it beat `showThroughput`'s 250ms timer, and it is a
    # different answer on a different day. A degenerate *address* is the same
    # kind of probe and has one answer.
    _bench(ctx, "empty-host", ["-h", "", "-p", port, "-n", 1, "-c", 1,
                               "-t", "ping"])
    # The one transport with no address in it at all. `missing-socket` above
    # only reaches the error path; this reaches the connect.
    _bench(ctx, "live-socket", ["-s", sock, "-n", 10, "-c", 2, "-t", "set",
                                "-q"], adv=False, dbsize=c)
    _bench(ctx, "missing-host-argument", ["-h"])
    _bench(ctx, "unknown-test", ["-h", "::1", "-p", port, "-n", 1, "-c", 1,
                                 "-t", "nosuchtest", "-q"])

    ctx.emit_reply("bench/server-still-up", c.cmd("PING"))
    c.close()
    s.stop()
