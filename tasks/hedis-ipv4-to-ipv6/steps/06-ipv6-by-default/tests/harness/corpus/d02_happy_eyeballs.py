"""An address that answers nothing costs one delay, not one connect timeout.

This is the behaviour half of step 2, and the whole of it turns on a
distinction step 1 could not make. Step 1 required a caller to try the
addresses a name resolved to until one of them connected. Against addresses
that *refuse*, a caller that walks the list one attempt at a time satisfies
that in microseconds and is indistinguishable, from outside, from a caller that
races them. The difference only shows against an address that answers nothing
at all: the sequential caller sits inside one `connect()` until the kernel gives
up on the SYN, while the racer has the next family's attempt running beside it a
Connection Attempt Delay later. `lib/dark.py` is how such an address is produced
here without `CAP_NET_ADMIN`, and its self-check is section 1.

What that buys, and what each section asks:

  1. the instrument itself, measured in the same run against known answers;
  2. one family's head blackholed and the other family live -- the shape RFC
     8305 exists for;
  3. *both* families' heads blackholed and a live address deeper in the list,
     which is the shape neither the baseline nor step 1 can reach at all: the
     baseline hangs on the IPv4 head it is the only one it asked for, step 1
     hangs on the IPv6 head the resolver put first, and only a caller running
     attempts beside each other gets past both;
  4. which attempt wins when more than one could, and that the winner is the
     one started first rather than the one that happened to finish first;
  5. that an attempt which *fails* does not hold the next one back -- the delay
     is how long a running attempt postpones its successor, not a fixed
     per-address toll;
  6. the callers that do not wait for their connection, which at this step race
     as well, and the server's own responsiveness while they do;
  7. what does not change: one address is still one attempt, a name with
     nothing behind it still fails, the Unix socket is still not an internet
     address, and a source address asked for as a requirement is still a
     requirement -- an attempt that cannot bind it fails rather than proceeding
     without it.

**Three observations in four are clock-free** (76 of 100), and that is
deliberate: what answered, which server answered, and whether a link came up
are facts that do not move when the host is loaded. But the Connection Attempt
Delay is a duration and no amount of cleverness makes it anything else, so
where a cell has to say "how long", it says it in one of four words with
boundaries an order of magnitude apart:

    immediate   under 0.15 s    -- every attempt was started at once
    delays      0.15 s to 4 s   -- attempts were staggered
    slow        4 s and up      -- staggered by far more than a delay
    timeout     never answered

The golden tree lands at 0.5 s and 1.0 s in the cells that use them -- two and
four delays -- so `delays` has a factor of three below it and a factor of four
above. A tree that fires every address at once reads `immediate`, one that
waits a second per address reads `slow`, and one that does not race at all
reads `timeout`. `g04_attempt_timing` answers the same questions from the
syscalls, where they are counts rather than durations, and between the two of
them no cell has to be read tightly.

The first boundary moves to 0.6 s for every cell whose *expected* answer is
`immediate`, wherever in the module it sits. Those are the cells where 0.15 s is
a thin margin around a reading that contains a whole `redis-cli` process --
fork, exec, dynamic linking, resolution -- and where the failure they exist to
catch is "much slower or never" rather than "one delay slower", so a boundary
four times looser still catches it. It cannot move for *every* cell, because the
ones that must read `delays` land at 0.5 s and would fall under it;
`PROCESS_IMMEDIATE` says which cells get which boundary and why.

**No cell here records the text of an address**, for the reason `d01` gives:
that is step 3's subject. What is recorded is which server answered, whether
there was a reply at all, and one word for the failure when there was not.

The names come from `fixtures/hosts`, and two of them are load-bearing:

    multi.test      ::1, 127.0.0.1, 127.99.99.1, 2001:db8::1, 2001:db8::2
    deep-tail.test  ::1, 127.0.0.1, 127.99.99.2, 127.99.99.3, 127.99.99.4,
                    2001:db8::3

Interleaved by family starting with IPv6 (RFC 8305 section 4), those two become

    multi.test      ::1, 127.0.0.1, 2001:db8::1, 127.99.99.1, 2001:db8::2
    deep-tail.test  ::1, 127.0.0.1, 2001:db8::3, 127.99.99.2, 127.99.99.3,
                    127.99.99.4

so the documentation-prefix addresses, which have no route out of this
container and fail instantly, move from the tail of each list into the middle
of it. That is what section 5 uses: an attempt that fails instantly in the
middle of a list is free only if failure releases the next attempt at once.
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, os.pardir, "lib"))
import ctx as ctxmod  # noqa: E402
import dark  # noqa: E402
import resp  # noqa: E402
import srv  # noqa: E402

PORTS = 10
FAMILY = "behaviour"

# How long a client is given before it is called hung. A loopback connection
# that is going to happen happens in milliseconds and the longest race here is
# four delays, so this is a factor of twelve above anything the step asks for;
# what it actually bounds is how long the *baseline* takes to fail, since on a
# blackholed address the baseline waits for the kernel's SYN retries -- and
# since a dozen cells here are ones the baseline and step 1 can only fail, this
# number is most of what the scenario costs to run against them.
DEADLINE = 12.0

# What a tool that never got a connection records in place of a reply -- the
# same convention, and the same reason, as `d01.NO_REPLY`.
NO_REPLY = "<no-reply>"

# The four words, and the boundaries the module docstring justifies.
IMMEDIATE = 0.15
STAGGERED = 4.0

# The first boundary for the cells whose expected answer is `immediate`. What is
# timed in those is a whole `redis-cli` process: fork, exec, dynamic linking,
# resolution and a race that is supposed to add nothing measurable to any of
# them. A reading of ~30 ms has all of that in it already, and the margin to
# 0.15 s is one scheduling hiccup wide -- a hiccup this module has actually
# taken, on `same/no-such-name` during a full-corpus replay, where the cell read
# `delays` and cost a regression cell that distinguishes nothing.
#
# Widening the shared `IMMEDIATE` instead is not available, and two cells are
# the whole reason: `dark/v6-dark/cli` and `win/v6-dark` each read `immediate`
# on the baseline and `delays` at 0.5 s on the step-2 and step-3 trees, so a
# first boundary above 0.5 s would collapse those two answers into one and
# destroy both. So a cell keeps `IMMEDIATE` when 0.15 s separates two answers
# some tree actually gives, and takes `PROCESS_IMMEDIATE` when `immediate` is
# the answer *every* tree gives and the only failure worth catching is an order
# of magnitude away:
#
#     IMMEDIATE (0.15 s)        dark/{v4,v6}-dark/cli, win/v6-dark,
#                               same/single-dark-name, and every cell whose
#                               answer is never `immediate` on any tree
#     PROCESS_IMMEDIATE (0.6)   dark/{v4,v6}-dark/cli-live-literal,
#                               win/both-live, same/no-such-name,
#                               fail-fast/{tail,multi,no-server}
#
# Section 5's failing answer is a per-address toll of a delay each over six
# addresses -- ~1.3 s -- so even there 0.6 keeps a factor of two under the value
# it has to stay below. The four cells that joined the list have no such
# neighbouring value at all: nothing any tree does to them lands between 0.15 s
# and 4 s, which is why widening them takes nothing away.
PROCESS_IMMEDIATE = 0.6

# Section 7's cluster cell. The bus is the one caller that asks for its source
# address as a *requirement*, and these are what let the answer be read quickly:
# a node timeout of 3 s makes the handshake timeout 3 s too (it is the larger of
# the node timeout and one second), so a peer that was never connected to is
# dropped from the node table at that point and the answer cannot change after
# it.
CLUSTER_DIRECTIVES = ("cluster-enabled yes",
                      "cluster-config-file nodes.conf",
                      "cluster-node-timeout 3000")

# Comfortably past that, and bounded: this cell's answer is "never", so the wait
# is the cell's whole runtime on every tree and cannot be spent twice.
LINK_WAIT = 5.0


def _bucket(seconds, immediate=IMMEDIATE):
    if seconds is None:
        return "timeout"
    if seconds < immediate:
        return "immediate"
    if seconds < STAGGERED:
        return "delays"
    return "slow"


def _start(s, ready=None, deadline=15.0):
    """Start a server and say whether it came up, never raising if it did not.

    `d01._start`'s reason applies unchanged: a scenario that raised here would
    lose every observation after the failure, so a tree that brought up one
    fewer listener would score the same as one that did not build.
    """
    s.start()
    try:
        s.wait_ready(deadline=deadline, host=ready)
        return True
    except Exception:  # noqa: BLE001 - a refusal to start is a result
        return False


def _timed_cli(ctx, label, host, port, cmd, adv=False, timeout=DEADLINE,
               sort=False, immediate=IMMEDIATE):
    """One redis-cli command at a name: what came back, and how long it took.

    Both halves are needed. The reply says the connection the race ended on was
    usable rather than merely open -- a caller that raced, won, and then
    reported the blackholed attempt's error connects and never sends the
    command. The bucket says the race happened rather than that the address
    list happened to be short.

    `immediate` is the first boundary of the four words, defaulted to the
    module's and overridden by the cells that expect `immediate` as their
    answer; `PROCESS_IMMEDIATE` lists which those are and says why.
    """
    t0 = time.monotonic()
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", host, "-p", port] + [str(a) for a in cmd],
                           timeout=timeout)
    took = time.monotonic() - t0
    if rc == 0:
        ctx.emit_text("%s/reply" % label, out, adv=adv, sort=sort)
    else:
        ctx.emit("%s/reply" % label, NO_REPLY, adv=adv)
    ctx.emit("%s/when" % label,
             _bucket(None if rc == "timeout" else took, immediate=immediate),
             adv=adv)
    return rc, out


def _bus_linked(c, deadline=LINK_WAIT):
    """Whether this node ever has a bus connection to a peer, inside `deadline`.

    `CLUSTER NODES` prints one row per known node and its eighth field is the
    link state, `connected` exactly when the node has a bus connection open --
    `myself` excepted, which prints `connected` unconditionally and is therefore
    skipped. Only the state is read: the rest of the row is node ids, epochs and
    a ping clock, none of which would mean the same thing twice.
    """
    end = time.time() + deadline
    while time.time() < end:
        blob = c.cmd("CLUSTER", "NODES")[1].decode("utf-8", "replace")
        for line in blob.splitlines():
            parts = line.split(" ")
            if (len(parts) > 7 and "myself" not in parts[2]
                    and parts[7] == "connected"):
                return True
        time.sleep(0.1)
    return False


def _failure_word(ctx, label, host, port, adv=False, timeout=DEADLINE):
    """Which *kind* of failure comes back when a name reaches nothing.

    `d01._cli_failure`'s vocabulary and its reason: the wording is the C
    library's and the host in the message is the operator's, so neither is
    recorded. What is recorded is which address's failure came back, and on a
    name whose addresses fail in two different ways that says which address the
    caller ran out of list on -- which is how the interleaved order shows up in
    a value that is not a duration.
    """
    rc, out = srv.run_tool(ctx.tree, "redis-cli",
                           ["-h", host, "-p", port, "PING"], timeout=timeout)
    text = (out or b"").decode("utf-8", "replace").lower()
    if rc == 0:
        word = "connected"
    elif rc == "timeout":
        word = "timeout"
    elif "unreachable" in text:
        word = "unreachable"
    elif "refused" in text:
        word = "refused"
    else:
        word = "other"
    ctx.emit(label, word, adv=adv)


def _bench(ctx, label, host, port, adv=False, timeout=DEADLINE):
    """The load generator at a name: a caller that does not wait for its socket.

    At step 1 this one takes the first address it can start an attempt on and
    reports whatever that attempt did. At this step it races like the rest, so a
    blackholed head no longer decides its answer.
    """
    rc, out = srv.run_benchmark(
        ctx.tree, ["-h", host, "-p", port, "-n", 4, "-c", 1, "-t", "ping",
                   "-q"], timeout=timeout)
    ctx.emit("%s/rc" % label, rc, adv=adv)
    ctx.emit("%s/reached" % label, b"PING_INLINE" in (out or b""), adv=adv)


def _worst_ping(host, port, seconds=3.0):
    """The slowest PING round trip seen over `seconds`, or None if one failed.

    This is how a server's own responsiveness is measured while something
    inside it is trying to connect somewhere. It is a duration, so it is read
    through `_bucket` like the others rather than recorded raw.
    """
    worst = 0.0
    end = time.time() + seconds
    try:
        with resp.Client(host, port, timeout=30.0) as c:
            while time.time() < end:
                t0 = time.monotonic()
                if c.cmd("PING")[1] != b"PONG":
                    return None
                worst = max(worst, time.monotonic() - t0)
                time.sleep(0.05)
    except Exception:  # noqa: BLE001 - an unreachable server is not a latency
        return None
    return worst


def run(ctx):
    p = [ctx.port(i) for i in range(PORTS)]

    # -- 1. the instrument, against known answers, in this run ---------------
    # Every cell below reads "did this attempt hang", and a blackhole
    # that had quietly stopped working would turn all of them into "connected"
    # and read as a tree that had done the work. So the same probe is pointed at
    # an address with a listener on it, at an address with nothing on it, and at
    # a darkened one, and the three answers are scored beside everything else.
    # Nothing in this section involves the tree, so the three words are the same
    # at every step; a recording where they are not is a broken instrument.
    c = dark.controls(p[7])
    ctx.emit("instrument/live-is-connected", c["open"])
    ctx.emit("instrument/unbound-is-refused", c["unbound"])
    ctx.emit("instrument/dark-is-hung", c["dark"])
    # `c["parked"]` is counted by the instrument and not scored here: the
    # blackhole owes that an attempt hangs, not that no connection was ever
    # parked, and `lib/dark.py` says so where the count is produced.

    # -- 2. one family's head dark, the other family live --------------------
    # The RFC's own case. Both orderings are asked, because a tree that reached
    # the live one by preferring a family rather than by racing would pass one
    # of them and fail the other.
    for label, holes, bindaddr, ready in (
            ("v6-dark", ["::1"], "127.0.0.1", "127.0.0.1"),
            ("v4-dark", ["127.0.0.1"], "::1", "::1")):
        with dark.Blackhole(holes, p[0]) as bh:
            ctx.emit("dark/%s/blackhole-ok" % label, bh.all_dark)
            s = ctx.server("d2-%s" % label, p[0],
                           ctxmod.default_directives(p[0], host=bindaddr),
                           host=ready)
            ctx.emit("dark/%s/server-up" % label, _start(s, ready))
            try:
                with s.client(host=ready) as sc:
                    sc.cmd("SET", "answered", label)
            except Exception as e:  # noqa: BLE001
                ctx.note("dark/%s setup: %s" % (label, type(e).__name__))
            # `dual.test` has exactly one address of each family and one of them
            # is the hole, so a reply here means the other one was attempted
            # while the hole was still outstanding.
            _timed_cli(ctx, "dark/%s/cli" % label, "dual.test", p[0],
                       ("GET", "answered"), adv=True)
            _bench(ctx, "dark/%s/bench" % label, "dual.test", p[0], adv=True)
            # The literal controls: the address that is not the hole is reached
            # as directly as ever, and the hole is still a hole. Without these,
            # a tree that had simply stopped using the blackholed family would
            # score the two cells above.
            _timed_cli(ctx, "dark/%s/cli-live-literal" % label, bindaddr, p[0],
                       ("GET", "answered"), immediate=PROCESS_IMMEDIATE)
            _timed_cli(ctx, "dark/%s/cli-dark-literal" % label, holes[0], p[0],
                       ("GET", "answered"), adv=True)
            s.stop()

    # -- 3. both families' heads dark, a live address deeper -----------------
    # What only a race reaches. The baseline asks the resolver for IPv4 alone
    # and hangs on `127.0.0.1`; step 1 asks for both and hangs on `::1`, which
    # the resolver puts first; a caller that starts the next attempt while the
    # first is still outstanding walks past both holes and reaches the server.
    #
    # `multi.test` puts the live address one interleaved position past the two
    # holes, `deep-tail.test` puts it four past them with two more holes and an
    # instantly-failing address in between -- so the first is two delays' worth
    # of race and the second is four, and both land in `delays`.
    for label, name, holes, bindaddr in (
            ("multi", "multi.test", ["::1", "127.0.0.1"], "127.99.99.1"),
            ("tail", "deep-tail.test",
             ["::1", "127.0.0.1", "127.99.99.2", "127.99.99.3"],
             "127.99.99.4")):
        with dark.Blackhole(holes, p[1]) as bh:
            ctx.emit("deep/%s/blackhole-ok" % label, bh.all_dark)
            s = ctx.server("d2-deep-%s" % label, p[1],
                           ctxmod.default_directives(p[1], host=bindaddr),
                           host=bindaddr)
            ctx.emit("deep/%s/server-up" % label, _start(s, bindaddr))
            _timed_cli(ctx, "deep/%s/set" % label, name, p[1],
                       ("SET", "past-the-holes", label), adv=True)
            _timed_cli(ctx, "deep/%s/get" % label, name, p[1],
                       ("GET", "past-the-holes"), adv=True)
            # The round trips that need the won connection to be in working
            # order rather than merely open, as `d01` section 3b does them.
            _timed_cli(ctx, "deep/%s/dbsize" % label, name, p[1], ("DBSIZE",),
                       adv=True)
            _timed_cli(ctx, "deep/%s/scan" % label, name, p[1], ("--scan",),
                       adv=True, sort=True)
            _bench(ctx, "deep/%s/bench" % label, name, p[1], adv=True)
            # And the negative that keeps "race the addresses" from becoming
            # "reach anything on the host": a name with none of this server's
            # addresses, whose own heads are the same two holes.
            miss = "multi.test" if label == "tail" else "deep-tail.test"
            _timed_cli(ctx, "deep/%s/miss" % label, miss, p[1], ("DBSIZE",),
                       adv=True)
            s.stop()

    # -- 4. which attempt wins -----------------------------------------------
    # Two servers on one port, one per family, each holding its own name for
    # itself. Whoever answers says which attempt the caller took: with both
    # live, the first attempt is the IPv6 one and it connects before the delay
    # runs out, so nothing else is ever started and IPv6 answers. Then the IPv6
    # server is replaced by a hole and the same question is asked again: now the
    # IPv6 attempt is the one that never finishes, and IPv4 answers a delay
    # later -- which is the pair of words this section is for, because the
    # baseline answers the second case at once from the only address it asked
    # for, and step 1 never answers it at all. A tree that preferred a family
    # rather than racing gets the first case and hangs on the second. A tree
    # that fired every address at once answers both, and is caught not here --
    # both readings are `immediate` -- but by `g04`, where the first case is one
    # `connect()` line and firing both is two.
    v6 = ctx.server("d2-win-v6", p[2], ctxmod.default_directives(p[2], "::1"),
                    host="::1")
    ctx.emit("win/v6-up", _start(v6, "::1"))
    v4 = ctx.server("d2-win-v4", p[2],
                    ctxmod.default_directives(p[2], "127.0.0.1"),
                    host="127.0.0.1")
    ctx.emit("win/v4-up", _start(v4, "127.0.0.1"))
    try:
        with v6.client(host="::1") as c6:
            c6.cmd("SET", "who", "v6")
        with v4.client(host="127.0.0.1") as c4:
            c4.cmd("SET", "who", "v4")
    except Exception as e:  # noqa: BLE001
        ctx.note("win setup: %s" % type(e).__name__)
    _timed_cli(ctx, "win/both-live", "dual.test", p[2], ("GET", "who"),
               adv=True, immediate=PROCESS_IMMEDIATE)
    v6.stop()
    with dark.Blackhole(["::1"], p[2]) as bh:
        ctx.emit("win/blackhole-ok", bh.all_dark)
        _timed_cli(ctx, "win/v6-dark", "dual.test", p[2], ("GET", "who"),
                   adv=True)
    v4.stop()

    # -- 5. a failed attempt does not hold the next one back -----------------
    # Nothing is darkened here. Every address of `deep-tail.test` except the
    # last interleaved one refuses or is unreachable, and the server is on that
    # last one -- so a caller that starts the next attempt the moment one fails
    # reaches it at once, and a caller that charges a full Connection Attempt
    # Delay for every address in front of it takes five of them. The
    # documentation-prefix address sits third in the interleaved order and is
    # the case that matters most: it fails without ever being on the network.
    #
    # Every tree reaches this server -- the baseline walks its four IPv4
    # addresses and step 1 walks all six -- so what separates them is only how
    # long it took, which is why this is the one section whose bucket carries
    # the requirement on its own.
    s = ctx.server("d2-nofail", p[3],
                   ctxmod.default_directives(p[3], host="127.99.99.4"),
                   host="127.99.99.4")
    ctx.emit("fail-fast/server-up", _start(s, "127.99.99.4"))
    _timed_cli(ctx, "fail-fast/tail", "deep-tail.test", p[3], ("PING",),
               adv=True, immediate=PROCESS_IMMEDIATE)
    s.stop()
    s = ctx.server("d2-nofail-multi", p[3],
                   ctxmod.default_directives(p[3], host="127.99.99.1"),
                   host="127.99.99.1")
    ctx.emit("fail-fast/multi-server-up", _start(s, "127.99.99.1"))
    _timed_cli(ctx, "fail-fast/multi", "multi.test", p[3], ("PING",), adv=True,
               immediate=PROCESS_IMMEDIATE)
    s.stop()
    # And with nothing listening at all: the caller runs out of addresses rather
    # than out of time, and reports the last attempt's failure. Which attempt is
    # last is what interleaving decides -- `2001:db8::3` ends the resolver's
    # order and `127.99.99.4` ends the interleaved one -- so this one word says
    # the list was reordered.
    _timed_cli(ctx, "fail-fast/no-server", "deep-tail.test", p[3], ("PING",),
               adv=True, immediate=PROCESS_IMMEDIATE)
    _failure_word(ctx, "fail-fast/no-server-last-error", "deep-tail.test",
                  p[3], adv=True)
    # The control for it: a name every one of whose addresses fails the same
    # way, so the word is the same however far down the list the caller got.
    # Without this, a tree could pass the cell above by saying "refused"
    # whenever it fails to connect.
    _failure_word(ctx, "fail-fast/dual-last-error", "dual.test", p[3])

    # -- 6. the callers that do not wait, and the loop they run in -----------
    # A replication link starts its connection and lets the event loop finish
    # it. At this step it races too, so a master behind two holes becomes
    # reachable; and because the race is run before the descriptor is handed
    # back, the loop is held for as long as the race takes. That cost is
    # bounded and the bound is what is scored: a name with one address has no
    # race to run and must cost nothing, and a name with six must cost the race
    # and not the connect timeout.
    for label, master_bind, slaveof, holes, worst_ok in (
            # The progress case: the master is four interleaved positions past
            # two holes, and only a racer links to it.
            ("deep-dark", "127.99.99.4", "deep-tail.test",
             ["::1", "127.0.0.1", "127.99.99.2", "127.99.99.3"], 3.0),
            # No master at all, so the race runs to the end of the list on every
            # retry. This is the one that says the loop is not held for a
            # connect timeout.
            ("deep-nomaster", None, "deep-tail.test",
             ["::1", "127.0.0.1", "127.99.99.2", "127.99.99.3"], 3.0),
            # One address, and it is a hole. There is no race to run, so a
            # caller that was not made to wait before must not be made to wait
            # now: the bound here is tight on purpose.
            ("single-dark", None, "v6only.test", ["::1"], 0.5)):
        with dark.Blackhole(holes, p[4]) as bh:
            ctx.emit("repl/%s/blackhole-ok" % label, bh.all_dark)
            m = None
            if master_bind:
                m = ctx.server("d2-m-%s" % label, p[4],
                               ctxmod.default_directives(p[4],
                                                         host=master_bind),
                               host=master_bind)
                ctx.emit("repl/%s/master-up" % label, _start(m, master_bind))
                try:
                    with m.client(host=master_bind) as mc:
                        mc.cmd("SET", "replicated", label)
                except Exception as e:  # noqa: BLE001
                    ctx.note("repl/%s master: %s" % (label, type(e).__name__))
            r = ctx.server("d2-r-%s" % label, p[5],
                           ctxmod.default_directives(p[5], "::1"))
            ctx.emit("repl/%s/replica-up" % label, _start(r, "::1"))
            try:
                with r.client() as rc:
                    rc.cmd("SLAVEOF", slaveof, str(p[4]))
                # A fresh client, because the measurement is of the server's
                # loop and not of a connection it already has in hand.
                worst = _worst_ping("::1", p[5])
                # A bound, not a measurement. Recording the golden's own worst
                # round trip would make any *better* implementation -- one that
                # ran the race inside the event loop instead of in front of it
                # -- a mismatch, which is the opposite of what the requirement
                # says. The bound is what the requirement says.
                ctx.emit("repl/%s/responsive" % label,
                         worst is not None and worst < worst_ok, adv=True)
                with r.client() as rc:
                    # 8 s for the same reason `d01` gives: for the cases that
                    # must never link, this deadline is the runtime.
                    ctx.emit("repl/%s/link-up" % label,
                             srv.wait_info(rc, "replication",
                                           "master_link_status", "up",
                                           deadline=8.0), adv=True)
                    ctx.emit("repl/%s/role" % label,
                             srv.info_field(rc, "replication", "role"))
                    ctx.emit_reply("repl/%s/replicated-key" % label,
                                   rc.cmd("GET", "replicated"), adv=True)
            except Exception as e:  # noqa: BLE001
                ctx.note("repl/%s: %s" % (label, type(e).__name__))
            r.stop()
            if m:
                m.stop()

    # -- 7. what this step does not change -----------------------------------
    # One address is still one attempt: a name with a single address, and that
    # address a hole, hangs exactly as it did. There is nothing to race, and a
    # tree that answered here would be reaching an address the name does not
    # have.
    with dark.Blackhole(["::1"], p[6]) as bh:
        ctx.emit("same/blackhole-ok", bh.all_dark)
        _timed_cli(ctx, "same/single-dark-name", "v6only.test", p[6],
                   ("PING",), adv=True)
        _timed_cli(ctx, "same/single-dark-literal", "::1", p[6], ("PING",),
                   adv=True)
    # A name with nothing behind it fails at the resolver, before any of this.
    _timed_cli(ctx, "same/no-such-name", "nx.invalid", p[6], ("PING",),
               immediate=PROCESS_IMMEDIATE)
    # And a transport with no address in it is untouched.
    sock = os.path.join(ctx.workspace.base, "d02.sock")
    u = ctx.server("d2-unix", p[6],
                   ctxmod.default_directives(p[6], "::1",
                                             extra=["unixsocket %s" % sock]))
    ctx.emit("same/unix-started", _start(u, "::1"))
    rc, out = srv.run_tool(ctx.tree, "redis-cli", ["-s", sock, "ping"],
                           timeout=30)
    ctx.emit("same/unix-cli-rc", rc)
    ctx.emit("same/unix-cli-reached", b"PONG" in (out or b""))
    u.stop()

    # And a source address asked for as a *requirement* is still a requirement:
    # an attempt that cannot bind it fails, and racing does not make it bind.
    # The cluster bus is the caller that asks that way -- it is the only one in
    # the repository that does -- so this is where the semantic is visible.
    #
    # The shape is chosen to make the bind the only thing that can fail. The
    # meeting node is bound to `::1` alone, so `::1` is the source address every
    # connection it opens has to have; the peer it is told to meet is bound to
    # `127.0.0.1` and is really listening there, so the destination exists and
    # answers. The address is handed over as an IPv4 literal, which is one
    # address and therefore one attempt however the attempts are run, and that
    # one attempt has to put an IPv6 source on an AF_INET socket. It cannot, and
    # the link never forms.
    #
    # This is a regression cell and reads the same on every tree -- the
    # baseline, step 1 and the golden all fail to link -- which is the point:
    # the easiest way to make a required bind race is to stop requiring it, and
    # a tree that did that would pass every other cell in this scenario.
    meeter = ctx.server("d2-bus-v6", p[8],
                        ctxmod.default_directives(p[8], "::1",
                                                  extra=list(CLUSTER_DIRECTIVES)),
                        host="::1")
    ctx.emit("same/cluster-bind/node-up", _start(meeter, "::1"))
    peer = ctx.server("d2-bus-v4", p[9],
                      ctxmod.default_directives(p[9], "127.0.0.1",
                                                extra=list(CLUSTER_DIRECTIVES)),
                      host="127.0.0.1")
    ctx.emit("same/cluster-bind/peer-up", _start(peer, "127.0.0.1"))
    linked = False
    try:
        with meeter.client(host="::1") as cc:
            cc.cmd("CLUSTER", "MEET", "127.0.0.1", str(p[9]))
            linked = _bus_linked(cc)
    except Exception as e:  # noqa: BLE001 - a node that never answered is a
        ctx.note("same/cluster-bind: %s" % type(e).__name__)  # result, not a stop
    ctx.emit("same/cluster-bind/link-up", linked, adv=True)
    peer.stop()
    meeter.stop()
