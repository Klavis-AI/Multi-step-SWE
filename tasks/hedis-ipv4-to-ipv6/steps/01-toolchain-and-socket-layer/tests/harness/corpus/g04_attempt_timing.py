"""How many attempts the tree started, in what order, and how many at once.

`g03` reads which families the tree asked the kernel for, which is step 1's
question. This reads the `connect()` calls themselves, which is step 2's: RFC
8305 is not a statement about families but about attempts -- their order, their
overlap, and the ones that are never made because an earlier one succeeded.

`d02` asks the same things from outside the process, and has to spend a
duration to do it. Here they are counts:

  * **order** is the sequence of destinations, and interleaving is visible in it
    directly. `deep-tail.test` resolves to `::1, 127.0.0.1, 127.99.99.2,
    127.99.99.3, 127.99.99.4, 2001:db8::3`; interleaved by family starting with
    IPv6 it becomes `::1, 127.0.0.1, 2001:db8::3, 127.99.99.2, 127.99.99.3,
    127.99.99.4`. Nothing has to be timed to tell those two apart.
  * **how many attempts were made at all** is what says the Connection Attempt
    Delay is being honoured at the top of the list: when the first address
    connects at once, a tree that waits the delay before starting the second
    attempt never starts it, and a tree that fires the whole list at once
    starts every one of them. One `connect()` line, or five.
  * **how many were outstanding together** is what says the attempts were raced
    rather than walked quickly. An attempt is outstanding from a `connect()`
    that returned `EINPROGRESS` until its descriptor is closed, so a caller that
    finishes one attempt before starting the next reports one however long its
    address list is.

Two observations are durations after all, for the reason that sentence gives
backwards: a gap between two `connect()` lines of *one* process carries no
process startup, no fork and no exec, so it can be scored to within a factor of
two where a stopwatch held outside the process cannot. They pin the two halves of
section 5 that an order and an overlap leave open -- how long the delay is, and
how long a racing caller waits after the last attempt before abandoning the ones
still in flight. Both are reduced to a word here rather than recorded as a
number, because a recorded number would be a different value on every machine.

The addresses are as `strace` rendered the sockaddr the tree handed the kernel
-- `inet_ntop`'s text, not the tree's -- so nothing here moves when step 3
changes how the tree spells an address, and this gate can be scored unchanged
at every step from 2 onward.

Both connectors are covered, because the repository has two and they are
separate code. `redis-cli` and `redis-benchmark` reach the network through
`deps/hiredis/net.c`; the replication link reaches it through `src/anet.c`. A
tree that raced in one and not the other would pass half of this.

The IPv4 literal is here for the reason `g03` gives: "IPv6-only" means the
operator stopped being handed IPv4 by default, not that the operator can no
longer ask for it. A tree that scored this gate by removing IPv4 fails that
case at every step, this one included.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import dark  # noqa: E402
import srv  # noqa: E402
import tracer  # noqa: E402

PORTS = 7
FAMILY = "gate"

# `close` is in the traced set as well as `socket` and `connect`: without it an
# abandoned attempt looks like one that is still running, and `max_in_flight`
# would report the total rather than the peak.
CALLS = "socket,connect,close"

# The head of the interleaved order, darkened wherever a race has to be made to
# take its time. Named once because `delay_gaps` has to be told which attempts
# could not have completed, and a second copy of the list would be a second
# place for that to stop being true.
DARK_HEAD = ["::1", "127.0.0.1"]

# The same bound `d02` uses, and for the same reason: on a blackholed address
# the baseline waits out the kernel's SYN retries, and this is what stops that
# from being the scenario's runtime.
DEADLINE = 12.0

# The four boundaries that turn a gap between two `connect()` lines into a word.
# `tracer` hands back seconds and stops there, because what a number means is a
# question about a requirement rather than about a trace; the requirement is
# here, so the arithmetic is too.
#
# They are this tight -- the golden's 250 ms sits with less than a factor of two
# on either side -- because of what is being averaged over. A gap is measured
# between two lines *of one process*, with no fork, no exec and no process
# startup inside it, and it is only ever read as the MINIMUM over the several
# gaps of one race. A timer fires late and never early, so every gap in the race
# is at least the delay the tree meant to wait; a scheduling stall can push one
# of them up but nothing can push one of them down. The minimum is therefore the
# reading least disturbed by a stall, and it cannot fall below the truth. That is
# the whole justification for scoring a duration this narrowly, and it is why the
# same numbers would be wrong in `d02`, where the clock starts before a process
# does.
NO_GAP = 0.05      # below this the attempts went out together, delay or no delay
DELAY_LO = 0.15    # above this something was waited for
DELAY_HI = 0.45    # and below this it was the 250 ms of RFC 8305 section 5
LONG_HI = 1.2      # above this it is a timeout being waited out, not a delay


def _gap_word(seconds):
    """The word for a gap between two attempt starts, or for not having one.

    `none-measured` is the answer for a trace that carried no stamps and for a
    race with fewer than two attempts in it -- both are "this trace cannot say",
    which is a different answer from "the attempts started together" and must not
    be confused with it.
    """
    if seconds is None:
        return "none-measured"
    if seconds < NO_GAP:
        return "none"
    if seconds < DELAY_LO:
        return "short"
    if seconds < DELAY_HI:
        return "attempt-delay"
    if seconds < LONG_HI:
        return "long"
    return "very-long"


def _start(s, ready=None, deadline=15.0):
    s.start()
    try:
        s.wait_ready(deadline=deadline, host=ready)
        return True
    except Exception:  # noqa: BLE001 - a refusal to start is a result
        return False


def _emit_trace(ctx, label, path, port, traces):
    text = tracer.read(path)
    traces[label] = text
    ctx.emit("gate/he/%s/attempts" % label, tracer.attempts(text, port))
    ctx.emit("gate/he/%s/max-in-flight" % label,
             tracer.max_in_flight(text, port), adv=True)


def _emit_min_gap(ctx, label, port, traces, gaps=None):
    """Score the smallest gap between two attempt starts in one race.

    Beside `_emit_trace` rather than inside it: a count is worth having from
    every trace, but a duration is only worth having from a race whose shape
    makes it mean something, and the two races below were built to make it mean
    something different in each.

    `gaps` selects which reading: `tracer.start_gaps` for "was a delay charged
    at all", `tracer.delay_gaps` for "how long is it". The two are not
    interchangeable and each cell below says which it asks for.
    """
    gaps = (gaps or tracer.start_gaps)(traces[label], port)
    ctx.emit("gate/he/%s/min-gap" % label, _gap_word(min(gaps) if gaps else None))


def _traced_tool(ctx, label, binary, args, port, traces, timeout=DEADLINE):
    """Run one of the tree's clients under the tracer and score what it did."""
    path = os.path.join(ctx.workspace.sub("traces"), label + ".txt")
    launcher = tracer.launcher(path, calls=CALLS)
    if binary == "redis-benchmark":
        # Tracing slows the client enough that the benchmark's progress-timer
        # race (see `srv.run_benchmark`) is likely rather than rare, and each
        # attempt rewrites the same trace file, so what is read below is the
        # trace of the attempt whose rc is recorded.
        rc, _ = srv.run_benchmark(ctx.tree, args, timeout=timeout,
                                  launcher=launcher)
    else:
        rc, _ = srv.run_tool(ctx.tree, binary, args, timeout=timeout,
                             launcher=launcher)
    ctx.emit("gate/he/%s/rc" % label, rc)
    _emit_trace(ctx, label, path, port, traces)


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]
    traces = {}

    # -- the first address answers: the second attempt is never made ---------
    # `dual.test` has one address of each family and the server is on the one
    # the interleaved order puts first. A tree that honours the delay starts one
    # attempt; a tree that starts every address at once starts two. The count is
    # the whole observation, and no clock is involved in it.
    s0 = ctx.server("g4-live-head", p[0],
                    ctxmod.default_directives(p[0], "::1"), host="::1")
    ctx.emit("gate/he/live-head/server-up", _start(s0, "::1"))
    _traced_tool(ctx, "cli-live-head", "redis-cli",
                 ["-h", "dual.test", "-p", p[0], "PING"], p[0], traces)
    s0.stop()

    # -- the first address is a hole: the second attempt rescues it ----------
    # One attempt outstanding and never finishing, a second started beside it.
    # This is the pair of numbers the whole step is about: two attempts made,
    # two of them in flight together.
    with dark.Blackhole(["::1"], p[1]) as bh:
        ctx.emit("gate/he/dark-head/blackhole-ok", bh.all_dark)
        s1 = ctx.server("g4-dark-head", p[1],
                        ctxmod.default_directives(p[1], "127.0.0.1"),
                        host="127.0.0.1")
        ctx.emit("gate/he/dark-head/server-up", _start(s1, "127.0.0.1"))
        _traced_tool(ctx, "cli-dark-head", "redis-cli",
                     ["-h", "dual.test", "-p", p[1], "PING"], p[1], traces)
        # The load generator is the second connector's caller and does not wait
        # for its socket; at this step it races as well.
        _traced_tool(ctx, "bench-dark-head", "redis-benchmark",
                     ["-h", "dual.test", "-p", p[1], "-n", 4, "-c", 1,
                      "-t", "ping", "-q"], p[1], traces)
        # The literal an operator asked for by name. IPv4 is still reachable
        # when it is what was asked for, at this step and at every later one,
        # and this is the cell that says so: one attempt, and it is AF_INET.
        _traced_tool(ctx, "cli-v4-literal", "redis-cli",
                     ["-h", "127.0.0.1", "-p", p[1], "PING"], p[1], traces)
        s1.stop()

    # -- both heads are holes: the attempts stack up -------------------------
    # `multi.test` interleaves to `::1, 127.0.0.1, 2001:db8::1, 127.99.99.1,
    # 2001:db8::2`, and the server is on the fourth of those. Two holes stay
    # outstanding, the documentation address fails without ever reaching the
    # network, and the fourth attempt is the one that connects -- so four
    # attempts are made, the fifth never is, and three are in flight at once.
    with dark.Blackhole(DARK_HEAD, p[2]) as bh:
        ctx.emit("gate/he/deep-dark/blackhole-ok", bh.all_dark)
        s2 = ctx.server("g4-deep-dark", p[2],
                        ctxmod.default_directives(p[2], "127.99.99.1"),
                        host="127.99.99.1")
        ctx.emit("gate/he/deep-dark/server-up", _start(s2, "127.99.99.1"))
        _traced_tool(ctx, "cli-deep-dark", "redis-cli",
                     ["-h", "multi.test", "-p", p[2], "PING"], p[2], traces)
        # This is the cell that pins the delay itself rather than the order or
        # the overlap, and it is read with `delay_gaps` rather than `start_gaps`
        # because the interleaved order of `multi.test` is
        #
        #     ::1, 127.0.0.1, 2001:db8::1, 127.99.99.1, 2001:db8::2
        #
        # -- two holes, and then an address with no route out of this container,
        # which fails inside the call. Its successor is therefore started at
        # once and the gap in front of that one is ~0, so a minimum over every
        # gap reads `none` on a tree that is honouring the delay perfectly. The
        # two gaps that follow the holes are the timer, and `delay_gaps` is
        # given the holes by name because nothing in the trace identifies them:
        # `127.99.99.1` is unbound, so its attempt also returns `EINPROGRESS`
        # and then fails from the reset a fraction of a millisecond later. There
        # are two such gaps, so the minimum is still the reading least disturbed
        # by a stall, and a tree that charged the delay per address would read
        # the same word here -- which is what `cli-no-server` below is for.
        _emit_min_gap(ctx, "cli-deep-dark", p[2], traces,
                      gaps=lambda t, q: tracer.delay_gaps(t, q, DARK_HEAD))
        s2.stop()

    # -- nothing is listening: the whole list, in the order it was tried -----
    # The one observation that states the interleaving rule on its own. Every
    # address is attempted because none of them connects, so the recorded
    # sequence is the tree's ordering of the resolver's answer and nothing else.
    _traced_tool(ctx, "cli-no-server", "redis-cli",
                 ["-h", "deep-tail.test", "-p", p[3], "PING"], p[3], traces)
    # The same minimum over a race where every attempt fails immediately, which
    # asks a different question from the one above. The delay of section 5 is a
    # postponement of the *successor of a running attempt*: an attempt that has
    # already failed has no successor to postpone, so a tree that reads it that
    # way starts the next address at once and this reads `none`. A tree that
    # charges the delay as a toll per address waits it out six times over even
    # though nothing is pending, and reads `attempt-delay`. `d02` section 5 asks
    # this from outside with a stopwatch; here it is two lines of a trace.
    _emit_min_gap(ctx, "cli-no-server", p[3], traces)

    # -- nothing can win: the wait after the last attempt --------------------
    # Every cell above ends when an attempt connects or when the last one fails.
    # The delay section 5 asks for *after* the final attempt -- the interval a
    # racing caller waits before giving up on the ones still in flight -- ends in
    # neither, so it is invisible in all of them.
    #
    # `dual.test` resolves to exactly `::1` and `127.0.0.1`, so blackholing both
    # leaves the race nothing to win: both attempts stay outstanding, and what
    # the caller does next is the whole observation. `redis-benchmark` is a
    # caller that does not wait for its socket, so it is the one that shows it --
    # the connector starts both attempts, waits the delay, hands back the first
    # descriptor and closes the rest, and the gap from the last `connect()` to
    # that close is the wait. A caller that waits would still be inside
    # `connect()` and would close nothing.
    #
    # It has to be read this way round because the deployment has no
    # CAP_NET_ADMIN (see the blackhole's own note): a userspace blackhole cannot
    # make a handshake *slow*, only absent. `listen(0)` drops the SYN, and any
    # listener with a backlog completes the handshake in the kernel before
    # `accept()` is ever called, so there is no address that answers late. With
    # no slow-but-live address to race against, the only way to see the wait is
    # from a race that never finishes.
    with dark.Blackhole(DARK_HEAD, p[6]) as bh:
        ctx.emit("gate/he/all-dark/blackhole-ok", bh.all_dark)
        _traced_tool(ctx, "bench-all-dark", "redis-benchmark",
                     ["-h", "dual.test", "-p", p[6], "-n", 4, "-c", 1,
                      "-t", "ping", "-q"], p[6], traces)
        ctx.emit("gate/he/bench-all-dark/grace",
                 _gap_word(tracer.grace_gap(traces["bench-all-dark"], p[6])))

    # -- the other connector: a replication link, which does not wait --------
    # `redis-cli` reaches the network through the bundled client library; the
    # link a replica opens to its master reaches it through `src/anet.c`. The
    # master is four interleaved positions past two holes, so only a link that
    # races reaches it, and the trace says whether it did and with how many
    # attempts open at once.
    holes = ["::1", "127.0.0.1", "127.99.99.2", "127.99.99.3"]
    with dark.Blackhole(holes, p[5]) as bh:
        ctx.emit("gate/he/repl/blackhole-ok", bh.all_dark)
        m = ctx.server("g4-master", p[5],
                       ctxmod.default_directives(p[5], "127.99.99.4"),
                       host="127.99.99.4")
        ctx.emit("gate/he/repl/master-up", _start(m, "127.99.99.4"))
        r = ctx.server("g4-replica", p[4],
                       ctxmod.default_directives(p[4], "::1"), host="::1")
        path = os.path.join(r.dir, "strace.txt")
        r.launcher = tracer.launcher(path, calls=CALLS)
        tracer.start(r)
        up = False
        try:
            r.wait_ready(deadline=30.0, host="::1")
            with r.client(host="::1") as rc:
                rc.cmd("SLAVEOF", "deep-tail.test", str(p[5]))
                up = srv.wait_info(rc, "replication", "master_link_status",
                                   "up", deadline=DEADLINE)
        except Exception as e:  # noqa: BLE001 - a link that never came up is a
            ctx.note("gate/he/repl: %s" % type(e).__name__)  # result, not a stop
        ctx.emit("gate/he/repl/link-up", up, adv=True)
        # Let the tracer see the whole of whatever race was running.
        time.sleep(0.5)
        tracer.stop(r)
        _emit_trace(ctx, "repl", path, p[5], traces)
        m.stop()

    # The instrument's own self-check, scored with the rest: a tracer that
    # produced nothing looks exactly like a program that opened nothing.
    ctx.emit("gate/he/instrument-ok", tracer.failures(traces))
