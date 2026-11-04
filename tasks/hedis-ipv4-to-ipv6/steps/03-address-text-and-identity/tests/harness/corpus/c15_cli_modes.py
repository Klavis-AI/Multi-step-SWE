"""redis-cli's continuous modes: the ones that never return on their own.

`c09_cli` covers the invocations that run a command and exit. This covers the
seven that do not -- `--stat`, `--latency`, `--latency-history`,
`--latency-dist`, `--lru-test`, `--intrinsic-latency` and `--slave` -- which
between them are a quarter of `redis-cli.c` and every line of it that the
migration has to keep working while the address it connects to changes family.

Two problems have to be solved to score them at all.

The first is stopping them. Each is run with a deadline and then killed, which
means the output is whatever had been written by then -- and a killed process
flushes nothing, so `stdbuf -oL` puts the CLI's stdout in line-buffered mode and
the loss is bounded to a partial last line.

The second is that their output is a measurement: latencies, rates, byte
counts, all different on every run. What is *not* different is the shape --
which columns, in which order, with which labels and separators. `_shape()`
masks every digit run to `#` and keeps the rest verbatim, so `min: 0, max: 3,
avg: 0.05 (61 samples)` records as `min: #, max: #, avg: #.## (# samples)`. A
mode that stopped producing output, lost a column, or started reporting an
error scores wrong; a mode that merely measured something different does not.

`--slave` is the exception and the most valuable of them: after the sync banner
its output is the replication stream itself, which for a known sequence of
writes is exactly determined. It is scored as text.
"""

import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import srv  # noqa: E402

PORTS = 2
FAMILY = "behaviour"

# Line-buffer the CLI's stdout. Without this a mode killed at its deadline has
# written nothing at all: a pipe is block-buffered, the modes emit well under
# 4 KiB, and SIGKILL does not flush.
LINEBUF = ["stdbuf", "-oL", "-eL"]

ANSI_COLOUR = re.compile(r"\x1b\[[0-9;]*m")
ANSI_MOVE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
DIGITS = re.compile(r"\d+")
SPACES = re.compile(r"[ \t]+")
# A masked number carrying a scale suffix: `1023K` and `1.05M` are the same
# measurement written at two scales, and `--stat` picks the scale from the
# value. Masking the digits alone would leave `#K` and `#.#M` looking like
# different output.
SCALED = re.compile(r"#(?:\.#)?[KMGTB](?![A-Za-z0-9])")


def _shape(data):
    """`data` with terminal control removed, numbers masked, columns collapsed.

    What survives is the part of the output the tree chose -- labels, column
    order, punctuation -- and not the part the machine happened to measure.
    Three things are taken out, each for its own reason:

      * colour, which says nothing about behaviour;
      * cursor movement, which is how `--latency` rewrites its one line in
        place. Treating a move as a line break is what turns a three-second run
        into one repeated row instead of one very long row whose length is a
        stopwatch reading;
      * runs of spaces. `--stat` pads each column to a fixed width, so masking
        `1.05K` and `1.05M` to `#.#K` and `#.#M` leaves padding that still
        differs by the width of the number that was there. The column *order*
        is the behaviour; the column widths are the measurement.
    """
    text = data.decode("utf-8", "replace")
    text = ANSI_COLOUR.sub("", text)
    text = ANSI_MOVE.sub("\n", text)
    text = text.replace("\r", "\n")
    return SPACES.sub(" ", SCALED.sub("#", DIGITS.sub("#", text)))


def _lines(data):
    """The distinct non-blank shapes in `data`, in first-seen order.

    A mode that prints the same row once a second prints it as many times as
    the deadline allowed, which is a stopwatch reading, not a behaviour.
    """
    seen, out = set(), []
    for line in _shape(data).splitlines():
        line = line.rstrip()
        if line and line not in seen:
            seen.add(line)
            out.append(line)
    return "\n".join(out)


def _mode(ctx, label, args, seconds, adv=False):
    """Run one continuous mode until its deadline and record its shape."""
    rc, out = srv.run_tool(ctx.tree, "redis-cli", args, timeout=seconds,
                           launcher=LINEBUF)
    ctx.emit("cli/%s/rc" % label, rc, adv=adv)
    ctx.emit_text("cli/%s/shape" % label, _lines(out), adv=adv)
    ctx.emit("cli/%s/produced-output" % label, len(out) > 0, adv=adv)
    return out


def _slave_stream(ctx, s, port):
    """`--slave`, with a known sequence of writes pushed through it.

    stdout and stderr go to separate files on purpose: the banner is written to
    stderr unbuffered while the stream is line-buffered on stdout, so merging
    them would record whichever won the race.
    """
    d = ctx.workspace.sub("slave")
    # `d` is the score uid's: out/err are read back with `srv.open_regular`
    # (no link followed, a FIFO does not block, raises where open() raised),
    # opened afresh on each poll so appended output is seen.
    op = os.path.join(d, "out")
    ep = os.path.join(d, "err")
    argv = LINEBUF + [ctx.tree.bin("redis-cli"), "-h", "::1", "-p", str(port),
                      "--slave"]
    # Created without following anything in `d`, which the score uid owns.
    with os.fdopen(srv.open_new(op, owner=False), "wb") as fo, \
            os.fdopen(srv.open_new(ep, owner=False), "wb") as fe:
        p = subprocess.Popen(argv, stdout=fo, stderr=fe,
                             preexec_fn=ctx.tree.preexec)
        try:
            # Wait for the initial transfer to finish before writing anything,
            # so the writes are in the stream rather than in the snapshot.
            end = time.time() + 30
            while time.time() < end:
                with srv.open_regular(ep) as f:
                    if b"SYNC done" in f.read():
                        break
                time.sleep(0.05)
            with s.client() as c:
                c.cmd("SELECT", 7)
                c.cmd("SET", "slave-key", "slave-value")
                c.cmd("INCRBY", "slave-counter", 5)
                c.cmd("RPUSH", "slave-list", "a", "b")
                c.cmd("DEL", "slave-key")
                # The sentinel the wait below watches for. It is a write like
                # the others and it is deliberately last: the replication stream
                # is ordered, so a run that has seen this one has seen all of
                # them. Waiting on any earlier write instead leaves whether the
                # rest arrived up to the scheduler, which is how this
                # observation came back different on two passes of the same
                # tree -- the `DEL` was in one and not the other.
                c.cmd("SET", "slave-end", "1")
            # The stream is asynchronous; give it a moment to arrive rather
            # than racing it.
            deadline = time.time() + 10
            while time.time() < deadline:
                with srv.open_regular(op) as f:
                    if b"slave-end" in f.read():
                        break
                time.sleep(0.05)
        finally:
            p.terminate()
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait(timeout=5)
    with srv.open_regular(op) as f:
        out = f.read()
    with srv.open_regular(ep) as f:
        err = f.read()
    # The banner carries the size of the RDB payload, which depends on the
    # dataset; its shape does not.
    ctx.emit_text("cli/slave/banner", _lines(err))
    ctx.emit_text("cli/slave/stream", _shape(out).strip())


def run(ctx):
    port = ctx.port(0)
    s = ctx.server("modes", port,
                   ctxmod.default_directives(port, "127.0.0.1 ::1", extra=[
                       # Keep the master from injecting a periodic PING into
                       # the replication stream while `--slave` is watching it.
                       "repl-ping-slave-period 300",
                   ]))
    s.start().wait_ready()
    H = ["-h", "::1", "-p", port]

    # -- the rolling server statistics --------------------------------------
    # `-i 0.1` so the deadline buys twenty or so rows rather than three.
    _mode(ctx, "stat", H + ["--stat", "-i", "0.1"], seconds=3)

    # -- the three latency views ---------------------------------------------
    _mode(ctx, "latency", H + ["--latency", "-i", "1"], seconds=3)
    _mode(ctx, "latency-history", H + ["--latency-history", "-i", "1"],
          seconds=4)
    _mode(ctx, "latency-dist", H + ["--latency-dist", "-i", "1"], seconds=3)

    # Intrinsic latency never talks to a server at all; it is the one mode that
    # ends by itself, so its exit status is part of the observation.
    _mode(ctx, "intrinsic-latency", ["--intrinsic-latency", "1"], seconds=20)

    # -- the cache-workload simulator ----------------------------------------
    _mode(ctx, "lru-test", H + ["-n", "9", "--lru-test", "100"], seconds=5,
          adv=True)
    with s.client() as c:
        c.cmd("SELECT", 9)
        # The keys it wrote are random; that it wrote any is not.
        ctx.emit("cli/lru-test/wrote-keys", c.cmd("DBSIZE")[1] > 0, adv=True)
        c.cmd("FLUSHDB")

    # -- the replication stream ----------------------------------------------
    _slave_stream(ctx, s, port)

    # -- the same modes over IPv4 and over a name -----------------------------
    # Adversarial: every mode above reaches the server over IPv6. A migration
    # that reaches one family from one code path and the other from another
    # shows up as a mode that works on `::1` and not on `127.0.0.1`.
    _mode(ctx, "stat-v4",
          ["-h", "127.0.0.1", "-p", port, "--stat", "-i", "0.1"], seconds=3,
          adv=True)
    _mode(ctx, "latency-hostname",
          ["-h", "localhost", "-p", port, "--latency", "-i", "1"], seconds=3,
          adv=True)

    # -- a continuous mode pointed at nothing ---------------------------------
    # Adversarial: these modes each have their own connect path, and the error
    # they produce when there is no server is the part of it a family change is
    # most likely to break.
    dead = ctx.port(1)
    _mode(ctx, "stat-refused",
          ["-h", "::1", "-p", dead, "--stat", "-i", "0.1"], seconds=5, adv=True)
    _mode(ctx, "latency-refused",
          ["-h", "::1", "-p", dead, "--latency", "-i", "1"], seconds=5,
          adv=True)
    _mode(ctx, "slave-refused", ["-h", "::1", "-p", dead, "--slave"],
          seconds=5, adv=True)

    s.stop()
