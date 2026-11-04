"""What families the tree actually asks the kernel for.

`g02` reads the source; this reads the syscalls. The two disagree in exactly
the case that matters: source that names no family at all still picks one, at
run time, out of whatever the resolver handed back and in whatever order the
code decided to walk it. A tree can pass every static check and still open an
IPv4 socket for a name that had an IPv6 address all along.

The instrument is `strace` over the tree's own processes, and the line it
prints -- `socket(AF_INET6, SOCK_STREAM, ...)` -- is the fact. `lib/tracer.py`
has the details of collecting it without losing half of it.

This gate is scored at every step, so every observation in it has to mean the
same thing at every step. That rules more out than it sounds like:

  * an IPv6 literal fixes both ends -- only an IPv6 socket may ever appear, and
    `127.0.0.1` must stay refused, which is the `IPV6_V6ONLY` half;
  * an IPv4 literal likewise -- "IPv6-only" means the operator stopped being
    handed IPv4 by default, not that the operator can no longer ask for it, so
    an IPv4 socket must still appear here at every step including the last. A
    tree that scores this gate by deleting IPv4 support fails this case;
  * a name with an address of each family fixes only the IPv6 end. Whether such
    a name should *also* still get an IPv4 listener is step 6's subject, so
    nothing here reads IPv4 for that name -- asserting it either way would make
    this gate contradict a later step's instruction.

The baseline resolves such a name for IPv4 alone: it binds `127.0.0.1`, leaves
`[::1]` refusing connections, and opens no `AF_INET6` socket at all.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402
import tracer  # noqa: E402

PORTS = 4
FAMILY = "gate"


def _reachable(host, port, timeout=5.0):
    """Whether a PING gets an answer at this address."""
    try:
        with resp.Client(host, port, timeout=timeout) as c:
            return c.cmd("PING")[1] == b"PONG"
    except Exception:
        return False


def _settle(server, hosts, port, deadline=120.0):
    """Wait until any of `hosts` answers, and never raise if none does.

    A scenario that raised while waiting would lose every observation after the
    wait, so a tree that listens on fewer addresses than expected would score
    the same as one that failed to build. Here it scores the addresses it did
    bring up and loses only the ones it did not.

    Two things end the wait early, and between them the deadline is almost
    never what ends it: the server answering, and the server having exited. A
    server that is going to come up comes up in well under a second, and one
    that is never going to come up is gone. What is left for the deadline to
    cover is a machine so loaded that a started process has not been scheduled
    yet, and the cost of being generous about that is nothing, because nothing
    waits the deadline out. It is set far above anything observed rather than
    close to it: a number tight enough to be informative is a number that
    expires on a busy host, and then the recording says the tree stopped
    listening on an address it is still listening on.
    """
    end = time.time() + deadline
    while time.time() < end:
        for h in hosts:
            if _reachable(h, port, timeout=1.0):
                return True
        if server.proc is not None and server.proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


def _v6_listeners(ctx, label, text):
    """How many IPv6 listening sockets there are, and how many were restricted.

    "IPV6_V6ONLY appears in this trace" is the cheap form of this question and
    it answers the wrong one: a process can set the option on a socket it then
    throws away, and a process with two IPv6 listeners can set it on one of
    them. What the step requires is every IPv6 listening socket restricted
    before it is bound, which is a fact about descriptors.
    """
    bound, restricted = tracer.v6_listeners(text)
    ctx.emit("gate/strace/%s/v6-listeners" % label, bound)
    ctx.emit("gate/strace/%s/v6-listeners-restricted" % label, restricted)


def _traced_server(ctx, label, port, bindaddr):
    """A started, traced server, and the path its trace is being written to."""
    s = ctx.server(label, port, ctxmod.default_directives(port, host=bindaddr),
                   host=bindaddr)
    path = os.path.join(s.dir, "strace.txt")
    # `bind` is in the traced set as well as `socket`, so the option can be
    # attributed to the descriptor it was set on: see `_v6_listeners` below.
    s.launcher = tracer.launcher(path, calls="socket,setsockopt,bind")
    tracer.start(s)
    return s, path


def run(ctx):
    both = ["::1", "127.0.0.1"]
    traces = {}

    # -- an IPv6 literal: one family in, one family out ----------------------
    p0 = ctx.port(0)
    s0, t0 = _traced_server(ctx, "bind-v6-literal", p0, "::1")
    _settle(s0, ["::1"], p0)
    ctx.emit("gate/strace/bind-v6-literal/reachable-v6", _reachable("::1", p0))
    # Adversarial: an IPv6 listener that answers on 127.0.0.1 is a dual-stack
    # socket without IPV6_V6ONLY, which is IPv4 service the operator did not
    # ask for.
    ctx.emit("gate/strace/bind-v6-literal/reachable-v4",
             _reachable("127.0.0.1", p0), adv=True)
    tracer.stop(s0)
    traces["bind-v6-literal"] = text0 = tracer.read(t0)
    fam0 = tracer.families(text0)
    ctx.emit("gate/strace/bind-v6-literal/af-inet", "AF_INET" in fam0, adv=True)
    ctx.emit("gate/strace/bind-v6-literal/af-inet6", "AF_INET6" in fam0)
    _v6_listeners(ctx, "bind-v6-literal", text0)

    # -- a name with an address of each family -------------------------------
    p1 = ctx.port(1)
    s1, t1 = _traced_server(ctx, "bind-dual-name", p1, "dual.test")
    _settle(s1, both, p1)
    ctx.emit("gate/strace/bind-dual-name/reachable-v6", _reachable("::1", p1))
    tracer.stop(s1)
    traces["bind-dual-name"] = text1 = tracer.read(t1)
    ctx.emit("gate/strace/bind-dual-name/af-inet6",
             "AF_INET6" in tracer.families(text1))
    _v6_listeners(ctx, "bind-dual-name", text1)

    # -- an IPv4 literal: the operator asked for IPv4 and must still get it ---
    # The first four here are adversarial. They are what stands between this
    # gate and a tree that scores it by making IPv4 unreachable.
    p2 = ctx.port(2)
    s2, t2 = _traced_server(ctx, "bind-v4-literal", p2, "127.0.0.1")
    _settle(s2, ["127.0.0.1"], p2)
    ctx.emit("gate/strace/bind-v4-literal/reachable-v4",
             _reachable("127.0.0.1", p2), adv=True)
    ctx.emit("gate/strace/bind-v4-literal/reachable-v6",
             _reachable("::1", p2), adv=True)
    tracer.stop(s2)
    traces["bind-v4-literal"] = text2 = tracer.read(t2)
    fam2 = tracer.families(text2)
    ctx.emit("gate/strace/bind-v4-literal/af-inet", "AF_INET" in fam2, adv=True)
    ctx.emit("gate/strace/bind-v4-literal/af-inet6", "AF_INET6" in fam2, adv=True)
    _v6_listeners(ctx, "bind-v4-literal", text2)

    # The instrument's own self-check, scored with the rest: every traced
    # server has to have left a trace with a socket call in it. Without this, a
    # tracer that produced nothing reads as a tree that opened nothing.
    ctx.emit("gate/strace/instrument-ok", tracer.failures(traces))

    # -- the bundled clients, against a server reachable both ways ------------
    p3 = ctx.port(3)
    s3 = ctx.server("clients", p3,
                    ctxmod.default_directives(p3, host="dual.test"),
                    host="127.0.0.1")
    s3.start()
    _settle(s3, both, p3)
    d = ctx.workspace.sub("traces")
    for label, binary, args in (
            ("cli-dual-name", "redis-cli",
             ["-h", "dual.test", "-p", p3, "ping"]),
            ("cli-v6-literal", "redis-cli", ["-h", "::1", "-p", p3, "ping"]),
            ("bench-dual-name", "redis-benchmark",
             ["-h", "dual.test", "-p", p3, "-n", 4, "-c", 1, "-t", "ping",
              "-q"])):
        path = os.path.join(d, label + ".txt")
        # Tracing slows the client enough that the benchmark's progress-timer
        # race (see `srv.run_benchmark`) is likely rather than rare, and each
        # attempt rewrites the same trace file, so the trace read below is the
        # trace of the attempt whose rc is recorded.
        if binary == "redis-benchmark":
            rc, _ = srv.run_benchmark(
                ctx.tree, args, timeout=60,
                launcher=tracer.launcher(path, calls="socket"))
        else:
            rc, _ = srv.run_tool(ctx.tree, binary, args, timeout=60,
                                 launcher=tracer.launcher(path, calls="socket"))
        ctx.emit("gate/strace/%s/rc" % label, rc)
        # Only the presence of the IPv6 socket is scored. Whether a client also
        # opens an IPv4 socket while racing the two families is step 2's
        # subject, and scoring it here would make this gate mean one thing at
        # step 1 and another at step 2.
        ctx.emit("gate/strace/%s/af-inet6" % label,
                 "AF_INET6" in tracer.families(tracer.read(path)))
    s3.stop()
